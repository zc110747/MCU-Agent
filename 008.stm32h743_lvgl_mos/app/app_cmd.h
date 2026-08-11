/**
  ******************************************************************************
  * @file    app_cmd.h
  * @brief   Serial console command parser.
  *
  *  Turns the byte stream from bsp_console (USART1 + USB CDC merged) into UI
  *  and emulator actions.  With no push-buttons on the board this is the only
  *  way to drive the firmware, so it doubles as the automated-test interface -
  *  see scripts/serial_test.py.
  ******************************************************************************
  */
#ifndef __APP_CMD_H
#define __APP_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/** Print the banner and reset the line buffer. */
void app_cmd_init(void);

/** Drain the console RX ring and execute any complete lines.  Main loop. */
void app_cmd_task(void);

/** 1 when typed characters are echoed back to the host. */
int  app_cmd_echo(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CMD_H */
