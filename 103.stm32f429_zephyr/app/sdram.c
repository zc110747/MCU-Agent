/**
  ******************************************************************************
  * @file    sdram.c
  * @brief   External SDRAM: self-test and application heap.
  *
  *  The controller itself is brought up by Zephyr's FMC SDRAM driver
  *  (drivers/memc/memc_stm32_sdram.c, POST_KERNEL priority 0) from the
  *  devicetree node &sdram, so by the time main() runs the chip is already
  *  out of its power-up sequence. All this file has to do is prove the bus is
  *  actually working and hand out memory.
  *
  *  Self-test: three windows (start / middle / end) x four patterns, the same
  *  coverage 101.stm32f429_net uses. It is destructive, so it must run before
  *  the heap is handed out -- it does.
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/logging/log.h>

#include "sdram.h"

LOG_MODULE_REGISTER(sdram, LOG_LEVEL_INF);

#define SDRAM_NODE   DT_NODELABEL(sdram1)
#define SDRAM_BASE   ((uintptr_t)DT_REG_ADDR(SDRAM_NODE))
#define SDRAM_SPAN   ((size_t)DT_REG_SIZE(SDRAM_NODE))

/* Hand out the first 8 MB; the remaining 24 MB stay addressable for future
 * frame buffers / larger pools without changing this module. */
#define SDRAM_HEAP_SIZE (8U * 1024U * 1024U)

#define TEST_WORDS   64U
#define TEST_WINDOW  256U /* bytes reserved at each probe point */

static struct k_heap sdram_heap;
static bool sdram_ready;

static int sdram_selftest(void)
{
	static const uint32_t patterns[] = {
		0xAA55AA55U, 0x55AA55AAU, 0xDEADBEEFU, 0x12345678U
	};
	volatile uint32_t *base = (volatile uint32_t *)SDRAM_BASE;
	const size_t offsets[] = { 0U, SDRAM_SPAN / 2U, SDRAM_SPAN - TEST_WINDOW };
	size_t r;
	size_t i;
	size_t p;

	for (r = 0U; r < ARRAY_SIZE(offsets); r++) {
		volatile uint32_t *win = base + (offsets[r] / sizeof(uint32_t));

		for (i = 0U; i < TEST_WORDS; i++) {
			win[i] = 0U;
		}

		for (p = 0U; p < ARRAY_SIZE(patterns); p++) {
			for (i = 0U; i < TEST_WORDS; i++) {
				win[i] = patterns[p];
			}
			for (i = 0U; i < TEST_WORDS; i++) {
				if (win[i] != patterns[p]) {
					LOG_ERR("mismatch at 0x%08x (expected 0x%08x, got 0x%08x)",
						(unsigned int)(offsets[r] + i * sizeof(uint32_t)),
						(unsigned int)patterns[p],
						(unsigned int)win[i]);
					return -EIO;
				}
			}
		}
	}

	return 0;
}

int sdram_init(void)
{
	const struct device *fmc = DEVICE_DT_GET(DT_NODELABEL(fmc));
	const struct device *sdram_dev = DEVICE_DT_GET(DT_NODELABEL(sdram));
	int rc;

	if (!device_is_ready(fmc) || !device_is_ready(sdram_dev)) {
		LOG_ERR("FMC SDRAM controller not ready");
		return -ENODEV;
	}

	rc = sdram_selftest();
	if (rc != 0) {
		LOG_ERR("SDRAM self-test failed (%d)", rc);
		return rc;
	}

	k_heap_init(&sdram_heap, (void *)SDRAM_BASE, SDRAM_HEAP_SIZE);
	sdram_ready = true;

	LOG_INF("SDRAM ready: %u MB at 0x%08x (self-test OK), heap %u MB",
		(unsigned int)(SDRAM_SPAN / (1024U * 1024U)),
		(unsigned int)SDRAM_BASE,
		(unsigned int)(SDRAM_HEAP_SIZE / (1024U * 1024U)));

	return 0;
}

bool sdram_is_ready(void)
{
	return sdram_ready;
}

void *sdram_alloc(size_t size)
{
	if (!sdram_ready || size == 0U) {
		return NULL;
	}

	return k_heap_alloc(&sdram_heap, size, K_NO_WAIT);
}

void sdram_free(void *ptr)
{
	if (sdram_ready && ptr != NULL) {
		k_heap_free(&sdram_heap, ptr);
	}
}

size_t sdram_total_bytes(void)
{
	return SDRAM_SPAN;
}

size_t sdram_heap_bytes(void)
{
	return SDRAM_HEAP_SIZE;
}

size_t sdram_free_bytes(void)
{
	struct sys_memory_stats stats;

	if (!sdram_ready) {
		return 0U;
	}

	if (sys_heap_runtime_stats_get(&sdram_heap.heap, &stats) != 0) {
		return 0U;
	}

	return stats.free_bytes;
}
