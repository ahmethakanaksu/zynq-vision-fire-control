/*
 * board_setup.c -- Zybo Z7-20 hardware init for ThreadX.
 *
 * Adapted from the Express Logic / ZC702 reference (which uses TTC0) to the
 * SCU Cortex-A9 private timer, because the Zybo Z7-20 Vivado design doesn't
 * expose the TTC peripheral via the BSP. Functionally equivalent: the ISR
 * calls _tx_timer_interrupt() at 100 Hz regardless of the underlying timer.
 *
 * Flow:
 *   main() -> hardware_setup() -> {UART init, GIC init, SCU timer init}
 *   timer fires every 10 ms -> TmrIntrHandler -> _tx_timer_interrupt()
 *
 * IRQ delivery:
 *   Xilinx vector table -> B IRQHandler -> (asm_vectors.s links IRQHandler ==
 *   __tx_irq_handler) -> _tx_thread_context_save -> IRQInterrupt ->
 *   XScuGic_InterruptHandler -> TmrIntrHandler -> _tx_timer_interrupt.
 */

#include <stdio.h>
#include <stdlib.h>
#include "xparameters.h"
#include "xstatus.h"
#include "xpseudo_asm.h"
#include "xil_exception.h"
#include "xscutimer.h"
#include "xscugic.h"
#include "xuartps.h"
#include "xil_printf.h"
#include "tx_api.h"

#define TIMER_DEVICE_ID     XPAR_XSCUTIMER_0_DEVICE_ID
#define TIMER_INTR_ID       XPAR_SCUTIMER_INTR     /* PPI id 29 on Zynq-7000 */
#define INTC_DEVICE_ID      XPAR_SCUGIC_SINGLE_DEVICE_ID
#define UART_DEVICE_ID      XPAR_XUARTPS_0_DEVICE_ID

#define TICK_HZ             100U                   /* 10 ms tick */

static int Init_UART(u16 DevId, XUartPs *UartPtr, u32 BaudRate);
static int SetupTimer(void);
static int SetupInterruptSystem(u16 IntcDeviceID, XScuGic *IntcInstancePtr);
static void TmrIntrHandler(void *CallBackRef);

void _tx_timer_interrupt(void);

static volatile u32 TmrIntrCntr;
static XUartPs     Uart0;
static XScuTimer   TimerInst;
XScuGic            Gic0;     /* referenced from asm_vectors.s via IRQInterrupt -> XScuGic_InterruptHandler */

int hardware_setup(void)
{
    if (Init_UART(UART_DEVICE_ID, &Uart0, 115200) != XST_SUCCESS) {
        xil_printf("Init UART failed\r\n");
        return XST_FAILURE;
    }
    xil_printf("Init UART success\r\n");

    if (SetupInterruptSystem(INTC_DEVICE_ID, &Gic0) != XST_SUCCESS) {
        xil_printf("Init GIC failed\r\n");
        return XST_FAILURE;
    }
    xil_printf("Init GIC success\r\n");

    if (SetupTimer() != XST_SUCCESS) {
        xil_printf("Init Timer failed\r\n");
        return XST_FAILURE;
    }
    xil_printf("Init SCU timer success\r\n");

    return XST_SUCCESS;
}

static int Init_UART(u16 DevId, XUartPs *UartPtr, u32 BaudRate)
{
    XUartPs_Config *UartConfig = XUartPs_LookupConfig(DevId);
    if (UartConfig == NULL) {
        return XST_FAILURE;
    }
    if (XUartPs_CfgInitialize(UartPtr, UartConfig, UartConfig->BaseAddress) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    XUartPs_SetBaudRate(UartPtr, BaudRate);
    return XST_SUCCESS;
}

static int SetupInterruptSystem(u16 IntcDeviceID, XScuGic *IntcInstancePtr)
{
    XScuGic_Config *IntcConfig = XScuGic_LookupConfig(IntcDeviceID);
    if (IntcConfig == NULL) {
        return XST_FAILURE;
    }
    if (XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
                              IntcConfig->CpuBaseAddress) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    /* Install XScuGic_InterruptHandler as the IRQ dispatcher. Reached from
     * our asm_vectors.s via: IRQHandler -> _tx_thread_context_save ->
     * __tx_irq_processing_return -> bl IRQInterrupt. "IRQInterrupt" is an
     * alias for this registered handler. */
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 IntcInstancePtr);
    return XST_SUCCESS;
}

static int SetupTimer(void)
{
    XScuTimer_Config *Config = XScuTimer_LookupConfig(TIMER_DEVICE_ID);
    if (Config == NULL) {
        return XST_FAILURE;
    }
    if (XScuTimer_CfgInitialize(&TimerInst, Config, Config->BaseAddr) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    /* Program for 10 ms (100 Hz). Private timer runs at CPU_FREQ / 2. */
    u32 load = (XPAR_CPU_CORTEXA9_CORE_CLOCK_FREQ_HZ / 2U / TICK_HZ) - 1U;
    XScuTimer_LoadTimer(&TimerInst, load);
    XScuTimer_EnableAutoReload(&TimerInst);

    if (XScuGic_Connect(&Gic0, TIMER_INTR_ID,
                        (Xil_ExceptionHandler)TmrIntrHandler,
                        &TimerInst) != XST_SUCCESS) {
        return XST_FAILURE;
    }
    XScuGic_Enable(&Gic0, TIMER_INTR_ID);
    XScuTimer_EnableInterrupt(&TimerInst);
    XScuTimer_Start(&TimerInst);
    return XST_SUCCESS;
}

static void TmrIntrHandler(void *CallBackRef)
{
    XScuTimer *tmr = (XScuTimer *)CallBackRef;
    if (XScuTimer_IsExpired(tmr)) {
        XScuTimer_ClearInterruptStatus(tmr);
        TmrIntrCntr++;
        _tx_timer_interrupt();
    }
}

/* IRQInterrupt symbol consumed by asm_vectors.s. It resolves to the
 * Xil_ExceptionRegisterHandler callback installed by SetupInterruptSystem;
 * XScuGic_InterruptHandler is the dispatcher that picks per-IRQ handlers
 * out of the GIC handler table.
 *
 * We provide it as a thin shim that forwards to the global Gic0 instance,
 * so the asm file's `bl IRQInterrupt` call compiles without linking
 * against XScuGic_InterruptHandler directly (that one expects an instance
 * pointer, not a void-function signature). */
void IRQInterrupt(void)
{
    XScuGic_InterruptHandler(&Gic0);
}

/* Xilinx standard exception hooks referenced by the common asm_vectors.s.
 * Unused in this build, kept as empty stubs so the link succeeds. */
void FIQInterrupt(void)              { for (;;) { /* nop */ } }
void SWInterrupt(u32 SwiNumber)      { (void)SwiNumber; }
void DataAbortInterrupt(void)        { for (;;) { /* halt */ } }
void PrefetchAbortInterrupt(void)    { for (;;) { /* halt */ } }
