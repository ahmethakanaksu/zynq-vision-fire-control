/*
 * app_hsm.h -- public interface for the legacy Controller AO.
 *
 * Exposes the HSM signal enum and the AO handle so udp_command.c
 * (the UDP 5002 command channel) can post events from outside.
 */

#ifndef APP_HSM_H_
#define APP_HSM_H_

#include "qpc.h"

/* Application signals exchanged with AO_Controller. */
enum AppSignals {
    TICK_SIG = Q_USER_SIG,
    ACTIVATE_SIG,
    DEACTIVATE_SIG,
    ALERT_SIG,
    CLEAR_SIG,
    MAX_PUB_SIG
};

/* Opaque handle to the Controller AO. Defined in app_hsm.c. */
extern QActive * const AO_Controller;

/* Public API */
void Controller_ctor(void);
void qp_app_start(void);

#endif /* APP_HSM_H_ */
