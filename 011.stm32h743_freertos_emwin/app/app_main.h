/**
  ******************************************************************************
  * @file    app_main.h
  * @brief   Application entry points called from main.c.
  ******************************************************************************
  */
#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

void application_init(void);
void application_run(void);

/* Push the emWin VRAM to the SPI6 panel (defined in Bsp/emWin/LCDConf.c). */
void emwin_flush_vram(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */
