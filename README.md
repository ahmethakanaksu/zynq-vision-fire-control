# zynq-vision-fire-control

> A real-time, defense-grade vision-guided **onboard mission computer (OMC)** for an unmanned combat aerial vehicle (Akıncı-class UCAV), built from scratch on a **Xilinx Zynq-7000 (Zybo Z7-20)**. The board carries a custom integration of **Eclipse ThreadX**, **NetX Duo over 100 Mbps Ethernet**, and **QP/C 8.1.4 hierarchical state machines (HSMs)**. It closes the kill chain — detection → MOT tracking → laser verification → class-aware fire decision — for a **custom-trained YOLO v3-tiny** detector running in real time inside a **Unity 6000.x** simulator that physically launches missile prefabs and renders detonation effects.

[![Eclipse ThreadX](https://img.shields.io/badge/RTOS-Eclipse%20ThreadX-005F9E?logo=eclipsefoundation&logoColor=white)](https://github.com/eclipse-threadx/threadx)
[![NetX Duo](https://img.shields.io/badge/Ethernet%20Stack-NetX%20Duo-1E88E5)](https://github.com/eclipse-threadx/netxduo)
[![QP/C](https://img.shields.io/badge/Active%20Object%20%2B%20HSM-QP%2FC%208.1.4-FF6B35)](https://www.state-machine.com/products/qpc)
[![YOLOv3-tiny](https://img.shields.io/badge/Computer%20Vision-YOLOv3--tiny-00BFA5?logo=ultralytics&logoColor=white)](https://github.com/ultralytics/yolov3)
[![Unity](https://img.shields.io/badge/Simulator-Unity%206000.x-222C37?logo=unity&logoColor=white)](https://unity.com/)
[![Zynq-7000](https://img.shields.io/badge/SoC-Xilinx%20Zynq--7000-ED1C24?logo=amd&logoColor=white)](https://www.xilinx.com/products/silicon-devices/soc/zynq-7000.html)
[![Vitis](https://img.shields.io/badge/Toolchain-Vitis%202020.2-76B900)](https://www.xilinx.com/products/design-tools/vitis.html)
[![C / C++](https://img.shields.io/badge/Language-C%20%2F%20C%2B%2B-00599C?logo=cplusplus&logoColor=white)](https://en.wikipedia.org/wiki/C%2B%2B)

**Live demo:** <https://www.youtube.com/watch?v=hgIOaLgsaew>

---

## Highlights

- **Three industrial-grade real-time runtimes integrated from scratch on bare-metal Cortex-A9** — Eclipse ThreadX (RTOS), NetX Duo (TCP/IP over Ethernet), and QP/C 8.1.4 (Active Object + HSM) — coexisting as one coherent stack.
- **6 Active Objects, each with its own HSM**, every shared service guarded by a **`TX_MUTEX` with priority inheritance** (`TX_INHERIT`).
- **100 Mbps Ethernet end-to-end** with sub-millisecond ICMP RTT; five concurrent UDP/TCP services on the same link, plus ICMP.
- **Custom-trained YOLO v3-tiny** on a hybrid (synthetic + open-source) ~10 000-image dataset for four classes — Tank, ZPT, MilitaryTruck, Civilian — reaching **mAP@0.5 = 0.995** and **best F1 ≈ 0.99 @ confidence 0.640**.
- **Defense-grade discipline throughout:** static memory only (no `malloc` / `new`), fully interrupt-driven I/O, fail-soft graceful degradation, libm-free numerics (squared-distance MOT, 5-term Taylor `atan` for LSR pitch / yaw).
- **Unity Akıncı UCAV simulation** with realistic terrain, weather, vehicle variation, mounted Cirit / MAM-L / SOM munitions, real missile prefabs, missile chase camera, and detonation effects.
- **Cluster mode** — DBSCAN-style connected-neighbourhood grouping plus a score-aware munition-preference table — added as a strict superset; the single-target path remains byte-for-byte unchanged.
- **End-to-end verifiable** — deterministic ASCII CSV wire protocol, structured per-AO UART trace, 14 / 14 PASS Python regression matrix, 60+ s of zero-assert continuous operation.

---

## Technology stack

| Layer | Technology | Role |
| --- | --- | --- |
| **RTOS** | **Eclipse ThreadX** (adapted Cortex-A9 port) | Threads, queues, **`TX_MUTEX` with priority inheritance**, `TX_TIMER`, fully interrupt-driven kernel — DO-178C / IEC 61508 lineage |
| **Ethernet stack** | **NetX Duo** | UDP + TCP + ICMP over 100 Mbps Ethernet; native ThreadX integration, zero-copy packet pools |
| **Active-Object framework + HSM** | **QP/C 8.1.4** (Quantum Leaps) | Hierarchical state machines, run-to-completion semantics, statically-backed QF event pools |
| **Computer vision** | **YOLO v3-tiny** (Ultralytics, custom-trained) | 4-class detector, ONNX export, GPU-side InferenceEngine inference inside Unity |
| **Simulator** | **Unity 6000.x** (URP) | Akıncı UCAV, terrain, weather, vehicles, missile physics, chase camera, detonation VFX |
| **Hardware** | **Xilinx Zynq-7000 / Zybo Z7-20** | Dual ARM Cortex-A9 @ 667 MHz, GIC, SCU private timer, GEM Ethernet, USB-UART |
| **Toolchain** | **Xilinx Vitis 2020.2** + arm-none-eabi-gcc/g++ | Cross-compilation, BSP regeneration, JTAG debug |

The three real-time runtimes are stitched together with attention to the things that bite in practice: QF event-pool init order (non-decreasing slot sizes, otherwise `qf_dyn.c:110` asserts), priority-inheritance mutex usage on every shared service, fail-soft `margin = 0U` allocation under burst load, and a libm-free build path so `sqrt` and `atan` are never linked.

---

## End-to-end kill chain

A simulated Akıncı UCAV overflies a corridor of varied terrain with a 60° downward-pointing gimballed camera. A frame is fed into YOLO v3-tiny on the Unity-side GPU; the resulting detection batch is shipped to the board over **UDP/Ethernet** as a single ASCII CSV line.

On the board:

1. **MOT track management** — a 64-slot `track_table`, mutex-guarded, associates each detection with an existing track when class and image-space distance match (squared, libm-free); otherwise a fresh slot and a new track id are issued. Tracks decay out automatically when missed for too many frames.
2. **Laser verification** — eligible hostile, undiscovered tracks become a single `LSRCMD` line carrying pitch / yaw in milli-degrees. The simulator runs a raycast and replies with `LSRRES`; the board flips the track to `discovered` on a hit.
3. **Engagement** — a discovered hostile is handed to `AO_Engagement`, which picks a missile from inventory using a **class-aware preference table** (Tank prefers MAM-L, ZPT prefers MAM-L, Truck prefers Cirit, Civilian is structurally never engaged), marks the track engaged, and emits one `FIRE` line. The simulator spawns the actual missile model, plays detonation effects, and follows it with a chase camera.
4. **Cluster mode** — `MODE_CLUSTER` replaces the per-track LSR pipeline with **DBSCAN-style connected-neighbourhood grouping**; one `CLSRCMD` covers the whole cluster, and a **score-aware** preference table decides one `CFIRE` per verified cluster. The single-target code path remains untouched — cluster mode is a strict superset.

Every transition above is gated by `AO_MissionController`'s `Idle / Armed.{Normal, Cluster}` HSM, which broadcasts ENABLE / DISABLE to every worker AO. A 1 Hz `STATUS` line publishes live mission state for telemetry.

---

## Architecture

The firmware is organised around **6 Active Objects** + **1 plain ThreadX dispatcher thread**, plus mutex-protected shared services. AOs never touch each other's private memory; they only post events.

```
                          +--------------------------+
   ARM/DISARM/MODE_*  ==> |  AO_MissionController    |  prio 7
                          |   Idle / Armed.Normal    |
                          |              .Cluster    |
                          +--------------------------+
                              |  ENABLE / DISABLE
                              v
   +--------------+   +-------------------+   +------------------+   +---------------+
   |  AO_Frame-   |   |   AO_LsrManager   |   |  AO_Engagement   |   | AO_Telemetry  |
   |  Processor   |   |   LSR / CLSR      |   |  FIRE / CFIRE    |   |  1 Hz STATUS  |
   |   prio 4     |   |   prio 5          |   |   prio 6         |   |   prio 2      |
   +--------------+   +-------------------+   +------------------+   +---------------+
        |                  |   ^                    ^                       ^
        | LsrRequest /     |   | TargetVerified /   |                       |
        | ClusterRequest   |   | ClusterFireDecide  |                       |
        +----------------->+   +-+------------------+                       |
                               | |                                          |
                               v v                                          |
                     +-------------------+      +---------------------+     |
                     |   track_table     |      |  missile_inventory  | <---+
                     |   TX_MUTEX        |      |   TX_MUTEX          |
                     |   64 tracks (MOT) |      |   4 Cirit + 4 MAM-L |
                     +-------------------+      |    + 1 SOM          |
                              ^                 +---------------------+
                              |
                     +-------------------+
                     |   cluster_table   |   (cluster mode)
                     |   TX_MUTEX        |
                     |   16 pending +    |
                     |   16 verified     |
                     +-------------------+
```

A separate `udp_router` ThreadX thread parses incoming UDP datagrams off NetX Duo and posts the right event to the right AO. Outbound traffic flows through `udp_uplink`, which **auto-learns** the simulator's address from the first inbound packet and re-uses it.

---

## YOLO v3-tiny — computer vision layer

| Item | Value |
| --- | --- |
| Model | YOLOv3-tiny (Ultralytics `yolov3-tinyu.pt` base) |
| Classes | Tank · ZPT · MilitaryTruck · Civilian |
| Dataset | **~10 000 images, hybrid** (synthetic + open-source aerial vehicle imagery) |
| Synthetic generator | Unity stage-based scenario generator with auto-labelling (pixel-accurate, no manual annotation) |
| Domain randomisation | Weather, light, material colours, vehicle yaw, scene density (5 stages: 0_empty / 1_single / 2-3 / 4-6 / 7-8) |
| Image size | 416 × 416 |
| Training | Ultralytics defaults, 80 epochs, batch 16, GPU on Colab |
| **mAP@0.5** | **0.995** |
| F1 (best) | **≈ 0.99 @ confidence 0.640** |
| Deployment | ONNX (46 MB), Unity InferenceEngine, GPU compute backend, every-N-frames pattern (N=3) |

The trained model runs **inside Unity** on the simulator GPU; per-detection results are NMS-filtered (IoU 0.45, score 0.25) and serialised into a single `FRM` CSV line shipped to the board over UDP.

---

## Wire protocol

All board ↔ simulator traffic is **single-line ASCII CSV** over UDP (port 5005 inbound, source port 5006 outbound). No binary, no endianness, no padding — fully Wireshark-inspectable.

| Direction | Message | Meaning |
| --- | --- | --- |
| Sim → board | `FRM,<frame>,<count>,<cls>,<cx>,<cy>,<w>,<h>,...` | YOLO detection batch |
| Sim → board | `LSRRES,<frame>,<count>,<track>,<hit>,...` | Per-track laser-range result |
| Sim → board | `CLSRRES,<frame>,<cluster_count>,<cluster_id>,<hit>,...` | Per-cluster verification result |
| Sim → board | `ARM` / `DISARM` / `RESET` / `MODE_NORMAL` / `MODE_CLUSTER` | Mission commands |
| Board → sim | `LSRCMD,<frame>,<count>,<track>,<pitch_mdeg>,<yaw_mdeg>,...` | Single-target laser request |
| Board → sim | `CLSRCMD,<frame>,<cluster_count>,<cluster_id>,<member_count>,<track>,<pitch>,<yaw>,...` | Cluster laser request |
| Board → sim | `FIRE,<frame>,<track>,<class>,<missile_id>` | Single-target fire decision |
| Board → sim | `CFIRE,<decision_frame>,<cluster_id>,<cluster_score>,<missile_id>` | Cluster fire decision |
| Board → sim | `STATUS,<armed>,<mode>,<tracks>,<hostile>,<discovered>,<pending>,<cirit>,<maml>,<som>,<fired>,<skipped>` | 1 Hz telemetry |

Track ids encode class via `class_idx * 100 + serial` (so 0..99 are Tank, 100..199 ZPT, 200..299 Truck, 300..399 Civilian); missile ids encode type via the tens digit (01..04 Cirit, 11..14 MAM-L, 21 SOM).

---

## Defense-grade design decisions

| Pattern | Realised in this project |
| --- | --- |
| **Static memory only** | No `malloc` / `new` anywhere; every stack, queue, event pool, and shared table is a static array sized at compile time |
| **Priority inheritance** | `TX_INHERIT` on every `TX_MUTEX` — Mars Pathfinder-class priority inversion is structurally prevented |
| **Run-to-completion** | QP/C Active Object semantics — an entire class of lock-based races is eliminated |
| **Hierarchical state machines** | Formal HSM in every AO; the mission state itself is a parent-child HSM (`Idle / Armed.{Normal, Cluster}`) |
| **Fully interrupt-driven I/O** | UART, Ethernet (GEM), SCU timer — zero polling; idle cores sleep |
| **Fail-soft graceful degradation** | QF pool exhaustion → `margin = 0U` returns NULL, AO logs and continues; munition exhaustion → engagement skipped, counter advances; uplink not yet learned → drop, counter advances |
| **No libm** | Squared-distance MOT thresholds, 5-term Taylor `atan` for LSR (bounded < 1.1 mdeg error) |
| **Deterministic wire protocol** | ASCII CSV, in-place tokenisation, no endianness, no alignment — parseable by Wireshark, netcat, or Python directly off the wire |
| **Live observability** | Per-AO UART prefixes (`[OMC AO_FP]`, `[OMC AO_LSR]`, `[OMC AO_ENG]`, `[OMC AO_MC]`, `[OMC AO_TEL]`), explicit state arrows (`-> Armed.Normal`), full kill-chain trace |

---

## Repository layout

```
zynq-vision-fire-control/
│
├── firmware/
│   └── workspace_clean/                Vitis 2020.2 workspace
│       ├── UCAV_threadx_netx/          application project (src/, .prj, .cproject)
│       ├── UCAV_threadx_netx_system/   Vitis system project (links app + platform)
│       └── zybo_platform/              hardware platform (.tcl, .xsa, .bit, FSBL sources)
│
├── object-detection-and-simulation/
│   ├── ASSETS.md                       Drive link for the large 3D content pack
│   └── LargeTerrain/                   Unity project
│       ├── Assets/                     C# scripts, scenes, settings, prefabs
│       ├── Packages/                   Unity package manifest
│       └── ProjectSettings/            Unity project configuration
│
├── README.md
└── .gitignore
```

---

## Getting started

### Prerequisites

- **Xilinx Vitis 2020.2** (Windows or Linux) for the firmware side.
- **Unity 6000.x** for the simulator (developed on 6000.0.62f1 / 6000.3.3f1).
- A **Zybo Z7-20** board, JTAG cable, and a host PC on the same Ethernet segment as the board (default board IP `192.168.1.10`, default host IP `192.168.1.20`).

### Building the firmware

1. Clone this repository.
2. Open Vitis 2020.2: `File → Open Workspace…` and select `firmware/workspace_clean/`.
3. Right-click **`zybo_platform`** → **Build Project**. This regenerates `ps7_cortexa9_0/`, `export/`, `zynq_fsbl_bsp/`, the FSBL `.elf`, and the BSP libraries that `.gitignore` deliberately keeps out of the repository.
4. Right-click **`UCAV_threadx_netx`** → **Build Project**. This produces `Debug/UCAV_threadx_netx.elf`.
5. Flash via JTAG (`Run As → Launch on Hardware`) or write the boot image to an SD card.

### Running the simulator

The Unity project ships **without** the large 3D content (terrain, vehicle models, FX) — those would push the repository past 5 GB.

1. Download the asset pack via the link in [`object-detection-and-simulation/ASSETS.md`](object-detection-and-simulation/ASSETS.md) (Google Drive).
2. Extract it into `object-detection-and-simulation/LargeTerrain/Assets/` so that the following folders appear:
   - `TerrainDemoScene_URP/`
   - `T90/`
   - `Models/`
   - `Vefects/`
3. Open `object-detection-and-simulation/LargeTerrain/` in Unity. Unity will regenerate `Library/`, `Temp/`, `obj/`, and the `*.csproj` / `*.sln` files on first import.

### Wiring the link

1. Make sure the board has booted; the UART trace must show `[OMC] subsystem ready` and the OMC must reach `Armed.Normal`.
2. Allow inbound UDP 5006 for the Unity Editor in Windows Firewall (or for the standalone build's executable). Note: a `Block` rule takes precedence over an `Allow` rule, so make sure no blanket `Block` covers the Editor's `.exe`.
3. Press Play in Unity. The first heartbeat from `DroneSerialManager` teaches the board the simulator's address; from that point onward the kill chain runs end to end (`FRM` → `LSRCMD` → `LSRRES` → `FIRE`, and the cluster pipeline once `MODE_CLUSTER` is sent).

---

## Verification

- **Python test harness** — nine scripts driving the board over UDP without Unity in the loop, covering MOT acceptance, LSR generation, LSR-result handling, single-target FIRE, full single-target kill chain, MissionController HSM, 1 Hz STATUS, cluster CLSRCMD, and full cluster CFIRE. Final regression run: **14 / 14 PASS**.
- **End-to-end with Unity** over the live 100 Mbps Ethernet link — visible laser beam in the scene, real Cirit / MAM-L missile prefabs spawned, detonation effects fired, full UART trace recorded.
- **Performance** — ICMP ping RTT < 1 ms; FRM-to-LSRCMD latency < 5 ms; 60+ s of continuous operation with **zero `Q_ASSERT`, zero pool exhaustion, zero NetX error code**.
- **Live preemption observable** — `AO_Engagement` (priority 6) is observed to preempt `AO_LsrManager` (priority 5) in the same CLSRRES batch, confirming preemptive priority-based scheduling at runtime.

---

## Engineering report

A full engineering report — covering architecture, design trade-offs, the MOT and clustering math, defense-grade alignment, operational scenarios with real UART traces, and the verification matrix — accompanies this repository (available on request; will be linked here once published).

---

## Demo video

End-to-end live demonstration: UCAV in flight, YOLO detector classifying ground vehicles, civilians tracked but spared, hostile vehicles verified by laser range-finder, the appropriate munition launched, and impact effects rendered.

<https://www.youtube.com/watch?v=hgIOaLgsaew>
