/*
 * cluster_table.cpp -- cluster registry implementation.
 *
 * Thread-safe via a single TX_MUTEX (priority inheritance). Helpers
 * suffixed with "_no_lock" must only be called with the mutex held.
 *
 * The build path uses BFS over the candidate set with a size-scaled link
 * threshold; see the algorithm note in the header for details. Squared
 * distances are used throughout so the build path never pulls libm.
 */

#include "cluster_table.hpp"
#include "track_table.hpp"
#include "log.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include <cstring>

namespace omc {
namespace {

CHAR     g_mutex_name[] = "omc_clu";
TX_MUTEX g_cluster_mutex;

PendingCluster  g_pending [MAX_PENDING_CLUSTERS];
VerifiedCluster g_verified[MAX_VERIFIED_CLUSTERS];

int  g_next_cluster_id = 1;
bool g_initialized     = false;

/* --- internal, must be called with the mutex held --- */

void clear_all_no_lock()
{
    std::memset(g_pending,  0, sizeof(g_pending));
    std::memset(g_verified, 0, sizeof(g_verified));
    g_next_cluster_id = 1;
}

PendingCluster* find_free_pending_no_lock()
{
    for (int i = 0; i < MAX_PENDING_CLUSTERS; ++i) {
        if (!g_pending[i].active) {
            return &g_pending[i];
        }
    }
    return nullptr;
}

PendingCluster* find_pending_by_id_no_lock(int cluster_id)
{
    for (int i = 0; i < MAX_PENDING_CLUSTERS; ++i) {
        if (g_pending[i].active && g_pending[i].cluster_id == cluster_id) {
            return &g_pending[i];
        }
    }
    return nullptr;
}

VerifiedCluster* find_verified_by_id_no_lock(int cluster_id)
{
    for (int i = 0; i < MAX_VERIFIED_CLUSTERS; ++i) {
        if (g_verified[i].active && g_verified[i].cluster_id == cluster_id) {
            return &g_verified[i];
        }
    }
    return nullptr;
}

VerifiedCluster* find_free_verified_no_lock()
{
    for (int i = 0; i < MAX_VERIFIED_CLUSTERS; ++i) {
        if (!g_verified[i].active) {
            return &g_verified[i];
        }
    }
    return nullptr;
}

int next_cluster_id_no_lock()
{
    return g_next_cluster_id++;
}

/* Cluster scoring:
 *   Tank=5, Zpt=3, MilitaryTruck=2. Civilian and Unknown contribute 0
 *   but should never reach this code path -- the eligibility filter in
 *   track_table_collect_lsr_candidates already removes them. */
int cluster_score_for_class(int cls_int)
{
    const VehicleClass cls = static_cast<VehicleClass>(cls_int);
    switch (cls) {
        case VehicleClass::Tank:          return 5;
        case VehicleClass::Zpt:           return 3;
        case VehicleClass::MilitaryTruck: return 2;
        default:                          return 0;
    }
}

/* Squared distance between two pixel centroids. */
float distance_sq(int x1, int y1, int x2, int y2)
{
    const float dx = static_cast<float>(x1 - x2);
    const float dy = static_cast<float>(y1 - y2);
    return dx * dx + dy * dy;
}

/* Cluster link threshold, returned squared (in pixels^2).
 *
 * Conceptually this is 0.8 * (average bbox size) + 30, clamped to a
 * [35, 120] px envelope so very small or very large boxes still cluster
 * sensibly. To stay free of libm we use (w+h)/2 as the size term -- it
 * tracks sqrt(w*w + h*h) within ~10 px for typical bbox aspect ratios,
 * the same envelope is applied, and the result is squared once at the
 * end so the build path can compare against squared distances. */
float cluster_link_threshold_sq_px(const LsrCandidate& a, const LsrCandidate& b)
{
    const float avg_size = (static_cast<float>(a.w + a.h) +
                            static_cast<float>(b.w + b.h)) * 0.25f;
    float thr = 0.8f * avg_size + 30.0f;
    if (thr < 35.0f)  thr = 35.0f;
    if (thr > 120.0f) thr = 120.0f;
    return thr * thr;
}

} /* anonymous namespace */

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

bool cluster_table_init()
{
    if (g_initialized) {
        return true;
    }
    UINT s = tx_mutex_create(&g_cluster_mutex, g_mutex_name, TX_INHERIT);
    if (s != TX_SUCCESS) {
        OMC_LOG("[OMC] cluster_table_init: tx_mutex_create failed 0x%02X\r\n",
                static_cast<unsigned>(s));
        return false;
    }
    clear_all_no_lock();
    g_initialized = true;

    OMC_LOG("[OMC] cluster table initialized (%d pending + %d verified slots)\r\n",
            MAX_PENDING_CLUSTERS, MAX_VERIFIED_CLUSTERS);
    return true;
}

void cluster_table_reset()
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);
    clear_all_no_lock();
    tx_mutex_put(&g_cluster_mutex);
    OMC_LOG("[OMC] cluster table reset\r\n");
}

/* ------------------------------------------------------------------ */
/* Build                                                              */
/* ------------------------------------------------------------------ */

int cluster_table_build_pending(int frame_id)
{
    if (!g_initialized) {
        return 0;
    }

    /* Step 1: pull eligible hostile candidates. The same call marks each
     * returned track as lsr_pending inside track_table -- exactly what
     * normal-mode LSR would do, so the two paths stay consistent on
     * track-side bookkeeping. */
    LsrCandidate cands[MAX_TRACKS];
    const int n_cand = track_table_collect_lsr_candidates(
        cands, MAX_TRACKS, frame_id);
    if (n_cand <= 0) {
        return 0;
    }

    /* Step 2: BFS-based connected-neighborhood clustering. The number
     * of clusters is not fixed in advance: each unvisited candidate
     * seeds a new cluster, and BFS grows it by absorbing every other
     * candidate whose centroid is within the size-scaled link
     * threshold. The frontier propagates, so two candidates that are
     * not directly close enough can still end up in the same cluster
     * via an intermediate one. The score is the sum of per-member
     * class weights (Tank=5, Zpt=3, Truck=2). */
    bool visited[MAX_TRACKS];
    std::memset(visited, 0, sizeof(visited));

    int n_built = 0;

    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);

    for (int i = 0; i < n_cand; ++i) {
        if (visited[i]) {
            continue;
        }

        PendingCluster* pc = find_free_pending_no_lock();
        if (pc == nullptr) {
            break; /* table full; remaining candidates dropped this round */
        }

        std::memset(pc, 0, sizeof(PendingCluster));
        pc->active          = true;
        pc->cluster_id      = next_cluster_id_no_lock();
        pc->source_frame_id = frame_id;
        pc->member_count    = 0;
        pc->score           = 0;

        /* BFS queue holds indices into 'cands'. */
        int queue[MAX_TRACKS];
        int qh = 0;
        int qt = 0;
        queue[qt++] = i;
        visited[i]  = true;

        while (qh < qt) {
            const int cur_idx = queue[qh++];
            const LsrCandidate& cur = cands[cur_idx];

            if (pc->member_count < MAX_CLUSTER_MEMBERS) {
                PendingClusterMember& m = pc->members[pc->member_count];
                m.track_id = cur.track_id;
                m.cx       = cur.cx;
                m.cy       = cur.cy;
                pc->member_count++;
                pc->score += cluster_score_for_class(cur.cls);
            }

            for (int j = 0; j < n_cand; ++j) {
                if (visited[j]) continue;
                const LsrCandidate& other = cands[j];

                const float thr_sq = cluster_link_threshold_sq_px(cur, other);
                const float dsq    = distance_sq(cur.cx, cur.cy, other.cx, other.cy);

                if (dsq <= thr_sq) {
                    visited[j] = true;
                    if (qt < MAX_TRACKS) {
                        queue[qt++] = j;
                    }
                }
            }
        }

        n_built++;
    }

    tx_mutex_put(&g_cluster_mutex);
    return n_built;
}

/* ------------------------------------------------------------------ */
/* Snapshot                                                           */
/* ------------------------------------------------------------------ */

int cluster_table_get_pending_for_frame(int frame_id,
                                        PendingCluster* out_buf,
                                        int max_count)
{
    if (!g_initialized || out_buf == nullptr || max_count <= 0) {
        return 0;
    }

    int copied = 0;

    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_PENDING_CLUSTERS && copied < max_count; ++i) {
        const PendingCluster& pc = g_pending[i];
        if (!pc.active)                     continue;
        if (pc.source_frame_id != frame_id) continue;

        out_buf[copied++] = pc;
    }
    tx_mutex_put(&g_cluster_mutex);

    return copied;
}

/* ------------------------------------------------------------------ */
/* CLSRRES handling                                                   */
/* ------------------------------------------------------------------ */

ClusterApplyResult cluster_table_apply_cluster_result(int cluster_id, int hit)
{
    ClusterApplyResult r = {0, 0, {0}};
    if (!g_initialized) {
        return r;
    }

    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);

    PendingCluster* pc = find_pending_by_id_no_lock(cluster_id);
    if (pc == nullptr) {
        tx_mutex_put(&g_cluster_mutex);
        return r;
    }

    /* Snapshot members so the caller can update track_table without
     * re-acquiring this mutex. */
    r.member_count = pc->member_count;
    for (int k = 0; k < pc->member_count; ++k) {
        r.track_ids[k] = pc->members[k].track_id;
    }

    if (hit == 1) {
        /* Promote into a verified slot. If a verified record with the
         * same cluster_id already exists we overwrite it (idempotent
         * re-hit); otherwise we allocate a free slot. */
        VerifiedCluster* vc = find_verified_by_id_no_lock(cluster_id);
        const bool was_already_verified = (vc != nullptr);
        if (vc == nullptr) {
            vc = find_free_verified_no_lock();
        }

        if (vc != nullptr) {
            std::memset(vc, 0, sizeof(VerifiedCluster));
            vc->active          = true;
            vc->engaged         = false;
            vc->cluster_id      = pc->cluster_id;
            vc->source_frame_id = pc->source_frame_id;
            vc->member_count    = pc->member_count;
            vc->score           = pc->score;
            for (int k = 0; k < pc->member_count; ++k) {
                vc->track_ids[k] = pc->members[k].track_id;
            }
            r.outcome = was_already_verified ? 3 : 2;
        } else {
            /* Verified table full -- a rare backpressure case. Pending
             * stays consumed (so the result doesn't get re-applied) and
             * outcome stays 0 to flag the drop to the caller. */
            r.outcome = 0;
        }
    } else {
        r.outcome = 1; /* miss, pending closed */
    }

    pc->active = false; /* always deactivate the pending entry */

    tx_mutex_put(&g_cluster_mutex);
    return r;
}

bool cluster_table_peek_best_verified(VerifiedCluster& out)
{
    if (!g_initialized) {
        return false;
    }

    bool found = false;
    int  best_score = -1;

    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_VERIFIED_CLUSTERS; ++i) {
        const VerifiedCluster& v = g_verified[i];
        if (!v.active) continue;
        if (v.engaged) continue;
        if (v.score > best_score) {
            best_score = v.score;
            out        = v;
            found      = true;
        }
    }
    tx_mutex_put(&g_cluster_mutex);

    return found;
}

bool cluster_table_mark_cluster_engaged(int cluster_id)
{
    if (!g_initialized) {
        return false;
    }

    bool found = false;
    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);
    VerifiedCluster* vc = find_verified_by_id_no_lock(cluster_id);
    if (vc != nullptr) {
        vc->engaged = true;
        found = true;
    }
    tx_mutex_put(&g_cluster_mutex);

    return found;
}

/* ------------------------------------------------------------------ */
/* Telemetry                                                          */
/* ------------------------------------------------------------------ */

ClusterStats cluster_table_stats()
{
    ClusterStats s = {0, 0, 0};
    if (!g_initialized) {
        return s;
    }
    tx_mutex_get(&g_cluster_mutex, TX_WAIT_FOREVER);
    for (int i = 0; i < MAX_PENDING_CLUSTERS; ++i) {
        if (g_pending[i].active) s.pending_active++;
    }
    for (int i = 0; i < MAX_VERIFIED_CLUSTERS; ++i) {
        if (!g_verified[i].active) continue;
        s.verified_active++;
        if (g_verified[i].engaged) s.verified_engaged++;
    }
    tx_mutex_put(&g_cluster_mutex);
    return s;
}

} /* namespace omc */
