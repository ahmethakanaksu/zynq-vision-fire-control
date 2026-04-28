using UnityEngine;

[RequireComponent(typeof(Camera))]
public class CameraFovDebugger : MonoBehaviour
{
    [SerializeField] private bool logOnStart = true;
    [SerializeField] private bool logEveryFrame = false;
    [SerializeField] private KeyCode logKey = KeyCode.K;

    private Camera cam;

    private void Awake()
    {
        cam = GetComponent<Camera>();
    }

    private void Start()
    {
        if (logOnStart)
            LogFovValues("Start");
    }

    private void Update()
    {
        if (logEveryFrame)
            LogFovValues("Update");

        if (Input.GetKeyDown(logKey))
            LogFovValues($"Key:{logKey}");
    }

    [ContextMenu("Log Camera FOV Info")]
    public void LogFovValuesFromContextMenu()
    {
        LogFovValues("ContextMenu");
    }

    private void LogFovValues(string source)
    {
        if (cam == null)
        {
            Debug.LogError("[CameraFovDebugger] Camera component not found.");
            return;
        }

        float verticalFov = cam.fieldOfView;
        float horizontalFov = Camera.VerticalToHorizontalFieldOfView(verticalFov, cam.aspect);

        Debug.Log(
            $"[CameraFovDebugger] Source={source} | " +
            $"Camera={cam.name} | " +
            $"Projection={cam.orthographic} | " +
            $"FOV Axis Setting in Inspector should be checked manually if needed | " +
            $"VerticalFOV={verticalFov:F3} deg | " +
            $"HorizontalFOV={horizontalFov:F3} deg | " +
            $"Aspect={cam.aspect:F6} | " +
            $"PixelWidth={cam.pixelWidth} | " +
            $"PixelHeight={cam.pixelHeight}"
        );
    }
}