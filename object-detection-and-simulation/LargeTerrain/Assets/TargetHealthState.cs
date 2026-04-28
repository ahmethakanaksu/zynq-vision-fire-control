using UnityEngine;

public class TargetHealthState : MonoBehaviour
{
    [Header("Death FX")]
    [SerializeField] private GameObject burningEffectPrefab;
    [SerializeField] private Vector3 burningEffectOffset = new Vector3(0f, 1.5f, 0f);
    [SerializeField] private Vector3 burningEffectScale = Vector3.one;

    private bool isDead = false;
    private GameObject activeBurningEffect;

    public bool IsDead => isDead;

    public void Kill()
    {
        if (isDead)
            return;

        isDead = true;

        StopMovement();
        SpawnBurningEffect();

        Debug.Log($"[TARGET DEAD] {gameObject.name}");
    }

    [ContextMenu("Kill Vehicle")]
    public void KillVehicleFromContextMenu()
    {
        Kill();
    }

    [ContextMenu("Revive Vehicle")]
    public void ReviveVehicleFromContextMenu()
    {
        isDead = false;
    }

    private void StopMovement()
    {
        PathFollowerVehicleAgent pathAgent = GetComponent<PathFollowerVehicleAgent>();
        if (pathAgent != null)
            pathAgent.enabled = false;

        ConvoyVehicleMember convoyMember = GetComponent<ConvoyVehicleMember>();
        if (convoyMember != null)
            convoyMember.enabled = false;

        Rigidbody rb = GetComponent<Rigidbody>();
        if (rb != null)
        {
            rb.linearVelocity = Vector3.zero;
            rb.angularVelocity = Vector3.zero;
            rb.isKinematic = true;
        }
    }

    private void SpawnBurningEffect()
    {
        if (burningEffectPrefab == null || activeBurningEffect != null)
            return;

        activeBurningEffect = Instantiate(
            burningEffectPrefab,
            transform
        );

        activeBurningEffect.transform.localPosition = burningEffectOffset;
        activeBurningEffect.transform.localRotation = Quaternion.identity;
        activeBurningEffect.transform.localScale = burningEffectScale;
    }
}