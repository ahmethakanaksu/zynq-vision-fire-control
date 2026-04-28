/*
 * missile_inventory.hpp -- mutex-protected munition registry.
 *
 * Owns the per-missile inventory mounted on the UCAV: 4 Cirit, 4 MAM-L,
 * 1 SOM. Missile IDs encode the type via the tens digit so the wire
 * protocol can carry an ID alone and the receiver still knows the type:
 *   01..04 = Cirit, 11..14 = MAM-L, 21 = SOM
 *
 * Allocation policy is class-aware (single-target) and cluster-aware
 * (when AO_Engagement is firing on a verified cluster); both are pure
 * decisions inside the mutex with no I/O.
 */

#ifndef OMC_MISSILE_INVENTORY_HPP_
#define OMC_MISSILE_INVENTORY_HPP_

#include "types.hpp"

namespace omc {

/* One-time initialization. Creates the mutex and marks all missiles
 * available. Idempotent. */
bool missile_inventory_init();

/* Mark all missiles available again (used by RESET command). */
void missile_inventory_reset();

/* Snapshot returned to telemetry; computed under the mutex. */
struct MissileInventoryStats {
    int cirit_available;
    int maml_available;
    int som_available;
    int total_available;
};

MissileInventoryStats missile_inventory_stats();

/* --- Single-target allocation ---
 *
 * Pick and consume one missile suitable for the given target class,
 * walking the preference table in order until something is available:
 *   Tank          : MAM-L > SOM    > Cirit
 *   Zpt           : MAM-L > Cirit  > SOM
 *   MilitaryTruck : Cirit > MAM-L  > SOM
 *
 * Civilian / Unknown classes are never engaged: returns -1 immediately.
 * Returns the missile_id on success, or -1 when nothing suitable is
 * left in inventory. */
int missile_inventory_allocate_for_class(VehicleClass cls);

/* --- Cluster-mode allocation ---
 *
 * Pick and consume one missile suitable for a verified cluster. The
 * preference depends on the cluster's score and whether it contains a
 * Tank, so heavy/strategic targets get the heavier round first:
 *   has_tank OR score >= 8 : MAM-L > SOM   > Cirit   (heavy/strategic)
 *   score >= 5             : MAM-L > Cirit > SOM     (medium)
 *   else                   : Cirit > MAM-L > SOM     (light)
 *
 * Returns the missile_id on success, or -1 when nothing suitable is
 * left in inventory. */
int missile_inventory_allocate_for_cluster(int score, bool has_tank);

} /* namespace omc */

#endif /* OMC_MISSILE_INVENTORY_HPP_ */
