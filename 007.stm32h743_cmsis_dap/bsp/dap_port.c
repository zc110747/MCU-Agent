/*----------------------------------------------------------------------------
 * CMSIS-DAP debug port I/O - STM32H743ZIT6
 *
 * Out-of-line half of DAP_config.h: whole-pin reconfiguration (mode, drive
 * type, speed, pull) plus one-time setup of the DWT cycle counter that the
 * SWCLK delay loop and the DAP timestamp feature both ride on.
 *
 * Everything here is careful to touch only the six debug pins. GPIOA also
 * carries the USB D-/D+ lines on PA11/PA12 and the H743's own SWD port on
 * PA13/PA14, so writing whole GPIO registers is not an option - every access
 * is a read-modify-write on the individual pin fields.
 *--------------------------------------------------------------------------*/

#include "DAP_config.h"

/* GPIO field widths: MODER/OSPEEDR/PUPDR are 2 bits per pin, OTYPER is 1. */
#define FIELD2_MASK(pin)        (3UL << ((pin) * 2U))
#define FIELD2(val, pin)        ((uint32_t)(val) << ((pin) * 2U))
#define FIELD1_MASK(pin)        (1UL << (pin))

/* MODER - guarded so we don't collide with stm32h7xx_hal_gpio.h's own
 * MODE_INPUT / MODE_OUTPUT (functionally identical, just 0 and 1). */
#ifndef MODE_INPUT
#define MODE_INPUT              0UL
#endif
#ifndef MODE_OUTPUT
#define MODE_OUTPUT             1UL
#endif
/* OTYPER */
#define OTYPE_PP                0UL
#define OTYPE_OD                1UL
/* OSPEEDR - VERY_HIGH keeps the edges sharp enough for multi-MHz SWCLK. */
#define OSPEED_LOW              0UL
#define OSPEED_VERY_HIGH        3UL
/* PUPDR */
#define PULL_NONE               0UL
#define PULL_UP                 1UL

/**
 * Configure one pin of a port without disturbing its neighbours.
 */
static void pin_configure(GPIO_TypeDef *port, uint32_t pin,
                          uint32_t mode, uint32_t otype,
                          uint32_t ospeed, uint32_t pull) {
  port->PUPDR   = (port->PUPDR   & ~FIELD2_MASK(pin)) | FIELD2(pull,   pin);
  port->OTYPER  = (port->OTYPER  & ~FIELD1_MASK(pin)) | ((otype & 1UL) << pin);
  port->OSPEEDR = (port->OSPEEDR & ~FIELD2_MASK(pin)) | FIELD2(ospeed, pin);
  /* MODER last: the pin only starts driving once the rest is in place. */
  port->MODER   = (port->MODER   & ~FIELD2_MASK(pin)) | FIELD2(mode,   pin);
}

/**
 * nRESET and nTRST are open-drain with a pull-up in every mode: the probe can
 * pull them low but never drives them high, so a target holding its own reset
 * line down is never fought. Released (high) on entry.
 */
static void reset_pins_idle(void) {
  DAP_GPIO_PORT->BSRR = PIN_nRESET_MASK | PIN_nTRST_MASK;   /* release first */
  pin_configure(DAP_GPIO_PORT, PIN_nRESET, MODE_OUTPUT, OTYPE_OD, OSPEED_VERY_HIGH, PULL_UP);
  pin_configure(DAP_GPIO_PORT, PIN_nTRST,  MODE_OUTPUT, OTYPE_OD, OSPEED_VERY_HIGH, PULL_UP);
}

/*----------------------------------------------------------------------------
 * Setup JTAG I/O pins: TCK, TMS, TDI, TDO, nTRST and nRESET.
 *--------------------------------------------------------------------------*/
void PORT_JTAG_SETUP(void) {
  /* Idle levels before the pins become outputs, so no glitch reaches the target. */
  DAP_GPIO_PORT->BSRR = PIN_SWCLK_TCK_MASK | PIN_SWDIO_TMS_MASK;
  DAP_TDI_PORT->BSRR  = PIN_TDI_MASK;

  pin_configure(DAP_GPIO_PORT, PIN_SWCLK_TCK, MODE_OUTPUT, OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);
  pin_configure(DAP_GPIO_PORT, PIN_SWDIO_TMS, MODE_OUTPUT, OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);
  pin_configure(DAP_TDI_PORT,   PIN_TDI,       MODE_OUTPUT, OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);
  /* TDO is driven by the target. */
  pin_configure(DAP_GPIO_PORT, PIN_TDO,       MODE_INPUT,  OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);

  reset_pins_idle();
}

/*----------------------------------------------------------------------------
 * Setup SWD I/O pins: SWCLK, SWDIO and nRESET.
 *--------------------------------------------------------------------------*/
void PORT_SWD_SETUP(void) {
  DAP_GPIO_PORT->BSRR = PIN_SWCLK_TCK_MASK | PIN_SWDIO_TMS_MASK;

  pin_configure(DAP_GPIO_PORT, PIN_SWCLK_TCK, MODE_OUTPUT, OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);
  /* SWDIO keeps its pull-up in both directions: when PIN_SWDIO_OUT_DISABLE()
   * flips it to input mid-transfer the line still idles high instead of
   * floating into the target's receiver. */
  pin_configure(DAP_GPIO_PORT, PIN_SWDIO_TMS, MODE_OUTPUT, OTYPE_PP, OSPEED_VERY_HIGH, PULL_UP);

  /* JTAG-only pins stay out of the way. */
  pin_configure(DAP_TDI_PORT,   PIN_TDI,       MODE_INPUT,  OTYPE_PP, OSPEED_LOW, PULL_NONE);
  pin_configure(DAP_GPIO_PORT, PIN_TDO,       MODE_INPUT,  OTYPE_PP, OSPEED_LOW, PULL_UP);

  reset_pins_idle();
}

/*----------------------------------------------------------------------------
 * Disable JTAG/SWD I/O pins: put everything in high-Z.
 *
 * Called on DAP_Disconnect and at power-up. The target must be free to run (or
 * be driven by another probe) the moment we are not talking to it, so no pin
 * may keep driving - including nRESET.
 *--------------------------------------------------------------------------*/
void PORT_OFF(void) {
  pin_configure(DAP_GPIO_PORT, PIN_SWCLK_TCK, MODE_INPUT, OTYPE_PP, OSPEED_LOW, PULL_NONE);
  pin_configure(DAP_GPIO_PORT, PIN_SWDIO_TMS, MODE_INPUT, OTYPE_PP, OSPEED_LOW, PULL_NONE);
  pin_configure(DAP_TDI_PORT,   PIN_TDI,       MODE_INPUT, OTYPE_PP, OSPEED_LOW, PULL_NONE);
  pin_configure(DAP_GPIO_PORT, PIN_TDO,       MODE_INPUT, OTYPE_PP, OSPEED_LOW, PULL_NONE);
  /* Keep the pull-ups on the two reset lines so they stay defined high. */
  pin_configure(DAP_GPIO_PORT, PIN_nRESET,    MODE_INPUT, OTYPE_OD, OSPEED_LOW, PULL_UP);
  pin_configure(DAP_GPIO_PORT, PIN_nTRST,     MODE_INPUT, OTYPE_OD, OSPEED_LOW, PULL_UP);
}

/*----------------------------------------------------------------------------
 * Setup of the debug unit I/O pins, LEDs and the cycle counter.
 * Called once from DAP_Setup() before any command is processed.
 *--------------------------------------------------------------------------*/
void DAP_SETUP(void) {
  /* GPIO clocks. GPIOA carries most debug pins, GPIOC carries TDI (PC4),
   * GPIOG the status LED. */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIOGEN;
  (void)RCC->AHB4ENR;                     /* ensure the enable has landed */

  /* Status LED: push-pull output, off. */
  LED_GPIO_PORT->BSRR = PIN_LED_CONNECTED_MASK << 16U;
  pin_configure(LED_GPIO_PORT, PIN_LED_CONNECTED, MODE_OUTPUT, OTYPE_PP, OSPEED_LOW, PULL_NONE);

  /* DWT cycle counter: the time base for PIN_DELAY_SLOW() (SWCLK generation)
   * and for the DAP timestamp feature. On the M7 the DWT is behind a lock, so
   * unlock it before enabling - writes are silently dropped otherwise. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR    = 0xC5ACCE55UL;
  DWT->CYCCNT = 0U;
  DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

  PORT_OFF();
}
