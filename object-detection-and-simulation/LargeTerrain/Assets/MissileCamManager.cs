using System.Collections.Generic;
using UnityEngine;

public class MissileCamManager : MonoBehaviour
{
    [System.Serializable]
    public class MissileCamEntry
    {
        public GuidedMissile missile;
        public RectTransform container;
    }

    [Header("Missile - Camera Container Pairs")]
    [SerializeField] private List<MissileCamEntry> entries = new List<MissileCamEntry>();

    [Header("Layout")]
    [SerializeField] private Vector2 bottomLeftMargin = new Vector2(10f, 10f);
    [SerializeField] private float gapBetweenCameras = 8f;

    private readonly List<MissileCamEntry> activeOrder = new List<MissileCamEntry>();
    private readonly HashSet<MissileCamEntry> trackedActive = new HashSet<MissileCamEntry>();

    private void Update()
    {
        bool layoutDirty = false;

        foreach (MissileCamEntry entry in entries)
        {
            if (entry.container == null)
                continue;

            bool wasTracked = trackedActive.Contains(entry);

            if (entry.missile == null)
            {
                if (wasTracked)
                {
                    trackedActive.Remove(entry);
                    activeOrder.Remove(entry);
                    layoutDirty = true;
                }
                continue;
            }

            bool isLaunched = entry.missile.IsLaunched;

            if (isLaunched && !wasTracked)
            {
                trackedActive.Add(entry);
                activeOrder.Add(entry);
                layoutDirty = true;
            }
            else if (!isLaunched && wasTracked)
            {
                trackedActive.Remove(entry);
                activeOrder.Remove(entry);
                layoutDirty = true;
            }
        }

        if (layoutDirty)
            ApplyLayout();
    }

    private void ApplyLayout()
    {
        float currentY = bottomLeftMargin.y;

        foreach (MissileCamEntry entry in activeOrder)
        {
            if (entry.container == null)
                continue;

            entry.container.anchorMin = Vector2.zero;
            entry.container.anchorMax = Vector2.zero;
            entry.container.pivot = Vector2.zero;
            entry.container.anchoredPosition = new Vector2(bottomLeftMargin.x, currentY);

            currentY += entry.container.sizeDelta.y + gapBetweenCameras;
        }
    }
}
