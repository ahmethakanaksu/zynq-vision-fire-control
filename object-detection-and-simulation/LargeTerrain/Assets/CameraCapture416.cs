using UnityEngine;
using System.IO;
using System.Globalization;
using System.Collections.Generic;

public class CameraCapture416 : MonoBehaviour
{
    public Camera captureCamera;

    [Header("Vehicles")]
    public string vehicleRootTag = "Vehicle";
    public bool logOnlyLargest = false;

    private const int W = 416;
    private const int H = 416;

    [Header("Debug Overlay")]
    public bool debugDrawBBoxes = false;
    public bool debugDrawOnlyLargest = false;
    public int debugThickness = 2;

    [ContextMenu("Capture 416x416 PNG + Log Coverage (RT-Accurate)")]
    public void CaptureToPng416_WithCoverageLog()
    {
        if (captureCamera == null) captureCamera = Camera.main;

        var rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32);
        var prevTarget = captureCamera.targetTexture;
        var prevActive = RenderTexture.active;

        // Render to RT
        captureCamera.targetTexture = rt;
        captureCamera.Render();
        RenderTexture.active = rt;

        // ReadPixels
        var tex = new Texture2D(W, H, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, W, H), 0, 0);
        tex.Apply();

        // Measure using RT-accurate viewport mapping
        LogVehicleCoverage_RTAccurate(captureCamera);

        // Restore
        captureCamera.targetTexture = prevTarget;
        RenderTexture.active = prevActive;
        RenderTexture.ReleaseTemporary(rt);

        // Save
        var bytes = tex.EncodeToPNG();
        var path = Path.Combine(Application.persistentDataPath, "capture_416.png");
        File.WriteAllBytes(path, bytes);
        Debug.Log("Saved: " + path);
    }

    private void LogVehicleCoverage_RTAccurate(Camera cam)
    {
        var vehicles = GameObject.FindGameObjectsWithTag(vehicleRootTag);
        if (vehicles == null || vehicles.Length == 0)
        {
            Debug.LogWarning($"No vehicles found with tag '{vehicleRootTag}'.");
            return;
        }

        // Force dot decimal in logs
        var inv = CultureInfo.InvariantCulture;

        var infos = new List<(string name, Rect rect, float coverage, float visibleRatio, int rendererCount)>();

        foreach (var v in vehicles)
        {
            if (!TryGetScreenRect_RTAccurate(v, cam, out Rect unclamped, out Rect clamped, out float visibleRatio, out int rc))
                continue;

            // must have some on-screen area
            if (clamped.width <= 1f || clamped.height <= 1f) continue;

            float coverage = (clamped.width * clamped.height) / (W * H);
            infos.Add((v.name, clamped, coverage, visibleRatio, rc));
        }

        if (infos.Count == 0)
        {
            Debug.LogWarning("Vehicles found but none produced a valid on-screen bbox.");
            return;
        }

        infos.Sort((a, b) => b.coverage.CompareTo(a.coverage));
        var largest = infos[0];

        Debug.Log(
            $"[Coverage] Largest: {largest.name} | " +
            $"bbox(px)=({largest.rect.x.ToString("F1", inv)},{largest.rect.y.ToString("F1", inv)}," +
            $"{largest.rect.width.ToString("F1", inv)},{largest.rect.height.ToString("F1", inv)}) | " +
            $"coverage={(largest.coverage * 100f).ToString("F2", inv)}% | " +
            $"visibleRatio={(largest.visibleRatio * 100f).ToString("F1", inv)}% | " +
            $"renderers={largest.rendererCount}"
        );

        if (!logOnlyLargest)
        {
            for (int i = 0; i < infos.Count; i++)
            {
                var it = infos[i];
                Debug.Log(
                    $"[Coverage] {i + 1}. {it.name} | " +
                    $"coverage={(it.coverage * 100f).ToString("F2", inv)}% | " +
                    $"visibleRatio={(it.visibleRatio * 100f).ToString("F1", inv)}% | " +
                    $"bbox=({it.rect.x.ToString("F1", inv)},{it.rect.y.ToString("F1", inv)}," +
                    $"{it.rect.width.ToString("F1", inv)},{it.rect.height.ToString("F1", inv)})"
                );
            }
        }

        // Practical guidance
        float c = largest.coverage;
        if (c < 0.01f) Debug.LogWarning("Largest object is tiny (<1%). YOLO will struggle unless you have tons of such examples and model/input is tuned.");
        else if (c < 0.02f) Debug.LogWarning("Largest object is very small (1–2%). Still hard; prefer bigger or mix with closer shots.");
        else if (c < 0.05f) Debug.Log("Largest object is small (2–5%). Usable; consider slightly closer for better learning.");
        else if (c <= 0.25f) Debug.Log("Largest object is in ideal band (5–25%).");
        else if (c <= 0.40f) Debug.Log("Largest object is large (25–40%). OK; watch truncation.");
        else Debug.LogWarning("Largest object is very large (>40%). Risk of truncation and less context.");
    }

    private bool TryGetScreenRect_RTAccurate(GameObject obj, Camera cam,
        out Rect unclamped, out Rect clamped, out float visibleRatio, out int rendererCount)
    {
        unclamped = new Rect();
        clamped = new Rect();
        visibleRatio = 0f;
        rendererCount = 0;

        var renderers = obj.GetComponentsInChildren<Renderer>();
        if (renderers == null || renderers.Length == 0) return false;

        bool any = false;
        float minX = float.PositiveInfinity, minY = float.PositiveInfinity;
        float maxX = float.NegativeInfinity, maxY = float.NegativeInfinity;

        foreach (var r in renderers)
        {
            if (!r.enabled) continue;
            rendererCount++;

            Bounds lb = r.localBounds;                 // local-space bounds (tighter)
            Vector3 c = lb.center;
            Vector3 e = lb.extents;

            // local-space 8 köþe
            Vector3[] localCorners = new Vector3[8]
            {
                c + new Vector3(-e.x, -e.y, -e.z),
                c + new Vector3(-e.x, -e.y,  e.z),
                c + new Vector3(-e.x,  e.y, -e.z),
                c + new Vector3(-e.x,  e.y,  e.z),
                c + new Vector3( e.x, -e.y, -e.z),
                c + new Vector3( e.x, -e.y,  e.z),
                c + new Vector3( e.x,  e.y, -e.z),
                c + new Vector3( e.x,  e.y,  e.z),
            };

            // local -> world -> viewport
            for (int i = 0; i < localCorners.Length; i++)
            {
                Vector3 world = r.transform.TransformPoint(localCorners[i]);
                Vector3 vp = cam.WorldToViewportPoint(world);
                if (vp.z <= 0f) continue;

                float x = vp.x * W;
                float y = vp.y * H; // (overlay debug için böyle kalsýn; YOLO yazarken istersen y'yi flipleriz)
                minX = Mathf.Min(minX, x);
                minY = Mathf.Min(minY, y);
                maxX = Mathf.Max(maxX, x);
                maxY = Mathf.Max(maxY, y);
                any = true;
            }

        }

        if (!any) return false;

        float w = maxX - minX;
        float h = maxY - minY;
        if (w <= 0f || h <= 0f) return false;

        unclamped = new Rect(minX, minY, w, h);

        float cx0 = Mathf.Clamp(minX, 0f, W);
        float cy0 = Mathf.Clamp(minY, 0f, H);
        float cx1 = Mathf.Clamp(maxX, 0f, W);
        float cy1 = Mathf.Clamp(maxY, 0f, H);
        clamped = Rect.MinMaxRect(cx0, cy0, cx1, cy1);

        float unclampedArea = w * h;
        float clampedArea = Mathf.Max(0f, clamped.width) * Mathf.Max(0f, clamped.height);
        visibleRatio = unclampedArea > 1e-5f ? (clampedArea / unclampedArea) : 0f;

        return true;
    }
    public Texture2D Capture416(bool logCoverage = false)
    {
        if (captureCamera == null) captureCamera = Camera.main;

        var rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32);
        var prevTarget = captureCamera.targetTexture;
        var prevActive = RenderTexture.active;

        // Render to RT
        captureCamera.targetTexture = rt;
        captureCamera.Render();
        RenderTexture.active = rt;

        // RT -> Texture2D
        var tex = new Texture2D(W, H, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, W, H), 0, 0);
        tex.Apply();

        if (logCoverage)
            LogVehicleCoverage_RTAccurate(captureCamera);

        // Restore
        captureCamera.targetTexture = prevTarget;
        RenderTexture.active = prevActive;
        RenderTexture.ReleaseTemporary(rt);

        return tex;
    }

    public void SaveTexturePng(Texture2D tex, string fullPath)
    {
        var bytes = tex.EncodeToPNG();
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath));
        File.WriteAllBytes(fullPath, bytes);
    }

    /// <summary>
    /// /////////////////////////////////////////
    /// </summary>
    [ContextMenu("Capture 416x416 PNG + Draw BBoxes (Debug)")]
    public void CaptureToPng416_WithBBoxOverlay()
    {
        if (captureCamera == null) captureCamera = Camera.main;

        var rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32);
        var prevTarget = captureCamera.targetTexture;
        var prevActive = RenderTexture.active;

        // Render to RT
        captureCamera.targetTexture = rt;
        captureCamera.Render();
        RenderTexture.active = rt;

        // ReadPixels
        var tex = new Texture2D(W, H, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, W, H), 0, 0);
        tex.Apply();

        // Compute bboxes (same as logging)
        var vehicles = GameObject.FindGameObjectsWithTag(vehicleRootTag);
        if (vehicles == null || vehicles.Length == 0)
        {
            Debug.LogWarning($"No vehicles found with tag '{vehicleRootTag}'.");
        }
        else
        {
            // draw bboxes
            int drawn = 0;

            // Optional: draw only largest or all
            List<(GameObject v, Rect rect, float coverage)> infos = new List<(GameObject, Rect, float)>();

            foreach (var v in vehicles)
            {
                if (!TryGetScreenRect_RTAccurate(v, captureCamera, out Rect unclamped, out Rect clamped, out float visibleRatio, out int rc))
                    continue;

                if (clamped.width <= 1f || clamped.height <= 1f) continue;

                float coverage = (clamped.width * clamped.height) / (W * H);
                infos.Add((v, clamped, coverage));
            }

            infos.Sort((a, b) => b.coverage.CompareTo(a.coverage));

            if (infos.Count == 0)
            {
                Debug.LogWarning("Vehicles found but none produced a valid on-screen bbox.");
            }
            else
            {
                if (logOnlyLargest)
                {
                    var it = infos[0];
                    DrawRectOutline(tex, it.rect, 2, Color.red);
                    DrawLabelDot(tex, it.rect, Color.red);
                    drawn = 1;
                }
                else
                {
                    // Largest red, others green
                    for (int i = 0; i < infos.Count; i++)
                    {
                        var it = infos[i];
                        var col = (i == 0) ? Color.red : Color.green;
                        DrawRectOutline(tex, it.rect, 2, col);
                        DrawLabelDot(tex, it.rect, col);
                        drawn++;
                    }
                }

                tex.Apply();
                Debug.Log($"[BBoxOverlay] Drawn boxes: {drawn}");
            }
        }

        // Restore
        captureCamera.targetTexture = prevTarget;
        RenderTexture.active = prevActive;
        RenderTexture.ReleaseTemporary(rt);

        // Save
        var bytes = tex.EncodeToPNG();
        var path = Path.Combine(Application.persistentDataPath, "capture_416_debug_bbox.png");
        File.WriteAllBytes(path, bytes);
        Debug.Log("Saved (debug bbox): " + path);
    }

    /// <summary>
    /// Draw rectangle outline onto texture (Rect is in pixel space, 0..W / 0..H).
    /// </summary>
    private void DrawRectOutline(Texture2D tex, Rect r, int thickness, Color color)
    {
        int x0 = Mathf.Clamp(Mathf.RoundToInt(r.xMin), 0, W - 1);
        int y0 = Mathf.Clamp(Mathf.RoundToInt(r.yMin), 0, H - 1);
        int x1 = Mathf.Clamp(Mathf.RoundToInt(r.xMax), 0, W - 1);
        int y1 = Mathf.Clamp(Mathf.RoundToInt(r.yMax), 0, H - 1);

        for (int t = 0; t < thickness; t++)
        {
            DrawLine(tex, x0, y0 + t, x1, y0 + t, color); // bottom
            DrawLine(tex, x0, y1 - t, x1, y1 - t, color); // top
            DrawLine(tex, x0 + t, y0, x0 + t, y1, color); // left
            DrawLine(tex, x1 - t, y0, x1 - t, y1, color); // right
        }
    }

    /// <summary>
    /// Small dot at bbox center, helps eyeballing center.
    /// </summary>
    private void DrawLabelDot(Texture2D tex, Rect r, Color color)
    {
        int cx = Mathf.Clamp(Mathf.RoundToInt(r.center.x), 0, W - 1);
        int cy = Mathf.Clamp(Mathf.RoundToInt(r.center.y), 0, H - 1);

        // 5x5 dot
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                SetPixelSafe(tex, cx + dx, cy + dy, color);
    }

    /// <summary>
    /// Simple line drawer (integer stepping). Good enough for debug overlays.
    /// </summary>
    private void DrawLine(Texture2D tex, int x0, int y0, int x1, int y1, Color c)
    {
        int dx = Mathf.Abs(x1 - x0);
        int dy = Mathf.Abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;

        while (true)
        {
            SetPixelSafe(tex, x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }

    private void SetPixelSafe(Texture2D tex, int x, int y, Color c)
    {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        tex.SetPixel(x, y, c);
    }

    public Texture2D Capture416_WithOverlay(System.Func<List<Rect>> getRectsPx, bool logCoverage = false)
    {
        if (captureCamera == null) captureCamera = Camera.main;

        var rt = RenderTexture.GetTemporary(W, H, 24, RenderTextureFormat.ARGB32);
        var prevTarget = captureCamera.targetTexture;
        var prevActive = RenderTexture.active;

        // Render to RT
        captureCamera.targetTexture = rt;
        captureCamera.Render();
        RenderTexture.active = rt;

        // RT -> Texture2D
        var tex = new Texture2D(W, H, TextureFormat.RGBA32, false);
        tex.ReadPixels(new Rect(0, 0, W, H), 0, 0);
        tex.Apply();

        if (logCoverage)
            LogVehicleCoverage_RTAccurate(captureCamera);

        // === DEBUG DRAW (same captured texture) ===
        if (debugDrawBBoxes && getRectsPx != null)
        {
            var rects = getRectsPx.Invoke();
            if (rects != null && rects.Count > 0)
            {
                if (debugDrawOnlyLargest)
                {
                    // largest by area
                    Rect best = rects[0];
                    float bestA = best.width * best.height;
                    for (int i = 1; i < rects.Count; i++)
                    {
                        float a = rects[i].width * rects[i].height;
                        if (a > bestA) { bestA = a; best = rects[i]; }
                    }
                    DrawRectOutline(tex, best, debugThickness, Color.red);
                    DrawLabelDot(tex, best, Color.red);
                }
                else
                {
                    // first red, others green
                    rects.Sort((a, b) => (b.width * b.height).CompareTo(a.width * a.height));
                    for (int i = 0; i < rects.Count; i++)
                    {
                        var col = (i == 0) ? Color.red : Color.green;
                        DrawRectOutline(tex, rects[i], debugThickness, col);
                        DrawLabelDot(tex, rects[i], col);
                    }
                }

                tex.Apply();
            }
        }

        // Restore
        captureCamera.targetTexture = prevTarget;
        RenderTexture.active = prevActive;
        RenderTexture.ReleaseTemporary(rt);

        return tex;
    }

    public Texture2D CloneTexture(Texture2D src)
    {
        if (src == null) return null;

        var dst = new Texture2D(src.width, src.height, src.format, false);
        dst.SetPixels32(src.GetPixels32());
        dst.Apply();
        return dst;
    }
    public void DrawRectsDebug(Texture2D tex, List<Rect> rects)
    {
        if (tex == null || rects == null || rects.Count == 0) return;

        rects.Sort((a, b) => (b.width * b.height).CompareTo(a.width * a.height));

        if (debugDrawOnlyLargest)
        {
            var r = rects[0];
            DrawRectOutline(tex, r, debugThickness, Color.red);
            DrawLabelDot(tex, r, Color.red);
        }
        else
        {
            for (int i = 0; i < rects.Count; i++)
            {
                var col = (i == 0) ? Color.red : Color.green;
                DrawRectOutline(tex, rects[i], debugThickness, col);
                DrawLabelDot(tex, rects[i], col);
            }
        }

        tex.Apply();
    }


}










