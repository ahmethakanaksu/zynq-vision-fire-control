using System.Collections.Generic;
using UnityEngine;

public class SimulationManager : MonoBehaviour
{
    [Header("References")]
    [SerializeField] private DroneRouteFollower drone;

    [Header("Route Settings")]
    [SerializeField] private float edgeInset = 20f;
    [SerializeField] private bool startOnPlay = true;
    [SerializeField] private bool generateNewRouteWhenCompleted = true;
    [SerializeField] private bool useOppositeEdge = true;

    [Header("Safe Flight Area")]
    [SerializeField] private float routeInnerMarginX = 250f;
    [SerializeField] private float routeInnerMarginZ = 250f;

    [Header("Corridor Settings")]
    [SerializeField] private float spawnStartEndPadding = 80f;
    [SerializeField] private float corridorHalfWidth = 120f;
    [SerializeField] private bool drawCorridorGizmos = true;

    [Header("Vehicle Prefabs")]
    [SerializeField] private GameObject[] tankPrefabs;
    [SerializeField] private GameObject[] zptPrefabs;
    [SerializeField] private GameObject[] truckPrefabs;
    [SerializeField] private GameObject[] civilPrefabs;

    [Header("Vehicle Counts")]
    [SerializeField] private int tankCount = 3;
    [SerializeField] private int zptCount = 3;
    [SerializeField] private int truckCount = 4;
    [SerializeField] private int civilCount = 6;

    [Header("Spawn Rules")]
    [SerializeField] private float minVehicleSpacing = 35f;
    [SerializeField] private float maxTerrainSlope = 28f;
    [SerializeField] private int maxSpawnAttemptsPerVehicle = 30;
    [SerializeField] private LayerMask vehicleBlockMask;

    [Header("Path Traffic Settings")]
    [SerializeField] private int pathCount = 4;
    [SerializeField] private float pathEdgePadding = 6f;
    [SerializeField] private bool allowBidirectionalTraffic = true;
    [SerializeField, Range(0f, 1f)] private float reverseDirectionChance = 0.5f;
    [SerializeField] private PathEndBehaviour soloVehicleEndBehaviour = PathEndBehaviour.WaitAndReturn;
    [SerializeField] private float soloVehicleWaitAtEndTime = 2f;


    [Header("Patrol Settings")]
    [SerializeField] private bool enableVehiclePatrol = true;
    [SerializeField] private float patrolRadiusMin = 12f;
    [SerializeField] private float patrolRadiusMax = 30f;

    [Header("Military Traffic Settings")]
    [SerializeField] private int minSoloMilitaryCount = 1;
    [SerializeField] private int maxSoloMilitaryCount = 3;
    [SerializeField] private int minConvoyCount = 0;
    [SerializeField] private int maxConvoyCount = 2;
    [SerializeField] private int minConvoySize = 2;
    [SerializeField] private int maxConvoySize = 5;

    [SerializeField] private PathEndBehaviour convoyEndBehaviour = PathEndBehaviour.WaitAndReturn;
    [SerializeField] private float convoyWaitAtEndTime = 3f;

    [SerializeField] private float convoySpeedMin = 4f;
    [SerializeField] private float convoySpeedMax = 6f;

    [SerializeField] private float convoySpacingX = 7f;
    [SerializeField] private float convoySpacingZ = 10f;

    [SerializeField] private float convoySpawnExclusionRadius = 45f;

    [Header("Debug")]
    [SerializeField] private bool drawCombinedBounds = true;
    [SerializeField] private bool drawSafeRouteBounds = true;

    private Terrain[] terrains;
    private Bounds worldBounds;
    private Bounds safeRouteBounds;

    private readonly List<GameObject> spawnedVehicles = new List<GameObject>();
    private readonly List<Vector3> usedSpawnPoints = new List<Vector3>();

    private readonly List<float> generatedPathOffsets = new List<float>();

    private Vector3 currentRouteStart;
    private Vector3 currentRouteEnd;
    private bool hasRoute = false;

    private enum Edge
    {
        Left,
        Right,
        Top,
        Bottom
    }

    private void Awake()
    {
        terrains = Terrain.activeTerrains;

        if (terrains == null || terrains.Length == 0)
        {
            Debug.LogError("SimulationManager: Sahnede terrain bulunamadı.");
            return;
        }

        worldBounds = CalculateCombinedTerrainBounds();
        safeRouteBounds = CalculateSafeRouteBounds();
    }

    private void Start()
    {
        if (drone == null)
        {
            Debug.LogError("SimulationManager: Drone referansı atanmadı.");
            return;
        }

        if (startOnPlay)
            StartSimulation();
    }

    private void Update()
    {
        if (generateNewRouteWhenCompleted && drone != null && drone.RouteCompleted)
            AssignRandomRouteAndSpawn();
    }

    [ContextMenu("Start Simulation")]
    public void StartSimulation()
    {
        if (drone == null)
        {
            Debug.LogError("SimulationManager: Drone atanmadı.");
            return;
        }

        if (terrains == null || terrains.Length == 0)
        {
            Debug.LogError("SimulationManager: Terrain listesi boş.");
            return;
        }

        AssignRandomRouteAndSpawn();
    }

    [ContextMenu("Assign Random Route And Spawn")]
    public void AssignRandomRouteAndSpawn()
    {
        safeRouteBounds = CalculateSafeRouteBounds();

        Edge startEdge = (Edge)Random.Range(0, 4);
        Edge endEdge = useOppositeEdge ? GetOppositeEdge(startEdge) : GetDifferentEdge(startEdge);

        Vector3 startPoint;
        Vector3 endPoint;

        int safety = 0;
        float minRouteDistance = Mathf.Min(safeRouteBounds.size.x, safeRouteBounds.size.z) * 0.7f;

        do
        {
            startPoint = GetRandomPointOnEdge(startEdge, safeRouteBounds);
            endPoint = GetRandomPointOnEdge(endEdge, safeRouteBounds);
            safety++;
        }
        while (FlatDistance(startPoint, endPoint) < minRouteDistance && safety < 50);

        currentRouteStart = startPoint;
        currentRouteEnd = endPoint;
        hasRoute = true;

        BuildPathOffsets();

        drone.SetRoute(startPoint, endPoint);

        ClearSpawnedVehicles();
        SpawnVehiclesAlongCorridor();

        Debug.Log($"Yeni rota atandı. Start: {startPoint} | End: {endPoint}");
    }

    private void SpawnVehiclesAlongCorridor()
    {
        usedSpawnPoints.Clear();

        // Önce siviller
        SpawnFromPool(civilPrefabs, civilCount);

        // Sonra tekil askeriler
        int soloMilitaryCount = Random.Range(minSoloMilitaryCount, maxSoloMilitaryCount + 1);
        SpawnSoloMilitary(soloMilitaryCount);

        // Sonra konvoylar
        SpawnMilitaryConvoys();
    }

    private void SpawnFromPool(GameObject[] prefabs, int count)
    {
        if (prefabs == null || prefabs.Length == 0 || count <= 0)
            return;

        for (int i = 0; i < count; i++)
        {
            GameObject prefab = prefabs[Random.Range(0, prefabs.Length)];
            SpawnSingleVehicleWithPrefab(prefab);
        }
    }
    private void SpawnMilitaryConvoys()
    {
        int convoyCount = Random.Range(minConvoyCount, maxConvoyCount + 1);

        for (int i = 0; i < convoyCount; i++)
        {
            int convoySize = Random.Range(minConvoySize, maxConvoySize + 1);
            TrySpawnSingleConvoy(convoySize);
        }
    }
    private void TrySpawnSingleConvoy(int convoySize)
    {
        for (int attempt = 0; attempt < maxSpawnAttemptsPerVehicle; attempt++)
        {
            int directionSign = 1;
            if (allowBidirectionalTraffic && Random.value < reverseDirectionChance)
                directionSign = -1;

            float pathOffset = GetRandomPathOffset();

            Vector3 route = currentRouteEnd - currentRouteStart;
            route.y = 0f;
            float routeLength = route.magnitude;

            float along = Random.Range(spawnStartEndPadding + 15f, Mathf.Max(spawnStartEndPadding + 16f, routeLength - spawnStartEndPadding - 15f));

            Vector3 basePoint = currentRouteStart + route.normalized * along + Vector3.Cross(Vector3.up, route.normalized) * pathOffset;
            basePoint.y = 0f;

            if (!TrySnapPointToTerrain(basePoint, out Vector3 snappedBase))
                continue;

            if (!IsPointInsideBounds(snappedBase, safeRouteBounds))
                continue;

            if (!IsSlopeValid(snappedBase))
                continue;

            if (!IsFarEnoughForConvoy(snappedBase, convoySpawnExclusionRadius))
                continue;

            CreateConvoy(convoySize, pathOffset, directionSign, along);
            return;
        }
    }
    private bool IsFarEnoughForConvoy(Vector3 point, float radius)
    {
        for (int i = 0; i < spawnedVehicles.Count; i++)
        {
            if (spawnedVehicles[i] == null)
                continue;

            float dist = FlatDistance(point, spawnedVehicles[i].transform.position);
            if (dist < radius)
                return false;
        }

        return true;
    }

    private List<Vector3> BuildFormationOffsets(int size)
    {
        List<Vector3> offsets = new List<Vector3>();

        // x = sağ/sol, z = ileri/geri (controller forward'una göre)
        switch (size)
        {
            case 2:
                offsets.Add(new Vector3(-convoySpacingX * 0.5f, 0f, 0f));
                offsets.Add(new Vector3(convoySpacingX * 0.5f, 0f, -convoySpacingZ));
                break;

            case 3:
                offsets.Add(new Vector3(0f, 0f, 0f));
                offsets.Add(new Vector3(-convoySpacingX, 0f, -convoySpacingZ));
                offsets.Add(new Vector3(convoySpacingX, 0f, -convoySpacingZ));
                break;

            case 4:
                offsets.Add(new Vector3(-convoySpacingX * 0.6f, 0f, 0f));
                offsets.Add(new Vector3(convoySpacingX * 0.6f, 0f, -convoySpacingZ * 0.2f));
                offsets.Add(new Vector3(-convoySpacingX * 0.8f, 0f, -convoySpacingZ * 1.2f));
                offsets.Add(new Vector3(convoySpacingX * 0.8f, 0f, -convoySpacingZ * 1.4f));
                break;

            default:
                for (int i = 0; i < size; i++)
                {
                    int row = i / 2;
                    int col = i % 2;

                    float x = col == 0 ? -convoySpacingX * 0.8f : convoySpacingX * 0.8f;
                    float z = -row * convoySpacingZ;

                    // hafif düzensizlik
                    x += Random.Range(-1.2f, 1.2f);
                    z += Random.Range(-1.5f, 1.5f);

                    offsets.Add(new Vector3(x, 0f, z));
                }
                break;
        }

        return offsets;
    }
    private void CreateConvoy(int convoySize, float pathOffset, int directionSign, float startAlong)
    {
        GameObject convoyRoot = new GameObject($"Convoy_{convoySize}");
        ConvoyController controller = convoyRoot.AddComponent<ConvoyController>();

        float convoySpeed = Random.Range(convoySpeedMin, convoySpeedMax);

        controller.Initialize(
            currentRouteStart,
            currentRouteEnd,
            pathOffset,
            directionSign,
            startAlong,
            convoySpeed,
            2f,
            3f,
            convoyEndBehaviour,
            convoyWaitAtEndTime
        );

        List<Vector3> formationOffsets = BuildFormationOffsets(convoySize);

        for (int i = 0; i < convoySize; i++)
        {
            GameObject prefab = GetRandomMilitaryPrefab();
            if (prefab == null)
                continue;

            Vector3 memberSpawn = controller.GetWorldSlotPosition(formationOffsets[i]);
            memberSpawn = new Vector3(memberSpawn.x, memberSpawn.y, memberSpawn.z);

            Quaternion rot = GetRandomGroundAlignedRotation();
            GameObject go = Instantiate(prefab, memberSpawn, rot);

            spawnedVehicles.Add(go);
            usedSpawnPoints.Add(memberSpawn);

            ConvoyVehicleMember member = go.GetComponent<ConvoyVehicleMember>();
            if (member == null)
                member = go.AddComponent<ConvoyVehicleMember>();

            member.Initialize(controller, formationOffsets[i]);
            controller.RegisterMember(member, formationOffsets[i]);
        }
    }
    private bool TryGetRandomSpawnPointInCorridor(out Vector3 result)
    {
        result = Vector3.zero;

        if (!hasRoute)
            return false;

        Vector3 route = currentRouteEnd - currentRouteStart;
        route.y = 0f;

        float routeLength = route.magnitude;
        if (routeLength < 1f)
            return false;

        Vector3 dir = route.normalized;
        Vector3 right = Vector3.Cross(Vector3.up, dir);

        float usableLength = Mathf.Max(1f, routeLength - spawnStartEndPadding * 2f);
        float distanceAlong = Random.Range(0f, usableLength) + spawnStartEndPadding;
        float lateralOffset = Random.Range(-corridorHalfWidth, corridorHalfWidth);

        Vector3 point = currentRouteStart + dir * distanceAlong + right * lateralOffset;
        point.y = 0f;

        if (!IsPointInsideBounds(point, safeRouteBounds))
            return false;

        if (!TrySnapPointToTerrain(point, out Vector3 snappedPoint))
            return false;

        if (!IsSlopeValid(snappedPoint))
            return false;

        result = snappedPoint;
        return true;
    }

    private bool TrySnapPointToTerrain(Vector3 point, out Vector3 snappedPoint)
    {
        snappedPoint = point;
        Terrain t = GetTerrainUnderPosition(point);

        if (t == null)
            return false;

        float y = t.SampleHeight(point) + t.transform.position.y;
        snappedPoint = new Vector3(point.x, y, point.z);
        return true;
    }

    private bool IsSlopeValid(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return false;

        Vector3 terrainLocal = point - t.transform.position;
        Vector3 size = t.terrainData.size;

        float normX = Mathf.Clamp01(terrainLocal.x / size.x);
        float normZ = Mathf.Clamp01(terrainLocal.z / size.z);

        Vector3 normal = t.terrainData.GetInterpolatedNormal(normX, normZ);
        float slope = Vector3.Angle(normal, Vector3.up);

        return slope <= maxTerrainSlope;
    }

    private bool IsFarEnoughFromOthers(Vector3 point, GameObject prefab)
    {
        VehicleProfile newProfile = prefab.GetComponent<VehicleProfile>();
        if (newProfile == null)
            return true;

        for (int i = 0; i < spawnedVehicles.Count; i++)
        {
            if (spawnedVehicles[i] == null)
                continue;

            VehicleProfile existingProfile = spawnedVehicles[i].GetComponent<VehicleProfile>();
            if (existingProfile == null)
                continue;

            float dist = FlatDistance(point, spawnedVehicles[i].transform.position);

            float neededSpacing = Mathf.Max(newProfile.preferredSpacing, existingProfile.preferredSpacing);

            if (newProfile.faction == VehicleFaction.Civilian && existingProfile.faction == VehicleFaction.Military)
                neededSpacing *= 2f;
            else if (newProfile.faction == VehicleFaction.Military && existingProfile.faction == VehicleFaction.Civilian)
                neededSpacing *= 1.6f;

            if (dist < neededSpacing)
                return false;
        }

        return true;
    }

    private float FlatDistance(Vector3 a, Vector3 b)
    {
        a.y = 0f;
        b.y = 0f;
        return Vector3.Distance(a, b);
    }

    private bool IsPointInsideBounds(Vector3 point, Bounds bounds)
    {
        return point.x >= bounds.min.x &&
               point.x <= bounds.max.x &&
               point.z >= bounds.min.z &&
               point.z <= bounds.max.z;
    }

    private Terrain GetTerrainUnderPosition(Vector3 worldPos)
    {
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

    private Quaternion GetRandomGroundAlignedRotation()
    {
        float y = Random.Range(0f, 360f);
        return Quaternion.Euler(0f, y, 0f);
    }

    private void ClearSpawnedVehicles()
    {
        for (int i = 0; i < spawnedVehicles.Count; i++)
        {
            if (spawnedVehicles[i] != null)
                Destroy(spawnedVehicles[i]);
        }

        spawnedVehicles.Clear();
        usedSpawnPoints.Clear();
    }

    private Bounds CalculateCombinedTerrainBounds()
    {
        Terrain first = terrains[0];
        Vector3 firstPos = first.transform.position;
        Vector3 firstSize = first.terrainData.size;

        Bounds bounds = new Bounds(firstPos + firstSize * 0.5f, firstSize);

        for (int i = 1; i < terrains.Length; i++)
        {
            Terrain t = terrains[i];
            Vector3 pos = t.transform.position;
            Vector3 size = t.terrainData.size;

            Bounds terrainBounds = new Bounds(pos + size * 0.5f, size);
            bounds.Encapsulate(terrainBounds.min);
            bounds.Encapsulate(terrainBounds.max);
        }

        return bounds;
    }

    private Bounds CalculateSafeRouteBounds()
    {
        Bounds b = worldBounds;

        float safeSizeX = Mathf.Max(50f, b.size.x - routeInnerMarginX * 2f);
        float safeSizeZ = Mathf.Max(50f, b.size.z - routeInnerMarginZ * 2f);

        return new Bounds(
            b.center,
            new Vector3(safeSizeX, b.size.y, safeSizeZ)
        );
    }

    private Edge GetDifferentEdge(Edge start)
    {
        Edge result;
        do
        {
            result = (Edge)Random.Range(0, 4);
        } while (result == start);

        return result;
    }

    private Edge GetOppositeEdge(Edge edge)
    {
        switch (edge)
        {
            case Edge.Left: return Edge.Right;
            case Edge.Right: return Edge.Left;
            case Edge.Top: return Edge.Bottom;
            case Edge.Bottom: return Edge.Top;
            default: return Edge.Right;
        }
    }

    private Vector3 GetRandomPointOnEdge(Edge edge, Bounds bounds)
    {
        float minX = bounds.min.x + edgeInset;
        float maxX = bounds.max.x - edgeInset;
        float minZ = bounds.min.z + edgeInset;
        float maxZ = bounds.max.z - edgeInset;

        switch (edge)
        {
            case Edge.Left:
                return new Vector3(bounds.min.x + edgeInset, 0f, Random.Range(minZ, maxZ));
            case Edge.Right:
                return new Vector3(bounds.max.x - edgeInset, 0f, Random.Range(minZ, maxZ));
            case Edge.Top:
                return new Vector3(Random.Range(minX, maxX), 0f, bounds.max.z - edgeInset);
            case Edge.Bottom:
                return new Vector3(Random.Range(minX, maxX), 0f, bounds.min.z + edgeInset);
            default:
                return Vector3.zero;
        }
    }

    private void BuildPathOffsets()
    {
        generatedPathOffsets.Clear();

        int count = Mathf.Max(1, pathCount);

        float usableHalfWidth = Mathf.Max(2f, corridorHalfWidth - pathEdgePadding);
        float totalUsableWidth = usableHalfWidth * 2f;

        if (count == 1)
        {
            generatedPathOffsets.Add(0f);
            return;
        }

        float spacing = totalUsableWidth / (count - 1);

        for (int i = 0; i < count; i++)
        {
            float offset = -usableHalfWidth + spacing * i;
            generatedPathOffsets.Add(offset);
        }
    }

    private float GetRandomPathOffset()
    {
        if (generatedPathOffsets.Count == 0)
            BuildPathOffsets();

        return generatedPathOffsets[Random.Range(0, generatedPathOffsets.Count)];
    }
    private Vector3 GetSpawnPointOnPath(float pathOffset, int directionSign)
    {
        Vector3 route = currentRouteEnd - currentRouteStart;
        route.y = 0f;

        float routeLength = route.magnitude;
        if (routeLength < 1f)
            return currentRouteStart;

        Vector3 dir = route.normalized;
        Vector3 right = Vector3.Cross(Vector3.up, dir);

        float usableLength = Mathf.Max(1f, routeLength - spawnStartEndPadding * 2f);
        float along = Random.Range(0f, usableLength) + spawnStartEndPadding;

        Vector3 point = currentRouteStart + dir * along + right * pathOffset;
        point.y = 0f;

        if (TrySnapPointToTerrain(point, out Vector3 snapped))
            return snapped;

        return point;
    }

    private GameObject GetRandomMilitaryPrefab()
    {
        List<GameObject> pool = new List<GameObject>();

        if (tankPrefabs != null) pool.AddRange(tankPrefabs);
        if (zptPrefabs != null) pool.AddRange(zptPrefabs);
        if (truckPrefabs != null) pool.AddRange(truckPrefabs);

        if (pool.Count == 0)
            return null;

        return pool[Random.Range(0, pool.Count)];
    }
    private void SpawnSoloMilitary(int count)
    {
        for (int i = 0; i < count; i++)
        {
            GameObject prefab = GetRandomMilitaryPrefab();
            if (prefab == null)
                return;

            SpawnSingleVehicleWithPrefab(prefab);
        }
    }
    private void SpawnSingleVehicleWithPrefab(GameObject prefab)
    {
        for (int attempt = 0; attempt < maxSpawnAttemptsPerVehicle; attempt++)
        {
            VehicleProfile profile = prefab.GetComponent<VehicleProfile>();
            if (profile == null)
                return;

            int directionSign = 1;
            if (allowBidirectionalTraffic && Random.value < reverseDirectionChance)
                directionSign = -1;

            float pathOffset = GetRandomPathOffset();
            Vector3 spawnPoint = GetSpawnPointOnPath(pathOffset, directionSign);

            if (!IsPointInsideBounds(spawnPoint, safeRouteBounds))
                continue;

            if (!IsSlopeValid(spawnPoint))
                continue;

            if (!IsFarEnoughFromOthers(spawnPoint, prefab))
                continue;

            if (Physics.CheckSphere(spawnPoint + Vector3.up * 2f, minVehicleSpacing * 0.35f, vehicleBlockMask, QueryTriggerInteraction.Ignore))
                continue;

            Quaternion rot = GetRandomGroundAlignedRotation();
            GameObject go = Instantiate(prefab, spawnPoint, rot);

            spawnedVehicles.Add(go);
            usedSpawnPoints.Add(spawnPoint);

            PathFollowerVehicleAgent agent = go.GetComponent<PathFollowerVehicleAgent>();
            if (agent == null)
                agent = go.AddComponent<PathFollowerVehicleAgent>();

            agent.Initialize(
                spawnPoint,
                currentRouteStart,
                currentRouteEnd,
                pathOffset,
                directionSign,
                soloVehicleEndBehaviour,
                soloVehicleWaitAtEndTime
            );

            return;
        }
    }
    private void OnDrawGizmosSelected()
    {
        Terrain[] activeTerrains = Terrain.activeTerrains;

        if (activeTerrains != null && activeTerrains.Length > 0)
        {
            Terrain first = activeTerrains[0];
            Bounds bounds = new Bounds(first.transform.position + first.terrainData.size * 0.5f, first.terrainData.size);

            for (int i = 1; i < activeTerrains.Length; i++)
            {
                Terrain t = activeTerrains[i];
                Bounds tb = new Bounds(t.transform.position + t.terrainData.size * 0.5f, t.terrainData.size);
                bounds.Encapsulate(tb.min);
                bounds.Encapsulate(tb.max);
            }

            if (drawCombinedBounds)
            {
                Gizmos.color = Color.green;
                Gizmos.DrawWireCube(bounds.center, bounds.size);
            }

            if (drawSafeRouteBounds)
            {
                float safeSizeX = Mathf.Max(50f, bounds.size.x - routeInnerMarginX * 2f);
                float safeSizeZ = Mathf.Max(50f, bounds.size.z - routeInnerMarginZ * 2f);
                Bounds safe = new Bounds(bounds.center, new Vector3(safeSizeX, bounds.size.y, safeSizeZ));

                Gizmos.color = Color.white;
                Gizmos.DrawWireCube(safe.center, safe.size);
            }
        }

        if (drawCorridorGizmos && hasRoute)
        {
            Vector3 route = currentRouteEnd - currentRouteStart;
            route.y = 0f;

            if (route.sqrMagnitude > 0.01f)
            {
                Vector3 dir = route.normalized;
                Vector3 right = Vector3.Cross(Vector3.up, dir);

                Vector3 start = currentRouteStart + dir * spawnStartEndPadding;
                Vector3 end = currentRouteEnd - dir * spawnStartEndPadding;

                Vector3 a = start + right * corridorHalfWidth;
                Vector3 b = start - right * corridorHalfWidth;
                Vector3 c = end - right * corridorHalfWidth;
                Vector3 d = end + right * corridorHalfWidth;

                Gizmos.color = Color.yellow;
                Gizmos.DrawLine(a, b);
                Gizmos.DrawLine(b, c);
                Gizmos.DrawLine(c, d);
                Gizmos.DrawLine(d, a);

                Gizmos.color = Color.cyan;
                Gizmos.DrawLine(currentRouteStart, currentRouteEnd);
            }
        }
    }
}