using UnityEngine;
using Unity.InferenceEngine;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.IO.Ports;

public class DroneYoloUartDetector : MonoBehaviour
{
    [Header("Model")]
    [SerializeField] private ModelAsset modelAsset;
    [SerializeField] private BackendType backendType = BackendType.GPUCompute;

    [Header("Input")]
    [SerializeField] private RenderTexture sourceRenderTexture;
    [SerializeField] private int modelInputWidth = 416;
    [SerializeField] private int modelInputHeight = 416;

    [Header("Detection")]
    [SerializeField] private float scoreThreshold = 0.25f;
    [SerializeField] private float nmsThreshold = 0.45f;
    [SerializeField] private int inferenceEveryNFrames = 3;

    [Header("Preview / Overlay")]
    [SerializeField] private int previewWidth = 416;
    [SerializeField] private int previewHeight = 416;
    [SerializeField] private YoloBBoxOverlay overlay;

    [Header("UART")]
    [SerializeField] private bool sendToSerial = false;
    [SerializeField] private string serialPortName = "COM3";
    [SerializeField] private int baudRate = 115200;

    private Model runtimeModel;
    private Worker worker;
    private SerialPort serialPort;

    private int frameId = 0;
    private int updateCounter = 0;

    public enum VehicleClass
    {
        Tank = 0,
        ZPT = 1,
        Truck = 2,
        Civil = 3
    }

    [Serializable]
    public class Detection
    {
        public Rect rect;     // preview/image space
        public int classId;
        public float score;

        public float CenterX => rect.x + rect.width * 0.5f;
        public float CenterY => rect.y + rect.height * 0.5f;
    }

    private void Start()
    {
        if (modelAsset == null)
        {
            Debug.LogError("ModelAsset atanmadı.");
            enabled = false;
            return;
        }

        if (sourceRenderTexture == null)
        {
            Debug.LogError("Source RenderTexture atanmadı.");
            enabled = false;
            return;
        }

        runtimeModel = ModelLoader.Load(modelAsset);
        worker = new Worker(runtimeModel, backendType);

        if (sendToSerial)
        {
            TryOpenSerial();
        }
    }

    private void Update()
    {
        updateCounter++;
        if (updateCounter % inferenceEveryNFrames != 0)
            return;

        RunInferenceFrame();
    }

    private void RunInferenceFrame()
    {
        if (worker == null || sourceRenderTexture == null)
            return;

        frameId++;

        using Tensor inputTensor = TextureConverter.ToTensor(
            sourceRenderTexture,
            width: modelInputWidth,
            height: modelInputHeight,
            channels: 3
        );

        worker.Schedule(inputTensor);

        Tensor<float> outputTensor = worker.PeekOutput() as Tensor<float>;
        if (outputTensor == null)
        {
            Debug.LogError("Output tensor float değil veya PeekOutput başarısız.");
            return;
        }

        using Tensor<float> cpuTensor = outputTensor.ReadbackAndClone();
        float[] data = cpuTensor.DownloadToArray();

        int dim1 = cpuTensor.shape[1]; // 8
        int dim2 = cpuTensor.shape[2]; // 845

        List<Detection> detections = ParseDetections(data, dim1, dim2);
        List<Detection> finalDetections = ApplyNms(detections, nmsThreshold);

        if (overlay != null)
        {
            overlay.Draw(finalDetections, previewWidth, previewHeight);
        }

        if (finalDetections.Count > 0)
        {
            string uartMessage = BuildFrameMessage(frameId, finalDetections);
            Debug.Log(uartMessage.TrimEnd());

            if (DroneSerialManager.Instance != null)
                DroneSerialManager.Instance.EnqueueSend(uartMessage);
        }
    }

    private List<Detection> ParseDetections(float[] data, int dim1, int dim2)
    {
        List<Detection> results = new List<Detection>();

        // Beklenen format:
        // [cx, cy, w, h, tank, zpt, truck, civil]
        for (int det = 0; det < dim2; det++)
        {
            float cx = GetValue(data, dim2, 0, det);
            float cy = GetValue(data, dim2, 1, det);
            float w = GetValue(data, dim2, 2, det);
            float h = GetValue(data, dim2, 3, det);

            int bestClass = -1;
            float bestScore = -1f;

            for (int classOffset = 0; classOffset < 4; classOffset++)
            {
                float score = GetValue(data, dim2, 4 + classOffset, det);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestClass = classOffset;
                }
            }

            if (bestScore < scoreThreshold)
                continue;

            // Çıktın piksel benzeri space'te görünüyor.
            float x = cx - (w * 0.5f);
            float y = cy - (h * 0.5f);

            Rect rect = new Rect(x, y, w, h);

            // Güvenlik clamp'i
            rect.x = Mathf.Clamp(rect.x, 0f, previewWidth);
            rect.y = Mathf.Clamp(rect.y, 0f, previewHeight);
            rect.width = Mathf.Clamp(rect.width, 0f, previewWidth);
            rect.height = Mathf.Clamp(rect.height, 0f, previewHeight);

            results.Add(new Detection
            {
                rect = rect,
                classId = bestClass,
                score = bestScore
            });
        }

        return results;
    }

    private static float GetValue(float[] data, int dim2, int attr, int det)
    {
        return data[(attr * dim2) + det];
    }

    private List<Detection> ApplyNms(List<Detection> detections, float iouThreshold)
    {
        List<Detection> output = new List<Detection>();
        List<Detection> sorted = detections.OrderByDescending(d => d.score).ToList();

        while (sorted.Count > 0)
        {
            Detection best = sorted[0];
            output.Add(best);
            sorted.RemoveAt(0);

            sorted = sorted
                .Where(d => d.classId != best.classId || IoU(best.rect, d.rect) < iouThreshold)
                .ToList();
        }

        return output;
    }

    private float IoU(Rect a, Rect b)
    {
        float x1 = Mathf.Max(a.xMin, b.xMin);
        float y1 = Mathf.Max(a.yMin, b.yMin);
        float x2 = Mathf.Min(a.xMax, b.xMax);
        float y2 = Mathf.Min(a.yMax, b.yMax);

        float iw = Mathf.Max(0f, x2 - x1);
        float ih = Mathf.Max(0f, y2 - y1);
        float inter = iw * ih;

        float union = (a.width * a.height) + (b.width * b.height) - inter;
        if (union <= 0f) return 0f;

        return inter / union;
    }

    private string BuildFrameMessage(int currentFrameId, List<Detection> detections)
    {
        StringBuilder sb = new StringBuilder(256);
        sb.Append("FRM,");
        sb.Append(currentFrameId);
        sb.Append(",");
        sb.Append(detections.Count);

        for (int i = 0; i < detections.Count; i++)
        {
            Detection d = detections[i];

            int cx = Mathf.RoundToInt(d.CenterX);
            int cy = Mathf.RoundToInt(d.CenterY);
            int w = Mathf.RoundToInt(d.rect.width);
            int h = Mathf.RoundToInt(d.rect.height);

            sb.Append(",");
            sb.Append(d.classId);
            sb.Append(",");
            sb.Append(cx);
            sb.Append(",");
            sb.Append(cy);
            sb.Append(",");
            sb.Append(w);
            sb.Append(",");
            sb.Append(h);
        }

        sb.Append("\n");
        return sb.ToString();
    }

    private void TryOpenSerial()
    {
        try
        {
            serialPort = new SerialPort(serialPortName, baudRate);
            serialPort.NewLine = "\n";
            serialPort.Open();
            Debug.Log($"Serial açıldı: {serialPortName} @ {baudRate}");
        }
        catch (Exception ex)
        {
            Debug.LogWarning($"Serial açılamadı: {ex.Message}");
        }
    }

    private void OnDestroy()
    {
        if (worker != null)
            worker.Dispose();

        if (serialPort != null)
        {
            try
            {
                if (serialPort.IsOpen)
                    serialPort.Close();
            }
            catch
            {
            }
        }
    }
}