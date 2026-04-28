using UnityEngine;

[RequireComponent(typeof(Rigidbody))]
public class DroneRouteFollower : MonoBehaviour
{
    [Header("Flight Settings")]
    [SerializeField] private float fixedY = 1419f;
    [SerializeField] private float moveSpeed = 40f;
    [SerializeField] private float rotationSpeed = 4f;
    [SerializeField] private float reachThreshold = 8f;

    [Header("Visual Rotation Offset")]
    [SerializeField] private float visualYawOffset = -90f;

    private Rigidbody rb;

    private Vector3 routeStart;
    private Vector3 routeEnd;
    private bool hasRoute = false;
    private bool routeCompleted = false;

    public bool HasRoute => hasRoute;
    public bool RouteCompleted => routeCompleted;
    public Vector3 RouteStart => routeStart;
    public Vector3 RouteEnd => routeEnd;

    private void Awake()
    {
        rb = GetComponent<Rigidbody>();
        rb.useGravity = false;
        rb.linearVelocity = Vector3.zero;
        rb.angularVelocity = Vector3.zero;
    }

    private void Start()
    {
        Vector3 p = transform.position;
        p.y = fixedY;
        transform.position = p;
        rb.position = p;
    }

    private void FixedUpdate()
    {
        if (!hasRoute || routeCompleted)
            return;

        Vector3 currentPos = rb.position;
        currentPos.y = fixedY;

        Vector3 flatTarget = new Vector3(routeEnd.x, fixedY, routeEnd.z);
        Vector3 moveDir = flatTarget - currentPos;
        moveDir.y = 0f;

        float distance = moveDir.magnitude;

        if (distance <= reachThreshold)
        {
            routeCompleted = true;

            Vector3 finalPos = new Vector3(routeEnd.x, fixedY, routeEnd.z);
            rb.MovePosition(finalPos);
            return;
        }

        moveDir.Normalize();

        Vector3 nextPos = currentPos + moveDir * moveSpeed * Time.fixedDeltaTime;
        nextPos.y = fixedY;

        rb.MovePosition(nextPos);

        if (moveDir.sqrMagnitude > 0.0001f)
        {
            Quaternion targetRot = Quaternion.LookRotation(moveDir, Vector3.up);
            Quaternion offsetRot = targetRot * Quaternion.Euler(0f, visualYawOffset, 0f);
            Quaternion newRot = Quaternion.Slerp(rb.rotation, offsetRot, rotationSpeed * Time.fixedDeltaTime);
            rb.MoveRotation(newRot);
        }
    }

    public void SetRoute(Vector3 start, Vector3 end)
    {
        routeStart = new Vector3(start.x, fixedY, start.z);
        routeEnd = new Vector3(end.x, fixedY, end.z);

        routeCompleted = false;
        hasRoute = true;

        rb.position = routeStart;
        transform.position = routeStart;

        Vector3 lookDir = routeEnd - routeStart;
        lookDir.y = 0f;

        if (lookDir.sqrMagnitude > 0.0001f)
        {
            Quaternion lookRot = Quaternion.LookRotation(lookDir.normalized, Vector3.up);
            Quaternion offsetRot = lookRot * Quaternion.Euler(0f, visualYawOffset, 0f);
            rb.rotation = offsetRot;
            transform.rotation = offsetRot;
        }
    }

    public void ClearRoute()
    {
        hasRoute = false;
        routeCompleted = false;
    }

    private void OnDrawGizmosSelected()
    {
        if (!hasRoute)
            return;

        Gizmos.color = Color.cyan;
        Gizmos.DrawSphere(routeStart, 8f);

        Gizmos.color = Color.red;
        Gizmos.DrawSphere(routeEnd, 8f);

        Gizmos.color = Color.yellow;
        Gizmos.DrawLine(routeStart, routeEnd);
    }
}