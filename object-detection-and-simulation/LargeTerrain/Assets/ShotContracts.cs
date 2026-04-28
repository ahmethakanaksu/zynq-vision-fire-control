using System;
using UnityEngine;

[Serializable]
public class DetectionInput
{
    public int classId;
    public string externalTargetId;
    public string frameId;
    public string timestamp;
    public Vector3 rotationEuler;
}

[Serializable]
public class ShotResult
{
    public bool success;
    public bool classMatched;
    public string externalTargetId;
    public int expectedClassId;
    public int hitClassId;
    public float distanceDroneToTarget;
    public Vector3 shotRotationEuler;
    public Vector3 castOrigin;
    public string timestamp;
    public string frameId;
    public string hitObjectName;
}