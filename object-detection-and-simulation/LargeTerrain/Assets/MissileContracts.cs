using System;
using UnityEngine;

[Serializable]
public class MissileFireCommand
{
    public string targetExternalId;
    public int missileTypeId;
    public string missileExternalId;
    public string timestamp;
    public int frameId;
}

[Serializable]
public class MissileGuidanceCommand
{
    public string missileExternalId;
    public Vector3 rotationEuler;
    public string timestamp;
    public int frameId;
}

[Serializable]
public class MissileTelemetry
{
    public string missileExternalId;
    public string targetExternalId;
    public int missileTypeId;

    public Vector3 missilePosition;
    public Vector3 missileRotationEuler;
    public Vector3 targetPosition;

    public string timestamp;
    public int frameId;
}