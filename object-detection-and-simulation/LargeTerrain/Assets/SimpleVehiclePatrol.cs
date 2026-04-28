using UnityEngine;

public class SimpleVehiclePatrol : MonoBehaviour
{
    [Header("Movement")]
    [SerializeField] private float minMoveSpeed = 5f;
    [SerializeField] private float maxMoveSpeed = 9f;
    [SerializeField] private float rotationSpeed = 3f;
    [SerializeField] private float acceleration = 4f;
    [SerializeField] private float reachDistance = 5f;

    [Header("Idle Behaviour")]
    [SerializeField] private float minWaitAtPointTime = 0.4f;
    [SerializeField] private float maxWaitAtPointTime = 1.8f;
    [SerializeField] private float idleChance = 0.35f;

    [Header("Target Sampling")]
    [SerializeField] private int maxTargetFindAttempts = 25;
    [SerializeField] private float maxSlope = 24f;
    [SerializeField] private float preferredForwardDistanceMin = 12f;
    [SerializeField] private float preferredForwardDistanceMax = 35f;
    [SerializeField] private float lateralJitter = 10f;
    [SerializeField] private float backwardMoveChance = 0.18f;

    [Header("Grounding")]
    [SerializeField] private float groundAlignSpeed = 6f;
    [SerializeField] private bool alignToGroundNormal = false;

    private Terrain[] terrains;

    private Vector3 patrolCenter;
    private float patrolRadius;
    private float corridorHalfWidth;
    private Vector3 corridorStart;
    private Vector3 corridorEnd;

    private Vector3 currentTarget;
    private bool hasTarget = false;
    private float waitTimer = 0f;
    private bool initialized = false;

    private float chosenMoveSpeed;
    private float currentSpeed;

    private Vector3 routeDir;
    private Vector3 preferredTravelDir;

    public void SetPatrolData(Vector3 center, float radius, float corridorWidthHalf, Vector3 routeStart, Vector3 routeEnd)
    {
        patrolCenter = center;
        patrolRadius = radius;
        corridorHalfWidth = corridorWidthHalf;
        corridorStart = routeStart;
        corridorEnd = routeEnd;

        terrains = Terrain.activeTerrains;
        initialized = true;

        routeDir = corridorEnd - corridorStart;
        routeDir.y = 0f;
        routeDir = routeDir.sqrMagnitude > 0.001f ? routeDir.normalized : Vector3.forward;

        // Araçların yarısı bir yöne, yarısı diğer yöne eğilimli olabilir
        preferredTravelDir = Random.value < 0.5f ? routeDir : -routeDir;

        chosenMoveSpeed = Random.Range(minMoveSpeed, maxMoveSpeed);
        currentSpeed = 0f;

        SnapToTerrainImmediate();
        PickNewTarget(true);
    }

    private void Update()
    {
        if (!initialized)
            return;

        if (!hasTarget)
        {
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, acceleration * Time.deltaTime);

            waitTimer -= Time.deltaTime;
            if (waitTimer <= 0f)
                PickNewTarget(false);

            return;
        }

        Vector3 currentPos = transform.position;
        Vector3 flatTarget = new Vector3(currentTarget.x, currentPos.y, currentTarget.z);
        Vector3 moveDir = flatTarget - currentPos;
        moveDir.y = 0f;

        float dist = moveDir.magnitude;

        if (dist <= reachDistance)
        {
            hasTarget = false;

            if (Random.value < idleChance)
                waitTimer = Random.Range(minWaitAtPointTime, maxWaitAtPointTime);
            else
                waitTimer = Random.Range(0.05f, 0.25f);

            return;
        }

        moveDir.Normalize();
        currentSpeed = Mathf.MoveTowards(currentSpeed, chosenMoveSpeed, acceleration * Time.deltaTime);

        Vector3 nextPos = currentPos + moveDir * currentSpeed * Time.deltaTime;
        nextPos = SnapPointToTerrain(nextPos);

        transform.position = nextPos;

        if (moveDir.sqrMagnitude > 0.0001f)
        {
            Quaternion targetRot;

            if (alignToGroundNormal)
            {
                Vector3 groundNormal = GetGroundNormal(nextPos);
                targetRot = Quaternion.LookRotation(moveDir, groundNormal);
            }
            else
            {
                targetRot = Quaternion.LookRotation(moveDir, Vector3.up);
            }

            transform.rotation = Quaternion.Slerp(transform.rotation, targetRot, rotationSpeed * Time.deltaTime);
        }
    }

    private void PickNewTarget(bool firstPick)
    {
        for (int i = 0; i < maxTargetFindAttempts; i++)
        {
            Vector3 candidate = GenerateBiasedCandidate();

            if (!IsInsideCorridor(candidate))
                continue;

            if (FlatDistance(candidate, patrolCenter) > patrolRadius)
                continue;

            candidate = SnapPointToTerrain(candidate);

            if (!IsValidSlope(candidate))
                continue;

            currentTarget = candidate;
            hasTarget = true;

            if (!firstPick)
                chosenMoveSpeed = Random.Range(minMoveSpeed, maxMoveSpeed);

            // Bazen tercih edilen yön biraz değişsin
            Vector3 toTarget = currentTarget - transform.position;
            toTarget.y = 0f;
            if (toTarget.sqrMagnitude > 0.001f)
                preferredTravelDir = Vector3.Slerp(preferredTravelDir, toTarget.normalized, 0.35f);

            return;
        }

        currentTarget = SnapPointToTerrain(patrolCenter);
        hasTarget = true;
    }

    private Vector3 GenerateBiasedCandidate()
    {
        Vector3 right = Vector3.Cross(Vector3.up, preferredTravelDir).normalized;

        bool goBackward = Random.value < backwardMoveChance;
        Vector3 baseDir = goBackward ? -preferredTravelDir : preferredTravelDir;

        float forwardDist = Random.Range(preferredForwardDistanceMin, preferredForwardDistanceMax);
        float sideOffset = Random.Range(-lateralJitter, lateralJitter);

        Vector3 candidate = transform.position + baseDir * forwardDist + right * sideOffset;
        candidate.y = 0f;

        // Çok uzağa taşarsa patrol center çevresine geri çek
        Vector3 flatCenterToCandidate = candidate - patrolCenter;
        flatCenterToCandidate.y = 0f;

        if (flatCenterToCandidate.magnitude > patrolRadius)
        {
            candidate = patrolCenter + flatCenterToCandidate.normalized * patrolRadius * 0.9f;
            candidate.y = 0f;
        }

        return candidate;
    }

    private bool IsInsideCorridor(Vector3 point)
    {
        Vector3 route = corridorEnd - corridorStart;
        route.y = 0f;

        float routeLength = route.magnitude;
        if (routeLength < 0.001f)
            return false;

        Vector3 dir = route.normalized;
        Vector3 toPoint = point - corridorStart;
        toPoint.y = 0f;

        float along = Vector3.Dot(toPoint, dir);
        if (along < 0f || along > routeLength)
            return false;

        Vector3 projected = corridorStart + dir * along;
        float lateral = FlatDistance(point, projected);

        return lateral <= corridorHalfWidth;
    }

    private void SnapToTerrainImmediate()
    {
        transform.position = SnapPointToTerrain(transform.position);
    }

    private Vector3 SnapPointToTerrain(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return point;

        float y = t.SampleHeight(point) + t.transform.position.y;
        point.y = Mathf.Lerp(transform.position.y, y, groundAlignSpeed * Time.deltaTime);

        // İlk frame / ani set durumlarında Time.deltaTime çok küçükse koruma:
        if (!Application.isPlaying || Time.deltaTime <= 0.0001f)
            point.y = y;

        return point;
    }

    private bool IsValidSlope(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return false;

        Vector3 local = point - t.transform.position;
        Vector3 size = t.terrainData.size;

        float normX = Mathf.Clamp01(local.x / size.x);
        float normZ = Mathf.Clamp01(local.z / size.z);

        Vector3 normal = t.terrainData.GetInterpolatedNormal(normX, normZ);
        float slope = Vector3.Angle(normal, Vector3.up);

        return slope <= maxSlope;
    }

    private Vector3 GetGroundNormal(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return Vector3.up;

        Vector3 local = point - t.transform.position;
        Vector3 size = t.terrainData.size;

        float normX = Mathf.Clamp01(local.x / size.x);
        float normZ = Mathf.Clamp01(local.z / size.z);

        return t.terrainData.GetInterpolatedNormal(normX, normZ);
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

        return GetClosestTerrain(worldPos);
    }

    private Terrain GetClosestTerrain(Vector3 worldPos)
    {
        Terrain closest = null;
        float closestDist = float.MaxValue;

        for (int i = 0; i < terrains.Length; i++)
        {
            Terrain t = terrains[i];
            Vector3 center = t.transform.position + t.terrainData.size * 0.5f;
            float dist = Vector2.Distance(
                new Vector2(worldPos.x, worldPos.z),
                new Vector2(center.x, center.z)
            );

            if (dist < closestDist)
            {
                closestDist = dist;
                closest = t;
            }
        }

        return closest;
    }

    private float FlatDistance(Vector3 a, Vector3 b)
    {
        a.y = 0f;
        b.y = 0f;
        return Vector3.Distance(a, b);
    }

    private void OnDrawGizmosSelected()
    {
        if (!initialized)
            return;

        Gizmos.color = Color.blue;
        Gizmos.DrawWireSphere(patrolCenter, patrolRadius);

        Gizmos.color = Color.magenta;
        Gizmos.DrawSphere(currentTarget, 2f);

        Gizmos.color = Color.cyan;
        Gizmos.DrawLine(transform.position, currentTarget);
    }
}