using UnityEngine;

public class DroneShotProcessor : MonoBehaviour
{
    [Header("References")]
    [SerializeField] private Transform castOrigin;

    [Header("Cast Settings")]
    [SerializeField] private float castRadius = 3f;
    [SerializeField] private float visibleLazerRadius = 3f;
    [SerializeField] private float castDistance = 5000f;
    [SerializeField] private LayerMask hitMask = ~0;
    [SerializeField] private bool drawDebugRay = true;
    [SerializeField] private float debugRayDuration = 3f;
    [SerializeField] private float visibleRayDuration = 1f;
    

    [Header("Runtime Ray Cylinder")]
    [SerializeField] private bool drawRuntimeCylinder = true;
    [SerializeField] private Material cylinderMaterial;
    [SerializeField] private Color colorHit = Color.green;
    [SerializeField] private Color colorWrongClass = Color.yellow;
    [SerializeField] private Color colorMiss = Color.red;

    [Header("Test Input")]
    [SerializeField] private int testClassId = 0;
    [SerializeField] private string testExternalTargetId = "target_001";
    [SerializeField] private string testTimestamp = "test_timestamp";
    [SerializeField] private string testFrameId = "test_frame_id";
    [SerializeField] private Vector3 testRotationEuler = new Vector3(50f, 0f, 0f);

    private void Awake()
    {
        if (castOrigin == null)
            castOrigin = transform;
    }

    [ContextMenu("Test Shot From Inspector")]
    public void TestShotFromInspector()
    {
        DetectionInput input = new DetectionInput
        {
            classId = testClassId,
            externalTargetId = testExternalTargetId,
            timestamp = testTimestamp,
            frameId = testFrameId,
            rotationEuler = testRotationEuler
        };

        ProcessDetectionInput(input);
    }

    private void Update()
    {
        if (Input.GetKeyDown(KeyCode.T))
        {
            TestShotFromInspector();
        }
    }

    public ShotResult ProcessDetectionInput(DetectionInput input)
    {
        Vector3 origin = castOrigin.position;
        Quaternion shotRotation = castOrigin.rotation * Quaternion.Euler(input.rotationEuler);
        Vector3 direction = shotRotation * Vector3.forward;

        if (drawDebugRay)
            Debug.DrawRay(origin, direction * castDistance, Color.red, debugRayDuration);

        ShotResult result = new ShotResult
        {
            success = false,
            classMatched = false,
            externalTargetId = input.externalTargetId,
            expectedClassId = input.classId,
            hitClassId = -1,
            distanceDroneToTarget = -1f,
            shotRotationEuler = input.rotationEuler,
            castOrigin = origin,
            timestamp = input.timestamp,
            frameId = input.frameId,
            hitObjectName = ""
        };

        RaycastHit[] hits = Physics.SphereCastAll(
            origin,
            castRadius,
            direction,
            castDistance,
            hitMask,
            QueryTriggerInteraction.Ignore
        );

        if (hits == null || hits.Length == 0)
        {
            Debug.Log(
                $"[SHOT RESULT] success=false no hit | " +
                $"externalTargetId={result.externalTargetId} " +
                $"expectedClassId={result.expectedClassId} " +
                $"rotation={result.shotRotationEuler} " +
                $"origin={result.castOrigin} " +
                $"timestamp={result.timestamp} " +
                $"frameId={result.frameId}"
            );

            if (drawRuntimeCylinder)
                SpawnRayCylinder(origin, origin + direction * castDistance, colorMiss);

            return result;
        }

        System.Array.Sort(hits, (a, b) => a.distance.CompareTo(b.distance));

        TargetInfo matchedTarget = null;
        RaycastHit matchedHit = default;

        foreach (RaycastHit hit in hits)
        {
            TargetInfo targetInfo = hit.collider.GetComponentInParent<TargetInfo>();
            if (targetInfo == null)
                continue;

            int hitClassId = (int)targetInfo.ClassId;

            if (hitClassId == input.classId)
            {
                matchedTarget = targetInfo;
                matchedHit = hit;
                break;
            }
        }

        if (matchedTarget != null)
        {
            result.success = true;
            result.classMatched = true;
            result.hitClassId = (int)matchedTarget.ClassId;
            result.distanceDroneToTarget = Vector3.Distance(origin, matchedTarget.transform.position);
            result.hitObjectName = matchedTarget.gameObject.name;

            if (!string.IsNullOrWhiteSpace(input.externalTargetId))
                matchedTarget.SetExternalTargetId(input.externalTargetId);

            Debug.Log(
                $"[SHOT RESULT] success={result.success} " +
                $"classMatched={result.classMatched} " +
                $"externalTargetId={result.externalTargetId} " +
                $"expectedClassId={result.expectedClassId} " +
                $"hitClassId={result.hitClassId} " +
                $"distance={result.distanceDroneToTarget:F2} " +
                $"rotation={result.shotRotationEuler} " +
                $"origin={result.castOrigin} " +
                $"hitObject={result.hitObjectName} " +
                $"timestamp={result.timestamp} " +
                $"frameId={result.frameId}"
            );

            if (drawRuntimeCylinder)
                SpawnRayCylinder(origin, matchedTarget.transform.position, colorHit);

            return result;
        }

        Debug.Log(
            $"[SHOT RESULT] success=false class not matched | " +
            $"externalTargetId={result.externalTargetId} " +
            $"expectedClassId={result.expectedClassId} " +
            $"rotation={result.shotRotationEuler} " +
            $"origin={result.castOrigin} " +
            $"timestamp={result.timestamp} " +
            $"frameId={result.frameId}"
        );

        if (drawRuntimeCylinder)
            SpawnRayCylinder(origin, origin + direction * castDistance, colorWrongClass);

        return result;
    }

    private void SpawnRayCylinder(Vector3 start, Vector3 end, Color color)
    {
        float length = Vector3.Distance(start, end);
        if (length < 0.01f)
            return;

        GameObject cyl = GameObject.CreatePrimitive(PrimitiveType.Cylinder);
        Destroy(cyl.GetComponent<Collider>());

        cyl.transform.position = (start + end) * 0.5f;
        cyl.transform.up = (end - start).normalized;
        // Unity silindir: yükseklik 2 birim, yarıçap 0.5 birim
        cyl.transform.localScale = new Vector3(visibleLazerRadius * 2f, length * 0.5f, visibleLazerRadius * 2f);

        Renderer rend = cyl.GetComponent<Renderer>();
        if (cylinderMaterial != null)
        {
            rend.material = cylinderMaterial;
        }
        rend.material.color = color;

        Destroy(cyl, visibleRayDuration);
    }

    public Vector3? GetTrackedTargetPosition(string externalTargetId)
    {
        if (TargetRegistry.Instance == null)
            return null;

        return TargetRegistry.Instance.GetTargetPosition(externalTargetId);
    }
}