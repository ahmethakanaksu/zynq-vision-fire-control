using UnityEngine;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

public class ClusterLsrBridge : MonoBehaviour
{
    [SerializeField] private DroneShotProcessor shotProcessor;
    [SerializeField] private ClusterTargetProxyManager clusterTargetProxyManager;
    [SerializeField] private bool logMessages = true;

    private struct ClusterResult
    {
        public int clusterId;
        public int hit;
    }

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

        if (!line.StartsWith("CLSRCMD,"))
            return;

        if (ClusterModeState.Instance == null || !ClusterModeState.Instance.IsClusterMode)
            return;

        if (logMessages)
            Debug.Log($"[CLSRCMD IN] {line}");

        ParseDispatchAndRespond(line);
    }

    private void ParseDispatchAndRespond(string line)
    {
        if (shotProcessor == null)
        {
            Debug.LogError("[CLSRCMD] shotProcessor atanmadı.");
            return;
        }

        string[] parts = line.Split(',');

        if (parts.Length < 3)
        {
            Debug.LogWarning($"[CLSRCMD] Geçersiz satır: {line}");
            return;
        }

        string frameId = parts[1];

        if (!int.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int clusterCount))
        {
            Debug.LogWarning($"[CLSRCMD] cluster_count parse edilemedi: {line}");
            return;
        }

        int index = 3;
        List<ClusterResult> clusterResults = new List<ClusterResult>(clusterCount);

        for (int c = 0; c < clusterCount; c++)
        {
            if (index + 1 >= parts.Length)
            {
                Debug.LogWarning($"[CLSRCMD] cluster header eksik: {line}");
                break;
            }

            if (!int.TryParse(parts[index++], NumberStyles.Integer, CultureInfo.InvariantCulture, out int clusterId))
            {
                Debug.LogWarning("[CLSRCMD] cluster_id parse edilemedi.");
                return;
            }

            if (!int.TryParse(parts[index++], NumberStyles.Integer, CultureInfo.InvariantCulture, out int memberCount))
            {
                Debug.LogWarning("[CLSRCMD] member_count parse edilemedi.");
                return;
            }

            bool clusterHit = false;
            List<string> memberTrackIds = new List<string>(memberCount);

            for (int m = 0; m < memberCount; m++)
            {
                if (index + 2 >= parts.Length)
                {
                    Debug.LogWarning($"[CLSRCMD] member alanları eksik: {line}");
                    break;
                }

                string trackId = parts[index++];
                string pitchStr = parts[index++];
                string yawStr = parts[index++];

                memberTrackIds.Add(trackId);

                if (string.IsNullOrWhiteSpace(trackId) || !char.IsDigit(trackId[0]))
                    continue;

                int classId = trackId[0] - '0';

                if (!int.TryParse(pitchStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int pitchMdeg))
                    continue;

                if (!int.TryParse(yawStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int yawMdeg))
                    continue;

                float pitchDeg = pitchMdeg / 1000.0f;
                float yawDeg = yawMdeg / 1000.0f;

                DetectionInput input = new DetectionInput
                {
                    classId = classId,
                    externalTargetId = trackId,
                    timestamp = DateTime.UtcNow.ToString("O"),
                    frameId = frameId,
                    rotationEuler = new Vector3(pitchDeg, yawDeg, 0f)
                };

                ShotResult result = shotProcessor.ProcessDetectionInput(input);

                if (result.classMatched)
                    clusterHit = true;
            }

            clusterResults.Add(new ClusterResult
            {
                clusterId = clusterId,
                hit = clusterHit ? 1 : 0
            });

            if (clusterTargetProxyManager != null)
            {
                clusterTargetProxyManager.UpdateClusterProxy(clusterId, memberTrackIds);
            }
        }

        string response = BuildClusterResultMessage(frameId, clusterResults);

        if (logMessages)
            Debug.Log($"[CLSRRES OUT] {response.TrimEnd()}");

        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.EnqueueSend(response);
    }

    private string BuildClusterResultMessage(string frameId, List<ClusterResult> results)
    {
        StringBuilder sb = new StringBuilder(128);
        sb.Append("CLSRRES,");
        sb.Append(frameId);
        sb.Append(",");
        sb.Append(results.Count);

        for (int i = 0; i < results.Count; i++)
        {
            sb.Append(",");
            sb.Append(results[i].clusterId);
            sb.Append(",");
            sb.Append(results[i].hit);
        }

        sb.Append("\n");
        return sb.ToString();
    }
}