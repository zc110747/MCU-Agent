/*----------------------------------------------------------------------------
 * CMSIS-DAP Configuration - STM32H743ZIT6
 *
 * Hardware abstraction for the ARM CMSIS-DAP firmware (DAP.c / SW_DP.c /
 * JTAG_DP.c). Everything the protocol core needs to know about *this* board
 * lives here: pin mapping, port setup, LEDs, timestamps and the delay budget.
 *
 * Debug port wiring (all on GPIOA, 5 V-tolerant, no level shifters):
 *
 *   Signal            Pin    Direction / drive
 *   ----------------- -----  --------------------------------------------
 *   JTMS / SWDIO      PA0    push-pull, switched to input for SWD reads
 *   JTCK / SWCLK      PA1    push-pull output
 *   nRESET            PA2    open-drain + pull-up (never drives high)
 *   JTRST             PA3    open-drain + pull-up (never drives high)
 *   JTDI              PC4    push-pull output (on GPIOC, not GPIOA)
 *   JTDO              PA7    input with pull-up
 *
 * PA13/PA14 (the H743's own SWD port) are untouched, so the board stays
 * debuggable with an ST-Link while it is acting as a debug probe itself.
 *--------------------------------------------------------------------------*/

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#include <stddef.h>
#include <stdint.h>
#include "stm32h7xx.h"

/*===========================================================================*/
/* Debug Unit Information                                                    */
/*===========================================================================*/

/// Processor Clock of the Cortex-M MCU used in the Debug Unit.
/// Must match what bsp_init() actually programs (PLL1P = 400 MHz).
#define CPU_CLOCK               400000000U

/// Number of processor cycles for I/O Port write operations.
///
/// This is CMSIS-DAP's model of "how long does a GPIO write cost", and it feeds
/// two things: the SWCLK delay compensation, and the *maximum* clock the probe
/// claims it can produce (CPU_CLOCK / (IO_PORT_WRITE_CYCLES * 2)).
///
/// On the H7 the core runs at 400 MHz but GPIOA sits on AHB4 at 200 MHz, and a
/// store to BSRR is posted through the write buffer. Measured cost of the
/// set/clear pair inside the bit loop is roughly 8 core cycles. Declaring 8
/// caps the "fast clock" threshold at 25 MHz, which keeps OpenOCD on the
/// cycle-accurate delay path for every realistic adapter speed.
#define IO_PORT_WRITE_CYCLES    8U

/// Indicate that Serial Wire Debug (SWD) communication mode is available.
#define DAP_SWD                 1

/// Indicate that JTAG communication mode is available.
#define DAP_JTAG                1

/// Configure maximum number of JTAG devices on the scan chain.
#define DAP_JTAG_DEV_CNT        8U

/// Default communication mode on the Debug Access Port.
/// 1 = SWD - what OpenOCD asks for on Cortex-M targets anyway.
#define DAP_DEFAULT_PORT        1U

/// Default communication speed on the Debug Access Port for SWD and JTAG mode.
/// 1 MHz is conservative enough to survive dupont jumpers; OpenOCD overrides it
/// immediately with whatever `adapter speed` says.
#define DAP_DEFAULT_SWJ_CLOCK   1000000U

/// Maximum Package Size for Command and Response data.
/// 64 = USB full-speed HID report size. This is what makes us CMSIS-DAP v1.
#define DAP_PACKET_SIZE         64U

/// Maximum Package Buffers for Command and Response data.
/// Lets OpenOCD keep several requests in flight; each buffer costs
/// DAP_PACKET_SIZE bytes of RAM in both directions.
#define DAP_PACKET_COUNT        8U

/// Indicate that UART Serial Wire Output (SWO) trace is available.
#define SWO_UART                0

/// Indicate that Manchester Serial Wire Output (SWO) trace is available.
#define SWO_MANCHESTER          0

/// SWO Trace Buffer Size (must be 2^n).
#define SWO_BUFFER_SIZE         0U

/// SWO Streaming Trace.
#define SWO_STREAM              0

/// Clock frequency of the Test Domain Timer. Zero if not available.
/// We expose the DWT cycle counter, which ticks at the core clock.
#define TIMESTAMP_CLOCK         CPU_CLOCK

/// Debug Unit is connected to fixed Target Device.
#define TARGET_DEVICE_FIXED     0

/* Strings reported through DAP_Info. OpenOCD prints these on connect; the
 * product string is also what the HID backend matches on, so it must contain
 * "CMSIS-DAP". */
#define DAP_VENDOR              "WorkBuddy"
#define DAP_PRODUCT             "STM32H743 CMSIS-DAP v1"
#define DAP_SER_NUM             "H743DAP0001"

/*===========================================================================*/
/* Pin mapping                                                               */
/*===========================================================================*/

#define DAP_GPIO_PORT           GPIOA

#define PIN_SWDIO_TMS           0U      /* PA0 - JTMS / SWDIO */
#define PIN_SWCLK_TCK           1U      /* PA1 - JTCK / SWCLK */
#define PIN_nRESET              2U      /* PA2 - NRST         */
#define PIN_nTRST               3U      /* PA3 - JTRST        */
#define PIN_TDO                 7U      /* PA7 - JTDO         */

/* JTDI moved to GPIOC (PC4) on this board revision. */
#define DAP_TDI_PORT            GPIOC
#define PIN_TDI                 4U      /* PC4 - JTDI         */

#define PIN_SWDIO_TMS_MASK      (1UL << PIN_SWDIO_TMS)
#define PIN_SWCLK_TCK_MASK      (1UL << PIN_SWCLK_TCK)
#define PIN_nRESET_MASK         (1UL << PIN_nRESET)
#define PIN_nTRST_MASK          (1UL << PIN_nTRST)
#define PIN_TDI_MASK            (1UL << PIN_TDI)
#define PIN_TDO_MASK            (1UL << PIN_TDO)

/* MODER helpers: 2 bits per pin, 00 = input, 01 = general purpose output. */
#define MODER_MASK(pin)         (3UL << ((pin) * 2U))
#define MODER_OUTPUT(pin)       (1UL << ((pin) * 2U))

/* Status LED - green user LED on PG7 of the 鹿小班 H743 board (active high). */
#define LED_GPIO_PORT           GPIOG
#define PIN_LED_CONNECTED       7U
#define PIN_LED_RUNNING         7U
#define PIN_LED_CONNECTED_MASK  (1UL << PIN_LED_CONNECTED)
#define PIN_LED_RUNNING_MASK    (1UL << PIN_LED_RUNNING)

/*===========================================================================*/
/* DAP Hardware I/O Pin Access Functions                                     */
/*===========================================================================*/

/* Implemented out-of-line in bsp/dap_port.c - they reconfigure whole ports and
 * are called once per connect, so inlining buys nothing. */
extern void PORT_JTAG_SETUP(void);
extern void PORT_SWD_SETUP(void);
extern void PORT_OFF(void);
extern void DAP_SETUP(void);

/* --------------------------------------------------------------------------
 * SWCLK/TCK  (PA1, push-pull output)
 * ------------------------------------------------------------------------*/
__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_SWCLK_TCK) & 1U;
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_SET(void) {
  DAP_GPIO_PORT->BSRR = PIN_SWCLK_TCK_MASK;
}

__STATIC_FORCEINLINE void PIN_SWCLK_TCK_CLR(void) {
  DAP_GPIO_PORT->BSRR = PIN_SWCLK_TCK_MASK << 16U;
}

/* --------------------------------------------------------------------------
 * SWDIO/TMS  (PA0)
 *
 * In JTAG mode this is TMS: always an output. In SWD mode the line is
 * bidirectional and the direction flips several times per transfer, so the
 * ENABLE/DISABLE pair below is on the hot path - it touches MODER only.
 * ------------------------------------------------------------------------*/
__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_SWDIO_TMS) & 1U;
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_SET(void) {
  DAP_GPIO_PORT->BSRR = PIN_SWDIO_TMS_MASK;
}

__STATIC_FORCEINLINE void PIN_SWDIO_TMS_CLR(void) {
  DAP_GPIO_PORT->BSRR = PIN_SWDIO_TMS_MASK << 16U;
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_SWDIO_TMS) & 1U;
}

__STATIC_FORCEINLINE void PIN_SWDIO_OUT(uint32_t bit) {
  DAP_GPIO_PORT->BSRR = (bit & 1U) ? PIN_SWDIO_TMS_MASK
                                   : (PIN_SWDIO_TMS_MASK << 16U);
}

/** Switch SWDIO to output (the probe drives the line). */
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_ENABLE(void) {
  DAP_GPIO_PORT->MODER = (DAP_GPIO_PORT->MODER & ~MODER_MASK(PIN_SWDIO_TMS))
                       | MODER_OUTPUT(PIN_SWDIO_TMS);
}

/** Switch SWDIO to input (the target drives the line; our pull-up idles high). */
__STATIC_FORCEINLINE void PIN_SWDIO_OUT_DISABLE(void) {
  DAP_GPIO_PORT->MODER &= ~MODER_MASK(PIN_SWDIO_TMS);
}

/* --------------------------------------------------------------------------
 * TDI (PC4, output) / TDO (PA7, input)
 *
 * TDI lives on GPIOC, so it needs its own port handle (DAP_TDI_PORT) instead
 * of the shared DAP_GPIO_PORT used by the rest of the debug lines.
 * ------------------------------------------------------------------------*/
__STATIC_FORCEINLINE uint32_t PIN_TDI_IN(void) {
  return (DAP_TDI_PORT->IDR >> PIN_TDI) & 1U;
}

__STATIC_FORCEINLINE void PIN_TDI_OUT(uint32_t bit) {
  DAP_TDI_PORT->BSRR = (bit & 1U) ? PIN_TDI_MASK : (PIN_TDI_MASK << 16U);
}

__STATIC_FORCEINLINE uint32_t PIN_TDO_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_TDO) & 1U;
}

/* --------------------------------------------------------------------------
 * nTRST (PA3) and nRESET (PA2)
 *
 * Both are open-drain with a pull-up: writing 1 releases the line rather than
 * driving it high, so a target that holds its own reset low is never fought.
 * ------------------------------------------------------------------------*/
__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_nTRST) & 1U;
}

__STATIC_FORCEINLINE void PIN_nTRST_OUT(uint32_t bit) {
  DAP_GPIO_PORT->BSRR = (bit & 1U) ? PIN_nTRST_MASK : (PIN_nTRST_MASK << 16U);
}

__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN(void) {
  return (DAP_GPIO_PORT->IDR >> PIN_nRESET) & 1U;
}

__STATIC_FORCEINLINE void PIN_nRESET_OUT(uint32_t bit) {
  DAP_GPIO_PORT->BSRR = (bit & 1U) ? PIN_nRESET_MASK : (PIN_nRESET_MASK << 16U);
}

/*===========================================================================*/
/* Debug Unit LEDs                                                           */
/*===========================================================================*/

__STATIC_FORCEINLINE void LED_CONNECTED_OUT(uint32_t bit) {
  LED_GPIO_PORT->BSRR = bit ? PIN_LED_CONNECTED_MASK
                            : (PIN_LED_CONNECTED_MASK << 16U);
}

__STATIC_FORCEINLINE void LED_RUNNING_OUT(uint32_t bit) {
  (void)bit;   /* one LED on this board; "connected" wins */
}

/*===========================================================================*/
/* Test Domain Timer                                                         */
/*===========================================================================*/

/** Return the current value of the DWT cycle counter (ticks at CPU_CLOCK). */
__STATIC_FORCEINLINE uint32_t TIMESTAMP_GET(void) {
  return DWT->CYCCNT;
}

/*===========================================================================*/
/* Reset target device with a device specific sequence                       */
/*===========================================================================*/

/** Drive the target nRESET (PA2, open-drain) low for a few ms, then release it.
 *
 *  This makes the CMSIS-DAP `DAP_ResetTarget` command actually reset the
 *  target. Previously it returned 0 ("not implemented"), so OpenOCD fell back
 *  to a *software* reset (SYSRESETREQ / VECTRESET). On a target whose core has
 *  gone to sleep and turned off its debug block (e.g. an F429 running firmware
 *  that idles with the debug clock gated), a software reset cannot bring the
 *  core back and the flash/download step fails with
 *  "Can't read component, the corresponding core might be turned off".
 *
 *  A real nRESET pulse is the only thing that reliably powers the debug domain
 *  back on, so implement it here. The line is open-drain: we only ever drive it
 *  low (BSRR reset bit) and let the external pull-up release it high. */
__STATIC_INLINE uint8_t RESET_TARGET(void) {
  extern void Delayms(uint32_t);  /* declared in DAP.h (real impl in DAP.c) */
  PIN_nRESET_OUT(0U);          /* assert nRESET (drive PA2 low) */
  Delayms(10U);                /* hold long enough for the target to reset */
  PIN_nRESET_OUT(1U);          /* release nRESET (float, pulled up high) */
  Delayms(10U);                /* let the target come out of reset */
  return 1U;                   /* 1 = reset was performed */
}

#endif /* __DAP_CONFIG_H__ */
