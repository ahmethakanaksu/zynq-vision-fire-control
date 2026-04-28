/*
 * track_table.cpp -- track registry implementation.
 *
 * Thread-safe via a single TX_MUTEX (priority inheritance). All public
 * entry points acquire the mutex; the internal "_no_lock" helpers
 * assume the caller already holds it.
 *
 * The MOT-style association (match_or_create) is a lightweight nearest-
 * neighbour assignment with class consistency, scaled to the bounding-
 * box diagonal -- the same logic as the legacy prototype, but expressed
 * in idiomatic C++ on top of ThreadX primitives.
 *
 * Numerical note: we never compute Euclidean distance; the whole match
 * path works in squared-distance space, so the build never pulls sqrt
 * / libm. sqrt is monotonic on non-negative values, so the comparison
 * is equivalent:
 *
 *     original:   thr  = 0.6 * sqrt(w*w + h*h),  clamp(thr,  18, 60)
 *     here:       thr2 = 0.36 * (w*w + h*h),     clamp(thr2, 324, 3600)
 *
 * dsq <= thr2 then makes the same decision as d <= thr but without the
 * sqrt call.
 */

#include "track_table.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include "omc/log.hpp"

#include <cstring>

namespace omc {
namespace {

/* Mutex name buffer is non-const because tx_mutex_create wants CHAR* */
CHAR     g_mutex_name[] = "omc_trk";
TX_MUTEX g_track_mutex;

Track g_tracks[MAX_TRACKS];
int   g_class_counters[4];   /* per-class running serial for ID generation */
bool  g_initialized = false;

/* --- internal, must be called with the mutex held --- */

void clear_all_no_lock()
{
    std::memset(g_tracks, 0, sizeof(g_tracks));
    for (int i = 0; i < 4; ++i) {
        g_class_counters[i] = 0;
    }
}

/* Squared image-space distance between two centroids. */
float distance_sq(int x1, int y1, int x2, int y2)
{
    const float dx = static_cast<float>(x1 - x2);
    const float dy = static_cast<float>(y1 - y2);
    return dx * dx + dy * dy;
}

/* Squared association threshold. See file-header note for the equivalence
 * with the original sqrt-based formulation. */
float association_threshold_sq_px(const Detection& d)
{
    const float diag_sq = static_cast<float>(d.w * d.w + d.h * d.h);
    float thr_sq = 0.36f * diag_sq;        /* (0.6)^2 = 0.36         */

    if (thr_sq < 324.0f)  thr_sq = 324.0f;  /* 18^2 = 324  (lower)   */
    if (thr_sq > 3600.0f) thr_sq = 3600.0f; /* 60^2 = 3600 (upper)   */
    return thr_sq;
}

/* Generate next track id for a class.
 *   100s digit encodes class: 0xx Tank, 1xx Zpt, 2xx MilitaryTruck, 3xx Civilian */
int next_track_id_no_lock(VehicleClass cls)
{
    const int idx = static_cast<int>(cls);
    if (idx < 0 || idx > 3) {
        return -1;
    }
    const int serial = g_class_counters[idx]++;
    return idx * 100 + serial;
}

/* Find the best existing track for a new detection, or -1 if none
 * qualifies. Match rules:
 *   - same class (Tank can't be matched to a Truck slot)
 *   - not already matched in this frame (one detection per track)
 *   - track not too stale (within MAX_MISSED_FRAMES)
 *   - centroid within the size-scaled threshold; among the candidates,
 *     the closest one wins.
 * If a match is found the caller updates the existing track in place
 * (same id, no new LSR). If not, a new slot is allocated and a fresh
 * id is issued. This is what stops the same vehicle from getting a
 * new id every frame just because the bounding box wobbled. */
int find_best_match_no_lock(const Detection& d)
{
    int   best_idx     = -1;
    float best_dist_sq = 1e30f;

    const float thr_sq = association_threshold_sq_px(d);

    for (int i = 0; i < MAX_TRACKS; ++i) {
        const Track& t = g_tracks[i];
        if (!t.active)                          continue;
        if (t.cls != d.cls)                     continue;
        if (t.matched_this_frame)               continue;
        if (t.missed_frames > MAX_MISSED_FRAMES) continue;

        const float dsq = distance_sq(d.cx, d.cy, t.cx, t.cy);
        if (dsq <= thr_sq && dsq < best_dist_sq) {
            best_dist_sq = dsq;
            best_idx     = i;
        }
    }
    return best_idx;
}

int find_free_slot_no_lock()
{
    for (int i = 0; i < MAX_TRACKS; ++i) {
        if (!g_tracks[i].active) {
            return i;
        }
    }
    return -1;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

bool track_table_init()
{
    if (g_initialized) {
        return true;
    }

    UINT s = tx_mutex_create(&g_track_mutex, g_mutex_name, TX_INHERIT);
    if (s != TX_SUCCESS) {
        OMC_LOG("[OMC] track_table_init: tx_mutex_create failed 0x%02X\r\n",
                   static_cast<unsigned>(s));
        return false;
    }

    clear_all_no_lock();   /* safe before scheduling: still single-threaded */
    g_initialized = true;

    OMC_LOG("[OMC] track table initialized (%d slots, mutex up)\r\n",
               static_cast<int>(MAX_TRACKS));
    return true;
}

void track_table_reset()
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    clear_all_no_lock();
    tx_mutex_put(&g_track_mutex);

    OMC_LOG("[OMC] track table reset\r\n");
}

/* ------------------------------------------------------------------ */
/* Per-frame tracking pipeline                                        */
/*                                                                    */
/* AO_FrameProcessor calls these in order for every frame:            */
/*   begin_frame    -- clear matched_this_frame on every active track */
/*   match_or_create -- once per detection: update if it matches an   */
/*                     existing track, otherwise allocate a new one   */
/*   end_frame      -- bump missed_frames on tracks that were not     */
/*                     matched, reclaim slots that crossed the limit  */
/* ------------------------------------------------------------------ */

void track_table_begin_frame()
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        if (g_tracks[i].active) {
            g_tracks[i].matched_this_frame = false;
        }
    }
    tx_mutex_put(&g_track_mutex);
}

void track_table_end_frame()
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        Track& t = g_tracks[i];
        if (!t.active) continue;

        if (!t.matched_this_frame) {
            t.missed_frames++;
            if (t.missed_frames > MAX_MISSED_FRAMES) {
                t.active = false;
            }
        }
    }
    tx_mutex_put(&g_track_mutex);
}

MatchOrCreateResult track_table_match_or_create(const Detection& d, int frame_id)
{
    MatchOrCreateResult r = {false, false, -1};
    if (!g_initialized) {
        return r;
    }

    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);

    int idx = find_best_match_no_lock(d);
    if (idx >= 0) {
        Track& t = g_tracks[idx];
        t.matched_this_frame = true;
        t.cx                 = d.cx;
        t.cy                 = d.cy;
        t.w                  = d.w;
        t.h                  = d.h;
        t.last_frame_id      = frame_id;
        t.missed_frames      = 0;

        r.ok            = true;
        r.was_new_track = false;
        r.track_id      = t.track_id;
    }
    else {
        idx = find_free_slot_no_lock();
        if (idx >= 0) {
            Track& t = g_tracks[idx];
            std::memset(&t, 0, sizeof(Track));
            t.active                = true;
            t.discovered            = false;
            t.lsr_pending           = false;
            t.engaged               = false;
            t.matched_this_frame    = true;
            t.track_id              = next_track_id_no_lock(d.cls);
            t.cls                   = d.cls;
            t.cx                    = d.cx;
            t.cy                    = d.cy;
            t.w                     = d.w;
            t.h                     = d.h;
            t.last_frame_id         = frame_id;
            t.missed_frames         = 0;
            t.lsr_request_frame_id  = -1;

            r.ok            = true;
            r.was_new_track = true;
            r.track_id      = t.track_id;
        }
        /* else: table full, r.ok stays false */
    }

    tx_mutex_put(&g_track_mutex);
    return r;
}

/* ------------------------------------------------------------------ */
/* LSR candidate collection                                           */
/* ------------------------------------------------------------------ */

int track_table_collect_lsr_candidates(LsrCandidate* out,
                                       int max_count,
                                       int frame_id)
{
    if (!g_initialized || out == nullptr || max_count <= 0) {
        return 0;
    }

    int written = 0;

    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        Track& t = g_tracks[i];

        if (!t.active)                          continue;
        if (!is_hostile_class(t.cls))           continue;
        if (t.discovered)                       continue;
        if (t.missed_frames > 0)                continue;

        /* Already pending: re-issue only if the LSR_RETRY_GAP_FRAMES
         * window has expired, so a single in-flight request is not
         * spammed every frame. */
        if (t.lsr_pending &&
            (frame_id - t.lsr_request_frame_id) < LSR_RETRY_GAP_FRAMES) {
            continue;
        }

        if (written >= max_count) {
            break;
        }

        out[written].track_id = t.track_id;
        out[written].cls      = static_cast<int>(t.cls);
        out[written].cx       = t.cx;
        out[written].cy       = t.cy;
        out[written].w        = t.w;
        out[written].h        = t.h;
        ++written;

        t.lsr_pending           = true;
        t.lsr_request_frame_id  = frame_id;
    }
    tx_mutex_put(&g_track_mutex);

    return written;
}

/* ------------------------------------------------------------------ */
/* LSR result update                                                  */
/* ------------------------------------------------------------------ */

int track_table_mark_lsr_result(int track_id, int hit)
{
    if (!g_initialized) {
        return 0;
    }

    int outcome = 0;

    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        Track& t = g_tracks[i];
        if (!t.active)               continue;
        if (t.track_id != track_id)  continue;

        /* Always clear the pending flag. Even a miss closes the request. */
        t.lsr_pending = false;

        if (hit == 1) {
            if (t.discovered) {
                outcome = 3; /* idempotent */
            } else {
                t.discovered = true;
                outcome = 2; /* newly discovered */
            }
        } else {
            outcome = 1; /* miss, request closed */
        }
        break;
    }
    tx_mutex_put(&g_track_mutex);

    return outcome;
}

bool track_table_mark_engaged(int track_id)
{
    if (!g_initialized) {
        return false;
    }

    bool found = false;

    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        Track& t = g_tracks[i];
        if (!t.active)              continue;
        if (t.track_id != track_id) continue;

        t.engaged = true;
        found     = true;
        break;
    }
    tx_mutex_put(&g_track_mutex);

    return found;
}

/* ------------------------------------------------------------------ */
/* Telemetry snapshot                                                 */
/* ------------------------------------------------------------------ */

TrackTableStats track_table_stats()
{
    TrackTableStats s = {0, 0, 0, 0};
    if (!g_initialized) {
        return s;
    }

    tx_mutex_get(&g_track_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_TRACKS; ++i) {
        const Track& t = g_tracks[i];
        if (!t.active) continue;
        s.active++;
        if (t.discovered)                       s.discovered++;
        if (t.lsr_pending && !t.discovered)     s.lsr_pending++;
        if (is_hostile_class(t.cls))            s.hostile++;
    }
    tx_mutex_put(&g_track_mutex);

    return s;
}

} /* namespace omc */
