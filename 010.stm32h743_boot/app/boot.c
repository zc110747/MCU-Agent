/**
  ******************************************************************************
  * @file    app/boot.c
  * @brief   Boot decision state machine.
  *
  *   BSP_Boot_Enter():
  *     1. read system config (magic + CRC32) at 0x081E0000
  *     2. validate app region:
  *          - app_len within [512, APP_SIZE]
  *          - config version components 0..99
  *          - vector table sane (SP in RAM, 8B aligned; reset vector in app)
  *          - version at 0x08021000 == config version
  *          - HMAC-SHA256 over the programmed app image == config.app_hmac
  *     3. all OK  -> start USB, 8 s countdown (LED 300 ms blink, tud_task):
  *                    USB connected during the window -> U-disk mode (no jump;
  *                    count cleared; unplug does NOT resume the countdown,
  *                    only the next reset re-evaluates)
  *                    countdown expires           -> reset hardware, jump
  *     4. any check failed -> stay in bootloader (U-disk mode)
  ******************************************************************************
  */
#include "boot.h"
#include "uart.h"
#include "led.h"
#include "qspi.h"
#include "usb_board.h"
#include "flash_upgrade.h"
#include "flash_secure.h"
#include "hmac_sha256.h"
#include "tusb.h"
#include <string.h>

/* Boot-to-app delay window (ms). Includes the earlier HMAC verify time. */
#define BOOT_JUMP_DELAY_MS       8000U
/* LED toggle interval during the 8 s jump-wait window (full cycle ~600 ms).
   Distinct from the 200 ms fast blink used in U-disk mode. */
#define BOOT_JUMP_LED_BLINK_MS   300U
#define HMAC_CHUNK               4096U

static void log_version(const char *tag, const uint8_t v[4])
{
    BSP_UART_Printf(" %s v%u.%u.%u.%u\r\n", tag, v[0], v[1], v[2], v[3]);
}

/* U-disk mode: pump TinyUSB forever. Never returns. */
static void enter_udisk_mode(void)
{
    uint32_t led_t = HAL_GetTick();

    BSP_UART_Printf("[BOOT] U-disk mode - no jump until next reset\r\n");
    while (1) {
        tud_task();
        if ((HAL_GetTick() - led_t) >= LED_BLINK_MS) {
            led_t = HAL_GetTick();
            BSP_LED_Toggle();
        }
    }
}

/* Deinit everything the bootloader used, then jump to the app entry. */
static void jump_to_app(void)
{
    uint32_t sp    = *(volatile uint32_t *)APP_BASE_ADDR;
    uint32_t reset = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);
    void (*app_entry)(void) = (void (*)(void))reset;

    BSP_UART_Printf("[BOOT] jumping to app @0x%08lX ...\r\n",
                    (unsigned long)APP_BASE_ADDR);

    /* 1. shut down every hardware block we touched */
    HAL_DeInit();                       /* reset all peripherals to reset state */
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);

    /* 2. stop the HAL tick, clear SysTick */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* 3. leave a clean memory/cache/MPU state for the app */
    __disable_irq();
    MPU->CTRL = 0U;                     /* MPU off */

    SCB_InvalidateICache();
    SCB_DisableICache();

    SCB_InvalidateDCache();
    SCB_DisableDCache();
    __DSB();
    __ISB();

    /* 4. vector table + stack + entry.
       Re-enable global interrupts so the app sees a reset-like core state
       (on a real reset PRIMASK is 0). The app may also enable IRQs itself;
       this just guarantees the bootloader does not leak a disabled-PRIMASK
       state across the jump, which would otherwise suppress every interrupt
       (including the SysTick that HAL_Delay relies on). */
    __set_MSP(sp);
    SCB->VTOR = APP_BASE_ADDR;
    __enable_irq();
    app_entry();
    while (1) {
    }
}

/* Compute HMAC-SHA256 over the programmed app image and compare to config. */
static int app_hmac_check(const app_config_t *cfg)
{
    HMAC_SHA256_CTX hctx;
    uint8_t digest[32];
    uint32_t remain = cfg->app_len;
    uint32_t off = 0;

    hmac_sha256_init(&hctx, BOOT_HMAC_KEY, BOOT_HMAC_KEY_LEN);
    while (remain > 0) {
        uint32_t chunk = (remain > HMAC_CHUNK) ? HMAC_CHUNK : remain;
        hmac_sha256_update(&hctx, (const uint8_t *)(APP_BASE_ADDR + off), chunk);
        off += chunk;
        remain -= chunk;
    }
    hmac_sha256_final(&hctx, digest);

    return (memcmp(digest, cfg->app_hmac, 32) == 0) ? 0 : -1;
}

void BSP_Boot_Enter(void)
{
    app_config_t cfg;
    uint8_t v[4];
    uint32_t t0, led_t;
    int app_ok = 0;

    /* ---- 1. system config ---- */
    if (BFLASH_ConfigRead(&cfg) != 0) {
        BSP_UART_Printf("[BOOT] no valid system config - stay in bootloader\r\n");
    } else {
        /* ---- 2. range / shape checks ---- */
        if (cfg.app_len < 512U || cfg.app_len > APP_SIZE) {
            BSP_UART_Printf("[BOOT] config app_len %lu out of range\r\n",
                            (unsigned long)cfg.app_len);
        } else if (cfg.version[0] > 99U || cfg.version[1] > 99U ||
                   cfg.version[2] > 99U || cfg.version[3] > 99U) {
            BSP_UART_Printf("[BOOT] config version component out of 0..99\r\n");
        } else if (!BFLASH_AppVectorValid()) {
            BSP_UART_Printf("[BOOT] app vector table invalid\r\n");
        } else {
            /* ---- 3. HMAC over the programmed image + version at 0x08021000 ---- */
            BFLASH_AppVersionRead(v);
            if (memcmp(v, cfg.version, 4) != 0) {
                BSP_UART_Printf("[BOOT] version @0x08021000 (");
                log_version("img", v);
                BSP_UART_Printf("        ) != config version (");
                log_version("cfg", cfg.version);
                BSP_UART_Printf("        )\r\n");
            } else if (app_hmac_check(&cfg) != 0) {
                BSP_UART_Printf("[BOOT] app HMAC mismatch - image corrupted\r\n");
            } else {
                app_ok = 1;
                BSP_UART_Printf("[BOOT] app image OK");
                log_version("app", cfg.version);
            }
        }
    }

    /* ---- bring up USB so the host can see the QSPI as a U-disk ---- */
    BSP_USB_Init();
    if (!tusb_init()) {
        BSP_UART_Printf("[BOOT] tusb_init FAILED - halting\r\n");
        while (1) {
        }
    }

    if (!app_ok) {
        BSP_UART_Printf("[BOOT] app not ready - U-disk mode (copy a package, then reset)\r\n");
        enter_udisk_mode();
    }

    /* ---- app ready: 8 s jump window (USB connected -> U-disk mode) ---- */
    BSP_UART_Printf("[BOOT] app ready - 8 s jump window (plug USB to enter U-disk)\r\n");
    t0 = HAL_GetTick();
    led_t = t0;
    while ((HAL_GetTick() - t0) < BOOT_JUMP_DELAY_MS) {
        tud_task();
        if (tud_connected()) {
            BSP_UART_Printf("[BOOT] USB connected in window -> U-disk mode\r\n");
            enter_udisk_mode();
        }
        if ((HAL_GetTick() - led_t) >= BOOT_JUMP_LED_BLINK_MS) {
            led_t = HAL_GetTick();
            BSP_LED_Toggle();
        }
    }

    /* window expired, USB not connected -> reset hardware and jump */
    jump_to_app();
}
