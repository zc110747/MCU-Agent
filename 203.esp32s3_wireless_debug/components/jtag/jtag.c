/**
 * @file jtag.c
 * @brief Bit-banged JTAG engine for ESP32-S3.
 *
 * Timing strategy: identical to swd.c - GPIO registers driven directly in the
 * hot path, half-bit delay is a calibrated NOP loop, requested clock clamped
 * to CONFIG_DEBUG_JTAG_MAX_CLOCK_HZ and never exceeded.
 *
 * jtag_ir / jtag_transfer / jtag_read_idcode / jtag_write_abort / jtag_sequence
 * are verbatim ports of ARM's JTAG_DP.c (CMSIS-DAP v2.0.0) with our pin macros.
 */
#include "jtag.h"
#include "swd.h"   /* for swd_reset_pulse() - nRESET shares the SWD pin */

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

static const char *TAG = "jtag";

/* ------------------------------------------------------------------ */
/* Pin configuration (Kconfig driven). TCK/TMS shared with SWD.         */
/* ------------------------------------------------------------------ */
#define TCK_GPIO      CONFIG_DEBUG_SWCLK_GPIO   /* shared with SWCLK */
#define TMS_GPIO      CONFIG_DEBUG_SWDIO_GPIO   /* shared with SWDIO */
#define TDI_GPIO      CONFIG_DEBUG_TDI_GPIO
#define TDO_GPIO      CONFIG_DEBUG_TDO_GPIO
#define NTRST_GPIO    CONFIG_DEBUG_NTRST_GPIO

/* Conservative timing model constants (ESP32-S3 @ up to 240 MHz) */
#define JTAG_HALF_OVERHEAD_CYCLES 24u
#define JTAG_CYCLES_PER_ITER      4u

/* ------------------------------------------------------------------ */
/* Register-level helpers (pins < 32 use the fast bank)                */
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

static inline void pin_out(int gpio, bool high)
{
    if (high) { pin_high(gpio); } else { pin_low(gpio); }
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
static uint32_t s_half_bit_iters = 0;
static uint32_t s_clock_hz = 0;
static uint8_t  s_idle_cycles = 0;

typedef struct {
    uint8_t  count;
    uint8_t  index;
    uint8_t  ir_length[JTAG_TAP_MAX];
    uint16_t ir_before[JTAG_TAP_MAX];
    uint16_t ir_after [JTAG_TAP_MAX];
} jtag_dev_t;

static jtag_dev_t s_dev = {
    .count = 1, .index = 0,
    .ir_length = { 4, 0, 0, 0, 0, 0, 0, 0 },  /* ARM Cortex-M IR = 4 bits */
    .ir_before = { 0 }, .ir_after = { 0 },
};

/* Hot-path macros */
#define JTAG_DELAY()                                    \
    do {                                               \
        for (uint32_t _i = s_half_bit_iters; _i; _i--) { \
            __asm__ __volatile__("nop");               \
        }                                              \
    } while (0)

#define JTAG_TCK_LOW()   pin_low(TCK_GPIO)
#define JTAG_TCK_HIGH()  pin_high(TCK_GPIO)
#define JTAG_CYCLE_TCK()  \
    do { JTAG_TCK_LOW(); JTAG_DELAY(); JTAG_TCK_HIGH(); JTAG_DELAY(); } while (0)

#define JTAG_CYCLE_TDI(tdi) \
    do { PIN_TDI_OUT(tdi); JTAG_TCK_LOW(); JTAG_DELAY(); JTAG_TCK_HIGH(); JTAG_DELAY(); } while (0)

#define JTAG_CYCLE_TDO(tdo) \
    do { JTAG_TCK_LOW(); JTAG_DELAY(); (tdo) = pin_in(TDO_GPIO); JTAG_TCK_HIGH(); JTAG_DELAY(); } while (0)

#define JTAG_CYCLE_TDIO(tdi, tdo) \
    do { PIN_TDI_OUT(tdi); JTAG_TCK_LOW(); JTAG_DELAY(); (tdo) = pin_in(TDO_GPIO); JTAG_TCK_HIGH(); JTAG_DELAY(); } while (0)

#define PIN_TMS_SET()  pin_high(TMS_GPIO)
#define PIN_TMS_CLR()  pin_low(TMS_GPIO)
#define PIN_TDI_OUT(v) pin_out(TDI_GPIO, (v))

/* ------------------------------------------------------------------ */
/* GPIO init                                                           */
/* ------------------------------------------------------------------ */
esp_err_t jtag_init(void)
{
    /* TCK: push-pull output (shared with SWCLK; swd_init also sets it) */
    gpio_config_t tck_conf = {
        .pin_bit_mask = 1uL << TCK_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tck_conf);

    /* TMS: input+output (shared with SWDIO; must stay switchable for SWD).
     * Internal pull-up so the chain idles high if the host ever tristates. */
    gpio_config_t tms_conf = {
        .pin_bit_mask = 1uL << TMS_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tms_conf);

    /* TDI: push-pull output */
    gpio_config_t tdi_conf = {
        .pin_bit_mask = 1uL << TDI_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tdi_conf);

    /* TDO: input only */
    gpio_config_t tdo_conf = {
        .pin_bit_mask = 1uL << TDO_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&tdo_conf);

#if NTRST_GPIO >= 0
    /* nTRST: open-drain style, released (input, target pull-up) by default. */
    gpio_config_t trst_conf = {
        .pin_bit_mask = 1uL << NTRST_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&trst_conf);
#endif

    jtag_set_clock(CONFIG_DEBUG_JTAG_DEFAULT_CLOCK_HZ);
    jtag_set_idle();
    return ESP_OK;
}

void jtag_set_idle(void)
{
    JTAG_TCK_LOW();          /* idle TCK low */
    PIN_TMS_SET();           /* idle TMS high */
    PIN_TDI_OUT(1u);         /* idle TDI high (bypass = 1) */
#if NTRST_GPIO >= 0
    pin_output_en(NTRST_GPIO, false);   /* nTRST released */
#endif
}

esp_err_t jtag_set_clock(uint32_t hz)
{
    if (hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t max_hz = CONFIG_DEBUG_JTAG_MAX_CLOCK_HZ;
    if (hz > max_hz) {
        hz = max_hz;
    }
    uint32_t cpu_hz = (uint32_t)esp_clk_cpu_freq();
    uint32_t half_cycles = cpu_hz / (2u * hz);
    s_half_bit_iters = (half_cycles > JTAG_HALF_OVERHEAD_CYCLES)
                       ? (half_cycles - JTAG_HALF_OVERHEAD_CYCLES) / JTAG_CYCLES_PER_ITER
                       : 0u;
    s_clock_hz = hz;
    ESP_LOGD(TAG, "clock=%u Hz, delay iters=%u (cpu=%u Hz)", (unsigned)hz, (unsigned)s_half_bit_iters, (unsigned)cpu_hz);
    return ESP_OK;
}

uint32_t jtag_get_clock(void)
{
    return s_clock_hz;
}

void jtag_set_idle_cycles(uint8_t n)
{
    s_idle_cycles = n;
}

/* ------------------------------------------------------------------ */
/* Chain config                                                        */
/* ------------------------------------------------------------------ */
void jtag_configure(uint8_t count, const uint8_t *ir_lengths)
{
    if (count > JTAG_TAP_MAX) {
        count = JTAG_TAP_MAX;
    }
    s_dev.count = count;
    uint32_t bits = 0;
    for (uint32_t n = 0; n < count; n++) {
        s_dev.ir_length[n] = ir_lengths[n];
        s_dev.ir_before[n] = (uint16_t)bits;
        bits += ir_lengths[n];
    }
    for (uint32_t n = 0; n < count; n++) {
        bits -= s_dev.ir_length[n];
        s_dev.ir_after[n] = (uint16_t)bits;
    }
    s_dev.index = 0;
}

void jtag_set_device_index(uint8_t index)
{
    if (index < s_dev.count) {
        s_dev.index = index;
    }
}

uint8_t jtag_get_count(void)
{
    return s_dev.count;
}

/* ------------------------------------------------------------------ */
/* JTAG reset (Test-Logic-Reset)                                       */
/* ------------------------------------------------------------------ */
esp_err_t jtag_line_reset(void)
{
    /* TMS high for >=5 TCK cycles drives the TAP through Test-Logic-Reset
     * and leaves it in Run-Test/Idle. */
    PIN_TMS_SET();
    for (int i = 0; i < 8; i++) {
        JTAG_CYCLE_TCK();
    }
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();     /* one more -> Run-Test/Idle */
    PIN_TDI_OUT(1u);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Core transfer (verbatim port of ARM JTAG_DP.c)                      */
/* ------------------------------------------------------------------ */

/* Generate JTAG Sequence */
void jtag_sequence(uint32_t info, const uint8_t *tdi, uint8_t *tdo)
{
    uint32_t i_val, o_val, bit, n, k;

    n = info & 0x3FU;
    if (n == 0U) {
        n = 64U;
    }
    if (info & 0x40U) {          /* JTAG_SEQUENCE_TMS */
        PIN_TMS_SET();
    } else {
        PIN_TMS_CLR();
    }
    while (n) {
        i_val = *tdi++;
        o_val = 0U;
        for (k = 8U; k && n; k--, n--) {
            JTAG_CYCLE_TDIO(i_val, bit);
            i_val >>= 1;
            o_val >>= 1;
            o_val |= bit << 7;
        }
        o_val >>= k;
        if (info & 0x80U) {      /* JTAG_SEQUENCE_TDO */
            *tdo++ = (uint8_t)o_val;
        }
    }
}

/* JTAG Set IR */
void jtag_ir(uint32_t ir)
{
    uint32_t n;

    PIN_TMS_SET();
    JTAG_CYCLE_TCK();            /* Select-DR-Scan */
    JTAG_CYCLE_TCK();            /* Select-IR-Scan */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Capture-IR */
    JTAG_CYCLE_TCK();            /* Shift-IR */

    PIN_TDI_OUT(1U);
    for (n = s_dev.ir_before[s_dev.index]; n; n--) {
        JTAG_CYCLE_TCK();        /* Bypass before data */
    }
    for (n = s_dev.ir_length[s_dev.index] - 1U; n; n--) {
        JTAG_CYCLE_TDI(ir);      /* Set IR bits (except last) */
        ir >>= 1;
    }
    n = s_dev.ir_after[s_dev.index];
    if (n) {
        JTAG_CYCLE_TDI(ir);      /* Set last IR bit */
        PIN_TDI_OUT(1U);
        for (--n; n; n--) {
            JTAG_CYCLE_TCK();    /* Bypass after data */
        }
        PIN_TMS_SET();
        JTAG_CYCLE_TCK();        /* Bypass & Exit1-IR */
    } else {
        PIN_TMS_SET();
        JTAG_CYCLE_TDI(ir);      /* Set last IR bit & Exit1-IR */
    }
    JTAG_CYCLE_TCK();            /* Update-IR */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Idle */
    PIN_TDI_OUT(1U);
}

/* JTAG Transfer I/O. request: A[3:2] RnW APnDP (same DAP_TRANSFER encoding).
 * returns ACK[2:0] (DAP_TRANSFER_OK/WAIT/FAULT/...). */
uint8_t jtag_transfer(uint32_t request, uint32_t *data)
{
    uint32_t ack, bit, val, n;

    PIN_TMS_SET();
    JTAG_CYCLE_TCK();            /* Select-DR-Scan */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Capture-DR */
    JTAG_CYCLE_TCK();            /* Shift-DR */

    for (n = s_dev.index; n; n--) {
        JTAG_CYCLE_TCK();        /* Bypass before data */
    }

    JTAG_CYCLE_TDIO(request >> 1, bit);   /* Set RnW, Get ACK.0 */
    ack  = bit << 1;
    JTAG_CYCLE_TDIO(request >> 2, bit);   /* Set A2,  Get ACK.1 */
    ack |= bit << 0;
    JTAG_CYCLE_TDIO(request >> 3, bit);   /* Set A3,  Get ACK.2 */
    ack |= bit << 2;

    if (ack != JTAG_TRANSFER_OK) {         /* exit on error */
        PIN_TMS_SET();
        JTAG_CYCLE_TCK();        /* Exit1-DR */
        goto exit;
    }

    if (request & JTAG_TRANSFER_RnW) {
        /* Read Transfer */
        val = 0U;
        for (n = 31U; n; n--) {
            JTAG_CYCLE_TDO(bit);  /* Get D0..D30 */
            val  |= bit << 31;
            val >>= 1;
        }
        n = s_dev.count - s_dev.index - 1U;
        if (n) {
            JTAG_CYCLE_TDO(bit);  /* Get D31 */
            for (--n; n; n--) {
                JTAG_CYCLE_TCK();  /* Bypass after data */
            }
            PIN_TMS_SET();
            JTAG_CYCLE_TCK();      /* Bypass & Exit1-DR */
        } else {
            PIN_TMS_SET();
            JTAG_CYCLE_TDO(bit);   /* Get D31 & Exit1-DR */
        }
        val |= bit << 31;
        if (data) { *data = val; }
    } else {
        /* Write Transfer */
        val = data ? *data : 0U;
        for (n = 31U; n; n--) {
            JTAG_CYCLE_TDI(val);   /* Set D0..D30 */
            val >>= 1;
        }
        n = s_dev.count - s_dev.index - 1U;
        if (n) {
            JTAG_CYCLE_TDI(val);   /* Set D31 */
            for (--n; n; n--) {
                JTAG_CYCLE_TCK();  /* Bypass after data */
            }
            PIN_TMS_SET();
            JTAG_CYCLE_TCK();      /* Bypass & Exit1-DR */
        } else {
            PIN_TMS_SET();
            JTAG_CYCLE_TDI(val);   /* Set D31 & Exit1-DR */
        }
    }

exit:
    JTAG_CYCLE_TCK();            /* Update-DR */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Idle */
    PIN_TDI_OUT(1U);

    /* Idle cycles */
    n = s_idle_cycles;
    while (n--) {
        JTAG_CYCLE_TCK();
    }
    return (uint8_t)ack;
}

/* JTAG Read IDCODE register of the selected TAP */
uint32_t jtag_read_idcode(void)
{
    uint32_t bit, val, n;

    PIN_TMS_SET();
    JTAG_CYCLE_TCK();            /* Select-DR-Scan */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Capture-DR */
    JTAG_CYCLE_TCK();            /* Shift-DR */

    for (n = s_dev.index; n; n--) {
        JTAG_CYCLE_TCK();        /* Bypass before data */
    }

    val = 0U;
    for (n = 31U; n; n--) {
        JTAG_CYCLE_TDO(bit);      /* Get D0..D30 */
        val  |= bit << 31;
        val >>= 1;
    }
    PIN_TMS_SET();
    JTAG_CYCLE_TDO(bit);         /* Get D31 & Exit1-DR */
    val |= bit << 31;

    JTAG_CYCLE_TCK();            /* Update-DR */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Idle */
    return val;
}

/* JTAG Write ABORT register (DPACC, A2=A3=0, write) */
void jtag_write_abort(uint32_t data)
{
    uint32_t n;

    PIN_TMS_SET();
    JTAG_CYCLE_TCK();            /* Select-DR-Scan */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Capture-DR */
    JTAG_CYCLE_TCK();            /* Shift-DR */

    for (n = s_dev.index; n; n--) {
        JTAG_CYCLE_TCK();        /* Bypass before data */
    }

    PIN_TDI_OUT(0U);
    JTAG_CYCLE_TCK();            /* Set RnW=0 (Write) */
    JTAG_CYCLE_TCK();            /* Set A2=0 */
    JTAG_CYCLE_TCK();            /* Set A3=0 */

    for (n = 31U; n; n--) {
        JTAG_CYCLE_TDI(data);    /* Set D0..D30 */
        data >>= 1;
    }
    n = s_dev.count - s_dev.index - 1U;
    if (n) {
        JTAG_CYCLE_TDI(data);    /* Set D31 */
        for (--n; n; n--) {
            JTAG_CYCLE_TCK();    /* Bypass after data */
        }
        PIN_TMS_SET();
        JTAG_CYCLE_TCK();        /* Bypass & Exit1-DR */
    } else {
        PIN_TMS_SET();
        JTAG_CYCLE_TDI(data);    /* Set D31 & Exit1-DR */
    }

    JTAG_CYCLE_TCK();            /* Update-DR */
    PIN_TMS_CLR();
    JTAG_CYCLE_TCK();            /* Idle */
    PIN_TDI_OUT(1U);
}

/* ------------------------------------------------------------------ */
/* Connect self-test (nRESET via shared SWD pin + line reset + IDCODE) */
/* ------------------------------------------------------------------ */
esp_err_t jtag_connect(void)
{
    uint32_t idcode = 0;
    jtag_set_idle();
    /* Bring target to clean post-reset state. nRESET is the shared SWD pin;
     * we reuse the SWD engine's pulse so we do not double-claim GPIO6. */
    swd_reset_pulse();
    jtag_set_idle();

    jtag_line_reset();
    vTaskDelay(pdMS_TO_TICKS(10));

    jtag_set_device_index(0);
    jtag_ir(JTAG_IR_IDCODE);
    idcode = jtag_read_idcode();

    ESP_LOGI(TAG, "JTAG connect self-test: IDCODE=0x%08" PRIX32, idcode);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Raw pins for DAP_SWJ_Pins                                          */
/* ------------------------------------------------------------------ */
void jtag_pin_tck(bool high) { pin_out(TCK_GPIO, high); }
void jtag_pin_tms(bool high) { pin_out(TMS_GPIO, high); }
void jtag_pin_tdi(bool high) { pin_output_en(TDI_GPIO, true); pin_out(TDI_GPIO, high); }

bool jtag_pin_tdi_in(void)
{
    pin_output_en(TDI_GPIO, true);
    return pin_in(TDI_GPIO);
}

bool jtag_pin_tdo_in(void)
{
    return pin_in(TDO_GPIO);
}

void jtag_pin_ntrst_assert(bool asserted)
{
#if NTRST_GPIO >= 0
    if (asserted) {
        pin_low(NTRST_GPIO);
        pin_output_en(NTRST_GPIO, true);
    } else {
        pin_output_en(NTRST_GPIO, false);
    }
#else
    (void)asserted;
#endif
}

bool jtag_ntrst_read(void)
{
#if NTRST_GPIO >= 0
    return pin_in(NTRST_GPIO) == 0;    /* low = asserted */
#else
    return false;
#endif
}
