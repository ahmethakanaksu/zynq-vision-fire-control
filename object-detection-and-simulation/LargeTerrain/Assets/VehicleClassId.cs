using UnityEngine;

public enum VehicleClassId
{
    Tank = 0,
    ZPT = 1,
    Truck = 2,
    Civil = 3
}

public static class VehicleClassIdUtility
{
    public static bool TryConvert(VehicleClass vehicleClass, out VehicleClassId classId)
    {
        switch (vehicleClass)
        {
            case VehicleClass.Tank:
                classId = VehicleClassId.Tank;
                return true;

            case VehicleClass.ZPT:
                classId = VehicleClassId.ZPT;
                return true;

            case VehicleClass.Truck:
                classId = VehicleClassId.Truck;
                return true;

            case VehicleClass.Civil:
                classId = VehicleClassId.Civil;
                return true;

            default:
                classId = VehicleClassId.Civil;
                return false;
        }
    }
}