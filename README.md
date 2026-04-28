# zynq-vision-fire-control

A vision-guided onboard mission computer for an unmanned combat aerial
vehicle, running on a Xilinx Zynq-7000 (Zybo Z7-20) and talking over
100 Mbps Ethernet to a Unity-based simulator that hosts a custom-trained
YOLO v3-tiny detector.

The board carries the full real-time stack of a defense-grade embedded
system. Detections from the simulator's onboard camera are turned into
tracks, verified with a laser-range finder, and engaged with one of the
mounted munitions, all without leaving the priority-inheritance / no-heap
/ interrupt-driven envelope expected on a flight-grade processor.

---

## What it does, end to end

A simulated UCAV (Akinci-class) flies a corridor over varied terrain,
its gimballed camera looking 60 degrees down. A frame from that camera
is fed into YOLO v3-tiny (custom-trained on roughly 10 000 mixed
synthetic and real images for four classes: Tank, ZPT, MilitaryTruck,
Civilian). Each detection batch is shipped to the board over UDP as a
single CSV line.

On the board:

1. **Track management.** A mutex-protected MOT-style table associates
   each detection with an existing track when class and image-space
   distance match; otherwise a fresh track and a new id are issued.
   Tracks decay out automatically when they go missing for too many
   frames.
2. **Laser verification.** Hostile, undiscovered tracks are turned into
   a single LSRCMD line carrying pitch and yaw in milli-degrees. The
   simulator runs a raycast and replies with LSRRES; the board flips
   the track to "discovered" on a hit.
3. **Engagement.** A discovered hostile is handed to the engagement AO,
   which picks a missile from inventory using a class-aware preference
   table (Tank prefers MAM-L, Truck prefers Cirit, etc.), marks the
   track engaged, and emits one FIRE line. The simulator spawns the
   actual missile model.
4. **Cluster mode.** Switching to MODE_CLUSTER replaces the per-track
   pipeline with a DBSCAN-style connected-neighborhood grouping and a
   score-aware cluster preference table that decides one CFIRE per
   verified cluster.

Civilians are tracked but never engaged. The engagement is gated by an
ARM/DISARM HSM that broadcasts ENABLE/DISABLE to every worker AO. A
1 Hz STATUS line publishes the live mission state for telemetry.

---

## Tech stack

| Layer | Technology |
| --- | --- |
| Hardware | Xilinx Zynq-7000 (Zybo Z7-20), dual ARM Cortex-A9 |
| Toolchain | Vitis 2020.2, arm-none-eabi-gcc / g++ |
| RTOS | Eclipse ThreadX (TX_MUTEX with priority inheritance, no heap, fully interrupt-driven) |
| TCP/IP stack | NetX Duo over 100 Mbps Ethernet (UDP rx/tx, UDP echo, TCP echo, ICMP) |
| Active Object framework | QP/C 8.1.4, hierarchical state machines on top of the QP/ThreadX port |
| Computer vision | YOLO v3-tiny, custom-trained on ~10 000 mixed synthetic/real images, four classes |
| Simulator | Unity 6000.x, Universal Render Pipeline |

The three real-time runtimes (ThreadX, NetX Duo, QP/C) are integrated
from scratch on Cortex-A9 with attention to the things that bite in
practice: pool init order on QP, priority-inheritance mutex usage on
shared services, fail-soft graceful degradation when the QF event pool
runs dry, and a libm-free build path (squared-distance comparisons for
MOT and cluster-link thresholds, a Taylor expansion for the only atan
the LSR pipeline needs).

---

## Architecture

The board firmware is organised around six Active Objects, each running
on its own ThreadX thread with its own QP event queue and HSM. Shared
state lives in mutex-protected tables; AOs never touch each other's
private memory directly, only via posted events.

```
                        +-----------------------+
   ARM/DISARM/MODE_*    |  AO_MissionController |  prio 7
   ------------------>  |   Idle / Armed.Normal |
                        |        / Cluster      |
                        +-----------------------+
                            |  ENABLE / DISABLE
                            v
   +-------------+    +------------------+    +----------------+    +---------------+
   |  AO_Frame   |    |   AO_LsrManager  |    | AO_Engagement  |    | AO_Telemetry  |
   |  Processor  |    |  LSR / CLSR      |    |  FIRE / CFIRE  |    |  1 Hz STATUS  |
   |   prio 4    |    |   prio 5         |    |   prio 6       |    |   prio 2      |
   +-------------+    +------------------+    +----------------+    +---------------+
        |                  |   ^                    ^                     ^
        |  LsrRequest      |   |  TargetVerified    |                     |
        +-----------------> +  +-+-------------+----+                     |
                            |    |                                        |
                            v    v                                        |
                     +-------------------+    +---------------------+     |
                     |   track_table     |    | missile_inventory   | <---+
                     |  TX_MUTEX         |    |  TX_MUTEX           |
                     |  64 tracks        |    |  4 Cirit + 4 MAM-L  |
                     +-------------------+    |   + 1 SOM           |
                            ^                 +---------------------+
                            |
                     +-------------------+
                     |   cluster_table   |  (cluster mode only)
                     |  TX_MUTEX         |
                     |  16 pending +     |
                     |  16 verified      |
                     +-------------------+
```

A separate `udp_router` ThreadX thread (not an AO, just a dispatcher)
parses incoming UDP datagrams and posts the appropriate event to the
right AO. Outbound traffic goes through `udp_uplink`, which learns the
peer's address from the first inbound packet and re-uses it.

---

## Repository layout

```
zynq-vision-fire-control/
|
+-- firmware/
|   `-- workspace_clean/                Vitis 2020.2 workspace
|       |-- UCAV_threadx_netx/          application project (src/, .prj, .cproject)
|       |-- UCAV_threadx_netx_system/   Vitis system project (links app + platform)
|       `-- zybo_platform/              hardware platform (.tcl, .xsa, .bit, FSBL sources)
|
+-- object-detection-and-simulation/
|   `-- LargeTerrain/                   Unity project
|       |-- Assets/                     scripts, scenes, settings, prefabs, etc.
|       |-- Packages/                   Unity package manifest
|       `-- ProjectSettings/            Unity project configuration
|
+-- README.md
+-- .gitignore
`-- LICENSE
```

---

## Getting started

### Prerequisites

- Xilinx Vitis 2020.2 (Windows or Linux) for the firmware side.
- Unity 6000.x for the simulator (any recent 6000.x release should work;
  the project was developed on 6000.0.62f1 and 6000.3.3f1).
- A Zybo Z7-20 board, JTAG cable, and a host PC on the same Ethernet
  segment as the board (default board IP `192.168.1.10`, default host
  IP `192.168.1.20`).

### Building the firmware

1. Clone this repository.
2. Open Vitis 2020.2: `File -> Open Workspace...` and select
   `firmware/workspace_clean/`.
3. Right-click `zybo_platform` -> **Build Project**. This regenerates
   `ps7_cortexa9_0/`, `export/`, `zynq_fsbl_bsp/`, the FSBL `.elf`, and
   the BSP libraries that the `.gitignore` deliberately keeps out of
   the repository.
4. Right-click `UCAV_threadx_netx` -> **Build Project**. This produces
   `Debug/UCAV_threadx_netx.elf`.
5. Flash via JTAG (`Run As -> Launch on Hardware`) or write the boot
   image to an SD card.

### Running the simulator

The Unity project ships **without** the large 3D content (terrain,
vehicle models, FX) -- those would push the repository past 5 GB.

1. Download the asset pack from the link in
   [`object-detection-and-simulation/ASSETS.md`](object-detection-and-simulation/ASSETS.md)
   (Google Drive).
2. Extract it into
   `object-detection-and-simulation/LargeTerrain/Assets/` so that the
   following folders appear:
   - `TerrainDemoScene_URP/`
   - `T90/`
   - `Models/`
   - `Vefects/`
3. Open `object-detection-and-simulation/LargeTerrain/` in Unity.
   Unity will regenerate `Library/`, `Temp/`, `obj/`, and the
   `*.csproj` / `*.sln` files on first import.

### Wiring the link

1. Make sure the board has booted, the UART trace shows
   `[OMC] subsystem ready` and the OMC is `Armed.Normal`.
2. Allow inbound UDP 5006 for the Unity Editor in Windows Firewall
   (or the standalone build's executable).
3. Press Play in Unity. The first heartbeat from `DroneSerialManager`
   teaches the board the simulator's address; from that point onward
   the kill chain runs end to end (FRM -> LSRCMD -> LSRRES -> FIRE).

---

## Wire protocol summary

| Direction | Message | Meaning |
| --- | --- | --- |
| Sim -> board | `FRM,<frame>,<count>,<cls>,<cx>,<cy>,<w>,<h>,...` | YOLO detection batch |
| Sim -> board | `LSRRES,<frame>,<count>,<track>,<hit>,...` | per-track laser-range result |
| Sim -> board | `CLSRRES,<frame>,<cluster_count>,<cluster_id>,<hit>,...` | per-cluster verification result |
| Sim -> board | `ARM` / `DISARM` / `RESET` / `MODE_NORMAL` / `MODE_CLUSTER` | mission commands |
| Board -> sim | `LSRCMD,<frame>,<count>,<track>,<pitch_mdeg>,<yaw_mdeg>,...` | laser request |
| Board -> sim | `CLSRCMD,<frame>,<cluster_count>,<cluster_id>,<member_count>,<track>,<pitch>,<yaw>,...` | cluster laser request |
| Board -> sim | `FIRE,<frame>,<track>,<class>,<missile_id>` | single-target fire decision |
| Board -> sim | `CFIRE,<decision_frame>,<cluster_id>,<cluster_score>,<missile_id>` | cluster fire decision |
| Board -> sim | `STATUS,<armed>,<mode>,<tracks>,<hostile>,<discovered>,<pending>,<cirit>,<maml>,<som>,<fired>,<skipped>` | 1 Hz telemetry |

Track ids encode class via `class_idx * 100 + serial`, so 0..99 are
Tank, 100..199 are ZPT, 200..299 are MilitaryTruck, 300..399 are
Civilian. Missile ids encode the type via the tens digit
(01..04 Cirit, 11..14 MAM-L, 21 SOM).

---

## Notes for reviewers

- All firmware comments and headers are in English; UART traces show the
  full HSM transitions and the kill chain step by step, so the system's
  behaviour can be followed live without instrumentation.
- The build is intentionally libm-free: `sqrt` is replaced by
  squared-distance comparisons, and the only `atan` needed by the LSR
  pipeline is approximated by a five-term Taylor expansion bounded
  under one milli-degree of error.
- Pool exhaustion under burst load is handled with a non-asserting
  margin; the AO drops the event with a UART log line instead of
  crashing the kernel.
- A detailed engineering report covering the architecture, the design
  trade-offs, and the verification matrix is being prepared and will
  be linked here when ready.

---

## License

See `LICENSE`.
