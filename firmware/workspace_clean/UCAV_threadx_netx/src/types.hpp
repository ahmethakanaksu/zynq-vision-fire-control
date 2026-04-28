/*
 * types.hpp -- shared domain types for the OMC subsystem.
 *
 * Header-only. Defines the data model that crosses module boundaries:
 * vehicle classes, missile types, single detections, Track records, and
 * the camera/frame constants that drive pixel-to-angle conversion.
 *
 * Capacity limits are constexpr so the compiler can size static arrays
 * directly: track_table at MAX_TRACKS slots, cluster_table rows at
 * MAX_CLUSTER_MEMBERS, and the QP event pools at MAX_DETECTIONS_PER_FRAME.
 *
 * Track id encoding (used end-to-end on the wire and in the tables):
 *   id = class_idx * 100 + serial
 *   0..99   Tank
 *   100..199 Zpt (armoured personnel carrier)
 *   200..299 MilitaryTruck
 *   300..399 Civilian
 * AO_Engagement reads this encoding directly to compute "has_tank" for
 * cluster fire decisions without taking another lock on track_table.
 *
 * Missile id encoding (decimal, the tens digit is the type):
 *   01..04 = Cirit, 11..14 = MAM-L, 21 = SOM.
 * The wire protocol carries the id alone; the receiver derives the
 * munition type from the digit.
 */

#ifndef OMC_TYPES_HPP_
#define OMC_TYPES_HPP_

#include <cstdint>

namespace omc {

/* --- Frame / camera geometry (matches Unity simulation) --- */
constexpr int   FRAME_W                     = 416;
constexpr int   FRAME_H                     = 416;
constexpr float CAMERA_HFOV_DEG             = 60.0f;
constexpr float CAMERA_VFOV_DEG             = 60.0f;
constexpr float CAMERA_BASE_PITCH_DOWN_DEG  = 50.0f;

/* --- Capacity limits --- */
constexpr int MAX_DETECTIONS_PER_FRAME      = 8;
constexpr int MAX_TRACKS                    = 64;
constexpr int MAX_MISSED_FRAMES             = 10;
constexpr int LSR_RETRY_GAP_FRAMES          = 5;
constexpr int MAX_LSR_ITEMS_PER_MSG         = 16;

/* Vehicle classes from the YOLO v3-tiny detector. The numeric values
 * are the wire-format encoding. */
enum class VehicleClass : int {
    Tank          = 0,
    Zpt           = 1,
    MilitaryTruck = 2,
    Civilian      = 3,
    Unknown       = 255
};

/*
 * Munition categories. IDs are encoded such that the tens digit
 * identifies the missile type:
 *   01..04 = Cirit, 11..14 = MAM-L, 21 = SOM
 */
enum class MissileType : int {
    None  = -1,
    Cirit = 0,
    Maml  = 1,
    Som   = 2
};

/* One detection produced by the vision pipeline for one frame. */
struct Detection {
    VehicleClass cls;
    int cx;
    int cy;
    int w;
    int h;
};

/*
 * A persistent board-side track. Lightweight MOT-style record:
 * detections from consecutive frames are matched to an existing track
 * by class consistency + bounded image-space distance. If a new
 * detection lands close enough to a previous track of the same class,
 * the track is updated in place (same id, no new LSR request);
 * otherwise a new slot and a fresh id are allocated. Tracks not
 * matched for more than MAX_MISSED_FRAMES frames in a row decay out.
 */
struct Track {
    bool active;
    bool discovered;          /* hostile, confirmed by an LSR hit */
    bool lsr_pending;         /* LSR request out, awaiting result */
    bool engaged;             /* FIRE or CFIRE has been issued */
    bool matched_this_frame;

    int track_id;
    VehicleClass cls;

    int cx;
    int cy;
    int w;
    int h;

    int last_frame_id;
    int missed_frames;        /* consecutive frames without a match */
    int lsr_request_frame_id; /* used with LSR_RETRY_GAP_FRAMES */
};

/* Class predicates used across modules. */
inline bool is_hostile_class(VehicleClass c)
{
    return c == VehicleClass::Tank ||
           c == VehicleClass::Zpt  ||
           c == VehicleClass::MilitaryTruck;
}

/*
 * Engagement priority for single-target mode. Higher value engages
 * first: Tank > Zpt > MilitaryTruck. Civilian returns 0 and is never
 * engaged; missile_inventory_allocate_for_class also rejects it.
 */
inline int class_priority(VehicleClass c)
{
    switch (c) {
        case VehicleClass::Tank:          return 3;
        case VehicleClass::Zpt:           return 2;
        case VehicleClass::MilitaryTruck: return 1;
        case VehicleClass::Civilian:      return 0;
        default:                          return -1;
    }
}

} /* namespace omc */

#endif /* OMC_TYPES_HPP_ */
