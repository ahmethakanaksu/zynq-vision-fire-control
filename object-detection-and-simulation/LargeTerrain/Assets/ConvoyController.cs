using System.Collections.Generic;
using UnityEngine;

public class ConvoyController : MonoBehaviour
{
    [Header("Debug")]
    [SerializeField] private bool drawDebug = false;

    private Terrain[] terrains;

    private Vector3 corridorStart;
    private Vector3 corridorEnd;
    private Vector3 corridorDir;
    private Vector3 corridorRight;

    private float pathOffset;
    private int directionSign = 1;
    private float currentAlong;
    private float minAlong;
    private float maxAlong;

    private float convoyCruiseSpeed = 5f;
    private float currentSpeed = 0f;
    private float convoyAcceleration = 2f;
    private float convoyBraking = 3f;

    private PathEndBehaviour endBehaviour = PathEndBehaviour.WaitAndReturn;
    private float waitAtEndTime = 3f;
    private float waitTimer = 0f;

    private bool initialized = false;
    private bool waitingAtEnd = false;
    private bool stopped = false;

    private readonly List<ConvoyVehicleMember> members = new List<ConvoyVehicleMember>();
    private readonly List<Vector3> memberOffsets = new List<Vector3>();

    public bool IsStopped => stopped;
    public float PathOffset => pathOffset;
    public int DirectionSign => directionSign;

    public void Initialize(
        Vector3 routeStart,
        Vector3 routeEnd,
        float assignedPathOffset,
        int moveDirectionSign,
        float startAlong,
        float speed,
        float accel,
        float braking,
        PathEndBehaviour behaviour,
        float waitTime
    )
    {
        terrains = Terrain.activeTerrains;

        corridorStart = routeStart;
        corridorEnd = routeEnd;

        corridorDir = corridorEnd - corridorStart;
        corridorDir.y = 0f;
        corridorDir = corridorDir.sqrMagnitude > 0.001f ? corridorDir.normalized : Vector3.forward;

        corridorRight = Vector3.Cross(Vector3.up, corridorDir).normalized;

        pathOffset = assignedPathOffset;
        directionSign = moveDirectionSign >= 0 ? 1 : -1;

        convoyCruiseSpeed = speed;
        convoyAcceleration = accel;
        convoyBraking = braking;

        endBehaviour = behaviour;
        waitAtEndTime = waitTime;

        currentAlong = startAlong;

        float totalLength = Vector3.Distance(
            new Vector3(corridorStart.x, 0f, corridorStart.z),
            new Vector3(corridorEnd.x, 0f, corridorEnd.z)
        );

        minAlong = 8f;
        maxAlong = totalLength - 8f;

        transform.position = GetAnchorWorldPosition(currentAlong);
        initialized = true;
    }

    public void RegisterMember(ConvoyVehicleMember member, Vector3 localOffset)
    {
        if (!members.Contains(member))
        {
            members.Add(member);
            memberOffsets.Add(localOffset);
        }
    }

    private void Update()
    {
        if (!initialized || stopped)
            return;

        if (waitingAtEnd)
        {
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, convoyBraking * Time.deltaTime);
            waitTimer -= Time.deltaTime;

            if (waitTimer <= 0f)
            {
                waitingAtEnd = false;
                directionSign *= -1;
            }

            return;
        }

        float slowFactor = ComputeConvoySlowdownFactor();
        float targetSpeed = convoyCruiseSpeed * slowFactor;

        if (slowFactor < 0.35f)
            currentSpeed = Mathf.MoveTowards(currentSpeed, 0f, convoyBraking * Time.deltaTime);
        else
            currentSpeed = Mathf.MoveTowards(currentSpeed, targetSpeed, convoyAcceleration * Time.deltaTime);

        float alongStep = currentSpeed * Time.deltaTime * directionSign;
        float nextAlong = currentAlong + alongStep;

        bool reachedBoundary = (directionSign > 0 && nextAlong >= maxAlong) || (directionSign < 0 && nextAlong <= minAlong);

        if (reachedBoundary)
        {
            currentAlong = directionSign > 0 ? maxAlong : minAlong;
            transform.position = GetAnchorWorldPosition(currentAlong);

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
                    DestroyConvoy();
                    break;
            }

            return;
        }

        currentAlong = nextAlong;
        transform.position = GetAnchorWorldPosition(currentAlong);
    }

    public Vector3 GetWorldSlotPosition(Vector3 localOffset)
    {
        Vector3 forward = corridorDir * directionSign;
        Vector3 right = Vector3.Cross(Vector3.up, forward).normalized;

        Vector3 anchor = GetAnchorWorldPosition(currentAlong);
        Vector3 world = anchor + right * localOffset.x + forward * localOffset.z;
        return world;
    }

    public Vector3 GetLookDirectionForMember(ConvoyVehicleMember member, Vector3 localOffset, Vector3 currentMemberPos)
    {
        Vector3 forward = corridorDir * directionSign;
        Vector3 slotPos = GetWorldSlotPosition(localOffset);

        Vector3 toSlot = slotPos - currentMemberPos;
        toSlot.y = 0f;

        if (toSlot.sqrMagnitude > 0.25f)
            return toSlot.normalized;

        return forward;
    }

    public float GetMemberMoveSpeed(ConvoyVehicleMember member, Vector3 localOffset, float distanceToSlot)
    {
        VehicleProfile profile = member.GetComponent<VehicleProfile>();
        if (profile == null)
            return convoyCruiseSpeed;

        float catchUpBonus = 0f;
        if (distanceToSlot > 6f)
            catchUpBonus = Mathf.Min(3f, (distanceToSlot - 6f) * 0.4f);

        float baseSpeed = Mathf.Clamp(
            convoyCruiseSpeed + catchUpBonus,
            profile.minCruiseSpeed,
            profile.maxCruiseSpeed + 2f
        );

        return baseSpeed;
    }

    private float ComputeConvoySlowdownFactor()
    {
        // çok geride kalan varsa leader hafif yavaşlasın
        float maxDistFromSlot = 0f;

        for (int i = 0; i < members.Count; i++)
        {
            if (members[i] == null)
                continue;

            Vector3 desired = GetWorldSlotPosition(memberOffsets[i]);
            float dist = Vector3.Distance(
                new Vector3(desired.x, 0f, desired.z),
                new Vector3(members[i].transform.position.x, 0f, members[i].transform.position.z)
            );

            if (dist > maxDistFromSlot)
                maxDistFromSlot = dist;
        }

        if (maxDistFromSlot > 20f)
            return 0.45f;

        if (maxDistFromSlot > 12f)
            return 0.7f;

        return 1f;
    }

    private Vector3 GetAnchorWorldPosition(float along)
    {
        Vector3 p = corridorStart + corridorDir * along + corridorRight * pathOffset;
        return SnapPointToTerrain(p);
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

    private void DestroyConvoy()
    {
        for (int i = 0; i < members.Count; i++)
        {
            if (members[i] != null)
                Destroy(members[i].gameObject);
        }

        Destroy(gameObject);
    }

    private void OnDrawGizmosSelected()
    {
        if (!drawDebug || !initialized)
            return;

        Gizmos.color = Color.yellow;
        Gizmos.DrawSphere(transform.position, 2f);

        for (int i = 0; i < memberOffsets.Count; i++)
        {
            Gizmos.color = Color.cyan;
            Gizmos.DrawSphere(GetWorldSlotPosition(memberOffsets[i]), 1.2f);
        }
    }
}