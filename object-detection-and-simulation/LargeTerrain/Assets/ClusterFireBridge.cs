using UnityEngine;
using System;
using System.Globalization;

public class ClusterFireBridge : MonoBehaviour
{
    [SerializeField] private DroneMissileController missileController;
    [SerializeField] private ClusterTargetProxyManager clusterTargetProxyManager;
    [SerializeField] private bool logMessages = true;

    private void OnEnable()
    {
        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.OnLineReceived += HandleIncomingLine;
    }

    private void OnDisable()
    {
        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.OnLineReceived -= HandleIncomingLine;
    }

    private void HandleIncomingLine(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
            return;

        if (!line.StartsWith("CFIRE,"))
            return;

        if (ClusterModeState.Instance == null || !ClusterModeState.Instance.IsClusterMode)
            return;

        if (logMessages)
            Debug.Log($"[CFIRE IN] {line}");

        ParseAndDispatch(line);
    }

    private void ParseAndDispatch(string line)
    {
        if (missileController == null || clusterTargetProxyManager == null)
        {
            Debug.LogError("[CFIRE] missileController veya clusterTargetProxyManager atanmadı.");
            return;
        }

        string[] parts = line.Split(',');

        // CFIRE,<decision_frame>,<cluster_id>,<cluster_score>,<missile_id>
        if (parts.Length < 5)
        {
            Debug.LogWarning($"[CFIRE] Geçersiz satır: {line}");
            return;
        }

        if (!int.TryParse(parts[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int frameId))
        {
            Debug.LogWarning($"[CFIRE] decision_frame parse edilemedi: {parts[1]}");
            return;
        }

        if (!int.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int clusterId))
        {
            Debug.LogWarning($"[CFIRE] cluster_id parse edilemedi: {parts[2]}");
            return;
        }

        string missileId = parts[4].Trim();
        if (string.IsNullOrWhiteSpace(missileId) || !char.IsDigit(missileId[0]))
        {
            Debug.LogWarning($"[CFIRE] missile_id geçersiz: {missileId}");
            return;
        }

        int missileTypeId = missileId[0] - '0'; // 0=cirit,1=maml,2=som
        if (missileTypeId < 0 || missileTypeId > 2)
        {
            Debug.LogWarning($"[CFIRE] missileTypeId geçersiz: {missileTypeId}");
            return;
        }

        string targetExternalId = clusterTargetProxyManager.GetClusterExternalId(clusterId);

        MissileFireCommand cmd = new MissileFireCommand
        {
            targetExternalId = targetExternalId,
            missileTypeId = missileTypeId,
            missileExternalId = missileId,
            timestamp = DateTime.UtcNow.ToString("O"),
            frameId = frameId
        };

        bool launched = missileController.ProcessFireCommand(cmd);

        Debug.Log(
            $"[CFIRE PROC] launched={launched} " +
            $"frameId={frameId} " +
            $"clusterId={clusterId} " +
            $"targetExternalId={targetExternalId} " +
            $"missileId={missileId} " +
            $"missileTypeId={missileTypeId}"
        );
    }
}