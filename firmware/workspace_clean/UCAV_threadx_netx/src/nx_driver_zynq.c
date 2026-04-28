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

/* Indicate that driver source is being compiled.  */

#define NX_DRIVER_SOURCE


/****** DRIVER SPECIFIC ****** Start of part/vendor specific include area.  Include driver-specific include file here!  */


/* Determine if the driver uses IP deferred processing or direct ISR processing.  */

#define NX_DRIVER_ENABLE_DEFERRED                /* Define this to enable deferred ISR processing.  */


/* Determine if the packet transmit queue logic is required for this driver.   */

/* No, not required for this driver.  #define NX_DIRVER_INTERNAL_TRANSMIT_QUEUE   */

/* Include driver specific include file.  */
#include "nx_driver_zynq.h"

/****** DRIVER SPECIFIC ****** End of part/vendor specific include file area!  */


/* Define the driver information structure that is only available within this file.  */

static NX_DRIVER_INFORMATION   nx_driver_information;


/****** DRIVER SPECIFIC ****** Start of part/vendor specific data area.  Include hardware-specific data here!  */

/* Define driver specific ethernet hardware address.  */

UCHAR   _nx_driver_hardware_address[] ={0x00, 0x11, 0x22, 0x33, 0x44, 0x56};

/****** DRIVER SPECIFIC ****** End of part/vendor specific data area!  */


/* Define the routines for processing each driver entry request.  The contents of these routines will change with
   each driver. However, the main driver entry function will not change, except for the entry function name.  */
   
static VOID         _nx_driver_interface_attach(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_initialize(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_enable(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_disable(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_packet_send(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_multicast_join(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_multicast_leave(NX_IP_DRIVER *driver_req_ptr);
static VOID         _nx_driver_get_status(NX_IP_DRIVER *driver_req_ptr);

#ifdef NX_DRIVER_ENABLE_DEFERRED
static VOID         _nx_driver_deferred_processing(NX_IP_DRIVER *driver_req_ptr);
#endif

static VOID         _nx_driver_transfer_to_netx(NX_IP *ip_ptr, NX_PACKET *packet_ptr);

#ifdef NX_DIRVER_INTERNAL_TRANSMIT_QUEUE
static VOID         _nx_driver_transmit_packet_enqueue(NX_PACKET *packet_ptr)
static NX_PACKET    *_nx_driver_transmit_packet_dequeue(VOID)
#endif

static VOID          nx_driver_zynq_ethernet_tx_isr(void *handle);
static VOID          nx_driver_zynq_ethernet_rx_isr(void *handle);
static VOID          nx_driver_zynq_ethernet_error_isr(void *handle, UCHAR direction, ULONG error_code);

/* Define the prototypes for the hardware implementation of this driver. The contents of these routines are
   driver-specific.  */

static UINT         _nx_driver_hardware_initialize(NX_IP_DRIVER *driver_req_ptr); 
static UINT         _nx_driver_hardware_enable(NX_IP_DRIVER *driver_req_ptr); 
static UINT         _nx_driver_hardware_disable(NX_IP_DRIVER *driver_req_ptr); 
static UINT         _nx_driver_hardware_packet_send(NX_PACKET *packet_ptr); 
static UINT         _nx_driver_hardware_multicast_join(NX_IP_DRIVER *driver_req_ptr);
static UINT         _nx_driver_hardware_multicast_leave(NX_IP_DRIVER *driver_req_ptr);
static UINT         _nx_driver_hardware_get_status(NX_IP_DRIVER *driver_req_ptr);
static int          _nx_driver_hardware_packet_transmitted(VOID);
static int          _nx_driver_hardware_packet_received(VOID);

extern void         XEmacPs_SetMdioDivisor(XEmacPs *InstancePtr, XEmacPs_MdcDiv Divisor);
extern UINT         Phy_Setup(XEmacPs*);
extern unsigned     configure_IEEE_phy_speed(XEmacPs *xemacpsp, unsigned speed);
extern XScuGic      Gic0;

#define EMACPS_IRPT_INTR      XPS_GEM0_INT_ID
#define EMACPS_SLCR_DIV_MASK  0xFC0FC0FF

/* Define SLCR setting */
#define SLCR_LOCK_ADDR              (XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR            (XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR     (XPS_SYS_CTRL_BASEADDR + 0x140)
#define SLCR_GEM1_CLK_CTRL_ADDR     (XPS_SYS_CTRL_BASEADDR + 0x144)

#define SLCR_LOCK_KEY_VALUE         0x767B
#define SLCR_UNLOCK_KEY_VALUE       0xDF0D
#define SLCR_ADDR_GEM_RST_CTRL      (XPS_SYS_CTRL_BASEADDR + 0x214)

#define RX_BD_LIST_START_ADDRESS    0x0FF00000
#define TX_BD_LIST_START_ADDRESS    0x0FF10000

#define XEMACPS_BD_TO_INDEX(ringptr, bdptr)  (((UINT)bdptr - (UINT)(ringptr)->BaseBdAddr)/(ringptr)->Separation)

/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    nx_driver_zynq                                      PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This is the entry point of the NetX Ethernet Driver. This driver    */ 
/*    function is responsible for initializing the Ethernet controller,   */ 
/*    enabling or disabling the controller as need, preparing             */ 
/*    a packet for transmission, and getting status information.          */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        The driver request from the   */ 
/*                                            IP layer.                   */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_interface_attach           Process attach request        */ 
/*    _nx_driver_initialize                 Process initialize request    */ 
/*    _nx_driver_enable                     Process link enable request   */ 
/*    _nx_driver_disable                    Process link disable request  */ 
/*    _nx_driver_packet_send                Process send packet requests  */ 
/*    _nx_driver_multicast_join             Process multicast join request*/ 
/*    _nx_driver_multicast_leave            Process multicast leave req   */ 
/*    _nx_driver_get_status                 Process get status request    */ 
/*    _nx_driver_deferred_processing        Drive deferred processing     */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    IP layer                                                            */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
/****** DRIVER SPECIFIC ****** Start of part/vendor specific global driver entry function name.  */
VOID  nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr)
/****** DRIVER SPECIFIC ****** End of part/vendor specific global driver entry function name.  */
{
    
    /* Default to successful return.  */
    driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    
    /* Process according to the driver request type in the IP control 
       block.  */
    switch (driver_req_ptr -> nx_ip_driver_command)
    {
        
        case NX_LINK_INTERFACE_ATTACH:
        {
            /* Process link interface attach requests.  */
            _nx_driver_interface_attach(driver_req_ptr);
            break;
        }
        case NX_LINK_INITIALIZE:
        {
            /* Process link initialize requests.  */
            _nx_driver_initialize(driver_req_ptr);
            break;
        }
        
        case NX_LINK_ENABLE:
        {
            /* Process link enable requests.  */
            _nx_driver_enable(driver_req_ptr);
            break;
        }
        
        case NX_LINK_DISABLE:
        {
            /* Process link disable requests.  */
            _nx_driver_disable(driver_req_ptr);
            break;
        }
        
        case NX_LINK_ARP_SEND:
        case NX_LINK_ARP_RESPONSE_SEND:
        case NX_LINK_PACKET_BROADCAST:
        case NX_LINK_RARP_SEND:
        case NX_LINK_PACKET_SEND:
        {
            /* Process packet send requests.  */
            _nx_driver_packet_send(driver_req_ptr);
            break;
        }
        
        case NX_LINK_MULTICAST_JOIN:
        {
            /* Process multicast join requests.  */
            _nx_driver_multicast_join(driver_req_ptr);
            break;
        }
        
        case NX_LINK_MULTICAST_LEAVE:
        {
            /* Process multicast leave requests.  */
            _nx_driver_multicast_leave(driver_req_ptr);
            break;
        }
        
        case NX_LINK_GET_STATUS:
        {
            /* Process get status requests.  */
            _nx_driver_get_status(driver_req_ptr);
            break;
        }
        
        case NX_LINK_DEFERRED_PROCESSING:
        {
            /* Process driver deferred requests.  */
            _nx_driver_deferred_processing(driver_req_ptr);
            break;
        }

        default:
        {
            /* Invalid driver request.  */
            /* Return the unhandled command status.  */
            driver_req_ptr -> nx_ip_driver_status =  NX_UNHANDLED_COMMAND;

            /* Return error.  */
            driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
        }
    
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_interface_attach                         PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the interface attach request.              */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_interface_attach(NX_IP_DRIVER *driver_req_ptr)
{


    /* Setup the driver's interface.  This example is for a simple one-interface
       Ethernet driver. Additional logic is necessary for multiple port devices.  */
    nx_driver_information.nx_driver_information_interface =  driver_req_ptr -> nx_ip_driver_interface;

    /* Return successful status.  */
    driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_initialize                               PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the initialize request.  The processing    */ 
/*    in this function is generic. All ethernet controller logic is to    */ 
/*    be placed in _nx_driver_hardware_initialize.                        */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_initialize        Process initialize request    */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_initialize(NX_IP_DRIVER *driver_req_ptr)
{

NX_IP           *ip_ptr;
NX_INTERFACE    *interface_ptr;
UINT            status;
    
    /* Setup the IP pointer from the driver request.  */
    ip_ptr =  driver_req_ptr -> nx_ip_driver_ptr;
    
    /* Setup interface pointer.  */
    interface_ptr = driver_req_ptr -> nx_ip_driver_interface;

    /* Initialize the driver's information structure.  */

    /* Default IP pointer to NULL.  */
    nx_driver_information.nx_driver_information_ip_ptr =               NX_NULL;

    /* Setup the driver state to not initialized.  */
    nx_driver_information.nx_driver_information_state =                NX_DRIVER_STATE_NOT_INITIALIZED;

    /* Setup the default packet pool for the driver's received packets.  */
    nx_driver_information.nx_driver_information_packet_pool_ptr = ip_ptr -> nx_ip_default_packet_pool;

    /* Clear the deferred events for the driver.  */
    nx_driver_information.nx_driver_information_deferred_events =       0;

#ifdef NX_DIRVER_INTERNAL_TRANSMIT_QUEUE

    /* Clear the transmit queue count and head pointer.  */
    nx_driver_information.nx_driver_transmit_packets_queued =  0;
    nx_driver_information.nx_driver_transmit_queue_head =      NX_NULL;
    nx_driver_information.nx_driver_transmit_queue_tail =      NX_NULL;
#endif

    /* Call the hardware-specific ethernet controller initialization.  */
    status =  _nx_driver_hardware_initialize(driver_req_ptr);

    if(status == NX_SUCCESS)
    {

        /* Setup driver information to point to IP pointer.  */
        nx_driver_information.nx_driver_information_ip_ptr = driver_req_ptr -> nx_ip_driver_ptr;
        
        /* Setup the link maximum transfer unit. */
        interface_ptr -> nx_interface_ip_mtu_size =  NX_DRIVER_ETHERNET_MTU - NX_DRIVER_ETHERNET_FRAME_SIZE;
        
        /* Setup the physical address of this IP instance.  Increment the 
           physical address lsw to simulate multiple nodes hanging on the
           ethernet.  */
        interface_ptr -> nx_interface_physical_address_msw =  
            (ULONG)((_nx_driver_hardware_address[0] << 8) | (_nx_driver_hardware_address[1]));
        interface_ptr -> nx_interface_physical_address_lsw =  
            (ULONG)((_nx_driver_hardware_address[2] << 24) | (_nx_driver_hardware_address[3] << 16) | 
                    (_nx_driver_hardware_address[4] << 8) | (_nx_driver_hardware_address[5]));
        
        /* Indicate to the IP software that IP to physical mapping
           is required.  */
        interface_ptr -> nx_interface_address_mapping_needed =  NX_TRUE;
        
        /* Move the driver's state to initialized.  */
        nx_driver_information.nx_driver_information_state = NX_DRIVER_STATE_INITIALIZED;
        
        /* Indicate successful initialize.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
        
    }

    else
    {
        
        /* Initialization failed.  Indicate that the request failed.  */
        driver_req_ptr -> nx_ip_driver_status =   NX_DRIVER_ERROR;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_enable                                   PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the initialize request. The processing     */ 
/*    in this function is generic. All ethernet controller logic is to    */ 
/*    be placed in _nx_driver_hardware_enable.                            */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_enable            Process enable request        */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_enable(NX_IP_DRIVER *driver_req_ptr)
{

NX_IP           *ip_ptr;
UINT            status;


    /* Setup the IP pointer from the driver request.  */
    ip_ptr =  driver_req_ptr -> nx_ip_driver_ptr;
    
    /* See if we can honor the NX_LINK_ENABLE request.  */
    if (nx_driver_information.nx_driver_information_state < NX_DRIVER_STATE_INITIALIZED)
    {

        /* Mark the request as not successful.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
        return;
    }
        
    /* Check if it is enabled by someone already */
    if (nx_driver_information.nx_driver_information_state >=  NX_DRIVER_STATE_LINK_ENABLED)
    {

        /* Yes, the request has already been made.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_ALREADY_ENABLED;
        return;
    }

    /* Call hardware specific enable.  */
    status =  _nx_driver_hardware_enable(driver_req_ptr);

    /* Was the hardware enable successful?  */
    if (status == NX_SUCCESS)
    {

        /* Update the driver state to link enabled.  */
        nx_driver_information.nx_driver_information_state = NX_DRIVER_STATE_LINK_ENABLED;

        /* Mark request as successful.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
        
        /* Mark the IP instance as link up.  */
        ip_ptr -> nx_ip_driver_link_up =  NX_TRUE;
    }
    else
    {
        
        /* Enable failed.  Indicate that the request failed.  */
        driver_req_ptr -> nx_ip_driver_status =   NX_DRIVER_ERROR;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_disable                                  PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the disable request. The processing        */ 
/*    in this function is generic. All ethernet controller logic is to    */ 
/*    be placed in _nx_driver_hardware_disable.                           */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_disable           Process disable request       */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_disable(NX_IP_DRIVER *driver_req_ptr)
{

NX_IP           *ip_ptr;
UINT            status;


    /* Setup the IP pointer from the driver request.  */
    ip_ptr =  driver_req_ptr -> nx_ip_driver_ptr;

    /* Check if the link is enabled.  */
    if (nx_driver_information.nx_driver_information_state !=  NX_DRIVER_STATE_LINK_ENABLED)
    {

        /* The link is not enabled, so just return an error.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
        return;
    }

    /* Call hardware specific disable.  */
    status =  _nx_driver_hardware_disable(driver_req_ptr);

    /* Was the hardware disable successful?  */
    if (status == NX_SUCCESS)
    {
    
        /* Mark the IP instance as link down.  */
        ip_ptr -> nx_ip_driver_link_up =  NX_FALSE;

        /* Update the driver state back to initialized.  */
        nx_driver_information.nx_driver_information_state =  NX_DRIVER_STATE_INITIALIZED;
        
        /* Mark request as successful.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    }
    else
    {

        /* Disable failed, return an error.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_packet_send                              PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the packet send request. The processing    */ 
/*    in this function is generic. All ethernet controller packet send    */ 
/*    logic is to be placed in _nx_driver_hardware_packet_send.           */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_send       Process packet send request   */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_packet_send(NX_IP_DRIVER *driver_req_ptr)
{

NX_IP           *ip_ptr;
NX_PACKET       *packet_ptr;
ULONG           *ethernet_frame_ptr;
UINT            status;


    /* Setup the IP pointer from the driver request.  */
    ip_ptr =  driver_req_ptr -> nx_ip_driver_ptr;

    /* Place the ethernet frame at the front of the packet.  */
    packet_ptr =  driver_req_ptr -> nx_ip_driver_packet;

    
    /* Check to make sure the link is up.  */
    if (nx_driver_information.nx_driver_information_state != NX_DRIVER_STATE_LINK_ENABLED)
    {
        /* Remove the Ethernet header.  */
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);

        /* Invalidate an unsuccessful packet send.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;

        /* Link is not up, simply free the packet.  */
        nx_packet_transmit_release(driver_req_ptr -> nx_ip_driver_packet);
        return;
    }
    
    /* Process driver send packet.  */
    
    /* Adjust the prepend pointer.  */
    packet_ptr -> nx_packet_prepend_ptr =  
            packet_ptr -> nx_packet_prepend_ptr - NX_DRIVER_ETHERNET_FRAME_SIZE;
        
    /* Adjust the packet length.  */
    packet_ptr -> nx_packet_length = packet_ptr -> nx_packet_length + NX_DRIVER_ETHERNET_FRAME_SIZE;
    
    /* Setup the ethernet frame pointer to build the ethernet frame.  Backup another 2
      * bytes to get 32-bit word alignment.  */
    ethernet_frame_ptr =  (ULONG *) (packet_ptr -> nx_packet_prepend_ptr - 2);
    
    /* Set up the hardware addresses in the Ethernet header. */
    *ethernet_frame_ptr       =  driver_req_ptr -> nx_ip_driver_physical_address_msw;
    *(ethernet_frame_ptr + 1) =  driver_req_ptr -> nx_ip_driver_physical_address_lsw;
    
    *(ethernet_frame_ptr + 2) =  (ip_ptr -> nx_ip_arp_physical_address_msw << 16) |
        (ip_ptr -> nx_ip_arp_physical_address_lsw >> 16);
    *(ethernet_frame_ptr + 3) =  (ip_ptr -> nx_ip_arp_physical_address_lsw << 16);

    /* Set up the frame type field in the Ethernet harder. */
    if ((driver_req_ptr -> nx_ip_driver_command == NX_LINK_ARP_SEND)||
        (driver_req_ptr -> nx_ip_driver_command == NX_LINK_ARP_RESPONSE_SEND))
    {

        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_ARP;
    }
    else if(driver_req_ptr -> nx_ip_driver_command == NX_LINK_RARP_SEND)
    {

        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_RARP;
    }

#ifdef FEATURE_NX_IPV6
    else if(packet_ptr -> nx_packet_ip_version == NX_IP_VERSION_V6)
    {

        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_IPV6;
    }
#endif

    else
    {

        *(ethernet_frame_ptr + 3) |= NX_DRIVER_ETHERNET_IP;
    }

    /* Endian swapping if NX_LITTLE_ENDIAN is defined.  */
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 1));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 2));
    NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 3));
    
    /* Determine if the packet exceeds the driver's MTU.  */
    if (packet_ptr -> nx_packet_length > NX_DRIVER_ETHERNET_MTU)
    {
    
        /* This packet exceeds the size of the driver's MTU. Simply throw it away! */

        /* Remove the Ethernet header.  */
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
        
        /* Indicate an unsuccessful packet send.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;

        /* Link is not up, simply free the packet.  */
        nx_packet_transmit_release(packet_ptr);
        return;
    }

    /* Transmit the packet through the Ethernet controller low level access routine. */
    status = _nx_driver_hardware_packet_send(packet_ptr);

    /* Determine if there was an error.  */
    if (status != NX_SUCCESS)
    {

        /* Driver's hardware send packet routine failed to send the packet.  */

        /* Remove the Ethernet header.  */
        NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
        
        /* Indicate an unsuccessful packet send.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;

        /* Link is not up, simply free the packet.  */
        nx_packet_transmit_release(packet_ptr);
    }
    else
    {

        /* Set the status of the request.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_multicast_join                           PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the multicast join request. The processing */ 
/*    in this function is generic. All ethernet controller multicast join */ 
/*    logic is to be placed in _nx_driver_hardware_multicast_join.        */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_multicast_join    Process multicast join request*/ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_multicast_join(NX_IP_DRIVER *driver_req_ptr)
{

UINT        status;


    /* Call hardware specific multicast join function. */
    status =  _nx_driver_hardware_multicast_join(driver_req_ptr);
    
    /* Determine if there was an error.  */
    if (status != NX_SUCCESS)
    {

        /* Indicate an unsuccessful request.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
    }
    else
    {

        /* Indicate the request was successful.   */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_multicast_leave                          PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the multicast leave request. The           */ 
/*    processing in this function is generic. All ethernet controller     */ 
/*    multicast leave logic is to be placed in                            */ 
/*    _nx_driver_hardware_multicast_leave.                                */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_multicast_leave   Process multicast leave req   */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_multicast_leave(NX_IP_DRIVER *driver_req_ptr)
{

UINT        status;


    /* Call hardware specific multicast leave function. */
    status =  _nx_driver_hardware_multicast_leave(driver_req_ptr);
    
    /* Determine if there was an error.  */
    if (status != NX_SUCCESS)
    {

        /* Indicate an unsuccessful request.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
    }
    else
    {

        /* Indicate the request was successful.   */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    }
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_get_status                               PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the get status request. The processing     */ 
/*    in this function is generic. All ethernet controller get status     */ 
/*    logic is to be placed in _nx_driver_hardware_get_status.            */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_hardware_get_status        Process get status request    */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_get_status(NX_IP_DRIVER *driver_req_ptr)
{

UINT        status;


    /* Call hardware specific get status function. */
    status =  _nx_driver_hardware_get_status(driver_req_ptr);
    
    /* Determine if there was an error.  */
    if (status != NX_SUCCESS)
    {

        /* Indicate an unsuccessful request.  */
        driver_req_ptr -> nx_ip_driver_status =  NX_DRIVER_ERROR;
    }
    else
    {

        /* Indicate the request was successful.   */
        driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;
    }
}


#ifdef NX_DRIVER_ENABLE_DEFERRED
/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_deferred_processing                      PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    XC, Express Logic, Inc.                                             */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing the deferred ISR action within the context */ 
/*    of the IP thread.                                                   */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver command from the IP    */ 
/*                                            thread                      */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_packet_transmitted        Clean up after transmission    */ 
/*    _nx_driver_packet_received           Process a received packet      */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    Driver entry function                                               */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  _nx_driver_deferred_processing(NX_IP_DRIVER *driver_req_ptr)
{

TX_INTERRUPT_SAVE_AREA

ULONG       deferred_events;


    /* Disable interrupts.  */
    TX_DISABLE

    /* Pickup deferred events.  */
    deferred_events =  nx_driver_information.nx_driver_information_deferred_events;
    nx_driver_information.nx_driver_information_deferred_events =  0;

    /* Restore interrupts.  */
    TX_RESTORE
    
    /* Check for a transmit complete event.  */
    if(deferred_events & NX_DRIVER_DEFERRED_PACKET_TRANSMITTED)
    {
    
        /* Process transmitted packet(s).  */
        _nx_driver_hardware_packet_transmitted();
    }

    /* Check for received packet.  */
    if(deferred_events & NX_DRIVER_DEFERRED_PACKET_RECEIVED)
    {
    
        /* Process received packet(s).  */
        _nx_driver_hardware_packet_received();
    }

    /* Mark request as successful.  */
    driver_req_ptr->nx_ip_driver_status =  NX_SUCCESS;
}
#endif


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_transfer_to_netx                         PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing incoming packets.  This routine would      */ 
/*    be called from the driver-specific receive packet processing        */ 
/*    function _nx_driver_hardware.                                       */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    ip_ptr                                Pointer to IP protocol block  */ 
/*    packet_ptr                            Packet pointer                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    Error indication                                                    */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_ip_packet_receive                 NetX IP packet receive        */ 
/*    _nx_ip_packet_deferred_receive        NetX IP packet receive        */ 
/*    _nx_arp_packet_deferred_receive       NetX ARP packet receive       */ 
/*    _nx_rarp_packet_deferred_receive      NetX RARP packet receive      */ 
/*    _nx_packet_release                    Release packet                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_received   Driver packet receive function*/ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID _nx_driver_transfer_to_netx(NX_IP *ip_ptr, NX_PACKET *packet_ptr)
{
USHORT    packet_type;


    packet_ptr -> nx_packet_ip_interface = nx_driver_information.nx_driver_information_interface;

    /* Pickup the packet header to determine where the packet needs to be
       sent.  */
    packet_type =  (USHORT)(((UINT) (*(packet_ptr -> nx_packet_prepend_ptr+12))) << 8) | 
        ((UINT) (*(packet_ptr -> nx_packet_prepend_ptr+13)));


    /* Route the incoming packet according to its ethernet type.  */
    if (packet_type == NX_DRIVER_ETHERNET_IP || packet_type == NX_DRIVER_ETHERNET_IPV6)
    {
        /* Note:  The length reported by some Ethernet hardware includes 
           bytes after the packet as well as the Ethernet header.  In some 
           cases, the actual packet length after the Ethernet header should 
           be derived from the length in the IP header (lower 16 bits of
           the first 32-bit word).  */

        /* Clean off the Ethernet header.  */
        packet_ptr -> nx_packet_prepend_ptr =  
            packet_ptr -> nx_packet_prepend_ptr + NX_DRIVER_ETHERNET_FRAME_SIZE;
 
        /* Adjust the packet length.  */
        packet_ptr -> nx_packet_length =  
            packet_ptr -> nx_packet_length - NX_DRIVER_ETHERNET_FRAME_SIZE;

        /* Route to the ip receive function.  */
#ifdef NX_DRIVER_ENABLE_DEFERRED
        _nx_ip_packet_deferred_receive(ip_ptr, packet_ptr);
#else
        _nx_ip_packet_receive(ip_ptr, packet_ptr);
#endif
    }
    else if (packet_type == NX_DRIVER_ETHERNET_ARP)
    {


        /* Clean off the Ethernet header.  */
        packet_ptr -> nx_packet_prepend_ptr =  
            packet_ptr -> nx_packet_prepend_ptr + NX_DRIVER_ETHERNET_FRAME_SIZE;

        /* Adjust the packet length.  */
        packet_ptr -> nx_packet_length =  
            packet_ptr -> nx_packet_length - NX_DRIVER_ETHERNET_FRAME_SIZE;

        /* Route to the ARP receive function.  */
        _nx_arp_packet_deferred_receive(ip_ptr, packet_ptr);
    }
    else if (packet_type == NX_DRIVER_ETHERNET_RARP)
    {

        /* Clean off the Ethernet header.  */
        packet_ptr -> nx_packet_prepend_ptr =  
            packet_ptr -> nx_packet_prepend_ptr + NX_DRIVER_ETHERNET_FRAME_SIZE;

        /* Adjust the packet length.  */
        packet_ptr -> nx_packet_length =  
            packet_ptr -> nx_packet_length - NX_DRIVER_ETHERNET_FRAME_SIZE;

        /* Route to the RARP receive function.  */
        _nx_rarp_packet_deferred_receive(ip_ptr, packet_ptr);
    }
    else
    {

        /* Invalid ethernet header... release the packet.  */
        nx_packet_release(packet_ptr);
    }

}


#ifdef NX_DIRVER_INTERNAL_TRANSMIT_QUEUE
/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_transmit_packet_enqueue                  PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function queues a transmit packet when the hardware transmit   */ 
/*    queue does not have the resources (buffer descriptors, etc.) to     */ 
/*    send the packet.  The queue is maintained as a singularly linked-   */ 
/*    list with head and tail pointers. The maximum number of packets on  */ 
/*    the transmit queue is regulated by the constant                     */ 
/*    NX_DRIVER_MAX_TRANSMIT_QUEUE_DEPTH. When this number is exceeded,   */ 
/*    the oldest packet is discarded after the new packet is queued.      */ 
/*                                                                        */ 
/*    Note: that it is assumed further driver interrupts are locked out   */ 
/*    during the call to this driver utility.                             */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    packet_ptr                            Packet pointer                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_packet_transmit_release           Release packet                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_send       Driver packet send function   */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID _nx_driver_transmit_packet_enqueue(NX_PACKET *packet_ptr)
{

}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_transmit_packet_dequeue                  PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function removes the oldest transmit packet when the hardware  */ 
/*    transmit queue has new resources (usually after a transmit complete */ 
/*    interrupt) to send the packet. If there are no packets in the       */ 
/*    transmit queue, a NULL is returned.                                 */ 
/*                                                                        */ 
/*    Note: that it is assumed further driver interrupts are locked out   */ 
/*    during the call to this driver utility.                             */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    packet_ptr                            Packet pointer                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_send       Driver packet send function   */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static NX_PACKET *_nx_driver_transmit_packet_dequeue(VOID)
{

    return(NX_NULL);

}

#endif



/****** DRIVER SPECIFIC ****** Start of part/vendor specific internal driver functions.  */


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_initialize                      PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific initialization.           */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    ETH_BSP_Config                        Configure Ethernet            */ 
/*    ETH_MACAddressConfig                  Setup MAC address             */ 
/*    ETH_DMARxDescReceiveITConfig          Enable receive descriptors    */ 
/*    nx_packet_allocate                    Allocate receive packet(s)    */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_initialize                 Driver initialize processing  */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            invalidate receive buffer   */ 
/*                                            cache, initialize tx driver */ 
/*                                            packets,                    */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_initialize(NX_IP_DRIVER *driver_req_ptr)
{

UINT                i;
XEmacPs_Bd          *TxDesc_ptr;
XEmacPs_Bd          *RxDesc_ptr;
XEmacPs             *instance_ptr;
XEmacPs_Config       *config;
int                  ret;
XEmacPs_Bd           BdTemplate;
int                  SlcrTxClkCntrl;
int                  link_speed;
NX_PACKET           *packet_ptr;


    /* SLCR unlock */
    *(volatile UINT*)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;

    /* Set up GEM0 1G clock configuration. */
    SlcrTxClkCntrl = *(volatile UINT*)(SLCR_GEM0_CLK_CTRL_ADDR);
    SlcrTxClkCntrl &= EMACPS_SLCR_DIV_MASK;
    SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 << 20);
    SlcrTxClkCntrl |= (XPAR_PS7_ETHERNET_0_ENET_SLCR_1000MBPS_DIV1 << 8);
    *(volatile UINT*)(SLCR_GEM0_CLK_CTRL_ADDR) = SlcrTxClkCntrl;

    /* SLCR lock */
    *(volatile UINT*)(SLCR_UNLOCK_ADDR) = SLCR_LOCK_KEY_VALUE;

    tx_thread_sleep(100);

    config = XEmacPs_LookupConfig(XPAR_PS7_ETHERNET_0_DEVICE_ID);
    instance_ptr = &nx_driver_information.nx_driver_instance;

    ret = XEmacPs_CfgInitialize(instance_ptr, config, config -> BaseAddress);
    Xil_AssertNonvoid(ret == XST_SUCCESS);


    /* Configure the MAC address */
    ret = XEmacPs_SetMacAddress(instance_ptr, _nx_driver_hardware_address, 1);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    ret = XEmacPs_SetHandler(instance_ptr, XEMACPS_HANDLER_DMASEND,
                             (void*)nx_driver_zynq_ethernet_tx_isr,
                             instance_ptr);

    Xil_AssertNonvoid(ret == XST_SUCCESS);


    ret = XEmacPs_SetHandler(instance_ptr, XEMACPS_HANDLER_DMARECV,
                             (void*)nx_driver_zynq_ethernet_rx_isr,
                             instance_ptr);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    ret = XEmacPs_SetHandler(instance_ptr, XEMACPS_HANDLER_ERROR,
                             (void*)nx_driver_zynq_ethernet_error_isr,
                             instance_ptr);

    Xil_AssertNonvoid(ret == XST_SUCCESS);


    XEmacPs_BdClear(&BdTemplate);

    RxDesc_ptr = (XEmacPs_Bd*)RX_BD_LIST_START_ADDRESS;

    /* Create the RX DMA channel */
    ret = XEmacPs_BdRingCreate(&(XEmacPs_GetRxRing(instance_ptr)), (u32)RxDesc_ptr, (u32)RxDesc_ptr, XEMACPS_BD_ALIGNMENT, NX_DRIVER_RX_DESCRIPTORS);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    ret = XEmacPs_BdRingClone(&XEmacPs_GetRxRing(instance_ptr), &BdTemplate, XEMACPS_RECV);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    /* Assign receive buffer addresses. */
    for(i = 0; i < NX_DRIVER_RX_DESCRIPTORS; i++)
    {
        ret = XEmacPs_BdRingAlloc(&(XEmacPs_GetRxRing(instance_ptr)), 1, &RxDesc_ptr);
        Xil_AssertNonvoid(ret == XST_SUCCESS);

        ret = nx_packet_allocate(nx_driver_information.nx_driver_information_packet_pool_ptr, &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);
        if(ret != NX_SUCCESS)
            return(ret);

        XEmacPs_BdSetAddressRx(RxDesc_ptr, packet_ptr -> nx_packet_prepend_ptr);

        ret = XEmacPs_BdRingToHw(&(XEmacPs_GetRxRing(instance_ptr)), 1, RxDesc_ptr);
        Xil_AssertNonvoid(ret == XST_SUCCESS);
    
        /* Invalidate cache so that physical memory will be read. */
        Xil_DCacheInvalidateRange((UINT)packet_ptr -> nx_packet_prepend_ptr, NX_DRIVER_ETHERNET_MTU);

        nx_driver_information.nx_driver_information_receive_packets[i] = packet_ptr;
    }

    /* Initialize TX packet pointers. */
    for(i = 0; i < NX_DRIVER_TX_DESCRIPTORS; i++)
    {
        nx_driver_information.nx_driver_information_transmit_packets[i] = NX_NULL;
    }

    /* Configure the RX Offset */
    ret = XEmacPs_ReadReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCFG_OFFSET);
    /* Clear bit 14 and 15 */
    ret = ret & (~ (3 << 14));
    /* Set bit 14 and 15 */
    ret = ret | (2 << 14);
    /* Write it back to NWCFG register. */
    XEmacPs_WriteReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCFG_OFFSET, ret);


    XEmacPs_BdClear(&BdTemplate);
    XEmacPs_BdSetStatus(&BdTemplate, XEMACPS_TXBUF_USED_MASK);
    TxDesc_ptr = (XEmacPs_Bd*)TX_BD_LIST_START_ADDRESS;
    /* Create the TX DMA channel */
    ret = XEmacPs_BdRingCreate(&(XEmacPs_GetTxRing(instance_ptr)), (u32)TxDesc_ptr, (u32)TxDesc_ptr, XEMACPS_BD_ALIGNMENT, NX_DRIVER_TX_DESCRIPTORS);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    ret = XEmacPs_BdRingClone(&XEmacPs_GetTxRing(instance_ptr), &BdTemplate, XEMACPS_SEND);
    Xil_AssertNonvoid(ret == XST_SUCCESS);


    /* Set MDIO divisor */
    XEmacPs_SetMdioDivisor(instance_ptr, MDC_DIV_224);


    /* Set the operation speed */
    link_speed = Phy_Setup(instance_ptr);
    xil_printf("link_speed is %d\n\r", link_speed);

    configure_IEEE_phy_speed(instance_ptr, link_speed);

    XEmacPs_SetOperatingSpeed(instance_ptr, link_speed);

    tx_thread_sleep(100);

    /* Setup the interrupt controller and enable interrupts */
    ret = XScuGic_Connect(&Gic0,  EMACPS_IRPT_INTR, (Xil_InterruptHandler)XEmacPs_IntrHandler,
                          (void*)instance_ptr);

    /* Default to successful return.  */
    driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;

    /* Setup indices.  */
    nx_driver_information.nx_driver_information_receive_current_index = 0;
    nx_driver_information.nx_driver_information_transmit_current_index = 0;
    nx_driver_information.nx_driver_information_transmit_release_index = 0;

    /* Make sure there are receive packets... otherwise, return an error.  */
    if (nx_driver_information.nx_driver_information_packet_pool_ptr == NULL)
    {

        /* There must be receive packets. If not, return an error!  */
        return(NX_DRIVER_ERROR);
    }

    /* Enable transmit and receive checksum computation */
    XEmacPs_SetOptions(instance_ptr, (XEMACPS_RX_CHKSUM_ENABLE_OPTION | XEMACPS_TX_CHKSUM_ENABLE_OPTION |XEMACPS_MULTICAST_OPTION)|XEMACPS_PROMISC_OPTION);

    XEmacPs_WriteReg(instance_ptr -> Config.BaseAddress, XEMACPS_DMACR_OFFSET, 0x00190F10);

    /* Return success!  */
    return(NX_SUCCESS);
} 


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_enable                          PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific link enable requests.     */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    ETH_Start                             Start Ethernet operation      */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_enable                     Driver link enable processing */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_enable(NX_IP_DRIVER *driver_req_ptr)
{
XEmacPs    *instance_ptr;

    /* Enable interrupt */
    XScuGic_Enable(&Gic0, XPS_GEM0_INT_ID);

    instance_ptr = &nx_driver_information.nx_driver_instance;
    /* Start the device. */
    XEmacPs_Start(instance_ptr);

    /* Start the receiver. */

    xil_printf("driver_hardware_enable done. (%s:%d)\r\n", __FILE__, __LINE__);

    /* Return success!  */
    return(NX_SUCCESS);
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_disable                         PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific link disable requests.    */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_disable                    Driver link disable processing*/ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_disable(NX_IP_DRIVER *driver_req_ptr)
{

    /* Return success!  */
    return(NX_SUCCESS);
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_send                     PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific packet send requests.     */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    packet_ptr                            Pointer to packet to send     */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_packet_send                Driver packet send processing */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_packet_send(NX_PACKET *packet_ptr)
{
int         ret;
XEmacPs     *instance_ptr;
XEmacPs_Bd  *bdSetPtr;
NX_PACKET   *tmp_ptr;
ULONG       curIdx;
TX_INTERRUPT_SAVE_AREA

    instance_ptr = &nx_driver_information.nx_driver_instance;

    TX_DISABLE

    /* Allocate a transmit BD */
    ret = XEmacPs_BdRingAlloc(&(XEmacPs_GetTxRing(instance_ptr)), 1, &bdSetPtr);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    for(tmp_ptr = packet_ptr; tmp_ptr != NX_NULL; tmp_ptr = tmp_ptr -> nx_packet_next)
    {
        /* Pick up the first BD. */
        curIdx = nx_driver_information.nx_driver_information_transmit_current_index;
        /* Save the pkt pointer to release.  */
        if(nx_driver_information.nx_driver_information_transmit_packets[curIdx] == NX_NULL)
        {
            nx_driver_information.nx_driver_information_transmit_packets[curIdx] = tmp_ptr;
        }
        else
        {
            /* Error! */
            xil_printf("tx non-null ptr fail\n\r");
        }

        /* Set the current index to the next descriptor.  */
        nx_driver_information.nx_driver_information_transmit_current_index = (curIdx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);

        Xil_DCacheFlushRange((u32)(packet_ptr -> nx_packet_prepend_ptr), packet_ptr -> nx_packet_length);

        XEmacPs_BdSetAddressTx(bdSetPtr, packet_ptr -> nx_packet_prepend_ptr);

        XEmacPs_BdSetLength(bdSetPtr, packet_ptr -> nx_packet_length);
        XEmacPs_BdClearLast(bdSetPtr);
        dmb();
        dsb();
    }

    XEmacPs_BdSetLast(bdSetPtr);
    dmb();
    dsb();

    /* Send the packet to the transmit logic. */
    ret = XEmacPs_BdRingToHw(&(XEmacPs_GetTxRing(instance_ptr)), 1, bdSetPtr);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    XEmacPs_BdClearTxUsed(bdSetPtr);

    XEmacPs_Transmit(instance_ptr);

    dmb();
    dsb();

    TX_RESTORE
    return(NX_SUCCESS);

}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_multicast_join                  PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific multicast join requests.  */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_multicast_join             Driver multicast join         */ 
/*                                            processing                  */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_multicast_join(NX_IP_DRIVER *driver_req_ptr)
{

    /* Return success.  */
    return(NX_SUCCESS);
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_multicast_leave                 PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific multicast leave requests. */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_multicast_leave            Driver multicast leave        */ 
/*                                            processing                  */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_multicast_leave(NX_IP_DRIVER *driver_req_ptr)
{

    /* Return success.  */
    return(NX_SUCCESS);
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_get_status                      PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes hardware-specific get status requests.      */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    driver_req_ptr                        Driver request pointer        */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    status                                [NX_SUCCESS|NX_DRIVER_ERROR]  */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_get_status                 Driver get status processing  */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static UINT  _nx_driver_hardware_get_status(NX_IP_DRIVER *driver_req_ptr)
{

    /* Return success.  */
    return(NX_SUCCESS);
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_transmitted              PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes packets transmitted by the ethernet         */ 
/*    controller.                                                         */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    nx_packet_transmit_release            Release transmitted packet    */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_deferred_processing        Deferred driver processing    */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            use driver info to get      */ 
/*                                            packet pointer,             */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static int  _nx_driver_hardware_packet_transmitted(VOID)
{

XEmacPs_Bd *bdSetPtr;
XEmacPs_Bd *currBdPtr;
int         numBds;
XEmacPs    *instance_ptr;
ULONG      *value;
int         i;
UINT        ret;
NX_PACKET  *packet_ptr;
ULONG idx = nx_driver_information.nx_driver_information_transmit_release_index;
TX_INTERRUPT_SAVE_AREA

    instance_ptr = &nx_driver_information.nx_driver_instance;

    TX_DISABLE
    numBds = XEmacPs_BdRingFromHwTx(&(XEmacPs_GetTxRing(instance_ptr)), NX_DRIVER_TX_DESCRIPTORS, &bdSetPtr);
    if(numBds == 0)
    {
        TX_RESTORE
        return 0;
    }

    currBdPtr = bdSetPtr;

    for(i = 0; i < numBds; i++)
    {
        value = (ULONG*)currBdPtr;

        /* Get packet pointer from packet buffer. */
        packet_ptr = nx_driver_information.nx_driver_information_transmit_packets[idx];

        /* Clear the entry in the in-use array.  */
        nx_driver_information.nx_driver_information_transmit_packets[idx] = NX_NULL;

        /* Update and rollover index. */
        idx = (idx + 1) & (NX_DRIVER_TX_DESCRIPTORS - 1);
        nx_driver_information.nx_driver_information_transmit_release_index = idx;

        /* Remove the Ethernet header and release packet. */
        if(packet_ptr != NX_NULL)
        {
            NX_DRIVER_ETHERNET_HEADER_REMOVE(packet_ptr);
            nx_packet_transmit_release(packet_ptr);
        }

        *value = 0;
        value++;
    
        if(XEMACPS_BD_TO_INDEX(&(XEmacPs_GetTxRing(instance_ptr)),currBdPtr) == (NX_DRIVER_TX_DESCRIPTORS - 1))
            *value = 0xC0000000;
        else
            *value = 0x80000000;

        currBdPtr = XEmacPs_BdRingNext(&(XEmacPs_GetTxRing(instance_ptr)), currBdPtr);
    
        dmb();
        dsb();
    }

    /* Free this BD */
    ret = XEmacPs_BdRingFree(&(XEmacPs_GetTxRing(instance_ptr)), numBds, bdSetPtr);
    Xil_AssertNonvoid(ret == XST_SUCCESS);

    TX_RESTORE

    return 0;
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    _nx_driver_hardware_packet_received                 PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processes packets received by the ethernet            */ 
/*    controller.                                                         */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_driver_transfer_to_netx           Transfer packet to NetX       */ 
/*    nx_packet_allocate                    Allocate receive packets      */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    _nx_driver_deferred_processing        Deferred driver processing    */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            invalidate packet cache,    */ 
/*                                            use driver information to   */ 
/*                                            process packets,            */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static int  _nx_driver_hardware_packet_received(VOID)
{

XEmacPs_Bd *bdSetPtr, *newBdSetPtr;
int         numRxBDs;
NX_PACKET   *packet_ptr;
int         length;
XEmacPs    *instance_ptr;
int         BdIndex;
ULONG      *temp;
ULONG       first_idx = nx_driver_information.nx_driver_information_receive_current_index;
int         status;

    instance_ptr = &nx_driver_information.nx_driver_instance;
    while(1)
    {
        numRxBDs = XEmacPs_BdRingFromHwRx(&(XEmacPs_GetRxRing(instance_ptr)), 1, &bdSetPtr);
        if(numRxBDs == 0)
            return 0;

        length = XEmacPs_BdGetLength(bdSetPtr);
        
        packet_ptr = nx_driver_information.nx_driver_information_receive_packets[first_idx];

        packet_ptr -> nx_packet_length = length;
        packet_ptr -> nx_packet_prepend_ptr += 2;
        packet_ptr -> nx_packet_append_ptr = packet_ptr -> nx_packet_prepend_ptr + length; 
        packet_ptr -> nx_packet_ip_interface = nx_driver_information.nx_driver_information_interface;
        
        /* Everything is OK, transfer the packet to NetX.  */
        _nx_driver_transfer_to_netx(nx_driver_information.nx_driver_information_ip_ptr, packet_ptr); 
        

        /* Release this BD. */
        XEmacPs_BdRingFree(&(XEmacPs_GetRxRing(instance_ptr)), 1, bdSetPtr);
        
        status = nx_packet_allocate(nx_driver_information.nx_driver_information_packet_pool_ptr, &packet_ptr,  NX_RECEIVE_PACKET, NX_NO_WAIT);
        if(status == NX_SUCCESS)
        {
            /* Replenish the HW BDs */
            status = XEmacPs_BdRingAlloc(&(XEmacPs_GetRxRing(instance_ptr)), 1, &newBdSetPtr);
            Xil_AssertNonvoid(status == XST_SUCCESS);
            
            BdIndex = XEMACPS_BD_TO_INDEX(&(XEmacPs_GetRxRing(instance_ptr)), newBdSetPtr);
            temp = (ULONG*)newBdSetPtr;
            if(BdIndex == (NX_DRIVER_RX_DESCRIPTORS - 1))
            {
                *temp = 2;
            }
            else
                *temp = 0;
            temp ++;
            *temp = 0;
            
            XEmacPs_BdSetAddressRx(newBdSetPtr, packet_ptr -> nx_packet_prepend_ptr);

            status = XEmacPs_BdRingToHw(&(XEmacPs_GetRxRing(instance_ptr)), 1, newBdSetPtr);
            
            if(status != XST_SUCCESS)
                xil_printf("status = %d, bdSetPtr=0x%.8x\n\r", status, newBdSetPtr);

            nx_driver_information.nx_driver_information_receive_packets[first_idx] = packet_ptr;

            Xil_AssertNonvoid(status == XST_SUCCESS);
            
            /* Invalidate packet so that physical memory will be read. */
            Xil_DCacheInvalidateRange((u32)(packet_ptr -> nx_packet_prepend_ptr), NX_DRIVER_ETHERNET_MTU);
        }
        else
        {
            xil_printf("rx alloc fail: %x, available: %d.\r\n",status,nx_driver_information.nx_driver_information_packet_pool_ptr->nx_packet_pool_available);
        }

        /* Set the first BD index for the next packet.  */
        first_idx = (first_idx + 1) & (NX_DRIVER_RX_DESCRIPTORS - 1);
        /* Update the current receive index.  */
        nx_driver_information.nx_driver_information_receive_current_index = first_idx;
    }
    return 0;
}


/**************************************************************************/ 
/*                                                                        */ 
/*  FUNCTION                                               RELEASE        */ 
/*                                                                        */ 
/*    nx_driver_zynq_ethernet_rx_isr                      PORTABLE C      */ 
/*                                                           5.1          */ 
/*  AUTHOR                                                                */ 
/*                                                                        */ 
/*    Yuxin Zhou, Express Logic, Inc.                                     */ 
/*                                                                        */ 
/*  DESCRIPTION                                                           */ 
/*                                                                        */ 
/*    This function processing incoming packets.  This routine is         */ 
/*    be called from the receive packet ISR and assumes that the          */ 
/*    interrupt is saved/restored around the call by ThreadX.             */ 
/*                                                                        */ 
/*  INPUT                                                                 */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  OUTPUT                                                                */ 
/*                                                                        */ 
/*    None                                                                */ 
/*                                                                        */ 
/*  CALLS                                                                 */ 
/*                                                                        */ 
/*    _nx_ip_driver_deferred_processing     IP receive packet processing  */ 
/*                                                                        */ 
/*  CALLED BY                                                             */ 
/*                                                                        */ 
/*    ISR                                                                 */ 
/*                                                                        */ 
/*  RELEASE HISTORY                                                       */ 
/*                                                                        */ 
/*    DATE              NAME                      DESCRIPTION             */ 
/*                                                                        */ 
/*  06-01-2012      Yuxin Zhou              Initial Version 5.0           */ 
/*  12-21-2017      Scott Larson            Modified comment(s),          */ 
/*                                            resulting in version 5.1    */ 
/*                                                                        */ 
/**************************************************************************/ 
static VOID  nx_driver_zynq_ethernet_rx_isr(void *handle)
{
ULONG    deferred_events;

    deferred_events = nx_driver_information.nx_driver_information_deferred_events;

    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_PACKET_RECEIVED;

    if (!deferred_events)
    {
        
        /* Call NetX deferred driver processing.  */
        _nx_ip_driver_deferred_processing(nx_driver_information.nx_driver_information_ip_ptr);
    }

}


static void  nx_driver_zynq_ethernet_tx_isr(void *handle)
{
ULONG    deferred_events;

#if 1
    deferred_events = nx_driver_information.nx_driver_information_deferred_events;

    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_PACKET_TRANSMITTED;

    if (!deferred_events)
    {
        
        /* Call NetX deferred driver processing.  */
        _nx_ip_driver_deferred_processing(nx_driver_information.nx_driver_information_ip_ptr);
    }
#else
    _nx_driver_hardware_packet_transmitted();

#endif

}


static int error_catch(void)
{
    Xil_AssertNonvoid(0);

}


static VOID nx_driver_zynq_ethernet_error_isr(void *handle, UCHAR direction, ULONG error_code)
{

UINT  txsr, txqbar;
XEmacPs    *instance_ptr;

    instance_ptr = &nx_driver_information.nx_driver_instance;

    nx_driver_information.nx_driver_information_deferred_events |= NX_DRIVER_DEFERRED_DRIVER_ERROR;

    txsr = XEmacPs_ReadReg(instance_ptr -> Config.BaseAddress, XEMACPS_TXSR_OFFSET);
    txqbar = XEmacPs_ReadReg(instance_ptr -> Config.BaseAddress, XEMACPS_TXQBASE_OFFSET);

    xil_printf("txsr=0x%.8x, txqbar=0x%.8x, error_code=0x%.8x\r\n", txsr, txqbar, error_code);

    if(direction == XEMACPS_RECV)
    {
#if 0
        /* Stop the reception. */
        rxqbar = XEmacPs_ReadReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCTRL_OFFSET);
        rxqbar &= ~(1 << 2);
        XEmacPs_WriteReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCTRL_OFFSET, rxqbar);
        xil_printf("Error on Reception: ");
#endif
        if(error_code & XEMACPS_RXSR_HRESPNOK_MASK)
            xil_printf("Receive DMA error\r\n");
        if(error_code & XEMACPS_RXSR_RXOVR_MASK)
        {
            xil_printf("Receive over run\r\n");
            return;
        }
        if(error_code & XEMACPS_RXSR_BUFFNA_MASK)
            xil_printf("Receive buffer not available\r\n");
        
    }
    else
    {
        ULONG reg;

        xil_printf("Error on Transmission.  error_code=0x%x\n\r", error_code);
        if((error_code == 0) || (error_code == 8))
        {
            nx_driver_zynq_ethernet_tx_isr(handle);
            return;
        }

        /* Halt the transmitter */
        reg = XEmacPs_ReadReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCTRL_OFFSET);
        reg &= ~(1 << 10);
        XEmacPs_WriteReg(instance_ptr -> Config.BaseAddress, XEMACPS_NWCTRL_OFFSET, reg);


        
        if(error_code & XEMACPS_TXSR_HRESPNOK_MASK)
            xil_printf("Tranmsit DMA error\n\r");
        if(error_code & XEMACPS_TXSR_URUN_MASK)
            xil_printf("Transmit under run\n\r");
        if(error_code & XEMACPS_TXSR_BUFEXH_MASK)
            xil_printf("Transmit buffer exhausted\r\n");
        if(error_code & XEMACPS_TXSR_RXOVR_MASK)
            xil_printf("Transmit retry exceeded limits\r\n");
        if(error_code & XEMACPS_TXSR_FRAMERX_MASK)
            xil_printf("Transmit collision\r\n");
        if(error_code & XEMACPS_TXSR_USEDREAD_MASK)
            xil_printf("Transmit Buffer not available\n\r");
    }


#if 0
    if(deferred_events)
    {
        /* Call NetX deferred driver processing.  */
        _nx_ip_driver_deferred_processing(nx_driver_information.nx_driver_information_ip_ptr);
    }
#endif
    error_catch();


}



/****** DRIVER SPECIFIC ****** Start of part/vendor specific internal driver functions.  */

