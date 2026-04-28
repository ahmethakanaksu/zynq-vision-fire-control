/*
 * signals.hpp -- QP/C signal numbering for the OMC subsystem.
 *
 * The legacy AO_Controller (app_hsm.h) reserves Q_USER_SIG..Q_USER_SIG+5,
 * so all project signals start at Q_USER_SIG + 100. Each Active Object
 * gets a contiguous block of ten signals for its inbox and gating:
 *
 *   100..109  AO_FrameProcessor
 *   110..119  AO_LsrManager
 *   120..129  AO_Engagement
 *   130..139  AO_MissionController
 *   140..149  AO_Telemetry
 *   150..159  Cluster-mode pipeline
 */

#ifndef OMC_SIGNALS_HPP_
#define OMC_SIGNALS_HPP_

extern "C" {
#include "qpc.h"
}

namespace omc {

enum AppSignal {
    /* AO_FrameProcessor signals */
    FRAME_RECEIVED_SIG          = Q_USER_SIG + 100,
    ENABLE_FRAME_PROCESSOR_SIG  = Q_USER_SIG + 101,
    DISABLE_FRAME_PROCESSOR_SIG = Q_USER_SIG + 102,

    /* AO_LsrManager signals */
    LSR_REQUEST_NEEDED_SIG      = Q_USER_SIG + 110,
    LSR_RESULT_RECEIVED_SIG     = Q_USER_SIG + 111,
    ENABLE_LSR_MANAGER_SIG      = Q_USER_SIG + 112,
    DISABLE_LSR_MANAGER_SIG     = Q_USER_SIG + 113,

    /* AO_Engagement signals */
    TARGET_VERIFIED_SIG         = Q_USER_SIG + 120,
    ENABLE_ENGAGEMENT_SIG       = Q_USER_SIG + 121,
    DISABLE_ENGAGEMENT_SIG      = Q_USER_SIG + 122,

    /* AO_MissionController signals -- top-level command/control */
    ARM_SIG                     = Q_USER_SIG + 130,
    DISARM_SIG                  = Q_USER_SIG + 131,
    MISSION_RESET_SIG           = Q_USER_SIG + 132,
    MODE_NORMAL_SIG             = Q_USER_SIG + 133,
    MODE_CLUSTER_SIG            = Q_USER_SIG + 134,

    /* AO_Telemetry signals */
    TELEMETRY_TICK_SIG          = Q_USER_SIG + 140,
    ENABLE_TELEMETRY_SIG        = Q_USER_SIG + 141,
    DISABLE_TELEMETRY_SIG       = Q_USER_SIG + 142,

    /* Cluster-mode pipeline signals */
    CLUSTER_REQUEST_NEEDED_SIG  = Q_USER_SIG + 150,
    CLUSTER_RESULT_RECEIVED_SIG = Q_USER_SIG + 151,
    CLUSTER_FIRE_DECISION_SIG   = Q_USER_SIG + 152,

    /* End marker */
    OMC_MAX_SIG                 = Q_USER_SIG + 200
};

} /* namespace omc */

#endif /* OMC_SIGNALS_HPP_ */
