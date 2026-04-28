using UnityEngine;

public class AkinciScript : MonoBehaviour
{
    [SerializeField] private float speed = 1f; // units per second

    void Update()
    {
        transform.position += Vector3.right * speed * Time.deltaTime;
    }
}
