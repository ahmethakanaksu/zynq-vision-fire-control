using UnityEngine;
using System.Collections.Generic;

public static class YoloBBoxUtil
{
    /// <summary>
    /// Objeyi (children dahil) mesh vertices üzerinden RT-boyutunda bbox'a çevirir.
    /// WorldToViewportPoint ile hesaplar => RT boyutuna ölçekler.
    /// </summary>
    public static bool TryGetBBoxFromVertices(
        GameObject root,
        Camera cam,
        int texW, int texH,
        out Rect unclampedPx,
        out Rect clampedPx,
        out float visibleRatio,
        out int usedPointCount,
        int vertexStride = 1,
        bool requireAnyPointInsideViewport = true)
    {
        unclampedPx = default;
        clampedPx = default;
        visibleRatio = 0f;
        usedPointCount = 0;

        if (!root || !cam) return false;

        float minX = float.PositiveInfinity, minY = float.PositiveInfinity;
        float maxX = float.NegativeInfinity, maxY = float.NegativeInfinity;

        bool any = false;
        bool anyInside = false;

        // 1) MeshFilter vertices (children dahil)
        var mfs = root.GetComponentsInChildren<MeshFilter>(true);
        for (int i = 0; i < mfs.Length; i++)
        {
            var mf = mfs[i];
            if (!mf || !mf.sharedMesh) continue;

            var rend = mf.GetComponent<Renderer>();
            if (rend && !rend.enabled) continue;

            var mesh = mf.sharedMesh;
            var verts = mesh.vertices;
            if (verts == null || verts.Length == 0) continue;

            var tr = mf.transform;

            int stride = Mathf.Max(1, vertexStride);
            for (int v = 0; v < verts.Length; v += stride)
            {
                Vector3 world = tr.TransformPoint(verts[v]);
                if (TryAccumulate(cam, world, texW, texH, ref minX, ref minY, ref maxX, ref maxY, ref anyInside))
                {
                    any = true;
                    usedPointCount++;
                }
            }
        }

        // 2) SkinnedMeshRenderer vertices (bake)
        var sks = root.GetComponentsInChildren<SkinnedMeshRenderer>(true);
        for (int i = 0; i < sks.Length; i++)
        {
            var sk = sks[i];
            if (!sk || !sk.enabled) continue;

            Mesh baked = new Mesh();
            sk.BakeMesh(baked);
            var verts = baked.vertices;

            if (verts != null && verts.Length > 0)
            {
                // NOT: baked mesh local-space'tir => skinned transform ile world'e
                var tr = sk.transform;

                int stride = Mathf.Max(1, vertexStride);
                for (int v = 0; v < verts.Length; v += stride)
                {
                    Vector3 world = tr.TransformPoint(verts[v]);
                    if (TryAccumulate(cam, world, texW, texH, ref minX, ref minY, ref maxX, ref maxY, ref anyInside))
                    {
                        any = true;
                        usedPointCount++;
                    }
                }
            }

            Object.Destroy(baked);
        }

        // 3) Fallback: hiç vertex yoksa renderer localBounds köþeleri
        if (!any)
        {
            var renderers = root.GetComponentsInChildren<Renderer>(true);
            foreach (var r in renderers)
            {
                if (!r || !r.enabled) continue;

                Bounds lb = r.localBounds;
                Vector3 c = lb.center;
                Vector3 e = lb.extents;

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

                for (int k = 0; k < localCorners.Length; k++)
                {
                    Vector3 world = r.transform.TransformPoint(localCorners[k]);
                    if (TryAccumulate(cam, world, texW, texH, ref minX, ref minY, ref maxX, ref maxY, ref anyInside))
                    {
                        any = true;
                        usedPointCount++;
                    }
                }
            }
        }

        if (!any) return false;
        if (requireAnyPointInsideViewport && !anyInside) return false;

        float w = maxX - minX;
        float h = maxY - minY;
        if (w <= 1f || h <= 1f) return false;

        unclampedPx = new Rect(minX, minY, w, h);

        float cx0 = Mathf.Clamp(minX, 0f, texW);
        float cy0 = Mathf.Clamp(minY, 0f, texH);
        float cx1 = Mathf.Clamp(maxX, 0f, texW);
        float cy1 = Mathf.Clamp(maxY, 0f, texH);
        clampedPx = Rect.MinMaxRect(cx0, cy0, cx1, cy1);

        float unclampedArea = unclampedPx.width * unclampedPx.height;
        float clampedArea = clampedPx.width * clampedPx.height;
        visibleRatio = unclampedArea > 1e-5f ? (clampedArea / unclampedArea) : 0f;

        return true;
    }

    // Viewport -> pixel (RT boyutu)
    private static bool TryAccumulate(
        Camera cam,
        Vector3 world,
        int texW, int texH,
        ref float minX, ref float minY, ref float maxX, ref float maxY,
        ref bool anyInside)
    {
        Vector3 vp = cam.WorldToViewportPoint(world);
        if (vp.z <= 0f) return false;

        if (vp.x >= 0f && vp.x <= 1f && vp.y >= 0f && vp.y <= 1f)
            anyInside = true;

        float x = vp.x * texW;
        float y = vp.y * texH; // (0,0) bottom-left mantýðý ile ayný

        minX = Mathf.Min(minX, x);
        minY = Mathf.Min(minY, y);
        maxX = Mathf.Max(maxX, x);
        maxY = Mathf.Max(maxY, y);

        return true;
    }
}
