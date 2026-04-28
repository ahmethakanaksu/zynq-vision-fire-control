using UnityEngine;
using System.Collections.Generic;

public class MockMissileGuidanceTester : MonoBehaviour
{
    [Header("References")]
    [SerializeField] private DroneMissileController missileController;

    [Header("Test")]
    [SerializeField] private bool autoGuideEnabled = true;

    private void Update()
    {
        if (!autoGuideEnabled || missileController == null)
            return;

        if (TargetRegistry.Instance == null)
            return;

        IEnumerable<GuidedMissile> activeMissiles = missileController.GetAllActiveMissiles();

        foreach (GuidedMissile missile in activeMissiles)
        {
            if (missile == null)
                continue;

            Vector3? targetPosNullable = TargetRegistry.Instance.GetTargetPosition(missile.TargetExternalId);
            if (!targetPosNullable.HasValue)
                continue;

            Vector3 dir = targetPosNullable.Value - missile.transform.position;
            if (dir.sqrMagnitude < 0.001f)
                continue;

            Quaternion desired = Quaternion.LookRotation(dir.normalized, Vector3.up);

            MissileGuidanceCommand cmd = new MissileGuidanceCommand
            {
                missileExternalId = missile.MissileExternalId,
                rotationEuler = desired.eulerAngles,
                timestamp = System.DateTime.UtcNow.ToString("O"),
                frameId = Time.frameCount
            };

            missileController.ProcessGuidanceCommand(cmd);
        }
    }
}
