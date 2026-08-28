/**
  ******************************************************************************
  * @file    Bsp/emWin/GUI_X.c
  * @brief   No-OS system layer for emWin (time / delay / logging).
  *
  *  Single threaded bare-metal port: timing is taken straight from the HAL
  *  1 ms SysTick so no separate emWin tick counter is needed.
  ******************************************************************************
  */
#include "GUI.h"
#include "main.h"

/*********************************************************************
*
*       Timing
*/
GUI_TIMER_TIME GUI_X_GetTime(void) {
  return (GUI_TIMER_TIME)HAL_GetTick();
}

void GUI_X_Delay(int ms) {
  uint32_t tEnd = HAL_GetTick() + (uint32_t)ms;
  while ((int32_t)(tEnd - HAL_GetTick()) > 0) {
    /* busy wait */
  }
}

/*********************************************************************
*
*       Misc stubs (no hardware to bring up, no logging in release)
*/
void GUI_X_Init(void) {
}

void GUI_X_ExecIdle(void) {
}

void GUI_X_Log     (const char *s) { GUI_USE_PARA(s); }
void GUI_X_Warn    (const char *s) { GUI_USE_PARA(s); }
void GUI_X_ErrorOut(const char *s) { GUI_USE_PARA(s); }
