/**
  ******************************************************************************
  * @file    sdram.h
  * @brief   External SDRAM (FMC bank 1, 32 MB @ 0xC0000000) helper.
  *
  *          sdram_init() runs a destructive read/write self-test over three
  *          regions of the chip and then exposes the first 8 MB as a k_heap so
  *          large, long-lived allocations do not eat into the 192 KB of
  *          internal SRAM.
  ******************************************************************************
  */
#ifndef APP_SDRAM_H_
#define APP_SDRAM_H_

#include <stdbool.h>
#include <stddef.h>

/** Check the FMC device, self-test the chip, create the heap. 0 on success. */
int sdram_init(void);

/** True once sdram_init() succeeded. */
bool sdram_is_ready(void);

/** Allocate / free from the SDRAM heap. NULL / no-op when SDRAM is absent. */
void *sdram_alloc(size_t size);
void sdram_free(void *ptr);

/** Sizes in bytes, for logging. */
size_t sdram_total_bytes(void);
size_t sdram_heap_bytes(void);

/** Bytes still free in the SDRAM heap (0 when SDRAM is absent). */
size_t sdram_free_bytes(void);

#endif /* APP_SDRAM_H_ */
