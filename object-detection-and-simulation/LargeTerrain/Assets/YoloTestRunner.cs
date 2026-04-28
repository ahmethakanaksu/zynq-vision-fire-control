using UnityEngine;
using Unity.InferenceEngine;
using System.Collections.Generic;
using System.Linq;

public class YoloTestRunner : MonoBehaviour
{
    [SerializeField] private ModelAsset modelAsset;
    [SerializeField] private Texture2D testImage;

    [Header("Thresholds")]
    [SerializeField] private float scoreThreshold = 0.25f;
    [SerializeField] private float nmsThreshold = 0.45f;

    [Header("Image Size")]
    [SerializeField] private int imageWidth = 416;
    [SerializeField] private int imageHeight = 416;

    private Model runtimeModel;
    private Worker worker;

    public enum VehicleClass
    {
        Tank = 0,
        ZPT = 1,
        Truck = 2,
        Civil = 3
    }

    public class Detection
    {
        public Rect rect;
        public int classId;
        public float score;
    }

    private void Start()
    {
        runtimeModel = ModelLoader.Load(modelAsset);
        worker = new Worker(runtimeModel, BackendType.GPUCompute);
        RunTest();
    }

    private void RunTest()
    {
        if (testImage == null)
        {
            Debug.LogError("Test image yok.");
            return;
        }

        Tensor inputTensor = TextureConverter.ToTensor(testImage, width: imageWidth, height: imageHeight, channels: 3);
        worker.Schedule(inputTensor);

        foreach (var output in runtimeModel.outputs)
        {
            Tensor<float> result = worker.PeekOutput(output.name) as Tensor<float>;
            if (result == null)
            {
                Debug.LogError($"Output {output.name} float tensor değil.");
                continue;
            }

            using Tensor<float> cpuTensor = result.ReadbackAndClone();
            float[] data = cpuTensor.DownloadToArray();

            int dim1 = cpuTensor.shape[1]; // 8
            int dim2 = cpuTensor.shape[2]; // 845

            Debug.Log($"Output: {output.name}");
            Debug.Log($"Shape: {cpuTensor.shape}");

            List<Detection> detections = ParseDetections(data, dim1, dim2);
            List<Detection> finalDetections = ApplyNMS(detections, nmsThreshold);

            Debug.Log($"Threshold sonrası aday: {detections.Count}");
            Debug.Log($"NMS sonrası final kutu: {finalDetections.Count}");

            foreach (var det in finalDetections.OrderByDescending(d => d.score))
            {
                string className = ((VehicleClass)det.classId).ToString();
                Debug.Log($"{className} | score={det.score:F3} | rect=({det.rect.x:F1}, {det.rect.y:F1}, {det.rect.width:F1}, {det.rect.height:F1})");
            }
        }

        inputTensor.Dispose();
    }

    private List<Detection> ParseDetections(float[] data, int dim1, int dim2)
    {
        List<Detection> detections = new List<Detection>();

        for (int det = 0; det < dim2; det++)
        {
            float cx = GetValue(data, dim1, dim2, 0, det);
            float cy = GetValue(data, dim1, dim2, 1, det);
            float w = GetValue(data, dim1, dim2, 2, det);
            float h = GetValue(data, dim1, dim2, 3, det);

            int bestClass = -1;
            float bestScore = -1f;

            // class skorları: 4,5,6,7
            for (int c = 0; c < 4; c++)
            {
                float score = GetValue(data, dim1, dim2, 4 + c, det);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestClass = c;
                }
            }

            if (bestScore < scoreThreshold)
                continue;

            // Burada model output'unun center-x, center-y, width, height olduğunu varsayıyoruz
            float x = cx - (w / 2f);
            float y = cy - (h / 2f);

            Detection detection = new Detection
            {
                rect = new Rect(x, y, w, h),
                classId = bestClass,
                score = bestScore
            };

            detections.Add(detection);
        }

        return detections;
    }

    private float GetValue(float[] data, int dim1, int dim2, int attr, int det)
    {
        int index = (attr * dim2) + det;
        return data[index];
    }

    private List<Detection> ApplyNMS(List<Detection> detections, float iouThreshold)
    {
        List<Detection> results = new List<Detection>();

        var sorted = detections.OrderByDescending(d => d.score).ToList();

        while (sorted.Count > 0)
        {
            Detection best = sorted[0];
            results.Add(best);
            sorted.RemoveAt(0);

            sorted = sorted
                .Where(d => d.classId != best.classId || IoU(best.rect, d.rect) < iouThreshold)
                .ToList();
        }

        return results;
    }

    private float IoU(Rect a, Rect b)
    {
        float x1 = Mathf.Max(a.xMin, b.xMin);
        float y1 = Mathf.Max(a.yMin, b.yMin);
        float x2 = Mathf.Min(a.xMax, b.xMax);
        float y2 = Mathf.Min(a.yMax, b.yMax);

        float intersectionWidth = Mathf.Max(0, x2 - x1);
        float intersectionHeight = Mathf.Max(0, y2 - y1);
        float intersectionArea = intersectionWidth * intersectionHeight;

        float unionArea = a.width * a.height + b.width * b.height - intersectionArea;

        if (unionArea <= 0f)
            return 0f;

        return intersectionArea / unionArea;
    }

    private void OnDestroy()
    {
        worker?.Dispose();
    }
}