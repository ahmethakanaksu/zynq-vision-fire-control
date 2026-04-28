/**************************************************************************/
/*                                                                        */
/*            Copyright (c) 1996-2017 by Express Logic Inc.               */
/*                                                                        */
/*  This software is copyrighted by and is the sole property of Express   */
/*  Logic, Inc.  All rights, title, ownership, or other interests         */
/*  in the software remain the property of Express Logic, Inc.  This      */
/*  software may only be used in accordance with the corresponding        */
/*  license agreement.  Any unauthorized use, duplication, transmission,  */
/*  distribution, or disclosure of this software is expressly forbidden.  */
/*                                                                        */
/*  This Copyright notice may not be removed or modified without prior    */
/*  written consent of Express Logic, Inc.                                */
/*                                                                        */
/*  Express Logic, Inc. reserves the right to modify this software        */
/*  without notice.                                                       */
/*                                                                        */
/*  Express Logic, Inc.                     info@expresslogic.com         */
/*  11423 West Bernardo Court               http://www.expresslogic.com   */
/*  San Diego, CA  92127                                                  */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** NetX Component                                                        */
/**                                                                       */
/**   Ethernet device driver for Xilinx Zynq-7000 Cortex-A9 SOC.          */
/**                                                                       */
/**************************************************************************/
/**************************************************************************/

#ifndef NX_DRIVER_ZYNQ_H
#define NX_DRIVER_ZYNQ_H


#ifdef   __cplusplus

/* Yes, C++ compiler is present.  Use standard C.  */
extern   "C" {
#endif


/* Include ThreadX header file, if not already.  */

#ifndef TX_API_H
#include "tx_api.h"
#endif


/* Include NetX header file, if not already.  */

#ifndef NX_API_H
#include "nx_api.h"
#endif


/****** DRIVER SPECIFIC ****** Start of part/vendor specific include area.  Include any such files here!  */

/* Include the Xilinx Processor IP Library header file */
#include "xemacps.h"

#include "xparameters.h"
#include "xparameters_ps.h"
#include "xil_types.h"
#include "xil_io.h"
#include "xil_exception.h"
#include "xil_mmu.h"
#include "xscugic.h"
#include "xemacps.h"
#include "xil_printf.h"
#include "xil_cache.h"
/****** DRIVER SPECIFIC ****** End of part/vendor specific include file area!  */


/* Determine if the driver's source file is being compiled. The constants and typdefs are only valid within
   the driver's source file compilation.  */

#ifdef NX_DRIVER_SOURCE

/* Define generic constants and macros for all NetX Ethernet drivers.  */

#define NX_DRIVER_ETHERNET_IP                   0x0800
#define NX_DRIVER_ETHERNET_IPV6                 0x86dd
#define NX_DRIVER_ETHERNET_ARP                  0x0806
#define NX_DRIVER_ETHERNET_RARP                 0x8035

#define NX_DRIVER_ETHERNET_MTU                  1514
#define NX_DRIVER_ETHERNET_FRAME_SIZE           14

#define NX_DRIVER_DEFERRED_PACKET_RECEIVED      1
#define NX_DRIVER_DEFERRED_DEVICE_RESET         2
#define NX_DRIVER_DEFERRED_PACKET_TRANSMITTED   4
#define NX_DRIVER_DEFERRED_DRIVER_ERROR         8

#define NX_DRIVER_STATE_NOT_INITIALIZED         1
#define NX_DRIVER_STATE_INITIALIZE_FAILED       2
#define NX_DRIVER_STATE_INITIALIZED             3
#define NX_DRIVER_STATE_LINK_ENABLED            4

#ifdef NX_DIRVER_INTERNAL_TRANSMIT_QUEUE
#define NX_DRIVER_MAX_TRANSMIT_QUEUE_DEPTH      10
#endif

#define NX_DRIVER_ERROR                         90


#define NX_DRIVER_ETHERNET_HEADER_REMOVE(p)   \
{   \
    p -> nx_packet_prepend_ptr =  p -> nx_packet_prepend_ptr + NX_DRIVER_ETHERNET_FRAME_SIZE;  \
    p -> nx_packet_length =  p -> nx_packet_length - NX_DRIVER_ETHERNET_FRAME_SIZE;    \
}


/****** DRIVER SPECIFIC ****** Start of part/vendor specific constants area.  Include any such constants here!  */

/* Uncomment the define below to clock the PHY from external 25MHz crystal (only for MII mode) */

#ifdef  MII_MODE
#define PHY_CLOCK_MCO
#endif
   
/* Define the number of descriptors and attached packets for transmit and receive operations. */

#ifndef NX_DRIVER_TX_DESCRIPTORS
#define NX_DRIVER_TX_DESCRIPTORS   256
#endif

#ifndef NX_DRIVER_RX_DESCRIPTORS
#define NX_DRIVER_RX_DESCRIPTORS   64
#endif

/* ETHERNET DMA Rx descriptors Frame Length Shift */

#define ETH_DMARXDESC_FRAME_LENGTHSHIFT           16


/*
#define ENABLE_INTERRUPT_MODERATION
*/
#define INTERRUPT_MODERATION_MAX_TX_PACKETS       4

/****** DRIVER SPECIFIC ****** End of part/vendor specific constant area!  */


/* Define basic Ethernet driver information typedef. Note that this typedefs is designed to be used only
   in the driver's C file. */

typedef struct NX_DRIVER_INFORMATION_STRUCT
{
    /* NetX IP instance that this driver is attached to.  */
    NX_IP               *nx_driver_information_ip_ptr;

    /* Driver's current state.  */
    ULONG               nx_driver_information_state ;  

    /* Packet pool used for receiving packets. */
    NX_PACKET_POOL      *nx_driver_information_packet_pool_ptr;

    /* Define the driver interface association.  */
    NX_INTERFACE        *nx_driver_information_interface;

    /* Define the deferred event field. This will contain bits representing events 
       deferred from the ISR for processing in the thread context.  */
    ULONG               nx_driver_information_deferred_events;

    /****** DRIVER SPECIFIC ****** Start of part/vendor specific driver information area.  Include any such constants here!  */

    /* Indices to current receive/transmit descriptors.  */
    UINT                nx_driver_information_receive_current_index;
    UINT                nx_driver_information_transmit_current_index;

    /* Transmit release index.  */
    UINT                nx_driver_information_transmit_release_index;

    XEmacPs             nx_driver_instance;

#ifdef NX_DIRVER_INTERNAL_TRANSMIT_QUEUE

    /* Define the parameters in the internal driver transmit queue.  The queue is maintained as a singularly
       linked-list with head and tail pointers.  The maximum number of packets on the queue is regulated by 
       the constant NX_DRIVER_MAX_TRANSMIT_QUEUE_DEPTH, which is defined above. When this number is reached,
       the oldest packet is discarded after the new packet is queued.  */
    ULONG               nx_driver_transmit_packets_queued;
    NX_PACKET           nx_driver_transmit_queue_head;
    NX_PACKET           nx_driver_transmit_queue_tail;
#endif

    /* Define the association between buffer descriptors and NetX packets.  */
    NX_PACKET           *nx_driver_information_transmit_packets[NX_DRIVER_TX_DESCRIPTORS];
    NX_PACKET           *nx_driver_information_receive_packets[NX_DRIVER_RX_DESCRIPTORS];

    /****** DRIVER SPECIFIC ****** End of part/vendor specific driver information area.  */

}   NX_DRIVER_INFORMATION;

#endif


/****** DRIVER SPECIFIC ****** Start of part/vendor specific external function prototypes.  A typical NetX Ethernet driver
                               should expose its entry function as well as its interrupt handling function(s) here. All other
                               functions in the driver should have local scope, i.e., defined as static.  */
    
/* Define global driver entry function. */

VOID  nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr);

/* Define global driver interrupt dispatch function.  */

/****** DRIVER SPECIFIC ****** End of part/vendor specific external function prototypes.  */


#ifdef   __cplusplus
/* Yes, C++ compiler is present.  Use standard C.  */
    }
#endif
#endif

