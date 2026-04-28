using UnityEngine;

public class TargetInfo : MonoBehaviour
{
    [Header("Runtime Assigned")]
    [SerializeField] private string externalTargetId = "";
    [SerializeField] private VehicleClassId classId = VehicleClassId.Civil;

    private VehicleProfile profile;

    public string ExternalTargetId => externalTargetId;
    public VehicleClassId ClassId => classId;

    private void Awake()
    {
        profile = GetComponent<VehicleProfile>();

        if (profile != null && VehicleClassIdUtility.TryConvert(profile.vehicleClass, out VehicleClassId mapped))
        {
            classId = mapped;
        }
    }

    public void SetExternalTargetId(string id)
    {
        if (externalTargetId == id)
            return;

        if (TargetRegistry.Instance != null)
            TargetRegistry.Instance.UnregisterTarget(this);

        externalTargetId = id;

        if (TargetRegistry.Instance != null && !string.IsNullOrWhiteSpace(externalTargetId))
            TargetRegistry.Instance.RegisterTarget(this);
    }

    public void SetClassId(VehicleClassId id)
    {
        classId = id;
    }
}