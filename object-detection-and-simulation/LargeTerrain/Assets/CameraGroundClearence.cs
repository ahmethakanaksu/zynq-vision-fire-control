using UnityEngine;

public class CameraGroundClearance : MonoBehaviour
{
    [Header("References")]
    [SerializeField] private Transform camT;
    [SerializeField] private LayerMask groundMask;

    [Header("Distance Control (along camera forward)")]
    [SerializeField] private float targetDistance = 50f;
    [SerializeField] private float padding = 0.3f;

    [SerializeField] private float probeRadius = 0.6f;
    [SerializeField] private float probeDistance = 500f;

    [Header("Boom Limits")]
    [SerializeField] private float minBoom = 5f;
    [SerializeField] private float maxBoom = 2000f; // rigine göre ayarla (ideal boomLen civarý olabilir)

    [Header("Motion")]
    [SerializeField] private bool snap = true; // þimdilik true
    [SerializeField] private float smooth = 12f;
    [Tooltip("Bir frame'de en fazla ne kadar uzat/kýsalt (ani zýplamayý engeller)")]
    [SerializeField] private float maxDelta = 5f;

    [Header("Debug")]
    [SerializeField] private bool logDebug = false;

    private Vector3 vel;

    // debug
    private float dbgHitDist, dbgError, dbgSigned;
    private string dbgHitName;

    void Start()
    {
        if (!camT)
        {
            var cam = GetComponentInChildren<Camera>();
            if (cam) camT = cam.transform;
        }
    }
    public void SetTargetDistance(float d) => targetDistance = d;
    void LateUpdate()
    {
        if (!camT) return;

        // current boom (local)
        Vector3 curLocal = transform.localPosition;
        float curLen = curLocal.magnitude;
        if (curLen < 0.0001f) return;

        Vector3 dirPivotToCam = curLocal / curLen;

        // spherecast from camera forward
        bool hit = Physics.SphereCast(
            camT.position, probeRadius, camT.forward,
            out RaycastHit rh, probeDistance, groundMask, QueryTriggerInteraction.Ignore
        );

        // Eðer yer hiç yoksa: boom'u deðiþtirme (istersen maxBoom'a doðru uzatabilirsin)
        if (!hit) return;

        dbgHitDist = rh.distance;
        dbgHitName = rh.collider ? rh.collider.name : "";

        // Pozitifse: yer yakýn -> içeri çekmem lazým
        // Negatifse: yer uzak -> dýþarý itmem lazým
        dbgError = (targetDistance + padding) - dbgHitDist;

        // signed offset: + => kýsalt, - => uzat
        dbgSigned = Mathf.Clamp(dbgError, -maxDelta, maxDelta);

        // Yeni boom uzunluðu
        float desiredLen = Mathf.Clamp(curLen - dbgSigned, minBoom, maxBoom);
        Vector3 desiredLocal = dirPivotToCam * desiredLen;
        if (snap)
        {
            transform.localPosition = desiredLocal;
            vel = Vector3.zero;
        }
        else
        {
                transform.localPosition = Vector3.SmoothDamp(
                transform.localPosition,
                desiredLocal,
                ref vel,
                1f / Mathf.Max(0.0001f, smooth)
            );
        }

        if (logDebug)
        {
            Debug.Log($"[CameraGroundClearance] hit={dbgHitName} hitDist={dbgHitDist:F2} target={targetDistance:F2} error={dbgError:F2} signed={dbgSigned:F2} curLen={curLen:F2} desiredLen={desiredLen:F2}");
        }
    }
}
