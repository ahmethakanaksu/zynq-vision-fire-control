using UnityEngine;
using System;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

/*
 * DroneSerialManager -- Unity-to-board transport.
 *
 * The class name and public API are deliberately kept identical to the
 * earlier UART version of this script so the seven dependent scripts
 * (LsrCommandBridge, FireCommandBridge, ClusterLsrBridge, ClusterFireBridge,
 * ClusterModeState, BoardMockController, DroneYoloUartDetector) need
 * zero changes. They keep talking to:
 *
 *     DroneSerialManager.Instance.EnqueueSend(string line)
 *     DroneSerialManager.Instance.OnLineReceived  (event)
 *     DroneSerialManager.Instance.OnLineSent      (event)
 *     DroneSerialManager.Instance.InjectIncomingLine(string line)
 *
 * Only the private transport changed: System.IO.Ports.SerialPort has
 * been replaced with a single UdpClient, so the link runs at full
 * Ethernet bandwidth instead of 115200 baud serial.
 *
 * Wire layout:
 *     Drone (Unity)  :  binds UDP localPort       (default 5006)
 *     Board (Zybo)   :  listens on boardRxPort    (default 5005)
 *     drone -> board :  src = localPort,  dst = boardRxPort
 *     board -> drone :  dst = localPort   (auto-learned from our source
 *                       port on the board's first RX)
 *
 *  - One socket carries both directions, so a single bind is enough.
 *  - ReuseAddress is set so Unity Editor play/stop cycles don't fail
 *    with "address already in use" while the previous socket lingers.
 *  - Receive runs on a background thread with a 100 ms socket timeout
 *    so ClosePort can shut the loop down deterministically.
 */
public class DroneSerialManager : MonoBehaviour
{
    public static DroneSerialManager Instance { get; private set; }

    [Header("Network (UDP)")]
    [Tooltip("Board IP address (Zybo Z7-20). Outgoing FRM/LSRRES/CLSRRES/MODE_*/ARM/DISARM/RESET go here.")]
    [SerializeField] private string boardIp = "192.168.1.10";

    [Tooltip("Board RX port. The board's udp_router listens on this port (OMC_RX_PORT in the firmware).")]
    [SerializeField] private int boardRxPort = 5005;

    [Tooltip("Local UDP port the drone binds to. The board's reply destination is auto-learned from our source port on first RX, so this needs to be deterministic. 5006 mirrors the Python test harness.")]
    [SerializeField] private int localPort = 5006;

    [Header("Lifecycle")]
    [SerializeField] private bool autoOpenOnStart = true;

    [Tooltip("On open, send a small warm-up datagram so the board learns our destination address before any LSRCMD/FIRE/STATUS goes out.")]
    [SerializeField] private bool sendWarmupOnOpen = true;
    [SerializeField] private string warmupMessage = "PING-drone";

    private UdpClient   udpClient;
    private IPEndPoint  boardEndpoint;
    private Thread      readThread;
    private volatile bool keepReading;

    private readonly ConcurrentQueue<string> incomingLines = new ConcurrentQueue<string>();
    private readonly ConcurrentQueue<string> outgoingLines = new ConcurrentQueue<string>();

    public event Action<string> OnLineReceived;
    public event Action<string> OnLineSent;

    public void InjectIncomingLine(string line)
    {
        if (!string.IsNullOrWhiteSpace(line))
            incomingLines.Enqueue(line.Trim());
    }

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }

        Instance = this;
    }

    private void Start()
    {
        if (autoOpenOnStart)
            OpenPort();
    }

    public void OpenPort()
    {
        if (udpClient != null)
            return;

        try
        {
            udpClient = new UdpClient();
            udpClient.Client.SetSocketOption(SocketOptionLevel.Socket,
                                             SocketOptionName.ReuseAddress, true);
            udpClient.Client.Bind(new IPEndPoint(IPAddress.Any, localPort));
            udpClient.Client.ReceiveTimeout = 100;

            boardEndpoint = new IPEndPoint(IPAddress.Parse(boardIp), boardRxPort);

            keepReading = true;
            readThread = new Thread(ReadLoop);
            readThread.IsBackground = true;
            readThread.Start();

            Debug.Log($"[NET] UDP up -- local :{localPort} -> board {boardIp}:{boardRxPort}");

            if (sendWarmupOnOpen && !string.IsNullOrEmpty(warmupMessage))
            {
                EnqueueSend(warmupMessage);
            }
        }
        catch (Exception ex)
        {
            Debug.LogError($"[NET] UDP open failed: {ex.Message}");
        }
    }

    public void ClosePort()
    {
        keepReading = false;

        try
        {
            if (readThread != null && readThread.IsAlive)
                readThread.Join(200);
        }
        catch { }

        try
        {
            if (udpClient != null)
            {
                udpClient.Close();
                udpClient = null;
            }
        }
        catch { }

        Debug.Log("[NET] UDP closed");
    }

    private void ReadLoop()
    {
        IPEndPoint anyEndpoint = new IPEndPoint(IPAddress.Any, 0);

        while (keepReading)
        {
            try
            {
                if (udpClient == null)
                    break;

                byte[] data = udpClient.Receive(ref anyEndpoint);
                if (data == null || data.Length == 0)
                    continue;

                string text = Encoding.ASCII.GetString(data);

                /* Diagnostic. If this log doesn't appear in the Unity Console
                 * while the board UART shows it sending LSRCMD, the problem is
                 * at the OS/firewall layer (Windows Firewall inbound rule for
                 * Unity.exe), not in this code. */
                Debug.Log($"[NET RX] {data.Length}B from {anyEndpoint}: {text.Trim()}");

                /* Board uses xil_printf-style "\r\n" line endings and one
                 * datagram = one line in current firmware. Be defensive: split
                 * on '\n' in case ever-coalesced and trim '\r'/spaces. */
                string[] lines = text.Split('\n');
                for (int i = 0; i < lines.Length; i++)
                {
                    string l = lines[i].Trim();
                    if (!string.IsNullOrWhiteSpace(l))
                        incomingLines.Enqueue(l);
                }
            }
            catch (SocketException ex) when (ex.SocketErrorCode == SocketError.TimedOut)
            {
                /* Periodic timeout: lets the loop re-check keepReading so
                 * ClosePort can shut us down cleanly. */
            }
            catch (ObjectDisposedException)
            {
                break; /* socket closed during shutdown */
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[NET] Read error: {ex.Message}");
                Thread.Sleep(50);
            }
        }
    }

    private void Update()
    {
        while (incomingLines.TryDequeue(out string line))
        {
            OnLineReceived?.Invoke(line);
        }

        while (outgoingLines.TryDequeue(out string line))
        {
            OnLineSent?.Invoke(line);

            try
            {
                if (udpClient != null && boardEndpoint != null)
                {
                    byte[] payload = Encoding.ASCII.GetBytes(line);
                    udpClient.Send(payload, payload.Length, boardEndpoint);
                }
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[NET] Write error: {ex.Message}");
            }
        }
    }

    public void EnqueueSend(string line)
    {
        if (string.IsNullOrEmpty(line))
            return;

        if (!line.EndsWith("\n"))
            line += "\n";

        outgoingLines.Enqueue(line);
    }

    private void OnDestroy()
    {
        ClosePort();
    }
}
