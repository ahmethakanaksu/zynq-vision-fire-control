
using TMPro;
using UnityEngine;


[RequireComponent(typeof(Collider))]
public class GuidedMissile : MonoBehaviour
{
    [Header("Flight")]
    [SerializeField] private float flightSpeed = 140f;
    [SerializeField] private float rotationResponsiveness = 4f;
    [SerializeField] private bool useLocalRelativeGuidanceRotation = false;
    [SerializeField] private float maxRotationDegreesPerSecond = 120f;
    [SerializeField] private GameObject CamContainerPrefab;
    [SerializeField] private GameObject missileCam;
    [SerializeField] private TMP_Text fuelText;

    [Header("FX")]
    [SerializeField] private GameObject smokeObject;
    [SerializeField] private GameObject flameObject;

    [Header("Explosion")]
    [SerializeField] private float ciritBlastRadius = 8f;
    [SerializeField] private float mamlBlastRadius = 14f;
    [SerializeField] private float somBlastRadius = 24f;
    [SerializeField] private LayerMask blastAffectMask = ~0;

    [SerializeField] private GameObject ciritExplosionPrefab;
    [SerializeField] private GameObject mamlExplosionPrefab;
    [SerializeField] private GameObject somExplosionPrefab;

    [Header("Explosion Debug")]
    [SerializeField] private bool drawExplosionGizmo = true;

    private Vector3 lastExplosionPoint;
    private float lastExplosionRadius = 0f;
    private bool hasExploded = false;

    [Header("Visual")]
    [SerializeField] private Transform visualTransform;
    [SerializeField] private Vector3 visualEulerOffset = Vector3.zero;

    [Header("Lifetime")]
    [SerializeField] private float maxLifetime = 20f;
    [SerializeField] private float postDeathCamDuration = 5f;
    [SerializeField] private float camDetachDistance = 80f;

    [Header("Telemetry")]
    [SerializeField] private int telemetryEveryNFrames = 1;

    private string missileExternalId;
    private string targetExternalId;
    private MissileTypeId missileType;

    private bool launched = false;
    private float aliveTime = 0f;
    private bool camDetached = false;

    private Quaternion desiredWorldRotation;
    private DroneMissileController ownerController;

    private Rigidbody rb;
    private Collider col;

    private Quaternion visualOffsetRotation = Quaternion.identity;

    public string MissileExternalId => missileExternalId;
    public string TargetExternalId => targetExternalId;
    public MissileTypeId MissileType => missileType;
    public bool IsLaunched => launched;

    private void Awake()
    {
        rb = GetComponent<Rigidbody>();
        col = GetComponent<Collider>();

        if (rb == null)
            rb = gameObject.AddComponent<Rigidbody>();

        rb.useGravity = false;
        rb.isKinematic = true;
        rb.collisionDetectionMode = CollisionDetectionMode.ContinuousDynamic;
        rb.interpolation = RigidbodyInterpolation.Interpolate;

        visualOffsetRotation = Quaternion.Euler(visualEulerOffset);
        SetFxActive(false);
        SetCamActive(false);
    }

    public void Launch(
        DroneMissileController controller,
        string externalMissileId,
        string externalTargetId,
        MissileTypeId type,
        Vector3 initialForward
    )
    {
        ownerController = controller;
        missileExternalId = externalMissileId;
        targetExternalId = externalTargetId;
        missileType = type;

        transform.SetParent(null, true);

        launched = true;
        aliveTime = 0f;
        camDetached = false;

        rb.isKinematic = false;
        rb.linearVelocity = Vector3.zero;
        rb.angularVelocity = Vector3.zero;

        if (initialForward.sqrMagnitude < 0.001f)
            initialForward = transform.forward;

        desiredWorldRotation = Quaternion.LookRotation(initialForward.normalized, Vector3.up);
        rb.rotation = desiredWorldRotation;
        transform.rotation = desiredWorldRotation;
        SetFxActive(true);
        SetCamActive(true);
        ApplyVisualOffset();
    }

    public void ApplyGuidanceRotation(Vector3 rotationEuler)
    {
        if (!launched)
            return;

        if (useLocalRelativeGuidanceRotation)
            desiredWorldRotation = rb.rotation * Quaternion.Euler(rotationEuler);
        else
            desiredWorldRotation = Quaternion.Euler(rotationEuler);
    }

    private void SetFxActive(bool active)
    {
        if (smokeObject != null)
            smokeObject.SetActive(active);

        if (flameObject != null)
            flameObject.SetActive(active);
    }

    private void SetCamActive(bool active)
    {
        if (missileCam != null)
            missileCam.SetActive(active);

        if (CamContainerPrefab != null)
            CamContainerPrefab.SetActive(active);
    }

    private void Update()
    {
        if (!launched)
            return;

        aliveTime += Time.deltaTime;

        if (aliveTime >= maxLifetime)
        {
            DestroyMissile();
            return;
        }
        fuelText.text = (maxLifetime - aliveTime).ToString("F2");

        if (!camDetached && missileCam != null && TargetRegistry.Instance != null)
        {
            Vector3? targetPos = TargetRegistry.Instance.GetTargetPosition(targetExternalId);
            if (targetPos.HasValue && Vector3.Distance(transform.position, targetPos.Value) <= camDetachDistance)
            {
                missileCam.transform.SetParent(null);
                camDetached = true;
            }
        }

        ApplyVisualOffset();

        if (ownerController != null && telemetryEveryNFrames > 0 && Time.frameCount % telemetryEveryNFrames == 0)
        {
            ownerController.EmitTelemetryForMissile(this);
        }
    }

    private void FixedUpdate()
    {
        if (!launched)
            return;

        float slerpT = rotationResponsiveness * Time.fixedDeltaTime;
        Quaternion slerpedRotation = Quaternion.Slerp(rb.rotation, desiredWorldRotation, slerpT);

        float maxStep = maxRotationDegreesPerSecond * Time.fixedDeltaTime;
        Quaternion cappedRotation = Quaternion.RotateTowards(rb.rotation, slerpedRotation, maxStep);

        rb.MoveRotation(cappedRotation);

        Vector3 moveDir = cappedRotation * Vector3.forward;
        Vector3 nextPos = rb.position + moveDir.normalized * flightSpeed * Time.fixedDeltaTime;

        rb.MovePosition(nextPos);
    }

    private void ApplyVisualOffset()
    {
        if (visualTransform != null)
            visualTransform.localRotation = visualOffsetRotation;
    }

    private void OnCollisionEnter(Collision collision)
    {
        if (!launched)
            return;

        Debug.Log($"[MISSILE HIT] missileId={missileExternalId} hit={collision.collider.name}");
        Explode(collision.GetContact(0).point);
    }

    private void OnTriggerEnter(Collider other)
    {
        if (!launched)
            return;

        Debug.Log($"[MISSILE HIT] missileId={missileExternalId} triggerHit={other.name}");
        Explode(transform.position);
    }

    private float GetBlastRadius()
    {
        switch (missileType)
        {
            case MissileTypeId.Cirit:
                return ciritBlastRadius;

            case MissileTypeId.MAML:
                return mamlBlastRadius;

            case MissileTypeId.SOM:
                return somBlastRadius;

            default:
                return 10f;
        }
    }

    private GameObject GetExplosionPrefab()
    {
        switch (missileType)
        {
            case MissileTypeId.Cirit:
                return ciritExplosionPrefab;

            case MissileTypeId.MAML:
                return mamlExplosionPrefab;

            case MissileTypeId.SOM:
                return somExplosionPrefab;

            default:
                return null;
        }
    }

    private void Explode(Vector3 explosionPoint)
    {
        if (!launched)
            return;

        hasExploded = true;
        lastExplosionPoint = explosionPoint;
        lastExplosionRadius = GetBlastRadius();

        SetFxActive(false);
        ScheduleCamShutdown();

        GameObject explosionPrefab = GetExplosionPrefab();
        if (explosionPrefab != null)
        {
            Instantiate(explosionPrefab, explosionPoint, Quaternion.identity);
        }

        Collider[] hits = Physics.OverlapSphere(
            explosionPoint,
            lastExplosionRadius,
            blastAffectMask,
            QueryTriggerInteraction.Ignore
        );

        for (int i = 0; i < hits.Length; i++)
        {
            TargetInfo targetInfo = hits[i].GetComponentInParent<TargetInfo>();
            if (targetInfo == null)
                continue;

            Debug.Log(
                $"[MISSILE BLAST AFFECT] missileId={missileExternalId} " +
                $"missileType={(int)missileType} " +
                $"targetObject={targetInfo.gameObject.name} " +
                $"targetExternalId={targetInfo.ExternalTargetId} " +
                $"targetClass={(int)targetInfo.ClassId} " +
                $"blastPoint={explosionPoint} " +
                $"blastRadius={lastExplosionRadius}"
            );

            TargetHealthState healthState = hits[i].GetComponentInParent<TargetHealthState>();
            if (healthState != null)
            {
                healthState.Kill();
            }
        }

        DestroyMissile();
    }

    private void OnDrawGizmosSelected()
    {
        if (!drawExplosionGizmo)
            return;

        if (hasExploded)
        {
            Gizmos.color = Color.red;
            Gizmos.DrawWireSphere(lastExplosionPoint, lastExplosionRadius);
        }
    }

    private void ScheduleCamShutdown()
    {
        if (missileCam != null)
        {
            if (!camDetached)
                missileCam.transform.SetParent(null);
            Destroy(missileCam, postDeathCamDuration);
        }

        if (CamContainerPrefab != null)
        {
            var deactivator = CamContainerPrefab.AddComponent<TimedDeactivator>();
            deactivator.Activate(postDeathCamDuration);
        }
    }

    public void DestroyMissile()
    {
        if (ownerController != null)
            ownerController.NotifyMissileDestroyed(this);
        SetFxActive(false);
        Destroy(gameObject);
    }
}