using UnityEngine;
using System.IO;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;

public class DatasetGeneratorStage : MonoBehaviour
{
    public enum Stage
    {
        Empty,
        Single,
        TwoToThree,
        FourToSix,
        SevenToEight
    }

    [Header("Stage Control")]
    public Stage stage = Stage.Single;


    [Tooltip("Her tıklamada kaç kare üretsin? Manuel için 1 önerilir.")]
    public int framesPerRun = 1;

    [Tooltip("Kare üretirken otomatik random konuma ışınlasın mı?")]
    public bool teleportEachFrame = true;

    [Header("References")]
    public CameraCapture416 capture416;                  // senin capture scriptin
    public CameraGroundClearance clearance;              // targetDistance ayarlamak için
    public Camera captureCamera;                         // bbox/viewport için
    public Light sunLight;                               // optional: ışık randomization

    [Header("Output")]
    public string datasetFolderName = "dataset";
    public bool writeEmptyTxtForEmptyImages = true;

    [Header("Teleport Target (Aircraft Root)")]
    public Transform aircraftRoot;          // akinci (root)
    public float aircraftHeightOffset = 30f;
    public bool randomYaw = true;

    [SerializeField] private int penetrationIters = 6;
    [SerializeField] private float penetrationExtra = 0.02f;
    [Header("Ground Fit")]
    public bool alignToGroundNormal = true;
    [Range(0f, 60f)] public float maxSlopeDeg = 35f; // çok dikse retry
    public float liftBeforePush = 0.5f;             // önce biraz yukarı al


    [Header("Teleport Safety")]
    public float worldEdgeMargin = 800f;   // tüm haritanın dış kenarından ne kadar içeri
    public float tileEdgeMargin = 200f;    // seçilen tile'ın kendi kenarlarından ne kadar içeri

    [Header("Camera BackOff (after teleport)")]
    public bool cameraBackoffEnabled = true;
    public float backoffStep = 2.0f;     // her adımda kaç metre geri
    public int backoffMaxSteps = 25;     // en fazla kaç adım denesin
    public float backoffProbeRadius = 0.8f; // kamerayı sphere gibi düşün
    public float backoffForwardProbeDist = 200f; // forward'a bakıp ground var mı kontrol

    [Header("Timing")]
    public float delayAfterTeleportSec = 1.0f;   // teleport/backoff sonrası bekleme
    public float delayAfterSpawnSec = 0.2f;      // spawn sonrası (bounds/clearance) bekleme
    public float delayBetweenFramesSec = 0.0f;   // kareler arasında ekstra bekleme

    [Header("Separation (Vehicle vs Vehicle)")]
    [Tooltip("Spawnlanan araçlara atanacak layer. (Project Settings > Tags and Layers)")]
    public int vehicleLayer = 8; // örn: Vehicle layer index

    [Tooltip("Sadece Vehicle layer'ını seç (LayerMask).")]
    public LayerMask vehicleMask;

    [Tooltip("Araçlar arasında ekstra boşluk (metre). 0 = temas edebilir, 0.2 = aralık bırakır.")]
    public float vehicleGap = 0.2f;

    [Header("Spawn")]
    public LayerMask groundMask;                         // terrain layer
    public float spawnRayHeight = 200f;
    public Vector2 safeViewportX = new Vector2(0.15f, 0.85f);
    public Vector2 safeViewportY = new Vector2(0.20f, 0.80f);
    public float minSeparation = 6f;                     // araçlar arası min mesafe (world)
    public int spawnTriesPerVehicle = 20;

    [Header("Distances")]
    public float multiTargetDistance = 80f;

    [System.Serializable]
    public class PrefabSingleDistance
    {
        public GameObject prefab;
        public float singleTargetDistance = 40f;
        [Tooltip("0 tank, 1 zpt, 2 truck, 3 civil")]
        public int classId = 0;
    }

    [Header("Single Distance Map (prefab -> distance + classId)")]
    public List<PrefabSingleDistance> singleMap = new List<PrefabSingleDistance>();

    [Header("Prefab Pools")]
    public GameObject[] civilPrefabs;
    public GameObject[] tankPrefabs;
    public GameObject[] zptPrefabs;
    public GameObject[] truckPrefabs;



    [Header("Material Color Randomization (by material reference)")]
    [Tooltip("Rengi tamamen random RGB olacak materyaller (sadece bu listedekiler etkilenir).")]
    public Material[] fullRandomRgbMaterials;

    [Tooltip("R=G=B olacak şekilde 150-255 arası değişecek materyaller (sadece bu listedekiler etkilenir).")]
    public Material[] grayRangeMaterials;

    [Range(0, 255)] public int grayMin = 150;
    [Range(0, 255)] public int grayMax = 255;

    [Header("Material Pools (optional)")]
    public Material[] civilMaterials;
    public Material[] tankMaterials;
    public Material[] zptMaterials;
    public Material[] truckMaterials;

    [Header("Randomization")]
    public bool randomizeMaterials = true;
    public bool randomizeLight = true;
    [Range(0f, 0.25f)] public float hueJitter = 0.06f;
    [Range(0f, 0.5f)] public float satJitter = 0.12f;
    [Range(0f, 0.5f)] public float valJitter = 0.10f;

    [Header("Label Filters (INT8-safe)")]
    public int minBBoxMinSidePx = 18;
    [Range(0f, 1f)] public float minVisibleRatio = 0.6f;

    // internal
    private readonly CultureInfo inv = CultureInfo.InvariantCulture;

    [ContextMenu("Capture One (Stage)")]
    public void CaptureOneStage()
    {
        if (!ValidateRefs()) return;
        StartCoroutine(CaptureRoutine(framesPerRun));
    }
    private void SetLayerRecursively(Transform t, int layer)
    {
        t.gameObject.layer = layer;
        for (int i = 0; i < t.childCount; i++)
            SetLayerRecursively(t.GetChild(i), layer);
    }
    private bool ValidateRefs()
    {
        if (!captureCamera) captureCamera = Camera.main;
        if (!capture416) { Debug.LogError("capture416 missing"); return false; }
        if (!clearance) { Debug.LogError("clearance missing"); return false; }
        if (!captureCamera) { Debug.LogError("captureCamera missing"); return false; }
        return true;
    }

    private IEnumerator CaptureRoutine(int count)
    {
        for (int i = 0; i < count; i++)
        {
            // 0) cleanup old vehicles
            DestroyAllVehicles();

            // 1) teleport
            if (teleportEachFrame)
                TeleportRigRandom();

            if (cameraBackoffEnabled)
            {
                BackOffCameraIfInside();
                yield return null; // en az 1 frame
            }

            // Teleport/backoff sonrası: LateUpdate kesin çalışsın + istenen real-time bekleme
            yield return PostTeleportAndTargetWait();

            // 2) decide how many vehicles for this stage
            int n = StageToCount(stage);

            // 3) set target distance (multi stage)
            if (stage != Stage.Single)
            {
                SetClearanceTarget(multiTargetDistance);

                // targetDistance değişti -> clearance LateUpdate ile uygulasın
                yield return null;
                yield return new WaitForEndOfFrame();
            }

            // 4) spawn vehicles
            var spawned = new List<SpawnedVehicle>();

            if (stage == Stage.Empty)
            {
                // nothing
            }
            else if (stage == Stage.Single)
            {
                var pick = PickRandomFromAllPools();
                if (pick.prefab == null)
                {
                    Debug.LogError("No prefab to spawn");
                    yield break;
                }

                float singleDist = GetSingleDistanceOrFallback(pick.prefab, multiTargetDistance);
                SetClearanceTarget(singleDist);

                // targetDistance değişti -> clearance LateUpdate ile uygulasın
                yield return null;
                yield return new WaitForEndOfFrame();

                var sv = SpawnOneVehicle(pick.prefab);
                if (sv != null) spawned.Add(sv);
            }
            else
            {
                for (int k = 0; k < n; k++)
                {
                    var pick = PickRandomFromAllPools();
                    if (pick.prefab == null) break;

                    var sv = SpawnOneVehicle(pick.prefab);
                    if (sv != null) spawned.Add(sv);
                }
            }

            // 5) randomize environment
            if (randomizeLight && sunLight) RandomizeSunLight(sunLight);

            // 6) randomize materials
            if (randomizeMaterials)
            {
                foreach (var sv in spawned)
                    ApplyMaterialColorRandomization(sv.root);
            }

            // 7) Spawn sonrası: transformlar otursun + LateUpdate çalışsın
            yield return null;
            yield return new WaitForEndOfFrame();

            // İstersen ekstra real-time bekleme (spawn sonrası)
            if (delayAfterSpawnSec > 0f)
                yield return new WaitForSecondsRealtime(delayAfterSpawnSec);

            // Kamera gerçekten durulana kadar bekle (capture’dan önce en kritik adım)
            if (captureCamera)
                yield return WaitForCameraToSettle(captureCamera.transform, stableFrames: 3, posEps: 0.01f, maxFrames: 120);

            // 8) output paths
            string stageFolder = StageFolder(stage);
            string basePath = Path.Combine(Application.persistentDataPath, datasetFolderName);
            string imgDir = Path.Combine(basePath, "images", stageFolder);
            string labDir = Path.Combine(basePath, "labels", stageFolder);
            Directory.CreateDirectory(imgDir);
            Directory.CreateDirectory(labDir);

            int id = FindNextAvailableIndex(basePath);
            string fileId = id.ToString("D6");

            string imgPath = Path.Combine(imgDir, fileId + ".png");
            string labPath = Path.Combine(labDir, fileId + ".txt");

            // 9) CAPTURE CLEAN (YOLO image)  -> capture ile label arasında ASLA yield yok
            var texClean = capture416.Capture416(false);

            // 10) compute labels IMMEDIATELY (NO yield between capture and label!)
            var labels = new List<YoloLabelLine>();

            foreach (var sv in spawned)
            {
                if (!TryComputeYoloLabel(sv.root, sv.classId, out YoloLabelLine line, out float visibleRatio, out Rect clampedPx))
                    continue;

                if (visibleRatio < minVisibleRatio) continue;
                if (Mathf.Min(clampedPx.width, clampedPx.height) < minBBoxMinSidePx) continue;

                labels.Add(line);
            }
            // --- STRICT SAVE POLICY ---
            // Stage'e göre bu frame'in "geçerli" sayılması için kaç label lazım?
            int expectedLabels = 0;

            if (stage == Stage.Empty)
            {
                expectedLabels = 0;
            }
            else if (stage == Stage.Single)
            {
                expectedLabels = 1; // Single'da kesin 1 tane görmek istiyoruz
            }
            else
            {
                // Multi stage: spawnladığın her araç için bbox istiyorsun
                expectedLabels = spawned.Count;
            }

            // Eğer beklenen sayıda label yoksa bu frame'i ÇÖPE AT (ne png ne txt)
            bool accept =
                (stage == Stage.Empty) ? true : (labels.Count == expectedLabels);

            if (!accept)
            {
                Debug.LogWarning($"[Dataset][SKIP] Stage={stage} spawned={spawned.Count} labels={labels.Count} expected={expectedLabels} -> not saving");

                // Texture temizle, dosya yazma
                Destroy(texClean);

                // debug çizimleri de üretme (çünkü kabul edilmedi)
                // Bir sonraki frame'e geç
                continue;
            }

            // 11) save CLEAN image
            File.WriteAllBytes(imgPath, texClean.EncodeToPNG());

            // 12) save DEBUG image (separate)
            if (capture416.debugDrawBBoxes)
            {
                var texDbg = capture416.CloneTexture(texClean);

                var rects = new List<Rect>();
                foreach (var sv in spawned)
                {
                    if (YoloBBoxUtil.TryGetBBoxFromVertices(
                            sv.root, captureCamera, 416, 416,
                            out Rect unclamped, out Rect clamped,
                            out float vr, out int pts,
                            vertexStride: 2,
                            requireAnyPointInsideViewport: true))
                    {
                        if (vr < minVisibleRatio) continue;
                        if (Mathf.Min(clamped.width, clamped.height) < minBBoxMinSidePx) continue;
                        rects.Add(clamped);
                    }
                }

                capture416.DrawRectsDebug(texDbg, rects);

                string dbgPath = Path.Combine(imgDir, fileId + "_dbg.png");
                File.WriteAllBytes(dbgPath, texDbg.EncodeToPNG());

                Destroy(texDbg);
            }

            // 13) save labels
            if (labels.Count == 0)
            {
                if (stage == Stage.Empty && writeEmptyTxtForEmptyImages)
                    File.WriteAllText(labPath, "");
                else
                    File.WriteAllText(labPath, "");
            }
            else
            {
                using var sw = new StreamWriter(labPath, false);
                foreach (var l in labels)
                {
                    sw.WriteLine($"{l.classId} {l.xc.ToString("F6", inv)} {l.yc.ToString("F6", inv)} {l.w.ToString("F6", inv)} {l.h.ToString("F6", inv)}");
                }
            }

            Debug.Log($"[Dataset] Stage={stage} saved {fileId} | vehiclesSpawned={spawned.Count} | labels={labels.Count}\n{imgPath}");

            // cleanup clean tex
            Destroy(texClean);

            // 14) optional delay between frames
            if (delayBetweenFramesSec > 0f)
                yield return new WaitForSecondsRealtime(delayBetweenFramesSec);
        }
    }



    // ---------- core helpers ----------

    private void SetClearanceTarget(float d)
    {
        // clearance.targetDistance private; en temiz yol: CameraGroundClearance içine public setter eklemek.
        // Şimdilik reflection yok. Aşağıdaki gibi bir public method eklemeni öneririm:
        // public void SetTargetDistance(float d) => targetDistance = d;
        clearance.SendMessage("SetTargetDistance", d, SendMessageOptions.DontRequireReceiver);
    }

    private int StageToCount(Stage s)
    {
        return s switch
        {
            Stage.Empty => 0,
            Stage.Single => 1,
            Stage.TwoToThree => Random.Range(2, 4),   // 2-3
            Stage.FourToSix => Random.Range(4, 7),    // 4-6
            Stage.SevenToEight => Random.Range(7, 9), // 7-8
            _ => 1
        };
    }

    private string StageFolder(Stage s)
    {
        return s switch
        {
            Stage.Empty => "0_empty",
            Stage.Single => "1_single",
            Stage.TwoToThree => "2_3",
            Stage.FourToSix => "4_6",
            Stage.SevenToEight => "7_8",
            _ => "misc"
        };
    }

    private void TeleportRigRandom()
    {
        if (!aircraftRoot)
        {
            Debug.LogError("[Dataset] aircraftRoot is not assigned. Drag 'akinci' here.");
            return;
        }

        if (!TryPickRandomGroundPoint(out Vector3 groundPos))
        {
            Debug.LogWarning("[Dataset] Teleport failed (no ground hit). Check groundMask / terrain layers.");
            return;
        }

        // Y sabit kalsın
        float fixedY = aircraftRoot.position.y;
        aircraftRoot.position = new Vector3(groundPos.x, fixedY, groundPos.z);

        if (randomYaw)
        {
            var e = aircraftRoot.rotation.eulerAngles;
            e.y = Random.Range(0f, 360f);
            aircraftRoot.rotation = Quaternion.Euler(e);
        }
    }


    private bool TryPickRandomGroundPoint(out Vector3 groundPoint)
    {
        // 1) Sadece iç tile'ları al
        Terrain[] terrains = GetInnerTerrains(worldEdgeMargin);

        // terrain yoksa fallback
        if (terrains == null || terrains.Length == 0)
        {
            Vector3 center = aircraftRoot ? aircraftRoot.position : transform.position;
            float range = 10000f;

            for (int i = 0; i < 60; i++)
            {
                float x = center.x + Random.Range(-range, range);
                float z = center.z + Random.Range(-range, range);
                Vector3 rayOrigin = new Vector3(x, center.y + spawnRayHeight, z);

                if (Physics.Raycast(rayOrigin, Vector3.down, out RaycastHit hit,
                                    spawnRayHeight * 4f, groundMask, QueryTriggerInteraction.Ignore))
                {
                    groundPoint = hit.point;
                    return true;
                }
            }

            groundPoint = center;
            return false;
        }

        // 2) Random tile seç
        Terrain t = terrains[Random.Range(0, terrains.Length)];
        if (t == null || t.terrainData == null)
        {
            groundPoint = aircraftRoot ? aircraftRoot.position : transform.position;
            return false;
        }

        Vector3 tp = t.transform.position;
        Vector3 size = t.terrainData.size;

        // 3) Tile içinden, kenarlardan margin bırakarak random x/z seç
        float mx = Mathf.Clamp(tileEdgeMargin, 0f, size.x * 0.45f);
        float mz = Mathf.Clamp(tileEdgeMargin, 0f, size.z * 0.45f);

        for (int i = 0; i < 80; i++)
        {
            float x = tp.x + Random.Range(mx, size.x - mx);
            float z = tp.z + Random.Range(mz, size.z - mz);

            float y = tp.y + size.y + spawnRayHeight;
            Vector3 rayOrigin = new Vector3(x, y, z);

            float maxDist = size.y + spawnRayHeight * 3f;

            if (Physics.Raycast(rayOrigin, Vector3.down, out RaycastHit hit,
                                maxDist, groundMask, QueryTriggerInteraction.Ignore))
            {
                groundPoint = hit.point;
                return true;
            }
        }

        groundPoint = aircraftRoot ? aircraftRoot.position : transform.position;
        return false;
    }


    private Terrain[] GetInnerTerrains(float edgeMarginWorld)
    {
        Terrain[] all = Terrain.activeTerrains;
        if (all == null || all.Length == 0) return all;

        // world bounds of all terrains
        float minX = float.PositiveInfinity, minZ = float.PositiveInfinity;
        float maxX = float.NegativeInfinity, maxZ = float.NegativeInfinity;

        foreach (var t in all)
        {
            if (!t || !t.terrainData) continue;
            Vector3 p = t.transform.position;
            Vector3 s = t.terrainData.size;
            minX = Mathf.Min(minX, p.x);
            minZ = Mathf.Min(minZ, p.z);
            maxX = Mathf.Max(maxX, p.x + s.x);
            maxZ = Mathf.Max(maxZ, p.z + s.z);
        }

        // inner world bounds (shrink by edgeMarginWorld)
        minX += edgeMarginWorld;
        minZ += edgeMarginWorld;
        maxX -= edgeMarginWorld;
        maxZ -= edgeMarginWorld;

        var inner = new List<Terrain>();
        foreach (var t in all)
        {
            if (!t || !t.terrainData) continue;
            Vector3 p = t.transform.position;
            Vector3 s = t.terrainData.size;

            // tile tamamen "inner" region içinde kalsın
            bool inside =
                p.x >= minX && p.z >= minZ &&
                (p.x + s.x) <= maxX && (p.z + s.z) <= maxZ;

            if (inside) inner.Add(t);
        }

        // Eğer iç tile çıkmazsa (küçük grid): en azından tümünü döndür
        return inner.Count > 0 ? inner.ToArray() : all;
    }



    private class PickResult { public GameObject prefab; public int classId; }

    private PickResult PickRandomFromAllPools()
    {
        // Basit uniform seçim; istersen class quota ekleriz.
        int pick = Random.Range(0, 4);
        // tank=0, zpt=1, truck=2, civil=3
        if (pick == 0 && tankPrefabs != null && tankPrefabs.Length > 0)
            return new PickResult { prefab = tankPrefabs[Random.Range(0, tankPrefabs.Length)], classId = 0 };
        if (pick == 1 && zptPrefabs != null && zptPrefabs.Length > 0)
            return new PickResult { prefab = zptPrefabs[Random.Range(0, zptPrefabs.Length)], classId = 1 };
        if (pick == 2 && truckPrefabs != null && truckPrefabs.Length > 0)
            return new PickResult { prefab = truckPrefabs[Random.Range(0, truckPrefabs.Length)], classId = 2 };
        if (pick == 3 && civilPrefabs != null && civilPrefabs.Length > 0)
            return new PickResult { prefab = civilPrefabs[Random.Range(0, civilPrefabs.Length)], classId = 3 };

        // fallback: first available
        if (tankPrefabs != null && tankPrefabs.Length > 0) return new PickResult { prefab = tankPrefabs[0], classId = 1 };
        if (zptPrefabs != null && zptPrefabs.Length > 0) return new PickResult { prefab = zptPrefabs[0], classId = 2 };
        if (truckPrefabs != null && truckPrefabs.Length > 0) return new PickResult { prefab = truckPrefabs[0], classId = 3 };
        if (civilPrefabs != null && civilPrefabs.Length > 0) return new PickResult { prefab = civilPrefabs[0], classId = 0 };

        return new PickResult { prefab = null, classId = 0 };
    }

    private float GetSingleDistanceOrFallback(GameObject prefab, float fallback)
    {
        foreach (var s in singleMap)
            if (s != null && s.prefab == prefab)
                return s.singleTargetDistance;
        return fallback;
    }

    private class SpawnedVehicle
    {
        public GameObject root;
        public int classId;
        public GameObject prefab;
    }

    private SpawnedVehicle SpawnOneVehicle(GameObject prefab)
    {
        if (!prefab || !captureCamera)
            return null;

        for (int t = 0; t < spawnTriesPerVehicle; t++)
        {
            // 1) viewport içinde random nokta seç
            float vx = Random.Range(safeViewportX.x, safeViewportX.y);
            float vy = Random.Range(safeViewportY.x, safeViewportY.y);

            Ray r = captureCamera.ViewportPointToRay(new Vector3(vx, vy, 0f));

            // 2) ground'a vur
            if (!Physics.Raycast(r, out RaycastHit hit, 3000f, groundMask, QueryTriggerInteraction.Ignore))
                continue;

            Vector3 p = hit.point;

            // 3) (opsiyonel) separation kontrolü - sadece araçlar için daha temiz olur
            // Şimdilik basit: bu noktada başka bir collider var mı?
            // Çok false verirse layer bazlı filtreleyebiliriz.
            if (minSeparation > 0f)
            {
                // küçük bir "overlap" ile çok yakın spawn'ları azalt
                if (Physics.CheckSphere(p, minSeparation, ~0, QueryTriggerInteraction.Ignore))
                {
                    // Çok agresif gelirse bunu kapatabiliriz veya sadece vehicle layer'a indirgeriz.
                    // continue;
                }
            }

            // 4) instantiate
            var go = Instantiate(prefab, p, Quaternion.identity);
            go.tag = "Vehicle";

            // ✅ Vehicle layer’ına ata (vehicleMask bununla çalışıyor)
            SetLayerRecursively(go.transform, vehicleLayer);

            // 5) random yaw (sadece Y)
            float yaw = Random.Range(0f, 360f);
            go.transform.rotation = Quaternion.Euler(0f, yaw, 0f);

            // 6) zemine hizala + lift
            if (alignToGroundNormal)
            {
                if (!TryAlignAndPlaceOnGround(go.transform, hit, yaw))
                {
                    Destroy(go);
                    continue;
                }
            }
            else
            {
                go.transform.position = hit.point + Vector3.up * liftBeforePush;
            }

            // 7) zemine gömülmeyi düzelt (opsiyonel ama genelde iyi)
            PushOutIfOverlapping(go);

            // ✅ 7.5) araç-araç kesin ayrıklık kontrolü
            // overlap varsa: bu deneme çöpe, yeni nokta dene
            if (IsOverlappingOtherVehicles(go))
            {
                Destroy(go);
                continue;
            }

            // 8) class id çöz
            int cid = ResolveClassId(go, prefab);

            return new SpawnedVehicle { root = go, classId = cid, prefab = prefab };
        }

        return null;
    }


    private int ResolveClassId(GameObject spawned, GameObject prefab)
    {
        // Önce singleMap'ten bul (en sağlam)
        foreach (var s in singleMap)
            if (s != null && s.prefab == prefab)
                return s.classId;

        // değilse prefab pool seçimine göre zaten belirlenmişti ama burada fallback yok:
        // name heuristics istemiyorsan 0 döndür.
        return 0;
    }

    private void DestroyAllVehicles()
    {
        var existing = GameObject.FindGameObjectsWithTag("Vehicle");
        foreach (var v in existing)
        {
            // sahnede “kalıcı” araçların varsa yanlışlıkla siler.
            // dataset için spawnladıklarını özel bir parent altına alıp sadece onu temizlemek daha doğru.
            Destroy(v);
        }
    }

    // ---------- Randomization ----------

    private void RandomizeSunLight(Light l)
    {
        if (!l) return;
        l.intensity = Random.Range(3f, 8f);
    }

    // Performans için tek bir MPB kullanıyoruz (allocation yok)
    private MaterialPropertyBlock _mpb;
    private static readonly int BaseColorId = Shader.PropertyToID("_BaseColor");
    private static readonly int ColorId = Shader.PropertyToID("_Color");

    // Bu, araç spawnlandıktan sonra çağrılacak: SADECE listedeki materyallerin rengini override eder.
    // class içine ekle (field)
    private Dictionary<Material, Color> _colorCache = new Dictionary<Material, Color>();

    private void ApplyMaterialColorRandomization(GameObject root)
    {
        if (!root) return;

        if (_mpb == null) _mpb = new MaterialPropertyBlock();

        // ⚠️ Her frame başında cache temizle -> yeni resimde yeni renkler
        _colorCache.Clear();

        var fullSet = BuildMatSet(fullRandomRgbMaterials);
        var graySet = BuildMatSet(grayRangeMaterials);

        var renderers = root.GetComponentsInChildren<Renderer>(true);
        foreach (var r in renderers)
        {
            if (!r) continue;

            var shared = r.sharedMaterials;
            if (shared == null || shared.Length == 0) continue;

            for (int i = 0; i < shared.Length; i++)
            {
                var mat = shared[i];
                if (!mat) continue;

                bool isFull = fullSet != null && fullSet.Contains(mat);
                bool isGray = graySet != null && graySet.Contains(mat);
                if (!isFull && !isGray) continue;

                // ✅ Aynı materyal referansı -> aynı renk
                if (!_colorCache.TryGetValue(mat, out Color c))
                {
                    c = isFull ? RandomRgb() : RandomGray();
                    _colorCache[mat] = c;
                }

                r.GetPropertyBlock(_mpb, i);

                bool hasBase = mat.HasProperty(BaseColorId);
                bool hasCol = mat.HasProperty(ColorId);
                if (!hasBase && !hasCol) continue;

                if (hasBase) _mpb.SetColor(BaseColorId, c);
                else _mpb.SetColor(ColorId, c);

                r.SetPropertyBlock(_mpb, i);
            }
        }
    }

    private HashSet<Material> BuildMatSet(Material[] arr)
    {
        if (arr == null || arr.Length == 0) return null;
        var set = new HashSet<Material>();
        foreach (var m in arr)
            if (m) set.Add(m);
        return set;
    }

    private Color RandomRgb()
    {
        return new Color(Random.value, Random.value, Random.value, 1f);
    }

    private Color RandomGray()
    {
        int minV = Mathf.Clamp(grayMin, 0, 255);
        int maxV = Mathf.Clamp(grayMax, 0, 255);
        if (maxV < minV) { int t = minV; minV = maxV; maxV = t; }

        float v = Random.Range(minV, maxV + 1) / 255f;
        return new Color(v, v, v, 1f);
    }

    private bool IsInPrefabList(GameObject prefab, GameObject[] list)
    {
        if (!prefab || list == null) return false;
        for (int i = 0; i < list.Length; i++)
            if (list[i] == prefab) return true;
        return false;
    }

    private void ApplyBaseMapColorRule(Material m, bool fullRandomRgb)
    {
        if (!m) return;

        // URP/HDRP: _BaseColor, Built-in: _Color
        string prop =
            m.HasProperty("_BaseColor") ? "_BaseColor" :
            (m.HasProperty("_Color") ? "_Color" : null);

        if (prop == null) return;

        Color c;

        if (fullRandomRgb)
        {
            // Tam random RGB (istersen alpha 1)
            c = new Color(Random.value, Random.value, Random.value, 1f);
        }
        else
        {
            // Grayscale: R=G=B, 150-255 arası
            int minV = Mathf.Clamp(grayMin, 0, 255);
            int maxV = Mathf.Clamp(grayMax, 0, 255);
            if (maxV < minV) { int tmp = minV; minV = maxV; maxV = tmp; }

            float v = Random.Range(minV, maxV + 1) / 255f;
            c = new Color(v, v, v, 1f);
        }

        m.SetColor(prop, c);
    }

    private void JitterColor(Material m)
    {
        // URP/HDRP'de _BaseColor, built-in'de _Color
        string prop = m.HasProperty("_BaseColor") ? "_BaseColor" :
                      (m.HasProperty("_Color") ? "_Color" : null);
        if (prop == null) return;

        Color baseC = m.GetColor(prop);
        Color.RGBToHSV(baseC, out float h, out float s, out float v);

        h = Mathf.Repeat(h + Random.Range(-hueJitter, hueJitter), 1f);
        s = Mathf.Clamp01(s + Random.Range(-satJitter, satJitter));
        v = Mathf.Clamp01(v + Random.Range(-valJitter, valJitter));

        m.SetColor(prop, Color.HSVToRGB(h, s, v));
    }

    // ---------- Labels ----------

    private struct YoloLabelLine
    {
        public int classId;
        public float xc, yc, w, h; // normalized
    }

    private bool TryComputeYoloLabel(GameObject obj, int classId, out YoloLabelLine line, out float visibleRatio, out Rect clampedPx)
    {
        line = default;
        visibleRatio = 0f;
        clampedPx = default;

        if (!obj || !captureCamera) return false;

        const int W = 416;
        const int H = 416;

        // vertices-based bbox (RT-accurate)
        if (!YoloBBoxUtil.TryGetBBoxFromVertices(
                obj, captureCamera, W, H,
                out Rect unclamped, out Rect clamped,
                out visibleRatio, out int usedPts,
                vertexStride: 2,                    // istersen 1 yap
                requireAnyPointInsideViewport: true // tamamen ekran dışındaysa alma
            ))
            return false;

        clampedPx = clamped;

        float xCenter = (clampedPx.x + clampedPx.width * 0.5f) / W;
        float yCenter = (clampedPx.y + clampedPx.height * 0.5f) / H;
        // TOP-LEFT origin'e çevir
        yCenter = 1f - yCenter;
        float bw = clampedPx.width / W;
        float bh = clampedPx.height / H;

        line = new YoloLabelLine { classId = classId, xc = xCenter, yc = yCenter, w = bw, h = bh };
        return true;
    }

    private void BackOffCameraIfInside()
    {
        if (!captureCamera) return;

        Transform camT = captureCamera.transform;

        // Kamera önünde ground var mı? yoksa edge/boşluğa bakıyor olabilir.
        // Ama burada asıl hedef: kamera bir collider'ın içindeyse onu kurtarmak.
        // GroundMask ile overlap yakalayamazsa bile, "forward spherecast hit" ile kurtaracağız.

        for (int i = 0; i < backoffMaxSteps; i++)
        {
            bool insideGround = Physics.CheckSphere(
                camT.position,
                backoffProbeRadius,
                groundMask,
                QueryTriggerInteraction.Ignore
            );

            // Eğer içeride değilse ve forward'da ground hit varsa, yeterince iyiyiz
            bool forwardHasGround = Physics.SphereCast(
                camT.position,
                backoffProbeRadius,
                camT.forward,
                out _,
                backoffForwardProbeDist,
                groundMask,
                QueryTriggerInteraction.Ignore
            );

            if (!insideGround && forwardHasGround)
                return;

            // aksi halde geri çek
            camT.position += (-camT.forward) * backoffStep;
        }

        // hâlâ düzelmediyse log bas
        Debug.LogWarning("[CameraBackOff] Could not find safe camera position within max steps. Increase step/maxSteps or fix teleport margins.");
    }


    private int FindNextAvailableIndex(string basePath)
    {
        // basePath = .../persistentDataPath/datasetFolderName
        var used = new HashSet<int>();

        // images + labels altındaki tüm stage klasörlerini tarar
        string imagesRoot = Path.Combine(basePath, "images");
        string labelsRoot = Path.Combine(basePath, "labels");

        CollectIndicesFromRoot(imagesRoot, used);
        CollectIndicesFromRoot(labelsRoot, used);

        // En küçük eksik non-negative integer
        int id = 0;
        while (used.Contains(id)) id++;
        return id;
    }

    private void CollectIndicesFromRoot(string rootDir, HashSet<int> used)
    {
        if (!Directory.Exists(rootDir)) return;

        // png/txt her şeyi tara (alt klasörler dahil)
        var files = Directory.GetFiles(rootDir, "*.*", SearchOption.AllDirectories);
        foreach (var f in files)
        {
            string ext = Path.GetExtension(f).ToLowerInvariant();
            if (ext != ".png" && ext != ".txt") continue;

            string name = Path.GetFileNameWithoutExtension(f); // "000123" veya "000123_dbg"
            if (name.EndsWith("_dbg")) name = name.Substring(0, name.Length - 4);

            if (int.TryParse(name, out int id) && id >= 0)
                used.Add(id);
        }
    }
    private void DebugWhyNoBBox(GameObject root)
    {
        if (!root || !captureCamera) { Debug.Log("root/cam null"); return; }

        var mfs = root.GetComponentsInChildren<MeshFilter>(true);
        var sks = root.GetComponentsInChildren<SkinnedMeshRenderer>(true);
        var rends = root.GetComponentsInChildren<Renderer>(true);

        Debug.Log($"[BBoxDebug] {root.name} | MeshFilters={mfs.Length} Skinned={sks.Length} Renderers={rends.Length}");

        int mfWithMesh = 0;
        foreach (var mf in mfs) if (mf && mf.sharedMesh) mfWithMesh++;
        Debug.Log($"[BBoxDebug] MeshFilters with sharedMesh={mfWithMesh}");

        bool ok = YoloBBoxUtil.TryGetBBoxFromVertices(
            root, captureCamera, 416, 416,
            out var u, out var c,
            out var vr, out var pts,
            vertexStride: 2,
            requireAnyPointInsideViewport: true);

        Debug.Log($"[BBoxDebug] TryGetBBoxFromVertices ok={ok} pts={pts} visibleRatio={vr} unclamped={u} clamped={c}");
    }
    // Kamera gerçekten yerine oturdu mu? (LateUpdate sonrası hareket çok küçükse “settled” say)
    private IEnumerator WaitForCameraToSettle(Transform camT, int stableFrames = 3, float posEps = 0.01f, int maxFrames = 90)
    {
        if (!camT) yield break;

        int stable = 0;
        Vector3 prev = camT.position;

        for (int i = 0; i < maxFrames; i++)
        {
            // Bu frame’in LateUpdate’ı bitsin
            yield return new WaitForEndOfFrame();

            float d = Vector3.Distance(prev, camT.position);
            prev = camT.position;

            if (d <= posEps) stable++;
            else stable = 0;

            if (stable >= stableFrames)
                yield break;
        }
    }

    // “1 saniye bekledim ama clearance hala oynuyor” durumunda garantiye almak için:
    private IEnumerator PostTeleportAndTargetWait()
    {
        // 1) en az 1 frame: teleport/backoff sonrası transformlar otursun
        yield return null;
        // 2) LateUpdate kesin çalışsın
        yield return new WaitForEndOfFrame();

        // 3) real-time bekleme istiyorsan
        if (delayAfterTeleportSec > 0f)
            yield return new WaitForSecondsRealtime(delayAfterTeleportSec);

        // 4) tekrar LateUpdate
        yield return new WaitForEndOfFrame();
    }

    private void PushOutIfOverlapping(GameObject go)
    {
        var cols = go.GetComponentsInChildren<Collider>(true);
        if (cols == null || cols.Length == 0) return;

        for (int iter = 0; iter < penetrationIters; iter++)
        {
            bool any = false;

            foreach (var c in cols)
            {
                if (!c || c.isTrigger) continue;

                // Bu collider ile çakışanları ara
                var overlaps = Physics.OverlapBox(
                    c.bounds.center,
                    c.bounds.extents,
                    c.transform.rotation,
                    groundMask,
                    QueryTriggerInteraction.Ignore
                );

                foreach (var other in overlaps)
                {
                    if (!other || other == c) continue;

                    if (Physics.ComputePenetration(
                        c, c.transform.position, c.transform.rotation,
                        other, other.transform.position, other.transform.rotation,
                        out Vector3 dir, out float dist))
                    {
                        // Biraz ekstra it
                        go.transform.position += dir * (dist + penetrationExtra);
                        any = true;
                    }
                }
            }

            if (!any) break; // artık penetration yok
        }
    }
    private bool TryAlignAndPlaceOnGround(Transform tr, RaycastHit hit, float yawDeg)
    {
        Vector3 n = hit.normal;

        // slope kontrol
        float slope = Vector3.Angle(n, Vector3.up);
        if (slope > maxSlopeDeg) return false;

        // Yaw korunacak şekilde "up = normal"
        Quaternion yawRot = Quaternion.Euler(0f, yawDeg, 0f);
        Quaternion align = Quaternion.FromToRotation(Vector3.up, n);
        tr.rotation = align * yawRot;

        // önce zemine koy + biraz yukarı lift
        tr.position = hit.point + n * liftBeforePush;

        return true;
    }
    private bool IsOverlappingOtherVehicles(GameObject go)
    {
        // Bu aracın collider'ları
        var cols = go.GetComponentsInChildren<Collider>(true);
        if (cols == null || cols.Length == 0) return false;

        foreach (var c in cols)
        {
            if (!c || c.isTrigger) continue;

            // bounds’u biraz büyüt -> aralık (vehicleGap) bırak
            Vector3 halfExtents = c.bounds.extents + Vector3.one * vehicleGap;

            // sadece Vehicle layer’ı tara
            var hits = Physics.OverlapBox(
                c.bounds.center,
                halfExtents,
                c.transform.rotation,
                vehicleMask,
                QueryTriggerInteraction.Ignore
            );

            foreach (var other in hits)
            {
                if (!other || other == c) continue;
                if (other.transform.root == go.transform.root) continue; // kendi parçaları

                // gerçek penetrasyon var mı?
                if (Physics.ComputePenetration(
                    c, c.transform.position, c.transform.rotation,
                    other, other.transform.position, other.transform.rotation,
                    out _, out float dist))
                {
                    if (dist > 0f) return true;
                }
            }
        }

        return false;
    }


}