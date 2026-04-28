/*
 * missile_inventory.cpp -- munition registry implementation.
 *
 * Thread-safe via a single TX_MUTEX created with TX_INHERIT (priority
 * inheritance). Public entry points acquire and release the mutex;
 * helpers prefixed by "_no_lock" assume the caller already holds it.
 */

#include "missile_inventory.hpp"

extern "C" {
#include "tx_api.h"
#include "xil_printf.h"
}

#include "omc/log.hpp"

namespace omc {
namespace {

/* Internal record, kept private to this translation unit. */
struct MissileItem {
    int         missile_id;
    MissileType type;
    bool        available;
};

CHAR     g_mutex_name[] = "omc_msl";
TX_MUTEX g_mutex;

/* Mounted munitions on the UCAV at boot: 4 Cirit + 4 MAM-L + 1 SOM,
 * 9 rounds total. Missile id encodes the type via the tens digit
 * (01..04 Cirit, 11..14 MAM-L, 21 SOM), so the wire protocol can carry
 * just the id and the receiver still knows what was fired. */
MissileItem g_missiles[] = {
    {  1, MissileType::Cirit, true },
    {  2, MissileType::Cirit, true },
    {  3, MissileType::Cirit, true },
    {  4, MissileType::Cirit, true },
    { 11, MissileType::Maml,  true },
    { 12, MissileType::Maml,  true },
    { 13, MissileType::Maml,  true },
    { 14, MissileType::Maml,  true },
    { 21, MissileType::Som,   true }
};

constexpr int N_MISSILES = sizeof(g_missiles) / sizeof(g_missiles[0]);

bool g_initialized = false;

void reset_no_lock()
{
    for (int i = 0; i < N_MISSILES; ++i) {
        g_missiles[i].available = true;
    }
}

int count_no_lock(MissileType t)
{
    int c = 0;
    for (int i = 0; i < N_MISSILES; ++i) {
        if (g_missiles[i].available && g_missiles[i].type == t) {
            c++;
        }
    }
    return c;
}

} /* anonymous namespace */

bool missile_inventory_init()
{
    if (g_initialized) {
        return true;
    }

    UINT s = tx_mutex_create(&g_mutex, g_mutex_name, TX_INHERIT);
    if (s != TX_SUCCESS) {
        OMC_LOG("[OMC] missile_inventory_init: tx_mutex_create failed 0x%02X\r\n",
                   (unsigned)s);
        return false;
    }

    /* Static init already marks all available; reset is just safety. */
    reset_no_lock();
    g_initialized = true;

    OMC_LOG("[OMC] missile inventory ready (%d total: 4 Cirit + 4 MAM-L + 1 SOM)\r\n",
               N_MISSILES);
    return true;
}

void missile_inventory_reset()
{
    if (!g_initialized) {
        return;
    }
    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    reset_no_lock();
    tx_mutex_put(&g_mutex);

    OMC_LOG("[OMC] missile inventory reset\r\n");
}

/* Class preference table. For each hostile class the columns list the
 * munition we'd most like to use, second choice, third choice; the
 * allocator walks them in order and takes whatever is in stock.
 *
 *   Tank   : MAM-L > SOM   > Cirit  (heavy AT round first)
 *   Zpt    : MAM-L > Cirit > SOM    (medium armour, save SOM for later)
 *   Truck  : Cirit > MAM-L > SOM    (light, no need to spend MAM-L)
 */
namespace {
MissileType preferred_for_class(VehicleClass cls, int pref_idx)
{
    switch (cls) {
        case VehicleClass::Tank:
            if (pref_idx == 0) return MissileType::Maml;
            if (pref_idx == 1) return MissileType::Som;
            if (pref_idx == 2) return MissileType::Cirit;
            break;
        case VehicleClass::Zpt:
            if (pref_idx == 0) return MissileType::Maml;
            if (pref_idx == 1) return MissileType::Cirit;
            if (pref_idx == 2) return MissileType::Som;
            break;
        case VehicleClass::MilitaryTruck:
            if (pref_idx == 0) return MissileType::Cirit;
            if (pref_idx == 1) return MissileType::Maml;
            if (pref_idx == 2) return MissileType::Som;
            break;
        default:
            break;
    }
    return MissileType::None;
}
} /* anonymous namespace */

int missile_inventory_allocate_for_class(VehicleClass cls)
{
    if (!g_initialized) {
        return -1;
    }
    if (cls != VehicleClass::Tank &&
        cls != VehicleClass::Zpt  &&
        cls != VehicleClass::MilitaryTruck) {
        return -1; /* civilian / unknown -- never engage */
    }

    int allocated_id = -1;

    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    for (int pref = 0; pref < 3 && allocated_id < 0; ++pref) {
        const MissileType wanted = preferred_for_class(cls, pref);
        if (wanted == MissileType::None) {
            continue;
        }
        for (int i = 0; i < N_MISSILES; ++i) {
            if (g_missiles[i].available && g_missiles[i].type == wanted) {
                g_missiles[i].available = false;
                allocated_id = g_missiles[i].missile_id;
                break;
            }
        }
    }
    tx_mutex_put(&g_mutex);

    return allocated_id;
}

/* ------------------------------------------------------------------ */
/* Cluster-mode allocation                                            */
/* ------------------------------------------------------------------ */

/* Cluster preference table. The cluster score is the sum of
 * per-member class weights (Tank=5, Zpt=3, Truck=2). A heavier or
 * tank-bearing cluster pulls a heavier round; a small cluster of
 * trucks is engaged with Cirit so SOM is held in reserve. */
namespace {
MissileType preferred_for_cluster(int score, bool has_tank, int pref_idx)
{
    if (has_tank || score >= 8) {
        if (pref_idx == 0) return MissileType::Maml;
        if (pref_idx == 1) return MissileType::Som;
        if (pref_idx == 2) return MissileType::Cirit;
    }
    else if (score >= 5) {
        if (pref_idx == 0) return MissileType::Maml;
        if (pref_idx == 1) return MissileType::Cirit;
        if (pref_idx == 2) return MissileType::Som;
    }
    else {
        if (pref_idx == 0) return MissileType::Cirit;
        if (pref_idx == 1) return MissileType::Maml;
        if (pref_idx == 2) return MissileType::Som;
    }
    return MissileType::None;
}
} /* anonymous namespace */

int missile_inventory_allocate_for_cluster(int score, bool has_tank)
{
    if (!g_initialized) {
        return -1;
    }

    int allocated_id = -1;

    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    for (int pref = 0; pref < 3 && allocated_id < 0; ++pref) {
        const MissileType wanted = preferred_for_cluster(score, has_tank, pref);
        if (wanted == MissileType::None) {
            continue;
        }
        for (int i = 0; i < N_MISSILES; ++i) {
            if (g_missiles[i].available && g_missiles[i].type == wanted) {
                g_missiles[i].available = false;
                allocated_id = g_missiles[i].missile_id;
                break;
            }
        }
    }
    tx_mutex_put(&g_mutex);

    return allocated_id;
}

MissileInventoryStats missile_inventory_stats()
{
    MissileInventoryStats out = {0, 0, 0, 0};
    if (!g_initialized) {
        return out;
    }

    tx_mutex_get(&g_mutex, TX_WAIT_FOREVER);
    out.cirit_available = count_no_lock(MissileType::Cirit);
    out.maml_available  = count_no_lock(MissileType::Maml);
    out.som_available   = count_no_lock(MissileType::Som);
    out.total_available = out.cirit_available + out.maml_available + out.som_available;
    tx_mutex_put(&g_mutex);

    return out;
}

} /* namespace omc */
