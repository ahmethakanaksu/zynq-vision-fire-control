using System.Collections.Generic;
using UnityEngine;

public class CorridorVehicleAgent : MonoBehaviour
{
    [Header("Runtime Debug")]
    [SerializeField] private bool drawDebug = false;

    private Terrain[] terrains;
    private VehicleProfile profile;

    private Vector3 corridorStart;
    private Vector3 corridorEnd;
    private float corridorHalfWidth;

    private Vector3 moveDirection;
    private Vector3 currentTarget;
    private bool initialized = false;
    private bool reachedEnd = false;

    private float desiredSpeed;
    private float currentSpeed;
    private float currentLateralOffset;
    private float targetLateralOffset;

    private float preferredSideSign = 1f;
    private Vector3 corridorDir;

    private Quaternion modelOffsetRotation = Quaternion.identity;

    public VehicleFaction Faction => profile != null ? profile.faction : VehicleFaction.Civilian;
    public VehicleClass VehicleClass => profile != null ? profile.vehicleClass : VehicleClass.Civil;
    public bool ReachedEnd => reachedEnd;

    public void Initialize(
        Vector3 spawnPoint,
        Vector3 routeStart,
        Vector3 routeEnd,
        float widthHalf,
        Vector3 flowDirection
    )
    {
        terrains = Terrain.activeTerrains;
        profile = GetComponent<VehicleProfile>();

        if (profile == null)
        {
            Debug.LogError($"CorridorVehicleAgent on {name}: VehicleProfile eksik.");
            enabled = false;
            return;
        }

        corridorStart = routeStart;
        corridorEnd = routeEnd;
        corridorHalfWidth = widthHalf;

        moveDirection = flowDirection;
        moveDirection.y = 0f;

        corridorDir = (corridorEnd - corridorStart);
        corridorDir.y = 0f;
        corridorDir = corridorDir.sqrMagnitude > 0.001f ? corridorDir.normalized : Vector3.forward;

        if (moveDirection.sqrMagnitude < 0.001f)
            moveDirection = corridorDir;

        // koridor yönüyle aynı gidiyorsa bir tarafı, ters gidiyorsa diğer tarafı tercih etsin
        preferredSideSign = Vector3.Dot(moveDirection, corridorDir) >= 0f ? -1f : 1f;

        desiredSpeed = Random.Range(profile.minCruiseSpeed, profile.maxCruiseSpeed);
        currentSpeed = 0f;

        // soft band: tam çizgi değil, tercih edilen yarı bölgede başlasın
        float minBand = corridorHalfWidth * 0.20f;
        float maxBand = corridorHalfWidth * 0.75f;

        currentLateralOffset = preferredSideSign * Random.Range(minBand, maxBand);
        currentLateralOffset += Random.Range(-profile.lateralWander, profile.lateralWander);
        currentLateralOffset = Mathf.Clamp(currentLateralOffset, -corridorHalfWidth * 0.9f, corridorHalfWidth * 0.9f);

        targetLateralOffset = currentLateralOffset;

        transform.position = SnapPointToTerrain(spawnPoint);

        if (Mathf.Abs(profile.visualYawOffset) > 0.001f)
            modelOffsetRotation = Quaternion.Euler(0f, profile.visualYawOffset, 0f);

        PickNextLongTarget(true);

        initialized = true;
    }

    private void Update()
    {
        if (!initialized || reachedEnd)
            return;

        Vector3 currentPos = transform.position;

        float frontSlowFactor = ComputeFrontVehicleSlowdown();
        float targetSpeed = desiredSpeed * frontSlowFactor;

        if (frontSlowFactor < 0.35f)
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, profile.braking * Time.deltaTime);
        else
            currentSpeed = Mathf.MoveTowards(currentSpeed, targetSpeed, profile.acceleration * Time.deltaTime);

        Vector3 toTarget = currentTarget - currentPos;
        toTarget.y = 0f;

        if (toTarget.magnitude <= 8f)
        {
            if (!PickNextLongTarget(false))
            {
                reachedEnd = true;
                currentSpeed = 0f;
                return;
            }

            toTarget = currentTarget - currentPos;
            toTarget.y = 0f;
        }

        if (toTarget.sqrMagnitude < 0.001f)
            return;

        Vector3 moveDir = toTarget.normalized;
        Vector3 nextPos = currentPos + moveDir * currentSpeed * Time.deltaTime;
        nextPos = SnapPointToTerrain(nextPos);

        // çok yakında araç varsa bu framede ilerlemeyi bastır
        if (!WouldBeTooCloseToAnotherVehicle(nextPos))
        {
            transform.position = nextPos;
        }
        else
        {
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, profile.braking * Time.deltaTime);
        }

        Quaternion targetRot = Quaternion.LookRotation(moveDir, Vector3.up) * modelOffsetRotation;
        transform.rotation = Quaternion.Slerp(transform.rotation, targetRot, profile.turnSpeed * Time.deltaTime);

        UpdateLateralOffsetOverTime();
    }

    private void UpdateLateralOffsetOverTime()
    {
        if (Mathf.Abs(currentLateralOffset - targetLateralOffset) < 1f)
        {
            float minBand = corridorHalfWidth * 0.20f;
            float maxBand = corridorHalfWidth * 0.75f;

            float biasedTarget = preferredSideSign * Random.Range(minBand, maxBand);
            biasedTarget += Random.Range(-profile.lateralWander, profile.lateralWander);

            targetLateralOffset = Mathf.Clamp(
                biasedTarget,
                -corridorHalfWidth * 0.9f,
                corridorHalfWidth * 0.9f
            );
        }

        currentLateralOffset = Mathf.MoveTowards(currentLateralOffset, targetLateralOffset, 3f * Time.deltaTime);
    }

    private bool PickNextLongTarget(bool firstPick)
    {
        Vector3 corridorDir = (corridorEnd - corridorStart);
        corridorDir.y = 0f;
        corridorDir.Normalize();

        Vector3 right = Vector3.Cross(Vector3.up, corridorDir).normalized;
        if (Vector3.Dot(moveDirection, corridorDir) < 0f)
            right = -right;

        Vector3 currentPos = transform.position;
        float alongNow = GetAlongValue(currentPos);

        float forwardDistance = Random.Range(profile.forwardTargetMin, profile.forwardTargetMax);
        float desiredAlong = alongNow + Mathf.Sign(Vector3.Dot(moveDirection, corridorDir)) * forwardDistance;

        float minAlong = 10f;
        float maxAlong = Vector3.Distance(
            new Vector3(corridorStart.x, 0f, corridorStart.z),
            new Vector3(corridorEnd.x, 0f, corridorEnd.z)
        ) - 10f;

        if (desiredAlong < minAlong || desiredAlong > maxAlong)
        {
            // artık yol sonuna geldiyse hedef üretme
            return false;
        }

        Vector3 basePoint = corridorStart + corridorDir * desiredAlong;
        Vector3 candidate = basePoint + right * currentLateralOffset;
        candidate = ClampPointInsideCorridor(candidate);
        candidate = SnapPointToTerrain(candidate);

        currentTarget = candidate;

        if (!firstPick)
            desiredSpeed = Random.Range(profile.minCruiseSpeed, profile.maxCruiseSpeed);

        return true;
    }

    private Vector3 ClampPointInsideCorridor(Vector3 point)
    {
        Vector3 corridorDir = corridorEnd - corridorStart;
        corridorDir.y = 0f;
        corridorDir.Normalize();

        Vector3 toPoint = point - corridorStart;
        toPoint.y = 0f;

        float along = Vector3.Dot(toPoint, corridorDir);
        float length = FlatDistance(corridorStart, corridorEnd);
        along = Mathf.Clamp(along, 0f, length);

        Vector3 projected = corridorStart + corridorDir * along;

        Vector3 lateralVec = point - projected;
        lateralVec.y = 0f;

        if (lateralVec.magnitude > corridorHalfWidth)
            lateralVec = lateralVec.normalized * corridorHalfWidth * 0.9f;

        return projected + lateralVec;
    }

    private float ComputeFrontVehicleSlowdown()
    {
        CorridorVehicleAgent[] all = FindObjectsOfType<CorridorVehicleAgent>();
        float bestFactor = 1f;

        Vector3 myPos = transform.position;
        Vector3 myForward = transform.forward;
        myForward.y = 0f;
        myForward.Normalize();

        for (int i = 0; i < all.Length; i++)
        {
            CorridorVehicleAgent other = all[i];
            if (other == this || other.reachedEnd)
                continue;

            Vector3 delta = other.transform.position - myPos;
            delta.y = 0f;

            float dist = delta.magnitude;
            if (dist < 0.001f)
                continue;

            Vector3 dirToOther = delta.normalized;
            float forwardDot = Vector3.Dot(myForward, dirToOther);

            // Ön sektör dışında ise ignore
            if (forwardDot < 0.4f)
                continue;

            bool sameFaction = other.Faction == Faction;

            Vector3 otherForward = other.transform.forward;
            otherForward.y = 0f;
            otherForward.Normalize();

            float headingDot = Vector3.Dot(myForward, otherForward);
            bool oppositeDirection = headingDot < -0.3f;


            float desiredGap = Mathf.Max(profile.preferredSpacing, 10f);
            float stopGap = Mathf.Max(profile.emergencyStopDistance, 4f);

            // Farklı faction ise daha geniş tampon
            if (oppositeDirection)
                desiredGap *= 0.75f;

            // Hızlı giden araç daha erken frenlesin
            desiredGap += currentSpeed * 1.2f;
            stopGap += currentSpeed * 0.35f;

            // Ön sektörde ama biraz çaprazsa biraz daha toleranslı olabilir
            float angleFactor = Mathf.InverseLerp(0.4f, 1f, forwardDot);
            float weightedDist = dist * Mathf.Lerp(1.15f, 1f, angleFactor);

            if (weightedDist <= stopGap)
            {
                bestFactor = 0f;
            }
            else if (weightedDist < desiredGap)
            {
                float factor = Mathf.InverseLerp(stopGap, desiredGap, weightedDist);
                bestFactor = Mathf.Min(bestFactor, factor);
            }
        }

        return bestFactor;
    }

    private float GetAlongValue(Vector3 point)
    {
        Vector3 corridorDir = corridorEnd - corridorStart;
        corridorDir.y = 0f;
        corridorDir.Normalize();

        Vector3 toPoint = point - corridorStart;
        toPoint.y = 0f;

        return Vector3.Dot(toPoint, corridorDir);
    }

    private Vector3 SnapPointToTerrain(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return point;

        float y = t.SampleHeight(point) + t.transform.position.y;
        point.y = y;
        return point;
    }

    private Terrain GetTerrainUnderPosition(Vector3 worldPos)
    {
        if (terrains == null || terrains.Length == 0)
            terrains = Terrain.activeTerrains;

        for (int i = 0; i < terrains.Length; i++)
        {
            Terrain t = terrains[i];
            Vector3 tp = t.transform.position;
            Vector3 size = t.terrainData.size;

            bool insideX = worldPos.x >= tp.x && worldPos.x <= tp.x + size.x;
            bool insideZ = worldPos.z >= tp.z && worldPos.z <= tp.z + size.z;

            if (insideX && insideZ)
                return t;
        }

        return null;
    }

    private float FlatDistance(Vector3 a, Vector3 b)
    {
        a.y = 0f;
        b.y = 0f;
        return Vector3.Distance(a, b);
    }

    private void OnDrawGizmosSelected()
    {
        if (!drawDebug || !initialized)
            return;

        Gizmos.color = Color.magenta;
        Gizmos.DrawSphere(currentTarget, 3f);

        Gizmos.color = Color.cyan;
        Gizmos.DrawLine(transform.position, currentTarget);
    }
    private bool WouldBeTooCloseToAnotherVehicle(Vector3 candidatePos)
    {
        CorridorVehicleAgent[] all = FindObjectsOfType<CorridorVehicleAgent>();

        for (int i = 0; i < all.Length; i++)
        {
            CorridorVehicleAgent other = all[i];
            if (other == this || other.reachedEnd)
                continue;

            float dist = FlatDistance(candidatePos, other.transform.position);

            float minGap = profile.emergencyStopDistance;

            if (other.Faction != Faction)
                minGap *= 1.25f;

            if (dist < minGap)
                return true;
        }

        return false;
    }
}