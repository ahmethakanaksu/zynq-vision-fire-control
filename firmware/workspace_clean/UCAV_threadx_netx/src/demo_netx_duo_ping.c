/* This is a small ping demo of the high-performance NetX Duo TCP/IP stack.  */

#include "tx_api.h"
#include "nx_api.h"
#include "xil_mmu.h"

/* Define the ThreadX and NetX object control blocks...  */

NX_PACKET_POOL    pool_0;
NX_IP             ip_0;  
#ifdef NX_ENABLE_DHCP
NX_DHCP           dhcp_client;
UCHAR             ip_address[4];
UCHAR             network_mask[4];
TX_THREAD         thread_0;
UCHAR             thread_0_stack[2048];
#endif


/* Define the IP thread's stack area.  */

ULONG             ip_thread_stack[2 * 1024 / sizeof(ULONG)];


/* Define packet pool for the demonstration.  */

//#define NX_PACKET_POOL_SIZE ((1536 + sizeof(NX_PACKET)) * 50)

ULONG packet_pool_area[(1536 + sizeof(NX_PACKET)) * 100 / sizeof(ULONG) + 8] __attribute__ ((aligned (32)));


/* Define the ARP cache area.  */

ULONG             arp_space_area[1024 / sizeof(ULONG)];

                                                           
/* Define an error counter.  */

ULONG             error_counter;

#ifdef NX_ENABLE_DHCP
VOID    thread_0_entry(ULONG thread_input);
#endif

VOID  nx_driver_zynq(NX_IP_DRIVER *driver_req_ptr);

VOID hardware_setup(void);

int main(int argc, char ** argv)
{
    Xil_SetTlbAttributes(0x0FF00000, 0xc02);

    /* Setup the hardware. */
    hardware_setup();

    /* Enter the ThreadX kernel.  */
    tx_kernel_enter();
}


/* Define what the initial system looks like.  */

void    tx_application_define(void *first_unused_memory)
{

UINT  status;
    
     
    /* Initialize the NetX system.  */
    nx_system_initialize();
    
    /* Create a packet pool.  */
    status =  nx_packet_pool_create(&pool_0, "NetX Main Packet Pool", 1536,  (ULONG*)(((int)packet_pool_area + 31) & ~31) , sizeof(packet_pool_area)-32);

    /* Check for pool creation error.  */
    if (status)
        error_counter++;

    /* Create an IP instance.  */
    status = nx_ip_create(&ip_0, 
                          "NetX IP Instance 0", 
#ifdef NX_ENABLE_DHCP
                          IP_ADDRESS(0,0,0,0),
                          IP_ADDRESS(0,0,0,0), 
#else
                          IP_ADDRESS(192, 168, 1, 10),
                          0xFFFFFF00UL, 
#endif
                          &pool_0, nx_driver_zynq,
                          (UCHAR*)ip_thread_stack,
                          sizeof(ip_thread_stack),
                          1);
    
    /* Check for IP create errors.  */
    if (status)
        error_counter++;
        
    /* Enable ARP and supply ARP cache memory for IP Instance 0.  */
    status =  nx_arp_enable(&ip_0, (void *)arp_space_area, sizeof(arp_space_area));

    /* Check for ARP enable errors.  */
    if (status)
        error_counter++;

    /* Enable TCP traffic.  */
    status =  nx_tcp_enable(&ip_0);
    
    /* Check for TCP enable errors.  */
    if (status)
        error_counter++;
    
    /* Enable UDP traffic.  */
    status =  nx_udp_enable(&ip_0);
    
    /* Check for UDP enable errors.  */
    if (status)
        error_counter++;

    /* Enable ICMP.  */
    status =  nx_icmp_enable(&ip_0);

    /* Check for errors.  */
    if (status)
        error_counter++;

    /* Start the UDP echo server (port 5001). */
    extern void udp_echo_start(void);
    udp_echo_start();

    /* Start the TCP echo server (port 7). */
    extern void tcp_echo_start(void);
    tcp_echo_start();

    /* Start QP/HSM framework (Active Object + state machine). */
    extern void qp_app_start(void);
    qp_app_start();

    /* Start the UDP command server (port 5002): drives the legacy
     * AO_Controller HSM via plain-text UDP commands. */
    extern void udp_command_start(void);
    udp_command_start();

    /* Start the OMC subsystem: shared services, Active Objects, and the
     * UDP router that listens on port 5005 for incoming protocol traffic. */
    extern void project_start(void);
    project_start();

#ifdef NX_ENABLE_DHCP
    /* Create the main thread.  */
    tx_thread_create(&thread_0, "thread 0", thread_0_entry, 0,
                     thread_0_stack, sizeof(thread_0_stack),
                     4, 4, TX_NO_TIME_SLICE, TX_AUTO_START);
#endif
}

#ifdef NX_ENABLE_DHCP

/* Define the test threads.  */
void    thread_0_entry(ULONG thread_input)
{
UINT    status;
ULONG   actual_status;
ULONG   temp;


    /* Create the DHCP instance.  */
    printf("DHCP In Progress...\n");

    nx_dhcp_create(&dhcp_client, &ip_0, "dhcp_client");

    /* Start the DHCP Client.  */
    nx_dhcp_start(&dhcp_client);
    
    /* Wait util address is solved. */
    status = nx_ip_status_check(&ip_0, NX_IP_ADDRESS_RESOLVED, &actual_status, 1000);
    
    if (status)
    {
        
        /* DHCP Failed...  no IP address! */
        printf("Can't resolve address\n");
    }
    else
    {
        
        /* Get IP address. */
        nx_ip_address_get(&ip_0, (ULONG *) &ip_address[0], (ULONG *) &network_mask[0]);

        /* Convert IP address & network mask from little endian.  */
        temp =  *((ULONG *) &ip_address[0]);
        NX_CHANGE_ULONG_ENDIAN(temp);
        *((ULONG *) &ip_address[0]) =  temp;
        
        temp =  *((ULONG *) &network_mask[0]);
        NX_CHANGE_ULONG_ENDIAN(temp);
        *((ULONG *) &network_mask[0]) =  temp;

        /* Output IP address. */
        printf("IP address: %d.%d.%d.%d\nMask: %d.%d.%d.%d", 
               (UINT) (ip_address[0]),
               (UINT) (ip_address[1]),
               (UINT) (ip_address[2]),
               (UINT) (ip_address[3]),               
               (UINT) (network_mask[0]),
               (UINT) (network_mask[1]),
               (UINT) (network_mask[2]),
               (UINT) (network_mask[3]));
    }
}
#endif

