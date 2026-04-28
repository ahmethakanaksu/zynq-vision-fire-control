/*
 * events.hpp -- QP/C custom event types for the OMC subsystem.
 *
 * Every event subclass embeds 'QEvt super' as its FIRST member so the
 * pointer cast between QEvt* and the subclass is well defined under the
 * standard layout / common initial sequence rule. Events are allocated
 * from QF event pools (one pool per type, sized in the matching
 * *_init_event_pool() functions).
 */

#ifndef OMC_EVENTS_HPP_
#define OMC_EVENTS_HPP_

extern "C" {
#include "qpc.h"
}

#include "types.hpp"

namespace omc {

/* AO_FrameProcessor inbox: carries one fully parsed YOLO frame from
 * udp_router. Payload is copied by value so the QP queue can outlive
 * the rx thread's working buffer. */
struct FrameEvent {
    QEvt      super;                /* must be first */
    int       frame_id;
    int       detection_count;
    Detection detections[MAX_DETECTIONS_PER_FRAME];
};

/* AO_LsrManager inbox: list of hostile tracks that need laser-range
 * verification for a given frame. Payload carries only the centroid
 * pixel coordinates; the pitch/yaw conversion to milli-degrees is done
 * on the AO_LsrManager side via compute_lsr_angles_mdeg so the math
 * stays isolated in one place. */
struct LsrRequestItem {
    int track_id;
    int cx;
    int cy;
};

struct LsrRequestEvent {
    QEvt          super;            /* must be first */
    int           frame_id;
    int           item_count;
    LsrRequestItem items[MAX_DETECTIONS_PER_FRAME];
};

/* AO_LsrManager inbox: carries the per-track hit/miss results returned
 * by Unity's raycast. Each item references a track_id that was sent in
 * an earlier LSRCMD, plus a 0/1 hit flag. */
struct LsrResultItem {
    int track_id;
    int hit;     /* 1 = laser hit, 0 = miss */
};

struct LsrResultEvent {
    QEvt          super;            /* must be first */
    int           frame_id;
    int           item_count;
    LsrResultItem items[MAX_DETECTIONS_PER_FRAME];
};

/* AO_Engagement inbox: a single track has just been verified and is
 * eligible for engagement. AO_LsrManager fires this after every
 * "DISCOVERED (new)" transition. Small enough (~12B with QEvt) to
 * share the LsrResultEvent pool, so no dedicated QF pool is needed. */
struct TargetVerifiedEvent {
    QEvt super;     /* must be first */
    int  frame_id;  /* frame at which verification arrived */
    int  track_id;
    int  cls;       /* VehicleClass as int, for wire-format convenience */
};

/* AO_LsrManager inbox (cluster mode): notification that a frame has
 * produced new pending clusters. Payload is just the frame_id;
 * AO_LsrManager pulls the actual cluster details from cluster_table
 * under its mutex when handling. Small enough to share the
 * LsrResultEvent QF pool. */
struct ClusterRequestEvent {
    QEvt super;     /* must be first */
    int  frame_id;
};

/* AO_LsrManager inbox (cluster mode): per-cluster verification results
 * from the simulator. Each item references a cluster_id that was sent
 * earlier in a CLSRCMD, plus a 0/1 hit flag. Sized for the worst case
 * where every pending cluster slot reports back at once.
 *
 * Footprint: 8 (QEvt) + 4 + 4 + 16*8 = 144 B. Doesn't fit the LsrResult
 * pool (76 B), so QF allocates from the next-larger pool that fits, the
 * FrameEvent pool (172 B). No new pool needed. */
struct ClusterResultItem {
    int cluster_id;
    int hit;        /* 1 = cluster verified, 0 = miss */
};

struct ClusterResultEvent {
    QEvt              super;       /* must be first */
    int               frame_id;
    int               item_count;
    ClusterResultItem items[16];   /* matches MAX_PENDING_CLUSTERS */
};

/* AO_Engagement inbox (cluster mode): triggers cluster-level fire
 * selection. Posted by AO_LsrManager after processing a CLSRRES batch.
 * Engagement queries cluster_table to pick the best verified cluster.
 * Payload-light, shares the LsrResultEvent QF pool. */
struct ClusterFireDecisionEvent {
    QEvt super;     /* must be first */
    int  frame_id;  /* frame at which the verification arrived */
};

} /* namespace omc */

#endif /* OMC_EVENTS_HPP_ */
