/*
 * track_table.hpp -- mutex-protected track registry.
 *
 * Owns the array of Track records and the per-class ID counters. All
 * public entry points are internally serialized with a TX_MUTEX (created
 * with TX_INHERIT, i.e. priority inheritance), so the udp_router thread
 * and every AO (FrameProcessor, LsrManager, Engagement, Telemetry,
 * MissionController) can call them concurrently without external locking.
 *
 * The table is the single source of truth for track lifecycle:
 *   create on first detection, update on every match, age out via
 *   missed_frames, set discovered/lsr_pending/engaged flags as the
 *   pipeline progresses.
 */

#ifndef OMC_TRACK_TABLE_HPP_
#define OMC_TRACK_TABLE_HPP_

#include "types.hpp"

namespace omc {

/* --- Lifecycle --- */

bool track_table_init();
void track_table_reset();

/* --- Per-frame tracking pipeline --- */

void track_table_begin_frame();
void track_table_end_frame();

struct MatchOrCreateResult {
    bool ok;
    bool was_new_track;
    int  track_id;
};
MatchOrCreateResult track_table_match_or_create(const Detection& d, int frame_id);

/* --- LSR candidate collection ---
 *
 * After end_frame, AO_FrameProcessor calls this to harvest the tracks
 * that are eligible for laser-range verification on this frame. Each
 * candidate carries its cached centroid + bbox so AO_LsrManager (or
 * cluster_table) can format the wire message without re-locking the
 * track table.
 *
 * Eligibility:
 *   - track active
 *   - hostile class
 *   - not already discovered
 *   - missed_frames == 0 (was actually seen this frame)
 *   - either not lsr_pending, or the retry gap has elapsed
 *
 * Side effect (under the mutex): each collected track is marked
 * lsr_pending = true and lsr_request_frame_id = frame_id, so the same
 * track does not enter another LSR batch until the result comes back
 * or the retry gap expires.
 *
 * Returns the number of candidates written into 'out' (0..max_count).
 */
struct LsrCandidate {
    int track_id;
    int cls;            /* VehicleClass as int, used by cluster scoring */
    int cx;
    int cy;
    int w;              /* bbox width,  used by the cluster link threshold */
    int h;              /* bbox height, used by the cluster link threshold */
};
int track_table_collect_lsr_candidates(LsrCandidate* out, int max_count, int frame_id);

/* --- LSR result update ---
 *
 * Apply one (track_id, hit) result coming back from the simulator.
 * Always clears lsr_pending; if hit==1 also flips discovered to true.
 * Tracks that are no longer active (decayed since the request went out)
 * are silently ignored -- a normal outcome under UDP loss.
 *
 * Returns:
 *   0 -- track not found (graceful no-op)
 *   1 -- track updated, hit=0 (miss, request closed)
 *   2 -- track updated, hit=1 (newly discovered)
 *   3 -- track updated, hit=1 (was already discovered, idempotent)
 */
int track_table_mark_lsr_result(int track_id, int hit);

/* Mark a track as 'engaged' (a fire decision has been issued for it).
 * Idempotent. Returns true if the track was found and updated, false
 * otherwise. */
bool track_table_mark_engaged(int track_id);

/* --- Telemetry snapshot --- */

struct TrackTableStats {
    int active;
    int discovered;
    int hostile;
    int lsr_pending;
};
TrackTableStats track_table_stats();

} /* namespace omc */

#endif /* OMC_TRACK_TABLE_HPP_ */
