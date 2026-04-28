using UnityEngine;
using System;
using System.Globalization;

public class FireCommandBridge : MonoBehaviour
{
    [SerializeField] private DroneMissileController missileController;
    [SerializeField] private bool logIncomingMessages = true;

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

        if (!line.StartsWith("FIRE,"))
            return;

        if (logIncomingMessages)
            Debug.Log($"[FIRE IN] {line}");

        ParseAndDispatch(line);
    }

    private void ParseAndDispatch(string line)
    {
        if (missileController == null)
        {
            Debug.LogError("[FIRE] missileController atanmadı.");
            return;
        }

        string[] parts = line.Split(',');

        // FIRE,<frame_id>,<target_id>,<class_id>,<ammo_id>
        if (parts.Length < 5)
        {
            Debug.LogWarning($"[FIRE] Geçersiz satır: {line}");
            return;
        }

        string frameIdStr = parts[1];
        string targetId = parts[2];
        string classIdStr = parts[3];
        string ammoId = parts[4].Trim();

        if (string.IsNullOrWhiteSpace(targetId))
        {
            Debug.LogWarning("[FIRE] target_id boş.");
            return;
        }

        if (string.IsNullOrWhiteSpace(ammoId))
        {
            Debug.LogWarning("[FIRE] ammo_id boş.");
            return;
        }

        if (!int.TryParse(frameIdStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int frameId))
        {
            Debug.LogWarning($"[FIRE] frame_id parse edilemedi: {frameIdStr}");
            return;
        }

        if (!int.TryParse(classIdStr, NumberStyles.Integer, CultureInfo.InvariantCulture, out int classIdFromMessage))
        {
            Debug.LogWarning($"[FIRE] class_id parse edilemedi: {classIdStr}");
            return;
        }

        // target_id'nin ilk hanesinden sınıf çıkar
        char targetFirst = targetId[0];
        if (!char.IsDigit(targetFirst))
        {
            Debug.LogWarning($"[FIRE] target_id ilk karakteri rakam değil: {targetId}");
            return;
        }

        int classIdFromTarget = targetFirst - '0';

        // istersen doğrulama
        if (classIdFromMessage != classIdFromTarget)
        {
            Debug.LogWarning(
                $"[FIRE] class_id uyuşmuyor. message={classIdFromMessage}, target_id_first={classIdFromTarget}, target_id={targetId}"
            );
            // İstersen burada return de yapabilirsin.
            // Şimdilik target_id'den türetileni esas alıyoruz.
        }

        // ammo_id ilk hanesi mühimmat tipi
        char ammoFirst = ammoId[0];
        if (!char.IsDigit(ammoFirst))
        {
            Debug.LogWarning($"[FIRE] ammo_id ilk karakteri rakam değil: {ammoId}");
            return;
        }

        int missileTypeId = ammoFirst - '0';

        if (missileTypeId < 0 || missileTypeId > 2)
        {
            Debug.LogWarning($"[FIRE] Geçersiz missileTypeId: {missileTypeId} | ammo_id={ammoId}");
            return;
        }

        MissileFireCommand cmd = new MissileFireCommand
        {
            targetExternalId = targetId,
            missileTypeId = missileTypeId,
            missileExternalId = ammoId,
            timestamp = DateTime.UtcNow.ToString("O"),
            frameId = frameId
        };

        bool launched = missileController.ProcessFireCommand(cmd);

        Debug.Log(
            $"[FIRE PROC] launched={launched} " +
            $"frameId={frameId} " +
            $"targetId={targetId} " +
            $"classId={classIdFromTarget} " +
            $"ammoId={ammoId} " +
            $"missileTypeId={missileTypeId}"
        );
    }
}