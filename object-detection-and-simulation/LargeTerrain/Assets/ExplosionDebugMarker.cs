using UnityEngine;

public class ExplosionDebugMarker : MonoBehaviour
{
    [SerializeField] private float radius = 10f;
    [SerializeField] private float lifetime = 1.2f;
    [SerializeField] private Color gizmoColor = Color.red;

    private bool destroyScheduled = false;

    private void Start()
    {
        ScheduleDestroyIfNeeded();
    }

    public void Initialize(float blastRadius, float life)
    {
        radius = blastRadius;
        lifetime = life;
        ScheduleDestroyIfNeeded();
    }

    private void ScheduleDestroyIfNeeded()
    {
        if (destroyScheduled)
            return;

        destroyScheduled = true;
        Destroy(gameObject, lifetime);
    }

    private void OnDrawGizmos()
    {
        Gizmos.color = gizmoColor;
        Gizmos.DrawWireSphere(transform.position, radius);
    }
}