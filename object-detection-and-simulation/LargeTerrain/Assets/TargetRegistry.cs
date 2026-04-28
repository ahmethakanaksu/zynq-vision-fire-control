using System.Collections.Generic;
using UnityEngine;
using System.Collections.Generic;
public class TargetRegistry : MonoBehaviour
{
    public static TargetRegistry Instance { get; private set; }

    private readonly Dictionary<string, TargetInfo> targetsByExternalId = new Dictionary<string, TargetInfo>();

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }

        Instance = this;
    }

    public void RegisterTarget(TargetInfo info)
    {
        if (info == null)
            return;

        string id = info.ExternalTargetId;
        if (string.IsNullOrWhiteSpace(id))
            return;

        targetsByExternalId[id] = info;
    }

    public void UnregisterTarget(TargetInfo info)
    {
        if (info == null)
            return;

        List<string> keysToRemove = new List<string>();

        foreach (var kvp in targetsByExternalId)
        {
            if (kvp.Value == info)
                keysToRemove.Add(kvp.Key);
        }

        for (int i = 0; i < keysToRemove.Count; i++)
        {
            targetsByExternalId.Remove(keysToRemove[i]);
        }
    }

    public bool TryGetTarget(string externalId, out TargetInfo info)
    {
        return targetsByExternalId.TryGetValue(externalId, out info);
    }

    public Vector3? GetTargetPosition(string externalId)
    {
        if (targetsByExternalId.TryGetValue(externalId, out TargetInfo info) && info != null)
            return info.transform.position;

        return null;
    }
}