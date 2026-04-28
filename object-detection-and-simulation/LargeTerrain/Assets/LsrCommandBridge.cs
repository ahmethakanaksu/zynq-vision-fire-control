using UnityEngine;
using System;
using System.Globalization;
using System.Text;
using System.Collections.Generic;

public class LsrCommandBridge : MonoBehaviour
{
    [SerializeField] private DroneShotProcessor shotProcessor;
    [SerializeField] private bool logIncomingMessages = true;
    [SerializeField] private bool logOutgoingMessages = true;

    private struct LsrResultItem
    {
        public string trackId;
        public int hit; // 1 or 0
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

        if (!line.StartsWith("LSRCMD,"))
            return;

        if (logIncomingMessages)
            Debug.Log($"[LSRCMD IN] {line}");

        ParseDispatchAndRespond(line);
    }

    private void ParseDispatchAndRespond(string line)
    {
        if (shotProcessor == null)
        {
            Debug.LogError("[LSRCMD] shotProcessor atanmadı.");
            return;
        }

        string[] parts = line.Split(',');

        if (parts.Length < 3)
        {
            Debug.LogWarning($"[LSRCMD] Geçersiz satır: {line}");
            return;
        }

        string frameId = parts[1];

        if (!int.TryParse(parts[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int count))
        {
            Debug.LogWarning($"[LSRCMD] count parse edilemedi: {line}");
            return;
        }

        int expectedLength = 3 + (count * 3);
        if (parts.Length < expectedLength)
        {
            Debug.LogWarning($"[LSRCMD] Eksik alan var. Beklenen min alan: {expectedLength}, gelen: {parts.Length} | {line}");
            return;
        }

        List<LsrResultItem> resultItems = new List<LsrResultItem>(count);

        int index = 3;

        for (int i = 0; i < count; i++)
        {
            string trackId = parts[index++];
            string pitchStr = parts[index++];
            string yawStr = parts[index++];

            if (string.IsNullOrWhiteSpace(trackId))
            {
                Debug.LogWarning("[LSRCMD] track_id boş.");
                resultItems.Add(new LsrResultItem { trackId = "", hit = 0 });
                continue;
            }

            char firstChar = trackId[0];
            if (!char.IsDigit(firstChar))
            {
                Debug.LogWarning($"[LSRCMD] track_id ilk karakteri rakam değil: {trackId}");
                resultItems.Add(new LsrResultItem { trackId = trackId, hit = 0 });
                continue;
            }

            int classId = firstChar - '0';
            if (classId < 0 || classId > 3)
            {
                Debug.LogWarning($"[LSRCMD] classId geçersiz: {classId} | trackId={trackId}");
                resultItems.Add(new LsrResultItem { trackId = trackId, hit = 0 });
                continue;
            }

            if (!int.TryParse(pitchStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int pitchMdeg))
            {
                Debug.LogWarning($"[LSRCMD] pitch parse edilemedi: {pitchStr}");
                resultItems.Add(new LsrResultItem { trackId = trackId, hit = 0 });
                continue;
            }

            if (!int.TryParse(yawStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int yawMdeg))
            {
                Debug.LogWarning($"[LSRCMD] yaw parse edilemedi: {yawStr}");
                resultItems.Add(new LsrResultItem { trackId = trackId, hit = 0 });
                continue;
            }

            float pitchDeg = pitchMdeg / 100000.0f;
            float yawDeg = yawMdeg / 100000.0f;

            DetectionInput input = new DetectionInput
            {
                classId = classId,
                externalTargetId = trackId,
                timestamp = DateTime.UtcNow.ToString("o"),
                frameId = frameId,
                rotationEuler = new Vector3(yawDeg, 0f, pitchDeg)
            };

            ShotResult shotResult = shotProcessor.ProcessDetectionInput(input);

            int hit = shotResult.classMatched ? 1 : 0;

            resultItems.Add(new LsrResultItem
            {
                trackId = trackId,
                hit = hit
            });

            Debug.Log(
                $"[LSRCMD PROC] frame={frameId} " +
                $"trackId={trackId} classId={classId} " +
                $"pitch={pitchDeg:F3} yaw={yawDeg:F3} " +
                $"classMatched={shotResult.classMatched}"
            );
        }

        string response = BuildLsrResMessage(frameId, resultItems);

        if (logOutgoingMessages)
            Debug.Log($"[LSRRES OUT] {response.TrimEnd()}");

        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.EnqueueSend(response);
    }

    private string BuildLsrResMessage(string frameId, List<LsrResultItem> items)
    {
        StringBuilder sb = new StringBuilder(128);

        sb.Append("LSRRES,");
        sb.Append(frameId);
        sb.Append(",");
        sb.Append(items.Count);

        for (int i = 0; i < items.Count; i++)
        {
            sb.Append(",");
            sb.Append(items[i].trackId);
            sb.Append(",");
            sb.Append(items[i].hit);
        }

        sb.Append("\n");
        return sb.ToString();
    }
}