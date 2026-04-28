using System.Collections.Generic;
using System.Globalization;
using System.Text;
using UnityEngine;

public class BoardMockController : MonoBehaviour
{
    [Header("Camera")]
    [SerializeField] private float cameraHFov = 60f;
    [SerializeField] private float cameraVFov = 60f;
    [SerializeField] private float cameraBasePitchDown = 50f;

    [Header("Tracking")]
    [SerializeField] private int maxMissedFrames = 10;
    [SerializeField] private int lsrRetryGapFrames = 5;

    // ── Track ────────────────────────────────────────────────
    private class MockTrack
    {
        public int  trackId;
        public int  classId;
        public float cx, cy, w, h;
        public int  lastFrameId;
        public int  missedFrames;
        public bool discovered;
        public bool lsrPending;
        public int  lsrRequestFrameId = -1;
        public bool engaged;
        public bool matchedThisFrame;
    }

    [Header("Fire Cooldown")]
    [SerializeField] private float fireCooldown = 4f;

    private readonly List<MockTrack> tracks = new List<MockTrack>();
    private readonly int[] classCounters = new int[4];
    private float nextFireAllowedTime = 0f;

    // ── Missile inventory (mirrors board) ────────────────────
    private class MockMissile
    {
        public int  missileId;
        public int  type;       // 0=Cirit 1=MAML 2=SOM
        public bool available;
    }

    private List<MockMissile> missiles;

    // ─────────────────────────────────────────────────────────

    private void Awake()
    {
        ResetState();
    }

    private void Start()
    {
        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.OnLineSent += HandleLineSent;
        else
            Debug.LogError("[MOCK] DroneSerialManager.Instance null — mock devre dışı!");
    }

    private void OnDestroy()
    {
        if (DroneSerialManager.Instance != null)
            DroneSerialManager.Instance.OnLineSent -= HandleLineSent;
    }

    // FRM (Unity→Board) ve LSRRES (Unity→Board) ikisi de outgoing
    private void HandleLineSent(string line)
    {
        if (line.StartsWith("FRM,"))
            ProcessFrm(line);
        else if (line.StartsWith("LSRRES,"))
            ProcessLsrRes(line);
    }

    // ── State reset ──────────────────────────────────────────

    private void ResetState()
    {
        tracks.Clear();
        for (int i = 0; i < 4; i++) classCounters[i] = 0;

        missiles = new List<MockMissile>
        {
            new MockMissile { missileId =  1, type = 0, available = true },
            new MockMissile { missileId =  2, type = 0, available = true },
            new MockMissile { missileId =  3, type = 0, available = true },
            new MockMissile { missileId =  4, type = 0, available = true },
            new MockMissile { missileId = 11, type = 1, available = true },
            new MockMissile { missileId = 12, type = 1, available = true },
            new MockMissile { missileId = 13, type = 1, available = true },
            new MockMissile { missileId = 14, type = 1, available = true },
            new MockMissile { missileId = 21, type = 2, available = true },
        };
    }

    // ── Track helpers ────────────────────────────────────────

    private int NextTrackId(int classId)
    {
        return classId * 100 + classCounters[classId]++;
    }

    private MockTrack FindBestMatch(int classId, float cx, float cy, float w, float h)
    {
        float diag  = Mathf.Sqrt(w * w + h * h);
        float thr   = Mathf.Clamp(0.6f * diag, 18f, 60f);
        float thrSq = thr * thr;

        MockTrack best     = null;
        float     bestDistSq = float.MaxValue;

        foreach (MockTrack t in tracks)
        {
            if (t.classId != classId) continue;
            if (t.matchedThisFrame)  continue;
            if (t.missedFrames > maxMissedFrames) continue;

            float dx  = t.cx - cx;
            float dy  = t.cy - cy;
            float dsq = dx * dx + dy * dy;

            if (dsq <= thrSq && dsq < bestDistSq)
            {
                bestDistSq = dsq;
                best       = t;
            }
        }
        return best;
    }

    // ── FRM processing ───────────────────────────────────────

    private void ProcessFrm(string line)
    {
        string[] p = line.Trim().Split(',');
        if (p.Length < 3) return;

        if (!int.TryParse(p[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int frameId)) return;
        if (!int.TryParse(p[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int count))   return;
        if (count <= 0 || p.Length < 3 + count * 5) return;

        // Begin frame
        foreach (MockTrack t in tracks) t.matchedThisFrame = false;

        // Associate detections → tracks
        int idx = 3;
        for (int i = 0; i < count; i++)
        {
            int   classId = 0;
            float cx = 0f, cy = 0f, w = 0f, h = 0f;

            bool ok =
                int.TryParse(p[idx],     NumberStyles.Integer, CultureInfo.InvariantCulture, out classId) &&
                float.TryParse(p[idx+1], NumberStyles.Float,   CultureInfo.InvariantCulture, out cx)      &&
                float.TryParse(p[idx+2], NumberStyles.Float,   CultureInfo.InvariantCulture, out cy)      &&
                float.TryParse(p[idx+3], NumberStyles.Float,   CultureInfo.InvariantCulture, out w)       &&
                float.TryParse(p[idx+4], NumberStyles.Float,   CultureInfo.InvariantCulture, out h);
            idx += 5;

            if (!ok || classId < 0 || classId > 3) continue;

            MockTrack match = FindBestMatch(classId, cx, cy, w, h);
            if (match != null)
            {
                match.cx = cx; match.cy = cy; match.w = w; match.h = h;
                match.lastFrameId   = frameId;
                match.missedFrames  = 0;
                match.matchedThisFrame = true;
            }
            else
            {
                tracks.Add(new MockTrack
                {
                    trackId = NextTrackId(classId),
                    classId = classId,
                    cx = cx, cy = cy, w = w, h = h,
                    lastFrameId        = frameId,
                    missedFrames       = 0,
                    lsrRequestFrameId  = -1,
                    matchedThisFrame   = true
                });
            }
        }

        // End frame — expire stale tracks
        for (int i = tracks.Count - 1; i >= 0; i--)
        {
            if (!tracks[i].matchedThisFrame)
            {
                tracks[i].missedFrames++;
                if (tracks[i].missedFrames > maxMissedFrames)
                    tracks.RemoveAt(i);
            }
        }

        SendLsrBatch(frameId);
    }

    // ── LSR batch ────────────────────────────────────────────

    private static bool IsHostile(int classId) => classId == 0 || classId == 1 || classId == 2;

    private bool ShouldRequestLsr(MockTrack t, int frameId)
    {
        if (!IsHostile(t.classId))  return false;
        if (t.discovered)           return false;
        if (t.engaged)              return false;
        if (t.missedFrames > 0)     return false;
        if (!t.lsrPending)          return true;
        return (frameId - t.lsrRequestFrameId) >= lsrRetryGapFrames;
    }

    private void SendLsrBatch(int frameId)
    {
        List<MockTrack> candidates = new List<MockTrack>();
        foreach (MockTrack t in tracks)
            if (ShouldRequestLsr(t, frameId))
                candidates.Add(t);

        if (candidates.Count == 0) return;

        StringBuilder sb = new StringBuilder();
        sb.Append("LSRCMD,").Append(frameId).Append(',').Append(candidates.Count);

        foreach (MockTrack t in candidates)
        {
            ComputeAngles(t.cx, t.cy, out int pitchMdeg, out int yawMdeg);

            sb.Append(',').Append(t.trackId.ToString("D3"));
            sb.Append(',').Append(pitchMdeg);
            sb.Append(',').Append(yawMdeg);

            t.lsrPending          = true;
            t.lsrRequestFrameId   = frameId;
        }

        string msg = sb.ToString();
        Debug.Log($"[MOCK → LSRCMD] {msg}");
        DroneSerialManager.Instance.InjectIncomingLine(msg);
    }

    // ── Angle calculation (mirrors board's atan formula) ─────

    private void ComputeAngles(float cx, float cy, out int pitchMdeg, out int yawMdeg)
    {
        float nx = (cx - 208f) / 208f;   // -1..+1
        float ny = (208f - cy) / 208f;   // -1..+1, y ekseni çevrilmiş

        float hfovRad = cameraHFov * 0.5f * Mathf.Deg2Rad;
        float vfovRad = cameraVFov * 0.5f * Mathf.Deg2Rad;

        float yawDeg         = Mathf.Atan(nx * Mathf.Tan(hfovRad)) * Mathf.Rad2Deg;
        float pitchOffsetDeg = Mathf.Atan(ny * Mathf.Tan(vfovRad)) * Mathf.Rad2Deg;
        float pitchDeg       = cameraBasePitchDown - pitchOffsetDeg;

        // LsrCommandBridge: pitchMdeg / 10000f = derece
        pitchMdeg = Mathf.RoundToInt(pitchDeg * 10000f);
        yawMdeg   = Mathf.RoundToInt(yawDeg   * 10000f);
    }

    // ── LSRRES processing ────────────────────────────────────

    private void ProcessLsrRes(string line)
    {
        // LSRRES,<frameId>,<count>,<trackId>,<hit>,...
        string[] p = line.Trim().Split(',');
        if (p.Length < 3) return;

        if (!int.TryParse(p[1], NumberStyles.Integer, CultureInfo.InvariantCulture, out int frameId)) return;
        if (!int.TryParse(p[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int count))   return;

        int idx = 3;
        for (int i = 0; i < count; i++)
        {
            if (idx + 1 >= p.Length) break;

            if (!int.TryParse(p[idx], NumberStyles.Integer, CultureInfo.InvariantCulture, out int trackId))
            { idx += 2; continue; }

            string hitStr = p[idx + 1].Trim();
            idx += 2;

            MockTrack t = tracks.Find(x => x.trackId == trackId);
            if (t == null) continue;

            t.lsrPending = false;
            if (hitStr == "1") t.discovered = true;
        }

        MaybeSendFire(frameId);
    }

    // ── Fire decision (mirrors board) ────────────────────────

    private MockTrack ChooseBestTarget()
    {
        MockTrack best     = null;
        int       bestPrio = -1;

        foreach (MockTrack t in tracks)
        {
            if (!t.discovered)        continue;
            if (t.engaged)            continue;
            if (!IsHostile(t.classId)) continue;
            if (t.missedFrames > 2)   continue;

            int prio = ClassPriority(t.classId);
            if (prio > bestPrio) { bestPrio = prio; best = t; }
        }
        return best;
    }

    private static int ClassPriority(int classId)
    {
        switch (classId)
        {
            case 0: return 3; // Tank
            case 1: return 2; // ZPT
            case 2: return 1; // Truck
            default: return 0;
        }
    }

    // Mirrors preferred_missile_for_class()
    private static int PreferredMissileType(int classId, int priority)
    {
        switch (classId)
        {
            case 0: return priority == 0 ? 1 : priority == 1 ? 2 : 0; // Tank:  MAML>SOM>Cirit
            case 1: return priority == 0 ? 1 : priority == 1 ? 0 : 2; // ZPT:   MAML>Cirit>SOM
            case 2: return priority == 0 ? 0 : priority == 1 ? 1 : 2; // Truck: Cirit>MAML>SOM
            default: return -1;
        }
    }

    private int AllocateMissile(int classId)
    {
        for (int pref = 0; pref < 3; pref++)
        {
            int wantedType = PreferredMissileType(classId, pref);
            foreach (MockMissile m in missiles)
            {
                if (m.available && m.type == wantedType)
                {
                    m.available = false;
                    return m.missileId;
                }
            }
        }
        return -1;
    }

    private void MaybeSendFire(int frameId)
    {
        if (Time.time < nextFireAllowedTime) return;

        MockTrack target = ChooseBestTarget();
        if (target == null) return;

        int missileId = AllocateMissile(target.classId);
        if (missileId < 0) return;

        target.engaged = true;
        nextFireAllowedTime = Time.time + fireCooldown;

        // FIRE,<frameId>,<trackId>,<classId>,<missileId>
        string fireMsg = $"FIRE,{frameId},{target.trackId:D3},{target.classId},{missileId:D2}";
        Debug.Log($"[MOCK → FIRE] {fireMsg}");
        DroneSerialManager.Instance.InjectIncomingLine(fireMsg);
    }
}
