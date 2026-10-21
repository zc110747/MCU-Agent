/**
  ******************************************************************************
  * @file    board_io.h
  * @brief   Board level I/O: the two user LEDs and the PCF8574 buzzer.
  *
  *          LEDs   : PB1 = heartbeat, PB0 = web controlled, both active-low.
  *          Buzzer : PCF8574T P0 (I2C2 0x20), LOW = sound.
  *          ETH    : LAN8720A reset is PCF8574T P7 (asserted = 1), released
  *                   automatically before the Ethernet driver initialises.
  ******************************************************************************
  */
#ifndef APP_BOARD_IO_H_
#define APP_BOARD_IO_H_

#include <stdbool.h>

/** Configure the LED GPIOs and read back the current PCF8574 state. */
int board_io_init(void);

/** Web controlled LED (PB0). */
int board_led_set(bool on);
bool board_led_get(void);

/** Buzzer (PCF8574 P0). */
int board_beep_set(bool on);
bool board_beep_get(void);

#endif /* APP_BOARD_IO_H_ */
