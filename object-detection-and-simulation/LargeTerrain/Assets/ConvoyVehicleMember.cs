using UnityEngine;

public class ConvoyVehicleMember : MonoBehaviour
{
    [Header("Debug")]
    [SerializeField] private bool drawDebug = false;

    private VehicleProfile profile;
    private Terrain[] terrains;
    private ConvoyController controller;

    private Vector3 formationOffsetLocal;
    private bool initialized = false;

    private Quaternion modelOffsetRotation = Quaternion.identity;

    public VehicleFaction Faction => profile != null ? profile.faction : VehicleFaction.Military;

    public void Initialize(ConvoyController convoyController, Vector3 localOffset)
    {
        controller = convoyController;
        formationOffsetLocal = localOffset;

        profile = GetComponent<VehicleProfile>();
        terrains = Terrain.activeTerrains;

        if (profile == null)
        {
            Debug.LogError($"ConvoyVehicleMember on {name}: VehicleProfile eksik.");
            enabled = false;
            return;
        }

        if (Mathf.Abs(profile.visualYawOffset) > 0.001f)
            modelOffsetRotation = Quaternion.Euler(0f, profile.visualYawOffset, 0f);

        initialized = true;
        SnapImmediateToSlot();
    }

    private void Update()
    {
        if (!initialized || controller == null)
            return;

        Vector3 targetPos = controller.GetWorldSlotPosition(formationOffsetLocal);
        targetPos = SnapPointToTerrain(targetPos);

        Vector3 currentPos = transform.position;
        Vector3 toTarget = targetPos - currentPos;
        toTarget.y = 0f;

        float dist = toTarget.magnitude;

        float moveSpeed = controller.GetMemberMoveSpeed(this, formationOffsetLocal, dist);

        if (dist > 0.05f)
        {
            Vector3 nextPos = Vector3.MoveTowards(currentPos, targetPos, moveSpeed * Time.deltaTime);
            nextPos = SnapPointToTerrain(nextPos);
            transform.position = nextPos;

            Vector3 lookDir = controller.GetLookDirectionForMember(this, formationOffsetLocal, nextPos);
            lookDir.y = 0f;

            if (lookDir.sqrMagnitude > 0.001f)
            {
                Quaternion targetRot = Quaternion.LookRotation(lookDir.normalized, Vector3.up) * modelOffsetRotation;
                transform.rotation = Quaternion.Slerp(transform.rotation, targetRot, profile.turnSpeed * Time.deltaTime);
            }
        }
    }

    private void SnapImmediateToSlot()
    {
        if (controller == null)
            return;

        Vector3 p = controller.GetWorldSlotPosition(formationOffsetLocal);
        transform.position = SnapPointToTerrain(p);
    }

    private Vector3 SnapPointToTerrain(Vector3 point)
    {
        Terrain t = GetTerrainUnderPosition(point);
        if (t == null)
            return point;

        float y = t.SampleHeight(point) + t.transform.position.y;
        point.y = y;
        return point;
    }

    private Terrain GetTerrainUnderPosition(Vector3 worldPos)
    {
        if (terrains == null || terrains.Length == 0)
            terrains = Terrain.activeTerrains;

        for (int i = 0; i < terrains.Length; i++)
        {
            Terrain t = terrains[i];
            Vector3 tp = t.transform.position;
            Vector3 size = t.terrainData.size;

            bool insideX = worldPos.x >= tp.x && worldPos.x <= tp.x + size.x;
            bool insideZ = worldPos.z >= tp.z && worldPos.z <= tp.z + size.z;

            if (insideX && insideZ)
                return t;
        }

        return null;
    }

    private void OnDrawGizmosSelected()
    {
        if (!drawDebug || !initialized || controller == null)
            return;

        Gizmos.color = Color.red;
        Gizmos.DrawSphere(controller.GetWorldSlotPosition(formationOffsetLocal), 1.5f);
    }
}