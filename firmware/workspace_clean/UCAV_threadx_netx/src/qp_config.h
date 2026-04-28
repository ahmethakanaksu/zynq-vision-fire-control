/*
 * qp_config.h -- QP/C 8.1.4 application configuration.
 *
 * Adapted from qpc-8.1.4/ports/config/qp_config.h with conservative
 * defaults. QP/C runs on top of the QP/ThreadX port; ThreadX is the
 * underlying RTOS scheduler.
 */

#ifndef QP_CONFIG_H_
#define QP_CONFIG_H_

/* QP API: full backwards compatibility */
#define QP_API_VERSION 0

/* Active Object framework */
/* QF_MAX_ACTIVE is forbidden in the QP/ThreadX port: the value is
 * derived from TX_MAX_PRIORITIES inside ports/threadx/qp_port.h, so
 * we deliberately do NOT define it here. */
#define QF_MAX_EPOOL         3U    /* up to 3 event pools */
#define QF_MAX_TICK_RATE     1U    /* one tick rate (10 ms via ThreadX tick) */
#define QF_EVENT_SIZ_SIZE    2U    /* 64K max event size */
#define QF_TIMEEVT_CTR_SIZE  4U    /* 32-bit time event counter */
#define QF_EQUEUE_CTR_SIZE   1U    /* 255 events max in a queue */
#define QF_MPOOL_CTR_SIZE    2U    /* 64K blocks max in pool */
#define QF_MPOOL_SIZ_SIZE    2U    /* 64K bytes max block size */

/* QS software tracing is disabled in this build. */
#define QS_TIME_SIZE         4U
#define QS_CTR_SIZE          2U

#endif /* QP_CONFIG_H_ */
