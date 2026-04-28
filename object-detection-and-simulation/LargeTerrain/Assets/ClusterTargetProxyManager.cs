using UnityEngine;
using System.Collections.Generic;

public class ClusterTargetProxyManager : MonoBehaviour
{
    [SerializeField] private GameObject clusterTargetPrefab;
    [SerializeField] private string clusterTargetPrefix = "cluster_";

    private readonly Dictionary<int, TargetInfo> proxiesByClusterId = new Dictionary<int, TargetInfo>();

    public string GetClusterExternalId(int clusterId)
    {
        return $"{clusterTargetPrefix}{clusterId}";
    }

    public void UpdateClusterProxy(int clusterId, List<string> memberTrackIds)
    {
        if (clusterTargetPrefab == null || memberTrackIds == null || memberTrackIds.Count == 0)
            return;

        Vector3 centroid = Vector3.zero;
        int validCount = 0;

        for (int i = 0; i < memberTrackIds.Count; i++)
        {
            string trackId = memberTrackIds[i];

            if (TargetRegistry.Instance == null)
                continue;

            Vector3? pos = TargetRegistry.Instance.GetTargetPosition(trackId);
            if (pos.HasValue)
            {
                centroid += pos.Value;
                validCount++;
            }
        }

        if (validCount == 0)
            return;

        centroid /= validCount;

        TargetInfo proxy = GetOrCreateProxy(clusterId);
        if (proxy == null)
            return;

        proxy.transform.position = centroid;
    }

    private TargetInfo GetOrCreateProxy(int clusterId)
    {
        if (proxiesByClusterId.TryGetValue(clusterId, out TargetInfo existing) && existing != null)
            return existing;

        GameObject go = Instantiate(clusterTargetPrefab, transform);
        go.name = $"ClusterTarget_{clusterId}";

        TargetInfo targetInfo = go.GetComponent<TargetInfo>();
        if (targetInfo == null)
        {
            Debug.LogError("[ClusterTargetProxyManager] Prefab üzerinde TargetInfo yok.");
            Destroy(go);
            return null;
        }

        targetInfo.SetExternalTargetId(GetClusterExternalId(clusterId));
        proxiesByClusterId[clusterId] = targetInfo;

        return targetInfo;
    }
}