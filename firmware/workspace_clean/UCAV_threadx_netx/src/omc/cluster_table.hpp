/*
 * cluster_table.hpp -- mutex-protected cluster registry for cluster-mode
 * engagement.
 *
 * Cluster mode is a strict superset of the normal-mode pipeline: it is
 * entered only on a MODE_CLUSTER command and the normal-mode code path
 * does not touch this table. AO_FrameProcessor branches at the start of
 * its per-frame pipeline so the normal path stays untouched.
 *
 * Clustering algorithm:
 *   1. Collect hostile / undiscovered / non-pending tracks for this
 *      frame (via track_table_collect_lsr_candidates -- exactly the same
 *      eligibility filter normal-mode LSR uses).
 *   2. DBSCAN-style connected-neighborhood grouping. Two candidates are
 *      "linked" if their image-space distance is within a size-scaled
 *      threshold; BFS expands each component into one cluster. The
 *      number of clusters is not fixed in advance, it falls out of the
 *      connectivity graph.
 *   3. Each cluster gets a monotonically-increasing cluster_id, a member
 *      list (with cached centroid), and an additive score
 *      (Tank=5, Zpt=3, Truck=2; Civilian contributes 0 and is filtered
 *      upstream).
 *
 * Numerical note: like track_table we work in squared-distance space
 * everywhere, so the build never pulls libm. The link threshold uses
 * (w+h)/2 as the size term instead of the diagonal sqrt(w*w + h*h);
 * the two are within ~10 px of each other for typical bbox aspect
 * ratios and the result is clamped to the same [35, 120] px envelope
 * before squaring.
 */

#ifndef OMC_CLUSTER_TABLE_HPP_
#define OMC_CLUSTER_TABLE_HPP_

#include "types.hpp"

namespace omc {

constexpr int MAX_PENDING_CLUSTERS  = 16;
constexpr int MAX_VERIFIED_CLUSTERS = 16;
constexpr int MAX_CLUSTER_MEMBERS   = 8;

/* One member of a cluster, with the centroid cached so AO_LsrManager
 * can format the LSR angles without a second acquire on track_table. */
struct PendingClusterMember {
    int track_id;
    int cx;
    int cy;
};

struct PendingCluster {
    bool active;
    int  cluster_id;
    int  source_frame_id;
    int  member_count;
    PendingClusterMember members[MAX_CLUSTER_MEMBERS];
    int  score;
};

/* A cluster that has been confirmed by a CLSRRES hit and is now eligible
 * for cluster-level fire (CFIRE). Members are stored as bare track_ids
 * because by the time we engage the cluster the centroids are already
 * stale; AO_Engagement only needs the IDs to mark them engaged in
 * track_table. */
struct VerifiedCluster {
    bool active;
    bool engaged;
    int  cluster_id;
    int  source_frame_id;
    int  member_count;
    int  track_ids[MAX_CLUSTER_MEMBERS];
    int  score;
};

/* Lifecycle */
bool cluster_table_init();
void cluster_table_reset();

/* Build pending clusters from currently eligible hostile candidates.
 * Internally calls track_table_collect_lsr_candidates so each track in
 * a built cluster is also marked lsr_pending in track_table.
 * Returns the number of pending clusters added (0 if no candidates). */
int  cluster_table_build_pending(int frame_id);

/* Snapshot the pending clusters belonging to a given frame into out_buf.
 * Returns the count copied. Acquires the cluster mutex internally. */
int  cluster_table_get_pending_for_frame(int frame_id,
                                         PendingCluster* out_buf,
                                         int max_count);

/* --- CLSRRES handling and cluster fire decision --- */

/* Result of applying one (cluster_id, hit) pair from a CLSRRES batch.
 * member_count + track_ids capture the affected pending cluster's members
 * so the caller can update track_table (clear lsr_pending, mark
 * discovered) without re-acquiring the cluster mutex. */
struct ClusterApplyResult {
    int outcome;          /* 0 = cluster_id not found
                           * 1 = miss, pending cleared
                           * 2 = newly verified (promoted to verified slot)
                           * 3 = already verified (idempotent re-hit) */
    int member_count;
    int track_ids[MAX_CLUSTER_MEMBERS];
};
ClusterApplyResult cluster_table_apply_cluster_result(int cluster_id, int hit);

/* Pick the highest-scoring non-engaged verified cluster. Read-only:
 * does NOT mark it engaged. Returns true on success and fills 'out';
 * false if no eligible verified cluster exists. */
bool cluster_table_peek_best_verified(VerifiedCluster& out);

/* Mark a verified cluster as engaged (a CFIRE has been issued for it).
 * Idempotent. Returns true if the cluster was found and updated. */
bool cluster_table_mark_cluster_engaged(int cluster_id);

/* Telemetry */
struct ClusterStats {
    int pending_active;
    int verified_active;
    int verified_engaged;
};
ClusterStats cluster_table_stats();

} /* namespace omc */

#endif /* OMC_CLUSTER_TABLE_HPP_ */
