using UnityEngine;

public enum VehicleFaction
{
    Civilian,
    Military
}

public enum VehicleClass
{
    Civil,
    Tank,
    ZPT,
    Truck
}

public class VehicleProfile : MonoBehaviour
{
    [Header("Identity")]
    public VehicleFaction faction = VehicleFaction.Civilian;
    public VehicleClass vehicleClass = VehicleClass.Civil;

    [Header("Movement")]
    public float minCruiseSpeed = 6f;
    public float maxCruiseSpeed = 10f;
    public float acceleration = 4f;
    public float braking = 6f;
    public float turnSpeed = 3f;

    [Header("Spacing")]
    public float preferredSpacing = 18f;
    public float emergencyStopDistance = 8f;

    [Header("Behaviour")]
    public bool canJoinConvoy = false;
    public float lateralWander = 8f;
    public float forwardTargetMin = 80f;
    public float forwardTargetMax = 180f;

    [Header("Optional Visual")]
    public float visualYawOffset = 0f;
}