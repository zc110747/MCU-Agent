/**
  ******************************************************************************
  * @file    app/qspi_test.h
  * @brief   QSPI flash self-test (indirect HAL read/write + memory-mapped XIP)
  *
  * These routines are encapsulated and NOT called by default. They exist so the
  * behaviour can be exercised on demand (e.g. from a future automation or a
  * serial command) without duplicating the logic. The firmware's normal path
  * only initialises the QSPI peripheral and exposes it over USB MSC.
  ******************************************************************************
  */
#ifndef __QSPI_TEST_H
#define __QSPI_TEST_H

#include <stdio.h>
#include <stdint.h>
#include "qspi.h"

/* Run the full self-test: indirect HAL mode + memory-mapped XIP mode.
   Returns 0 on success, non-zero on failure count. */
int BSP_QSPI_RunSelfTest(void);

#endif /* __QSPI_TEST_H */
