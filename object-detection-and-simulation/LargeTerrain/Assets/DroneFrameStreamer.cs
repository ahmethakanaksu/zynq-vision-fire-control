using System;
using UnityEngine;
using UnityEngine.Rendering;

public class DroneFrameStreamer : MonoBehaviour
{
    [Header("Refs")]
    [SerializeField] private Camera droneCam;              // Drone child camera
    [SerializeField] private Transform droneTip;           // Empty object at drone tip
    [SerializeField] private Rigidbody droneRb;            // Drone rigidbody (for speed)

    [Header("Capture")]
    [SerializeField] private int width = 416;
    [SerializeField] private int height = 416;
    [SerializeField] private int targetFps = 15;           // 15-30 genelde yeter
    [SerializeField] private bool encodeJpg = true;
    [SerializeField, Range(1, 100)] private int jpgQuality = 75;

    private RenderTexture rt;
    private Texture2D cpuTex;   // REUSE (Destroy yok!)
    private uint frameId = 0;
    private float nextSendTime = 0f;
    private bool inFlight = false;

    void Awake()
    {
        if (droneCam == null) droneCam = GetComponentInChildren<Camera>();

        rt = new RenderTexture(width, height, 24, RenderTextureFormat.ARGB32);
        rt.wrapMode = TextureWrapMode.Clamp;
        rt.filterMode = FilterMode.Bilinear;
        rt.Create();

        cpuTex = new Texture2D(width, height, TextureFormat.RGBA32, false);

        // targetTexture'ı sürekli bağlamıyoruz; capture anında geçici bağlayacağız.
    }

    void OnDisable()
    {
        inFlight = false;
    }

    void OnDestroy()
    {
        if (droneCam != null && droneCam.targetTexture == rt)
            droneCam.targetTexture = null;

        if (rt != null)
            rt.Release();

        if (cpuTex != null)
        {
            // Oyun kapanırken güvenli şekilde yok et
            if (Application.isPlaying) Destroy(cpuTex);
            else DestroyImmediate(cpuTex);
        }
    }

    void LateUpdate()
    {
        // Edit mode’da çalışmasın
        if (!Application.isPlaying) return;

        if (droneCam == null || rt == null || cpuTex == null) return;

        // FPS limitle
        if (targetFps > 0)
        {
            if (Time.unscaledTime < nextSendTime) return;
            nextSendTime = Time.unscaledTime + (1f / targetFps);
        }

        // request birikmesin
        if (inFlight) return;

        // Bu frame RT’ye render al (Game view bozulmasın)
        var prevTarget = droneCam.targetTexture;
        droneCam.targetTexture = rt;
        droneCam.Render();
        droneCam.targetTexture = prevTarget;

        if (!SystemInfo.supportsAsyncGPUReadback)
        {
            CaptureWithReadPixels();
            return;
        }

        inFlight = true;
        AsyncGPUReadback.Request(rt, 0, TextureFormat.RGBA32, OnReadbackDone);
    }

    void OnReadbackDone(AsyncGPUReadbackRequest req)
    {
        inFlight = false;

        // Oyun durduysa / edit mode’a geçtiyse işlem yapma
        if (!Application.isPlaying) return;

        if (req.hasError) return;
        if (cpuTex == null) return;

        // 1) Metadata
        var meta = BuildMeta();

        // 2) Image bytes
        var raw = req.GetData<byte>();

        // REUSE texture
        cpuTex.LoadRawTextureData(raw);
        cpuTex.Apply(false, false);

        byte[] imgBytes = encodeJpg ? cpuTex.EncodeToJPG(jpgQuality) : cpuTex.EncodeToPNG();

        // 3) Gönder
        SendFrame(meta, imgBytes);
    }

    void CaptureWithReadPixels()
    {
        if (!Application.isPlaying) return;

        var meta = BuildMeta();

        RenderTexture prev = RenderTexture.active;
        RenderTexture.active = rt;

        cpuTex.ReadPixels(new Rect(0, 0, width, height), 0, 0);
        cpuTex.Apply(false, false);

        RenderTexture.active = prev;

        byte[] imgBytes = encodeJpg ? cpuTex.EncodeToJPG(jpgQuality) : cpuTex.EncodeToPNG();
        SendFrame(meta, imgBytes);
    }

    FrameMeta BuildMeta()
    {
        frameId++;

        long timestampMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        Vector3 tipPos = droneTip ? droneTip.position : transform.position;

        float speed = 0f;
        if (droneRb != null) speed = droneRb.linearVelocity.magnitude;

        Transform camT = droneCam.transform;
        Vector3 camPos = camT.position;

        float camRotX = camT.eulerAngles.x;

        return new FrameMeta
        {
            id = frameId,
            timestampMs = timestampMs,
            droneTipX = tipPos.x,
            droneTipY = tipPos.y,
            droneTipZ = tipPos.z,
            droneSpeed = speed,
            camX = camPos.x,
            camY = camPos.y,
            camZ = camPos.z,
            camRotX = camRotX
        };
    }

    void SendFrame(FrameMeta meta, byte[] imageBytes)
    {
        // TODO: TCP ile Zybo'ya gönder (bir önceki ZyboTcpSender'ı buraya entegre edebilirsin)
        Debug.Log($"Send frame {meta.id} bytes={imageBytes.Length}");
    }

    [Serializable]
    public struct FrameMeta
    {
        public uint id;
        public long timestampMs;

        public float droneTipX, droneTipY, droneTipZ;
        public float droneSpeed;

        public float camX, camY, camZ;
        public float camRotX;
    }
}