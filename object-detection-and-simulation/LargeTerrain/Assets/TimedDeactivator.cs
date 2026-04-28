using System.Collections;
using UnityEngine;

public class TimedDeactivator : MonoBehaviour
{
    public void Activate(float delay)
    {
        StartCoroutine(Run(delay));
    }

    private IEnumerator Run(float delay)
    {
        yield return new WaitForSeconds(delay);
        gameObject.SetActive(false);
        Destroy(this);
    }
}
