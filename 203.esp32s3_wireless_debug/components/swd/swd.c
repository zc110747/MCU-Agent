/**
 * @file swd.c
 * @brief Bit-banged SWD engine for ESP32-S3.
 *
 * Timing strategy:
 *  - GPIOs are driven through the GPIO output/set-clear registers directly
 *    (GPIO.out_w1ts / out_w1tc / enable_w1ts / enable_w1tc). No HAL calls in
 *    the hot path, no FreeRTOS calls, no printf.
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
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"
#include "esp_log.h"

static const char *TAG = "swd";

/* ------------------------------------------------------------------ */
/* Pin configuration (Kconfig driven - no hardcoded GPIOs here)        */
/* ------------------------------------------------------------------ */
#define SWDIO_GPIO  CONFIG_DEBUG_SWDIO_GPIO
#define SWCLK_GPIO  CONFIG_DEBUG_SWCLK_GPIO
#define NRESET_GPIO CONFIG_DEBUG_NRESET_GPIO

/* Conservative timing model constants (ESP32-S3 @ up to 240 MHz) */
#define SWD_HALF_OVERHEAD_CYCLES 24u /* gpio writes + loop setup        */
#define SWD_CYCLES_PER_ITER      4u  /* one NOP-loop iteration           */

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
static uint32_t s_clock_hz = 0;
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
    uint32_t half_cycles = cpu_hz / (2u * hz);
    s_half_bit_iters = (half_cycles > SWD_HALF_OVERHEAD_CYCLES)
                       ? (half_cycles - SWD_HALF_OVERHEAD_CYCLES) / SWD_CYCLES_PER_ITER
                       : 0u;
    s_clock_hz = hz;
    ESP_LOGD(TAG, "clock=%u Hz, delay iters=%u (cpu=%u Hz)", (unsigned)hz, (unsigned)s_half_bit_iters, (unsigned)cpu_hz);
    return ESP_OK;
}

uint32_t swd_get_clock(void)
{
    return s_clock_hz;
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
    pin_output_en(SWDIO_GPIO, true);
    pin_high(SWDIO_GPIO);
    for (int i = 0; i < 60; i++) {
        SW_CLOCK_CYCLE();
    }
    pin_low(SWCLK_GPIO);
    pin_high(SWDIO_GPIO);
    return ESP_OK;
}

esp_err_t swd_jtag_to_swd(void)
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
 * Keeping it in IRAM (CPU-internal, no wait states) lets the clock scale to
 * MHz with stable edges. The pin_* helpers are static inline and are pulled
 * into IRAM automatically with this function. */
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
    ESP_LOGD(TAG, "transfer: illegal ACK=0x%x (req=0x%02x) - target not in SWD mode / no link",
             (unsigned)(ack ? ack : SWD_ACK_NO_RESPONSE), request);
    for (n = s_turnaround + 33u; n; n--) {
        SW_CLOCK_CYCLE();
    }
    pin_output_en(SWDIO_GPIO, true);
    pin_high(SWDIO_GPIO);
    return (uint8_t)(ack ? ack : SWD_ACK_NO_RESPONSE);
}

/* ------------------------------------------------------------------ */
/* Convenience accessors                                               */
/* ------------------------------------------------------------------ */
static esp_err_t swd_wait_ok(uint8_t request, uint32_t *data, int retries)
{
    uint8_t ack;
    do {
        ack = swd_transfer(request, data);
    } while (ack == SWD_ACK_WAIT && retries-- > 0);
    return (ack == SWD_ACK_OK) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t swd_read_dp(uint8_t addr, uint32_t *data)
{
    return swd_wait_ok(SWD_REQ(addr, 0u, 1u), data, 64);
}

esp_err_t swd_write_dp(uint8_t addr, uint32_t data)
{
    return swd_wait_ok(SWD_REQ(addr, 0u, 0u), &data, 64);
}

esp_err_t swd_read_ap(uint8_t addr, uint32_t *data)
{
    /* AP reads are posted: the value arrives in the following transfer */
    uint32_t tmp = 0;
    esp_err_t err = swd_wait_ok(SWD_REQ(addr, 1u, 1u), &tmp, 64);
    if (err != ESP_OK) {
        return err;
    }
    return swd_wait_ok(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), data, 64);
}

esp_err_t swd_write_ap(uint8_t addr, uint32_t data)
{
    esp_err_t err = swd_wait_ok(SWD_REQ(addr, 1u, 0u), &data, 64);
    if (err != ESP_OK) {
        return err;
    }
    /* Flush the posted write */
    return swd_wait_ok(SWD_REQ(SWD_DP_ADDR_RDBUFF, 0u, 1u), NULL, 64);
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

/* Assert nRESET for ~100ms then release. Used at connect time ("connect
 * under reset") so the target returns to its boot-ROM SWD-enabled state
 * even if a previously loaded application remapped PA13/PA14. No-op if
 * nRESET is not wired (GPIO < 0). */
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
/* Full protocol connect                                               */
/* ------------------------------------------------------------------ */
esp_err_t swd_connect(void)
{
    uint32_t dpidr = 0;
    swd_set_idle();

    /* Bring the target to a clean post-reset state where the boot ROM has
     * the SWD port enabled (covers apps that remap PA13/PA14). */
    swd_reset_pulse();
    swd_set_idle();

    /* Switch from (possible) JTAG: line reset, magic, line reset */
    swd_line_reset();
    swd_jtag_to_swd();
    swd_line_reset();
    vTaskDelay(pdMS_TO_TICKS(10));

    /* First DP read after line reset must return a valid IDCODE */
    uint32_t parity_probe = 0;
    uint8_t ack = swd_transfer(SWD_REQ(SWD_DP_ADDR_IDCODE, 0u, 1u), &dpidr);
    if (ack != SWD_ACK_OK) {
        ESP_LOGW(TAG, "connect: DPIDR ack=0x%02x", ack);
        return ESP_ERR_NOT_FOUND;
    }
    (void)parity_probe;

    /* Clear sticky errors */
    swd_write_dp(SWD_DP_ADDR_ABORT, 0x0000001Fu);

    ESP_LOGI(TAG, "SWD connected, DPIDR=0x%08" PRIX32, dpidr);
    return ESP_OK;
}
