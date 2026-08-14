/**
  ******************************************************************************
  * @file    screen_cap.c
  * @brief   Capture the current OLED framebuffer to a JPEG on the SD card.
  *
  *  Flow: read the shadow framebuffer (drv_spi_oled.c keeps it in sync with
  *  every pixel pushed to the panel) -> software baseline JPEG encode
  *  (jpeg_enc.c) -> write "1:/catch/HH-MM-SS-NNN.jpg", de-duplicating with a
  *  "_k" suffix when the name already exists.
  ******************************************************************************
  */
#include "screen_cap.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "drv_spi_oled.h"
#include "drv_rtc.h"
#include "jpeg_enc.h"
#include "sram_pool.h"
#include "ff.h"

/* Worst-case 240x240 baseline JPEG we measured is ~67 KB (random noise);
 * real OLED content (UI / text / low-colour NES frames) is far smaller.
 * 80 KB gives comfortable headroom.  Allocated from the dynamic SRAM pool
 * (DTCM first, RAM_D2 as fallback) so it does not pin static RAM. */
#define CAP_JPEG_BUF       (80u * 1024u)
#define CAP_NAME_MAX       96u

/* Small deterministic xorshift PRNG - the hardware RNG is not enabled in this
 * firmware, and a 3-digit collision-avoidance nonce does not need to be
 * cryptographically strong.  The state is mixed with the wall-clock each call
 * so the same second does not always yield the same number. */
static uint32_t s_cap_rng = 0x9E3779B9u;

static uint32_t cap_rand3(void)
{
    s_cap_rng ^= s_cap_rng << 13;
    s_cap_rng ^= s_cap_rng >> 17;
    s_cap_rng ^= s_cap_rng << 5;
    return s_cap_rng % 1000u;        /* 0 .. 999 */
}

/**
  * @brief  Capture the current OLED content as a JPEG on the SD card.
  *
  * @param[out] out_path  Receives the saved path (e.g. "1:/catch/08-42-13-017.jpg").
  *                        May be NULL if the caller does not need it.
  * @param[in]  path_size Size of @p out_path in bytes.
  * @retval  0            Success, @p out_path filled.
  * @retval <0            Failure (see codes below).
  *            -1  bad argument
  *            -2  RTC unavailable
  *            -3  too many de-dup collisions
  *            -4  SD open error (not a collision)
  *            -5  out of memory for the JPEG buffer
  *            -6  JPEG encode failed
  *            -7  SD write failed
  */
int screen_cap_capture(char *out_path, int path_size)
{
    if ((out_path == NULL) || (path_size <= 0))
    {
        return -1;
    }

    /* --- timestamp ------------------------------------------------------- */
    rtc_datetime_t dt;
    if (drv_rtc_get(&dt) != RT_OK)
    {
        return -2;
    }

    /* mix wall-clock into the PRNG so repeated captures in the same second
     * still get different nonces */
    s_cap_rng ^= ((uint32_t)dt.second << 16) |
                 ((uint32_t)dt.minute << 8)  | (uint32_t)dt.hour;
    uint32_t nonce = cap_rand3();

    char base[32];
    (void)snprintf(base, sizeof(base), "%02u-%02u-%02u-%03lu",
                   (unsigned)dt.hour, (unsigned)dt.minute,
                   (unsigned)dt.second, (unsigned long)nonce);

    /* --- ensure the catch directory exists ------------------------------- */
    (void)f_mkdir(CAP_DIR);          /* FR_EXIST when already present: fine */

    /* --- pick a free filename (de-dup with _k suffix) -------------------- */
    char path[CAP_NAME_MAX];
    FIL  fil;
    FRESULT fr;
    int  k = 0;

    for (;;)
    {
        if (k == 0)
        {
            (void)snprintf(path, sizeof(path), "%s/%s.jpg", CAP_DIR, base);
        }
        else
        {
            (void)snprintf(path, sizeof(path), "%s/%s_%d.jpg", CAP_DIR, base, k);
        }

        fr = f_open(&fil, path, FA_WRITE | FA_CREATE_NEW);
        if (fr == FR_OK)
        {
            break;                      /* got a free name */
        }
        if (fr == FR_EXIST)
        {
            k++;
            if (k > 9999)
            {
                return -3;
            }
            continue;
        }
        return -4;                      /* any other open error */
    }

    /* --- encode the shadow framebuffer ----------------------------------- */
    const uint16_t *fb = LCD_GetFrameBuffer();
    int w = 0, h = 0;
    LCD_GetResolution(&w, &h);

    sram_region_t reg = SRAM_REGION_DTCM;
    uint8_t *jbuf = (uint8_t *)sram_alloc(reg, CAP_JPEG_BUF, 4U);
    if (jbuf == NULL)
    {
        jbuf = (uint8_t *)sram_alloc(SRAM_REGION_D2, CAP_JPEG_BUF, 4U);
        reg  = SRAM_REGION_D2;
    }
    if (jbuf == NULL)
    {
        f_close(&fil);
        return -5;
    }

    int jlen = jpeg_encode_rgb565(fb, w, h, jbuf, (int)CAP_JPEG_BUF);
    if (jlen <= 0)
    {
        sram_free(reg, jbuf);
        f_close(&fil);
        return -6;
    }

    /* --- write the file -------------------------------------------------- */
    UINT bw = 0;
    fr = f_write(&fil, jbuf, (UINT)jlen, &bw);
    f_close(&fil);
    sram_free(reg, jbuf);

    if ((fr != FR_OK) || ((int)bw != jlen))
    {
        return -7;
    }

    (void)strncpy(out_path, path, (size_t)path_size - 1U);
    out_path[path_size - 1U] = '\0';
    return 0;
}
