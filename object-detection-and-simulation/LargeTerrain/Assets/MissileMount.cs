using UnityEngine;

public class MissileMount : MonoBehaviour
{
    [SerializeField] private MissileTypeId missileType = MissileTypeId.Cirit;

    public MissileTypeId MissileType => missileType;
}