using UnityEngine;

public enum PathEndBehaviour
{
    Stop,
    WaitAndReturn,
    Despawn
}

public class PathFollowerVehicleAgent : MonoBehaviour
{
    [Header("Debug")]
    [SerializeField] private bool drawDebug = false;

    private Terrain[] terrains;
    private VehicleProfile profile;

    private Vector3 corridorStart;
    private Vector3 corridorEnd;
    private Vector3 corridorDir;
    private Vector3 corridorRight;

    private float assignedPathOffset;
    private int directionSign = 1; // +1 start->end, -1 end->start
    private float currentAlong;
    private float minAlong;
    private float maxAlong;

    private float desiredSpeed;
    private float currentSpeed;

    private float currentLateralJitter;
    private float targetLateralJitter;

    private PathEndBehaviour endBehaviour = PathEndBehaviour.WaitAndReturn;
    private float waitAtEndTime = 2f;
    private float waitTimer = 0f;

    private bool initialized = false;
    private bool waitingAtEnd = false;
    private bool stopped = false;

    private Quaternion modelOffsetRotation = Quaternion.identity;

    public VehicleFaction Faction => profile != null ? profile.faction : VehicleFaction.Civilian;
    public VehicleClass VehicleClass => profile != null ? profile.vehicleClass : VehicleClass.Civil;
    public float PathOffset => assignedPathOffset;
    public int DirectionSign => directionSign;
    public bool IsStopped => stopped;

    public void Initialize(
        Vector3 spawnPoint,
        Vector3 routeStart,
        Vector3 routeEnd,
        float pathOffset,
        int moveDirectionSign,
        PathEndBehaviour behaviour,
        float endWaitTime
    )
    {
        terrains = Terrain.activeTerrains;
        profile = GetComponent<VehicleProfile>();

        if (profile == null)
        {
            Debug.LogError($"PathFollowerVehicleAgent on {name}: VehicleProfile eksik.");
            enabled = false;
            return;
        }

        corridorStart = routeStart;
        corridorEnd = routeEnd;
        corridorDir = (corridorEnd - corridorStart);
        corridorDir.y = 0f;
        corridorDir = corridorDir.sqrMagnitude > 0.001f ? corridorDir.normalized : Vector3.forward;

        corridorRight = Vector3.Cross(Vector3.up, corridorDir).normalized;

        assignedPathOffset = pathOffset;
        directionSign = moveDirectionSign >= 0 ? 1 : -1;
        endBehaviour = behaviour;
        waitAtEndTime = endWaitTime;

        desiredSpeed = Random.Range(profile.minCruiseSpeed, profile.maxCruiseSpeed);
        currentSpeed = 0f;

        currentLateralJitter = Random.Range(-profile.lateralWander, profile.lateralWander);
        targetLateralJitter = currentLateralJitter;

        if (Mathf.Abs(profile.visualYawOffset) > 0.001f)
            modelOffsetRotation = Quaternion.Euler(0f, profile.visualYawOffset, 0f);

        transform.position = SnapPointToTerrain(spawnPoint);

        currentAlong = GetAlongValue(transform.position);

        float totalLength = FlatDistance(corridorStart, corridorEnd);
        minAlong = 6f;
        maxAlong = totalLength - 6f;

        initialized = true;
    }

    private void Update()
    {
        if (!initialized || stopped)
            return;

        if (waitingAtEnd)
        {
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, profile.braking * Time.deltaTime);
            waitTimer -= Time.deltaTime;

            if (waitTimer <= 0f)
            {
                waitingAtEnd = false;
                directionSign *= -1;
                desiredSpeed = Random.Range(profile.minCruiseSpeed, profile.maxCruiseSpeed);
            }

            return;
        }

        UpdateLateralJitter();

        float frontSlowFactor = ComputeFrontVehicleSlowdown();
        float targetSpeed = desiredSpeed * frontSlowFactor;

        if (frontSlowFactor < 0.35f)
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, profile.braking * Time.deltaTime);
        else
            currentSpeed = Mathf.MoveTowards(currentSpeed, targetSpeed, profile.acceleration * Time.deltaTime);

        float alongStep = currentSpeed * Time.deltaTime * directionSign;
        float nextAlong = currentAlong + alongStep;

        bool reachedBoundary = (directionSign > 0 && nextAlong >= maxAlong) || (directionSign < 0 && nextAlong <= minAlong);

        if (reachedBoundary)
        {
            currentAlong = directionSign > 0 ? maxAlong : minAlong;
            ApplyPositionFromPath(currentAlong);

            switch (endBehaviour)
            {
                case PathEndBehaviour.Stop:
                    stopped = true;
                    currentSpeed = 0f;
                    break;

                case PathEndBehaviour.WaitAndReturn:
                    waitingAtEnd = true;
                    waitTimer = waitAtEndTime;
                    currentSpeed = 0f;
                    break;

                case PathEndBehaviour.Despawn:
                    Destroy(gameObject);
                    break;
            }

            return;
        }

        currentAlong = nextAlong;
        ApplyPositionFromPath(currentAlong);
    }

    private void ApplyPositionFromPath(float along)
    {
        Vector3 basePoint = corridorStart + corridorDir * along;
        Vector3 targetPoint = basePoint + corridorRight * (assignedPathOffset + currentLateralJitter);
        targetPoint = SnapPointToTerrain(targetPoint);

        Vector3 currentPos = transform.position;
        Vector3 moveDir = targetPoint - currentPos;
        moveDir.y = 0f;

        transform.position = targetPoint;

        if (moveDir.sqrMagnitude > 0.0001f)
        {
            Quaternion targetRot = Quaternion.LookRotation(moveDir.normalized, Vector3.up) * modelOffsetRotation;
            transform.rotation = Quaternion.Slerp(transform.rotation, targetRot, profile.turnSpeed * Time.deltaTime);
        }
    }

    private void UpdateLateralJitter()
    {
        if (Mathf.Abs(currentLateralJitter - targetLateralJitter) < 0.3f)
        {
            targetLateralJitter = Random.Range(-profile.lateralWander, profile.lateralWander);
        }

        currentLateralJitter = Mathf.MoveTowards(currentLateralJitter, targetLateralJitter, 1.2f * Time.deltaTime);
    }

    private float ComputeFrontVehicleSlowdown()
    {
        PathFollowerVehicleAgent[] all = FindObjectsOfType<PathFollowerVehicleAgent>();
        float bestFactor = 1f;

        Vector3 myPos = transform.position;
        Vector3 myForward = transform.forward;
        myForward.y = 0f;
        myForward.Normalize();

        for (int i = 0; i < all.Length; i++)
        {
            PathFollowerVehicleAgent other = all[i];
            if (other == this || other.stopped)
                continue;

            // farklı path’teyse çok sıkı etkileşmesin
            if (Mathf.Abs(other.PathOffset - PathOffset) > 4f)
                continue;

            Vector3 delta = other.transform.position - myPos;
            delta.y = 0f;

            float dist = delta.magnitude;
            if (dist < 0.001f)
                continue;

            Vector3 dirToOther = delta.normalized;
            float forwardDot = Vector3.Dot(myForward, dirToOther);

            if (forwardDot < 0.5f)
                continue;

            float desiredGap = Mathf.Max(profile.preferredSpacing, 10f);
            float stopGap = Mathf.Max(profile.emergencyStopDistance, 4f);

            bool sameFaction = other.Faction == Faction;
            if (!sameFaction)
                desiredGap *= 1.35f;

            desiredGap += currentSpeed * 1.0f;
            stopGap += currentSpeed * 0.3f;

            if (dist <= stopGap)
            {
                bestFactor = 0f;
            }
            else if (dist < desiredGap)
            {
                float factor = Mathf.InverseLerp(stopGap, desiredGap, dist);
                bestFactor = Mathf.Min(bestFactor, factor);
            }
        }

        return bestFactor;
    }

    private float GetAlongValue(Vector3 point)
    {
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
        Vector3 basePoint = corridorStart + corridorDir * currentAlong;
        Vector3 targetPoint = basePoint + corridorRight * (assignedPathOffset + currentLateralJitter);
        Gizmos.DrawSphere(targetPoint, 2f);
        Gizmos.DrawLine(transform.position, targetPoint);
    }
}