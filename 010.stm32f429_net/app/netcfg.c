/**
  ******************************************************************************
  * @file    netcfg.c
  * @brief   Network parameter store (in-RAM only).
  *
  *          SDIO/FatFs SD-card persistence was removed; parameters live in
  *          RAM and reset to defaults on reboot. The API is kept compatible
  *          so callers (web_serve.c) need no changes.
  ******************************************************************************
  */
#include "netcfg.h"
#include <string.h>

int netcfg_load(netcfg_t *cfg)
{
  /* No persistent store: caller already holds the compiled-in defaults. */
  (void)cfg;
  return 0;
}

int netcfg_save(const netcfg_t *cfg)
{
  /* No persistent store: parameters stay in RAM until next reset. */
  (void)cfg;
  return 1;
}
