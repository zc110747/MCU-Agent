/**
  ******************************************************************************
  * @file    bsp/qspi.h
  * @brief   QSPI (QUADSPI) driver for Winbond W25Q64JV Flash on STM32H743
  *
  * Wiring (from LXB743ZI-P1 schematic):
  *   CLK  = PF10 (AF9)   NCS  = PG6  (AF10)
  *   IO0  = PF8  (AF10)  IO1  = PF9  (AF10)
  *   IO2  = PF7  (AF9)   IO3  = PF6  (AF9)
  ******************************************************************************
  */
#ifndef __BSP_QSPI_H
#define __BSP_QSPI_H

#include "stm32h7xx_hal.h"

/* Flash geometry */
#define QSPI_FLASH_SIZE       (8UL * 1024UL * 1024UL)   /* 8 MB  */
#define QSPI_SECTOR_SIZE      (4UL * 1024UL)            /* 4 KB  */
#define QSPI_PAGE_SIZE        256U
#define QSPI_BASE_ADDR        0x90000000UL              /* memory-mapped base */

/* W25Q64JV commands */
#define W25X_WRITE_ENABLE     0x06
#define W25X_WRITE_DISABLE    0x04
#define W25X_READ_STATUS_REG1 0x05
#define W25X_READ_STATUS_REG2 0x35
#define W25X_WRITE_STATUS_REG2 0x31
#define W25X_SECTOR_ERASE     0x20
#define W25X_PAGE_PROGRAM     0x02
#define W25X_QUAD_PAGE_PROGRAM 0x32  /* 1-1-4 page program (needs QE) */
#define W25X_READ_DATA        0x03
#define W25X_FAST_READ_QUAD   0xEB   /* 1-1-4 / 1-4-4 Quad I/O Fast Read */
#define W25X_CHIP_ERASE       0xC7
#define W25X_READ_JEDEC_ID    0x9F
#define W25X_RESET_ENABLE     0x66
#define W25X_RESET_MEMORY     0x99

/* Status register 1 bits */
#define W25X_SR1_BUSY         (1u << 0)
#define W25X_SR1_WEL          (1u << 1)

/* Status register 2 bits (W25Q64JV) */
#define W25X_SR2_QE           (1u << 1)   /* Quad Enable */

typedef enum {
    QSPI_OK = 0,
    QSPI_ERR_INIT,
    QSPI_ERR_ID,
    QSPI_ERR_ERASE,
    QSPI_ERR_WRITE,
    QSPI_ERR_READ,
    QSPI_ERR_TIMEOUT
} QSPI_Status_t;

QSPI_Status_t BSP_QSPI_Init(void);
QSPI_Status_t BSP_QSPI_ReadID(uint8_t *id, uint8_t len);
QSPI_Status_t BSP_QSPI_EraseSector(uint32_t addr);
QSPI_Status_t BSP_QSPI_WritePage(uint32_t addr, const uint8_t *data, uint16_t len);
QSPI_Status_t BSP_QSPI_ReadIndirect(uint32_t addr, uint8_t *buf, uint32_t len);
QSPI_Status_t BSP_QSPI_EnableMemoryMapped(void);
QSPI_Status_t BSP_QSPI_DisableMemoryMapped(void);
uint8_t       BSP_QSPI_IsBusy(void);

#endif /* __BSP_QSPI_H */
