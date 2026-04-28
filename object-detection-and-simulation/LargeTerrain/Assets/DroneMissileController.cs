using System.Collections.Generic;
using UnityEngine;

public class DroneMissileController : MonoBehaviour
{
    [Header("References")]
    [SerializeField] private Transform launchForwardReference;

    [Header("Missile Inventory")]
    [SerializeField] private GuidedMissile[] ciritMissiles;
    [SerializeField] private GuidedMissile[] mamlMissiles;
    [SerializeField] private GuidedMissile[] somMissiles;

    [Header("Test Fire Command")]
    [SerializeField] private string testTargetExternalId = "target_001";
    [SerializeField] private int testMissileTypeId = 0;
    [SerializeField] private string testMissileExternalId = "missile_test_001";
    [SerializeField] private string testTimestamp = "test_fire";
    [SerializeField] private int testFrameId = 0;

    [Header("Test Guidance Command")]
    [SerializeField] private string testGuidanceMissileId = "missile_test_001";
    [SerializeField] private Vector3 testGuidanceRotationEuler = Vector3.zero;
    [SerializeField] private string testGuidanceTimestamp = "test_guidance";
    [SerializeField] private int testGuidanceFrameId = 0;

    private readonly Dictionary<string, GuidedMissile> activeMissilesByExternalId = new Dictionary<string, GuidedMissile>();

    private void Awake()
    {
        if (launchForwardReference == null)
            launchForwardReference = transform;
    }

    [ContextMenu("Test Fire Missile")]
    public void TestFireMissile()
    {
        MissileFireCommand cmd = new MissileFireCommand
        {
            targetExternalId = testTargetExternalId,
            missileTypeId = testMissileTypeId,
            missileExternalId = testMissileExternalId,
            timestamp = testTimestamp,
            frameId = testFrameId
        };

        ProcessFireCommand(cmd);
    }

    [ContextMenu("Test Guidance Update")]
    public void TestGuidanceUpdate()
    {
        MissileGuidanceCommand cmd = new MissileGuidanceCommand
        {
            missileExternalId = testGuidanceMissileId,
            rotationEuler = testGuidanceRotationEuler,
            timestamp = testGuidanceTimestamp,
            frameId = testGuidanceFrameId
        };

        ProcessGuidanceCommand(cmd);
    }

    private void Update()
    {
        if (Input.GetKeyDown(KeyCode.F))
            TestFireMissile();

        if (Input.GetKeyDown(KeyCode.G))
            TestGuidanceUpdate();
    }

    public bool ProcessFireCommand(MissileFireCommand command)
    {
        MissileTypeId missileType = (MissileTypeId)command.missileTypeId;

        GuidedMissile missile = GetNextAvailableMissile(missileType);
        if (missile == null)
        {
            Debug.LogWarning($"[MISSILE FIRE] No available missile for type={command.missileTypeId}");
            return false;
        }

        if (activeMissilesByExternalId.ContainsKey(command.missileExternalId))
        {
            Debug.LogWarning($"[MISSILE FIRE] missileExternalId already active: {command.missileExternalId}");
            return false;
        }

        Vector3 launchForward = launchForwardReference.forward;

        missile.Launch(
            this,
            command.missileExternalId,
            command.targetExternalId,
            missileType,
            launchForward
        );

        activeMissilesByExternalId[command.missileExternalId] = missile;

        Debug.Log(
            $"[MISSILE FIRE] launched=true " +
            $"missileId={command.missileExternalId} " +
            $"targetId={command.targetExternalId} " +
            $"missileType={command.missileTypeId} " +
            $"timestamp={command.timestamp} " +
            $"frameId={command.frameId}"
        );

        return true;
    }

    public bool ProcessGuidanceCommand(MissileGuidanceCommand command)
    {
        if (!activeMissilesByExternalId.TryGetValue(command.missileExternalId, out GuidedMissile missile) || missile == null)
        {
            Debug.LogWarning($"[MISSILE GUIDANCE] active missile not found: {command.missileExternalId}");
            return false;
        }

        missile.ApplyGuidanceRotation(command.rotationEuler);

        Debug.Log(
            $"[MISSILE GUIDANCE] missileId={command.missileExternalId} " +
            $"rotation={command.rotationEuler} " +
            $"timestamp={command.timestamp} " +
            $"frameId={command.frameId}"
        );

        return true;
    }

    public void EmitTelemetryForMissile(GuidedMissile missile)
    {
        if (missile == null)
            return;

        Vector3 targetPos = Vector3.zero;
        bool hasTargetPos = false;

        if (TargetRegistry.Instance != null)
        {
            Vector3? trackedPos = TargetRegistry.Instance.GetTargetPosition(missile.TargetExternalId);
            Debug.Log($"[TARGET LOOKUP] id={missile.TargetExternalId} found={trackedPos.HasValue}");
            if (trackedPos.HasValue)
            {
                targetPos = trackedPos.Value;
                hasTargetPos = true;
            }
        }

        MissileTelemetry telemetry = new MissileTelemetry
        {
            missileExternalId = missile.MissileExternalId,
            targetExternalId = missile.TargetExternalId,
            missileTypeId = (int)missile.MissileType,
            missilePosition = missile.transform.position,
            missileRotationEuler = missile.transform.rotation.eulerAngles,
            targetPosition = hasTargetPos ? targetPos : Vector3.zero,
            timestamp = System.DateTime.UtcNow.ToString("O"),
            frameId = Time.frameCount
        };

        Debug.Log(
            $"[MISSILE TELEMETRY] " +
            $"missileId={telemetry.missileExternalId} " +
            $"targetId={telemetry.targetExternalId} " +
            $"missileType={telemetry.missileTypeId} " +
            $"missilePos={telemetry.missilePosition} " +
            $"missileRot={telemetry.missileRotationEuler} " +
            $"targetPos={telemetry.targetPosition} " +
            $"timestamp={telemetry.timestamp} " +
            $"frameId={telemetry.frameId}"
        );
    }

    public void NotifyMissileDestroyed(GuidedMissile missile)
    {
        if (missile == null)
            return;

        if (!string.IsNullOrWhiteSpace(missile.MissileExternalId))
            activeMissilesByExternalId.Remove(missile.MissileExternalId);
    }

    public bool TryGetActiveMissile(string missileExternalId, out GuidedMissile missile)
    {
        return activeMissilesByExternalId.TryGetValue(missileExternalId, out missile);
    }

    public IEnumerable<GuidedMissile> GetAllActiveMissiles()
    {
        return activeMissilesByExternalId.Values;
    }

    private GuidedMissile GetNextAvailableMissile(MissileTypeId type)
    {
        GuidedMissile[] pool = GetPool(type);
        if (pool == null || pool.Length == 0)
            return null;

        for (int i = 0; i < pool.Length; i++)
        {
            if (pool[i] != null && !pool[i].IsLaunched)
                return pool[i];
        }

        return null;
    }

    private GuidedMissile[] GetPool(MissileTypeId type)
    {
        switch (type)
        {
            case MissileTypeId.Cirit:
                return ciritMissiles;
            case MissileTypeId.MAML:
                return mamlMissiles;
            case MissileTypeId.SOM:
                return somMissiles;
            default:
                return null;
        }
    }
}