using UnityEngine;
using UnityEngine.UI;
using System.Collections.Generic;

public class YoloBBoxOverlay : MonoBehaviour
{
    [SerializeField] private RectTransform overlayRoot;
    [SerializeField] private GameObject boxPrefab;

    private readonly List<GameObject> spawned = new List<GameObject>();

    [System.Serializable]
    public class BoxView
    {
        public RectTransform rectTransform;
        public Image borderImage;
        public Text labelText;
    }

    public void Draw(List<DroneYoloUartDetector.Detection> detections, int sourceWidth, int sourceHeight)
    {
        Clear();

        if (overlayRoot == null || boxPrefab == null)
            return;

        float rootW = overlayRoot.rect.width;
        float rootH = overlayRoot.rect.height;

        float scaleX = rootW / sourceWidth;
        float scaleY = rootH / sourceHeight;

        for (int i = 0; i < detections.Count; i++)
        {
            var det = detections[i];
            GameObject go = Instantiate(boxPrefab, overlayRoot);
            spawned.Add(go);

            RectTransform rt = go.GetComponent<RectTransform>();
            Image img = go.GetComponent<Image>();

            // source space -> UI overlay space
            float x = det.rect.x * scaleX;
            float y = det.rect.y * scaleY;
            float w = det.rect.width * scaleX;
            float h = det.rect.height * scaleY;

            // UI'de anchor top-left kullanıyorsan buna göre ayar yap
            rt.anchorMin = new Vector2(0f, 1f);
            rt.anchorMax = new Vector2(0f, 1f);
            rt.pivot = new Vector2(0f, 1f);

            // Görüntü space'te Y aşağı doğru artar, UI'de top-left için ters çeviriyoruz
            rt.anchoredPosition = new Vector2(x, -y);
            rt.sizeDelta = new Vector2(w, h);

            if (img != null)
            {
                img.color = GetClassColor(det.classId);
            }

            var label = go.GetComponentInChildren<Text>();
            if (label != null)
            {
                label.text = $"{((DroneYoloUartDetector.VehicleClass)det.classId)} {det.score:F2}";
                label.color = GetClassColor(det.classId);
            }
        }
    }

    public void Clear()
    {
        for (int i = 0; i < spawned.Count; i++)
        {
            if (spawned[i] != null)
                Destroy(spawned[i]);
        }
        spawned.Clear();
    }

    private Color GetClassColor(int classId)
    {
        switch (classId)
        {
            case 0: return Color.red;      // Tank
            case 1: return Color.yellow;   // ZPT
            case 2: return Color.cyan;     // Truck
            case 3: return Color.green;    // Civil
            default: return Color.white;
        }
    }
}