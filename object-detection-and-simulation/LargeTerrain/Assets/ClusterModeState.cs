using UnityEngine;

public class ClusterModeState : MonoBehaviour
{
    public static ClusterModeState Instance { get; private set; }

    public bool IsClusterMode { get; private set; }

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }

        Instance = this;
        IsClusterMode = false;
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

        // MODE,CLSTR,1
        // MODE,CLSTR,0
        if (!line.StartsWith("MODE,CLSTR,"))
            return;

        string[] parts = line.Split(',');
        if (parts.Length < 3)
            return;

        IsClusterMode = parts[2] == "1";
        Debug.Log($"[CLUSTER MODE] IsClusterMode={IsClusterMode}");
    }

    public void SendClusterModeCommand(bool enabled)
    {
        string msg = enabled ? "CLSTR,1\n" : "CLSTR,0\n";
        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.EnqueueSend(msg);
    }
}