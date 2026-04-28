using UnityEngine;

[RequireComponent(typeof(TargetInfo))]
public class TargetRegistryMember : MonoBehaviour
{
    private TargetInfo targetInfo;

    private void Awake()
    {
        targetInfo = GetComponent<TargetInfo>();
    }

    private void Start()
    {
        if (TargetRegistry.Instance != null)
            TargetRegistry.Instance.RegisterTarget(targetInfo);
            Debug.Log($"[TARGET REGISTER] {targetInfo.ExternalTargetId} -> {gameObject.name}");
    }

    private void OnDestroy()
    {
        if (TargetRegistry.Instance != null)
            TargetRegistry.Instance.UnregisterTarget(targetInfo);
    }
}