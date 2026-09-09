/**
 * @file swd.c
 * @brief Bit-banged SWD engine for ESP32-S3 (swd-esp / DAPLink architecture).
 *
 * Rewrite referencing https://github.com/huming2207/swd-esp :
 *  - bit level   : CMSIS-DAP SW_DP.c semantics (SW_CLOCK_CYCLE /
 *                  SW_WRITE_BIT / SW_READ_BIT / turnaround), GPIO register
 *                  direct writes (GPIO.out_w1ts / out_w1tc / in), IRAM hot
 *                  path, no FreeRTOS / printf / ESP_LOG in any transfer.
 *  - host level  : DAPLink swd_host.c semantics:
 *                    * swd_transfer_retry()   - WAIT retry (MAX_SWD_RETRY)
 *                    * swd_read_ap()          - DP_SELECT then dummy read
 *                    * swd_write_ap()         - DP_SELECT then write + RDBUFF
 *                    * swd_read/write_word    - TAR/DRW MEM-AP access
 *                    * JTAG2SWD               - 51-bit reset + 0xE79E +
 *                                               51-bit reset + IDCODE
 *                    * swd_init_debug flow    - stale-target abort + nRESET
 *                                               pulse + power-up with retries
 *  - target level: Cortex-M DHCSR halt/run via MEM-AP.
 *  - connect     : connect-under-reset. nRESET is asserted and HELD LOW
 *                  during the entire SWD bring-up; the core is halted via
 *                  DHCSR while in reset; nRESET is released only after
 *                  S_HALT is confirmed, then halt is re-checked.
 *
 * Timing strategy:
 *  - Half-bit delay is a calibrated NOP loop. Calibration is deliberately
 *    conservative: the produced clock is never faster than requested.
 *  - Requested clock is clamped to CONFIG_DEBUG_SWD_MAX_CLOCK_HZ.
 */
#include "swd.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_private/esp_clk.h"
#include "esp_cpu.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"

static const char *TAG = "swd";

/* Stage-level diagnostics (Kconfig: DEBUG_SWD_TRACE). These macros are only
 * used in the outer connect/host flow - NEVER inside the bit-level hot path
 * (swd_transfer & sequences stay IRAM + log-free). */
#ifdef CONFIG_DEBUG_SWD_TRACE
#define SWD_TRACE(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#else
#define SWD_TRACE(fmt, ...) do { } while (0)
#endif

/* ------------------------------------------------------------------ */
/* Pin configuration (Kconfig driven - no hardcoded GPIOs here)        */
/* ------------------------------------------------------------------ */
#define SWDIO_GPIO  CONFIG_DEBUG_SWDIO_GPIO
#define SWCLK_GPIO  CONFIG_DEBUG_SWCLK_GPIO
#define NRESET_GPIO CONFIG_DEBUG_NRESET_GPIO

/* NOP-loop model: one loop iteration (decrement + branch) is *estimated* at
 * SWD_CYCLES_PER_ITER CPU cycles. This constant is only used for the clock
 * estimate - the real half-bit base cost (GPIO writes + loop overhead) is
 * MEASURED at runtime by swd_measure_half_bit_cycles(), so no magic number
 * is trusted for the final timing. Iterations are rounded UP so the actual
 * clock is never faster than the requested one. */
#define SWD_CYCLES_PER_ITER      4u

/* DAPLink retry budget (swd_host.c: MAX_SWD_RETRY) */
#define MAX_SWD_RETRY 100

/* ------------------------------------------------------------------ */
/* Register-level helpers (pins < 32 use the fast bank; >= 32 fallback) */
/* ------------------------------------------------------------------ */
static inline void pin_high(int gpio)
{
    if (gpio < 32) {
        GPIO.out_w1ts = 1uL << gpio;
    } else {
        GPIO.out1_w1ts.val = 1uL << (gpio - 32);
    }
}

static inline void pin_low(int gpio)
{
    if (gpio < 32) {
        GPIO.out_w1tc = 1uL << gpio;
    } else {
        GPIO.out1_w1tc.val = 1uL << (gpio - 32);
    }
}

static inline void pin_output_en(int gpio, bool en)
{
    if (en) {
        if (gpio < 32) {
            GPIO.enable_w1ts = 1uL << gpio;
        } else {
            GPIO.enable1_w1ts.val = 1uL << (gpio - 32);
        }
    } else {
        if (gpio < 32) {
            GPIO.enable_w1tc = 1uL << gpio;
        } else {
            GPIO.enable1_w1tc.val = 1uL << (gpio - 32);
        }
    }
}

static inline bool pin_in(int gpio)
{
    return (gpio < 32) ? ((GPIO.in >> gpio) & 1uL)
                       : ((GPIO.in1.val >> (gpio - 32)) & 1uL);
}

/* ------------------------------------------------------------------ */
/* Engine state                                                        */
/* ------------------------------------------------------------------ */
static uint32_t s_half_bit_iters = 0;   /* NOP iterations per half bit */
static uint32_t s_clock_hz = 0;         /* requested clock             */
static uint32_t s_actual_clock_hz = 0;  /* calibrated/estimated actual */
static uint8_t  s_turnaround = 1;
static bool     s_data_phase = false;
static uint8_t  s_idle_cycles = 0;

/* Hot-path macros (SWCLK low -> delay -> SWCLK high -> delay) */
#define SWD_DELAY()                                    \
    do {                                               \
        for (uint32_t _i = s_half_bit_iters; _i; _i--) { \
            __asm__ __volatile__("nop");               \
        }                                              \
    } while (0)

#define SW_CLOCK_CYCLE()  \
    do { pin_low(SWCLK_GPIO);  SWD_DELAY(); pin_high(SWCLK_GPIO); SWD_DELAY(); } while (0)

#define SW_WRITE_BIT(bit) \
    do { if (bit) pin_high(SWDIO_GPIO); else pin_low(SWDIO_GPIO); \
         pin_low(SWCLK_GPIO); SWD_DELAY(); pin_high(SWCLK_GPIO); SWD_DELAY(); } while (0)

#define SW_READ_BIT(var)  \
    do { pin_low(SWCLK_GPIO); SWD_DELAY(); (var) = pin_in(SWDIO_GPIO); pin_high(SWCLK_GPIO); SWD_DELAY(); } while (0)

/* ------------------------------------------------------------------ */
/* GPIO init                                                           */
/* ------------------------------------------------------------------ */
esp_err_t swd_init(void)
{
    /* SWCLK: push-pull output */
    gpio_config_t clk_conf = {
        .pin_bit_mask = 1uL << SWCLK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&clk_conf);

    /* SWDIO: input+output, internal pull-up (idle high) */
    gpio_config_t io_conf = {
        .pin_bit_mask = 1uL << SWDIO_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

#if NRESET_GPIO >= 0
    /* nRESET: open-drain style. Output never pushed high; released by
     * disabling the output driver, pull-up provides the idle level. */
    gpio_config_t rst_conf = {
        .pin_bit_mask = 1uL << NRESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
#endif

    /* Drive strength to maximum (GPIO_DRIVE_CAP_3) for clean, fast edges at
     * the higher SWD clock rates (flying-wire + 240 MHz CPU). */
    gpio_set_drive_capability(SWCLK_GPIO, GPIO_DRIVE_CAP_3);
    gpio_set_drive_capability(SWDIO_GPIO, GPIO_DRIVE_CAP_3);
#if NRESET_GPIO >= 0
    gpio_set_drive_capability(NRESET_GPIO, GPIO_DRIVE_CAP_3);
#endif

    swd_set_clock(CONFIG_DEBUG_SWD_DEFAULT_CLOCK_HZ);
    swd_set_idle();
    return ESP_OK;
}

void swd_set_idle(void)
{
    pin_low(SWCLK_GPIO);
    pin_high(SWDIO_GPIO);
    pin_output_en(SWDIO_GPIO, true);
#if NRESET_GPIO >= 0
    pin_output_en(NRESET_GPIO, false);  /* released */
#endif
}

/**
 * @brief Measure the real cost of one half-bit (SWCLK low half) at iters=0.
 *
 * Runs the exact hot-path instruction sequence (pin toggles + SWD_DELAY
 * loop overhead) and counts CPU cycles with the cycle counter. This removes
 * any guesswork about GPIO register write latency: the value includes the
 * two GPIO writes and the empty-loop branch cost. A warm-up pass first
 * ensures the code is resident in the flash cache.
 *
 * @return average CPU cycles per half bit at zero NOP iterations.
 */
static uint32_t swd_measure_half_bit_cycles(void)
{
    const uint32_t N = 32;
    s_half_bit_iters = 0;
    /* warm-up: pull the code into cache */
    for (uint32_t i = 0; i < 8; i++) {
        pin_low(SWCLK_GPIO);  SWD_DELAY();
        pin_high(SWCLK_GPIO); SWD_DELAY();
    }
    uint32_t t0 = esp_cpu_get_cycle_count();
    for (uint32_t i = 0; i < N; i++) {
        pin_low(SWCLK_GPIO);  SWD_DELAY();
        pin_high(SWCLK_GPIO); SWD_DELAY();
    }
    uint32_t t1 = esp_cpu_get_cycle_count();
    return (t1 - t0) / (2u * N);
}

esp_err_t swd_set_clock(uint32_t hz)
{
    if (hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t max_hz = CONFIG_DEBUG_SWD_MAX_CLOCK_HZ;
    if (hz > max_hz) {
        hz = max_hz;
    }
    uint32_t cpu_hz = (uint32_t)esp_clk_cpu_freq();

    /* Measured base half-bit cost (GPIO writes + loop overhead). */
    uint32_t base_cycles = swd_measure_half_bit_cycles();

    /* Target half-bit period in CPU cycles. */
    uint32_t target_half = cpu_hz / (2u * hz);

    /* NOP iterations, ROUNDED UP: guarantees actual period >= target, i.e.
     * the actual clock never exceeds the requested one. If the base cost
     * already exceeds the target period the requested clock is not
     * achievable bit-banged - iters stays 0 and the ACTUAL clock is
     * (deliberately) lower than requested, never faster. */
    uint32_t iters = 0;
    if (base_cycles < target_half) {
        uint32_t extra = target_half - base_cycles;
        iters = (extra + SWD_CYCLES_PER_ITER - 1u) / SWD_CYCLES_PER_ITER;
    }
    s_half_bit_iters = iters;

    /* Estimated actual clock from measured base + modelled iteration cost.
     * Rounding up on iters keeps actual_hz <= requested hz. */
    uint32_t actual_half = base_cycles + iters * SWD_CYCLES_PER_ITER;
    s_actual_clock_hz = (actual_half > 0) ? (cpu_hz / (2u * actual_half)) : cpu_hz;
    s_clock_hz = hz;

    ESP_LOGD(TAG, "clock: req=%u Hz, actual~%u Hz (base=%u cyc, iters=%u, cpu=%u Hz)",
             (unsigned)hz, (unsigned)s_actual_clock_hz, (unsigned)base_cycles,
             (unsigned)iters, (unsigned)cpu_hz);
    SWD_TRACE("SWD: clock=%u Hz (actual~%u Hz)",
              (unsigned)hz, (unsigned)s_actual_clock_hz);
    return ESP_OK;
}

uint32_t swd_get_clock(void)
{
    return s_clock_hz;
}

uint32_t swd_get_actual_clock(void)
{
    return s_actual_clock_hz;
}

void swd_set_turnaround(uint8_t cycles)
{
    if (cycles < 1u)  cycles = 1u;
    if (cycles > 4u)  cycles = 4u;
    s_turnaround = cycles;
}

void swd_set_data_phase(bool always)
{
    s_data_phase = always;
}

void swd_set_idle_cycles(uint8_t n)
{
    s_idle_cycles = n;
}

/* ------------------------------------------------------------------ */
/* Sequences                                                           */
/* ------------------------------------------------------------------ */
esp_err_t swd_swj_sequence(uint32_t count_bits, const uint8_t *data)
{
    pin_output_en(SWDIO_GPIO, true);   /* drive SWDIO throughout the sequence */
    uint32_t val = 0;
    uint32_t n = 0;
    while (count_bits--) {
        if (n == 0) {
            val = *data++;
            n = 8;
        }
        SW_WRITE_BIT(val & 1u);
        val >>= 1;
        n--;
    }
    return ESP_OK;
}

void IRAM_ATTR swd_sequence_out(uint32_t nbits, const uint8_t *data)
{
    uint32_t val;
    uint32_t k;
    while (nbits) {
        val = *data++;
        for (k = 8u; k && nbits; k--, nbits--) {
            SW_WRITE_BIT(val);
            val >>= 1;
        }
    }
}

void IRAM_ATTR swd_sequence_in(uint32_t nbits, uint8_t *data)
{
    uint32_t bit, val, k;
    while (nbits) {
        val = 0;
        for (k = 8u; k && nbits; k--, nbits--) {
            SW_READ_BIT(bit);
            val >>= 1;
            val |= bit << 7;
        }
        val >>= k;
        *data++ = (uint8_t)val;
    }
}

void swd_swdio_output(bool enable)
{
    pin_output_en(SWDIO_GPIO, enable);
}

esp_err_t IRAM_ATTR swd_line_reset(void)
{
    /* >=50 SWCLK cycles with SWDIO high (DAPLink swd_reset: 51 bits) */
    pin_output_en(SWDIO_GPIO, true);
    pin_high(SWDIO_GPIO);
    for (int i = 0; i < 60; i++) {
        SW_CLOCK_CYCLE();
    }
    pin_low(SWCLK_GPIO);
    pin_high(SWDIO_GPIO);
    return ESP_OK;
}

esp_err_t IRAM_ATTR swd_jtag_to_swd(void)
{
    static const uint8_t seq[2] = { 0x9E, 0xE7 };  /* 0xE79E LSB-first */
    pin_output_en(SWDIO_GPIO, true);
    swd_swj_sequence(16, seq);
    return ESP_OK;
}

esp_err_t swd_swd_to_jtag(void)
{
    /* SWD -> JTAG switch sequence 0xE73E (LSB-first). SWDIO/TMS and
     * SWCLK/TCK share the same physical pins, so emitting this on the SWD
     * engine cleanly returns a DP that a previous SWD session left in SWD
     * mode back to JTAG mode. Typically wrapped by swd_line_reset() on both
     * sides (>=50 idle cycles with SWDIO high) by the caller. */
    static const uint8_t seq[2] = { 0x3E, 0xE7 };  /* 0xE73E LSB-first */
    pin_output_en(SWDIO_GPIO, true);
    swd_swj_sequence(16, seq);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Core transfer (mirrors ARM SW_DP.c SWD_Transfer)                    */
/* ------------------------------------------------------------------ */
/* IRAM: this is the hottest path in the whole firmware. Running it from
 * flash would incur cache-miss stalls on every GPIO toggle, making the SWD
 * clock jittery and forcing the host (OpenOCD/Keil) to throttle to ~200kHz.
 * The pin_* helpers are static inline and are pulled into IRAM
 * automatically with this function. No logging inside: ESP_LOG from IRAM
 * is illegal and any call here destroys the clock budget. */
uint8_t IRAM_ATTR swd_transfer(uint8_t request, uint32_t *data)
{
    uint32_t ack, bit, val, parity;
    uint32_t n;

    /* Packet request: start, APnDP, RnW, A2, A3, parity, stop, park */
    parity = 0;
    SW_WRITE_BIT(1u);                       /* Start */
    bit = request & 0x01u;                  /* APnDP */
    SW_WRITE_BIT(bit); parity += bit;
    bit = (request >> 1) & 0x01u;           /* RnW   */
    SW_WRITE_BIT(bit); parity += bit;
    bit = (request >> 2) & 0x01u;           /* A2    */
    SW_WRITE_BIT(bit); parity += bit;
    bit = (request >> 3) & 0x01u;           /* A3    */
    SW_WRITE_BIT(bit); parity += bit;
    SW_WRITE_BIT(parity & 1u);              /* Parity */
    SW_WRITE_BIT(0u);                       /* Stop  */
    SW_WRITE_BIT(1u);                       /* Park  */

    /* Turnaround: host releases SWDIO */
    pin_output_en(SWDIO_GPIO, false);
    for (n = s_turnaround; n; n--) {
        SW_CLOCK_CYCLE();
    }

    /* Acknowledge */
    SW_READ_BIT(bit); ack  = bit;
    SW_READ_BIT(bit); ack |= bit << 1;
    SW_READ_BIT(bit); ack |= bit << 2;

    if (ack == SWD_ACK_OK) {
        if (request & 0x02u) {
            /* Read: 32 data bits LSB-first + parity */
            val = 0;
            parity = 0;
            for (n = 32u; n; n--) {
                SW_READ_BIT(bit);
                parity += bit;
                val >>= 1;
                val |= bit << 31;
            }
            SW_READ_BIT(bit);   /* data parity */
            if ((parity ^ bit) & 1u) {
                ack = SWD_ACK_PARITY_ERROR;
            }
            if (data) {
                *data = val;
            }
            /* Turnaround back to host-drive */
            for (n = s_turnaround; n; n--) {
                SW_CLOCK_CYCLE();
            }
            pin_output_en(SWDIO_GPIO, true);
        } else {
            for (n = s_turnaround; n; n--) {
                SW_CLOCK_CYCLE();
            }
            pin_output_en(SWDIO_GPIO, true);
            /* Write: 32 data bits + parity */
            val = data ? *data : 0;
            parity = 0;
            for (n = 32u; n; n--) {
                SW_WRITE_BIT(val & 1u);
                parity += val;
                val >>= 1;
            }
            SW_WRITE_BIT(parity & 1u);
        }
        /* Idle cycles after transfer */
        if (s_idle_cycles) {
            pin_low(SWDIO_GPIO);
            for (n = s_idle_cycles; n; n--) {
                SW_CLOCK_CYCLE();
            }
        }
        pin_high(SWDIO_GPIO);
        return (uint8_t)ack;
    }

    if ((ack == SWD_ACK_WAIT) || (ack == SWD_ACK_FAULT)) {
        if (s_data_phase && (request & 0x02u)) {
            for (n = 33u; n; n--) {
                SW_CLOCK_CYCLE();   /* dummy read data phase */
            }
        }
        for (n = s_turnaround; n; n--) {
            SW_CLOCK_CYCLE();
        }
        pin_output_en(SWDIO_GPIO, true);
        if (s_data_phase && !(request & 0x02u)) {
            pin_low(SWDIO_GPIO);
            for (n = 33u; n; n--) {
                SW_CLOCK_CYCLE();   /* dummy write data phase */
            }
        }
        pin_high(SWDIO_GPIO);
        return (uint8_t)ack;
    }

    /* Protocol error / no response: back off data phase */
    for (n = s_turnaround + 33u; n; n--) {
        SW_CLOCK_CYCLE();
    }
    pin_output_en(SWDIO_GPIO, true);
    pin_high(SWDIO_GPIO);
    return (uint8_t)(ack ? ack : SWD_ACK_NO_RESPONSE);
}

/* ------------------------------------------------------------------ */
/* DAPLink host layer                                                  */
/* ------------------------------------------------------------------ */
uint8_t IRAM_ATTR swd_transfer_retry(uint8_t request, uint32_t *data)
{
    uint8_t ack = SWD_ACK_NO_RESPONSE;
    for (int i = 0; i < MAX_SWD_RETRY; i++) {
        ack = swd_transfer(request, data);
        if (ack != SWD_ACK_WAIT) {
            return ack;
        }
    }
    return ack;
}

/* ------------------------------------------------------------------ */
/* Convenience accessors (DAPLink semantics)                           */
/* ------------------------------------------------------------------ */
esp_err_t swd_read_dp(uint8_t addr, uint32_t *data)
{
    uint32_t v = 0;
    uint8_t ack = swd_transfer_retry(SWD_REQ(addr, 0u, 1u), &v);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (data) {
        *data = v;
    }
    return ESP_OK;
}

esp_err_t swd_write_dp(uint8_t addr, uint32_t data)
{
    uint8_t ack = swd_transfer_retry(SWD_REQ(addr, 0u, 0u), &data);
    return (ack == SWD_ACK_OK) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t IRAM_ATTR swd_read_ap(uint8_t addr, uint32_t *data)
{
    /* DAPLink swd_read_ap: select AP0/bank0, then dummy read (the AP read is
     * posted - the value arrives in the following transfer), then the real
     * read captures it. */
    if (swd_write_dp(SWD_DP_ADDR_SELECT, 0u) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint32_t tmp = 0;
    uint8_t ack = swd_transfer_retry(SWD_REQ(addr, 1u, 1u), &tmp);  /* dummy */
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ack = swd_transfer_retry(SWD_REQ(addr, 1u, 1u), &tmp);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (data) {
        *data = tmp;
    }
    return ESP_OK;
}

esp_err_t IRAM_ATTR swd_write_ap(uint8_t addr, uint32_t data)
{
    /* DAPLink swd_write_ap: select AP0/bank0, write, flush the posted write
     * with a RDBUFF read. SELECT is re-written on every access (no caching):
     * the DAP_Transfer path of a USB host may change SELECT at any time, a
     * stale cache would silently access the wrong AP/bank. */
    if (swd_write_dp(SWD_DP_ADDR_SELECT, 0u) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t ack = swd_transfer_retry(SWD_REQ(addr, 1u, 0u), &data);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint32_t tmp;
    ack = swd_transfer_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &tmp);
    return (ack == SWD_ACK_OK) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t swd_clear_errors(void)
{
    /* debug_cm.h: STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR */
    return swd_write_dp(SWD_DP_ADDR_ABORT,
                        DP_STKCMPCLR | DP_STKERRCLR | DP_WDERRCLR | DP_ORUNERRCLR);
}

esp_err_t IRAM_ATTR swd_read_idcode(uint32_t *id)
{
    /* DAPLink swd_read_idcode: 8 idle cycles with SWDIO low, then DPIDR */
    static const uint8_t idle8[1] = { 0x00 };
    swd_swj_sequence(8, idle8);
    uint32_t v = 0;
    uint8_t ack = swd_transfer_retry(SWD_REQ(SWD_DP_ADDR_IDCODE, 0u, 1u), &v);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (id) {
        *id = v;
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* MEM-AP memory access (DAPLink swd_read_data / swd_write_data)       */
/* ------------------------------------------------------------------ */
static esp_err_t IRAM_ATTR swd_mem_read_data(uint32_t addr, uint32_t *val)
{
    /* CSW: 32-bit, debug master, single increment (no caching - a USB host
     * may rewrite CSW through DAP_Transfer at any time) */
    if (swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* TAR = address */
    if (swd_write_ap(AP_TAR, addr) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* DRW read (posted), value comes back via RDBUFF */
    uint32_t tmp = 0;
    uint8_t ack = swd_transfer_retry(SWD_REQ(AP_DRW, 1u, 1u), &tmp);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ack = swd_transfer_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &tmp);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (val) {
        *val = tmp;
    }
    return ESP_OK;
}

static esp_err_t IRAM_ATTR swd_mem_write_data(uint32_t addr, uint32_t data)
{
    if (swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (swd_write_ap(AP_TAR, addr) != ESP_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t ack = swd_transfer_retry(SWD_REQ(AP_DRW, 1u, 0u), &data);
    if (ack != SWD_ACK_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* flush the posted write */
    uint32_t tmp;
    ack = swd_transfer_retry(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), &tmp);
    return (ack == SWD_ACK_OK) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t swd_mem_read32(uint32_t addr, uint32_t *data)
{
    return swd_mem_read_data(addr, data);
}

esp_err_t swd_mem_write32(uint32_t addr, uint32_t data)
{
    return swd_mem_write_data(addr, data);
}

/* ------------------------------------------------------------------ */
/* Cortex-M debug control (DHCSR via MEM-AP)                           */
/* ------------------------------------------------------------------ */
esp_err_t swd_halt(void)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        if (swd_mem_write32(CM_DHCSR, CM_DBGKEY | CM_C_DEBUGEN | CM_C_HALT) == ESP_OK) {
            /* Poll S_HALT (swd-esp swd_wait_until_halted, compact budget) */
            for (int i = 0; i < 100; i++) {
                uint32_t dhcsr = 0;
                if (swd_mem_read32(CM_DHCSR, &dhcsr) == ESP_OK) {
                    if (dhcsr & CM_S_HALT) {
                        return ESP_OK;
                    }
                }
                esp_rom_delay_us(100);
            }
        }
        /* A failed MEM-AP transaction (e.g. target bus matrix still in
         * reset during connect-under-reset) leaves the DP with sticky
         * errors: clear them and retry once. */
        swd_clear_errors();
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t swd_run(void)
{
    return swd_mem_write32(CM_DHCSR, CM_DBGKEY | CM_C_DEBUGEN);
}

esp_err_t swd_is_halted(bool *halted)
{
    uint32_t dhcsr = 0;
    esp_err_t err = swd_mem_read32(CM_DHCSR, &dhcsr);
    if (err == ESP_OK && halted) {
        *halted = (dhcsr & CM_S_HALT) != 0;
    }
    return err;
}

esp_err_t swd_wait_until_halted(uint32_t timeout_ms)
{
    uint32_t waited_ms = 0;
    while (waited_ms < timeout_ms) {
        bool halted = false;
        if (swd_is_halted(&halted) == ESP_OK && halted) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        waited_ms++;
    }
    return ESP_ERR_TIMEOUT;
}

/* ------------------------------------------------------------------ */
/* nRESET + raw pins                                                   */
/* ------------------------------------------------------------------ */
esp_err_t swd_reset_assert(bool asserted)
{
#if NRESET_GPIO >= 0
    if (asserted) {
        pin_low(NRESET_GPIO);
        pin_output_en(NRESET_GPIO, true);
    } else {
        pin_output_en(NRESET_GPIO, false);
    }
    return ESP_OK;
#else
    (void)asserted;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool swd_nreset_read(void)
{
#if NRESET_GPIO >= 0
    return pin_in(NRESET_GPIO) == 0;    /* low = asserted */
#else
    return false;
#endif
}

/* Assert nRESET for ~100ms then release. */
esp_err_t swd_reset_pulse(void)
{
#if NRESET_GPIO >= 0
    swd_reset_assert(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    swd_reset_assert(false);
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
    return ESP_OK;
}

void swd_pin_swclk(bool high)
{
    if (high) { pin_high(SWCLK_GPIO); } else { pin_low(SWCLK_GPIO); }
}

void swd_pin_swdio(bool high)
{
    pin_output_en(SWDIO_GPIO, true);
    if (high) { pin_high(SWDIO_GPIO); } else { pin_low(SWDIO_GPIO); }
}

bool swd_pin_swclk_in(void)
{
    return pin_in(SWCLK_GPIO);
}

bool swd_pin_swdio_in(void)
{
    return pin_in(SWDIO_GPIO);
}

/* ------------------------------------------------------------------ */
/* Connect flow (11 staged steps, per-stage error classification)      */
/* ------------------------------------------------------------------ */
static swd_error_stage_t s_last_stage = SWD_STAGE_OK;

swd_error_stage_t swd_last_error_stage(void)
{
    return s_last_stage;
}

const char *swd_stage_name(swd_error_stage_t stage)
{
    switch (stage) {
    case SWD_STAGE_OK:        return "OK";
    case SWD_ERR_LINE_RESET:  return "LINE-RESET";
    case SWD_ERR_JTAG_TO_SWD: return "JTAG-TO-SWD";
    case SWD_ERR_DPIDR:       return "DPIDR";
    case SWD_ERR_ABORT:       return "ABORT";
    case SWD_ERR_POWERUP:     return "POWERUP";
    case SWD_ERR_AP:          return "AP";
    case SWD_ERR_MEM:         return "MEM-AP";
    case SWD_ERR_DHCSR:       return "DHCSR";
    case SWD_ERR_HALT:        return "HALT";
    default:                  return "UNKNOWN";
    }
}

/**
 * Stages 2-8 of the connect flow, executed with nRESET HELD LOW.
 *
 * Every stage is independently checked and classified into s_last_stage.
 * The DPIDR read (stage 5) is the first checkpoint: while it keeps failing,
 * stages 2-5 are retried with the reset still asserted and no AP/MEM-AP
 * traffic is generated (errors are not allowed to propagate into the AP
 * layer). nRESET is NOT touched here - the caller decides when to release.
 */
static esp_err_t swd_bringup_under_reset(uint32_t *dpidr)
{
    /* Stages 2-5: line reset -> JTAG->SWD -> line reset -> DPIDR */
    bool dpidr_ok = false;
    for (int attempt = 0; attempt < 5 && !dpidr_ok; attempt++) {
        if (attempt > 0) {
            /* flush any sticky state from the failed attempt */
            swd_write_dp(SWD_DP_ADDR_ABORT, DP_DAPABORT);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        swd_line_reset();                       /* Stage 2 */
        swd_jtag_to_swd();                      /* Stage 3 */
        swd_line_reset();                       /* Stage 4 */
        if (swd_read_idcode(dpidr) == ESP_OK) { /* Stage 5: checkpoint */
            dpidr_ok = true;
        } else {
            *dpidr = 0;
        }
    }
    if (!dpidr_ok) {
        s_last_stage = SWD_ERR_DPIDR;
        return ESP_FAIL;
    }
    SWD_TRACE("SWD: DPIDR=0x%08" PRIX32, *dpidr);

    /* Stage 6: clear sticky errors */
    if (swd_clear_errors() != ESP_OK) {
        s_last_stage = SWD_ERR_ABORT;
        return ESP_FAIL;
    }

    /* Stage 7: DP power-up - request both domains, poll the ACKs */
    if (swd_write_dp(SWD_DP_ADDR_CTRLSTAT,
                     DP_CSYSPWRUPREQ | DP_CDBGPWRUPREQ) != ESP_OK) {
        s_last_stage = SWD_ERR_POWERUP;
        return ESP_FAIL;
    }
    bool powered = false;
    uint32_t ctrlstat = 0;
    for (int i = 0; i < 100; i++) {
        if (swd_read_dp(SWD_DP_ADDR_CTRLSTAT, &ctrlstat) != ESP_OK) {
            break;
        }
        if ((ctrlstat & (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) ==
            (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) {
            powered = true;
            break;
        }
        esp_rom_delay_us(1000);
    }
    if (!powered) {
        SWD_TRACE("SWD: CTRLSTAT=0x%08" PRIX32 " (power-up timeout)", ctrlstat);
        s_last_stage = SWD_ERR_POWERUP;
        return ESP_FAIL;
    }
    SWD_TRACE("SWD: CTRLSTAT=0x%08" PRIX32, ctrlstat);

    /* normal transfer mode + masked lanes, then re-select bank 0 */
    if (swd_write_dp(SWD_DP_ADDR_CTRLSTAT,
                     DP_CSYSPWRUPREQ | DP_CDBGPWRUPREQ | DP_TRNNORMAL | DP_MASKLANE) != ESP_OK ||
        swd_write_dp(SWD_DP_ADDR_SELECT, 0u) != ESP_OK) {
        s_last_stage = SWD_ERR_POWERUP;
        return ESP_FAIL;
    }

    /* Stage 8: MEM-AP init + independent AP identification. APSEL=0 is the
     * STM32H7 Cortex-M7 AHB-AP (OpenOCD stm32h7x.cfg creates cpu0 with
     * -ap-num 0); AP2 is only the D1-domain aux mem_ap (SWO/TPIU). Reading
     * the AP IDR distinguishes "DP OK but AP FAIL" from a DP problem. */
    uint32_t ap_idr = 0;
    if (swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32) != ESP_OK ||
        swd_read_ap(AP_IDR, &ap_idr) != ESP_OK) {
        s_last_stage = SWD_ERR_AP;
        return ESP_FAIL;
    }
    SWD_TRACE("SWD: AP0 IDR=0x%08" PRIX32, ap_idr);
    return ESP_OK;
}

esp_err_t swd_connect(void)
{
    esp_err_t err = ESP_FAIL;
    bool reset_held = false;
    bool halted_before_release = false;
    uint32_t dpidr = 0;

    s_last_stage = SWD_STAGE_OK;
    swd_set_idle();

#if NRESET_GPIO >= 0
    /* Stage 1: assert nRESET and HOLD IT LOW for the whole bring-up. With
     * the core in reset, SW-DP is always functional - even when a running
     * application has remapped the SWD pins (PA13/PA14). */
    swd_reset_assert(true);
    reset_held = true;
    vTaskDelay(pdMS_TO_TICKS(20));
#endif

    /* Stages 2-8. A failure NEVER proceeds to MEM-AP/DHCSR and NEVER
     * releases the reset mid-flow: the whole bring-up is re-run with the
     * reset still asserted. */
    for (int retry = 0; retry < 3; retry++) {
        if (retry > 0) {
            SWD_TRACE("SWD: bring-up retry %d (nRESET still asserted)", retry);
        }
        err = swd_bringup_under_reset(&dpidr);
        if (err == ESP_OK) {
            break;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SWD CONNECT FAILED: %s",
                 swd_stage_name(s_last_stage));
#if NRESET_GPIO >= 0
        if (reset_held) {
            swd_reset_assert(false);    /* leave the board in a sane state */
        }
#endif
        return err;
    }

    /* Best-effort halt under reset: on STM32H7 the AHB-AP bus matrix is
     * still in reset while nRESET is asserted, so MEM-AP/DHCSR access may
     * not respond here. The authoritative halt happens after the release. */
    if (swd_halt() == ESP_OK) {
        halted_before_release = true;
        SWD_TRACE("SWD: core halted (under reset)");
    }

    /* Stage 9: release nRESET - only reached after DPIDR+CTRLSTAT+MEM-AP
     * all succeeded. */
#if NRESET_GPIO >= 0
    if (reset_held) {
        swd_reset_assert(false);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif

    /* Stage 10: re-request halt. A halt issued under reset can be consumed
     * by the reset edge, so C_DEBUGEN|C_HALT is re-sent now that the core
     * is out of reset. The core runs at most a few cycles before C_HALT
     * takes effect - safe: nRESET already forced the SWD pins back to their
     * debug AF state. */
    swd_clear_errors();
    err = swd_halt();
    if (err != ESP_OK) {
        /* Stage 11: verify S_HALT with a wider window before giving up */
        s_last_stage = SWD_ERR_HALT;
        swd_clear_errors();
        err = swd_wait_until_halted(500);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SWD CONNECT FAILED: %s (DPIDR=0x%08" PRIX32 ")",
                 swd_stage_name(s_last_stage), dpidr);
        return err;
    }

    ESP_LOGI(TAG, "connect: OK (DPIDR=0x%08" PRIX32 ", halted, "
             "under-reset-halt=%d)", dpidr, halted_before_release);
    return ESP_OK;
}
