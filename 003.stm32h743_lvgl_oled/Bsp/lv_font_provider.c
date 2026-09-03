/**
  ******************************************************************************
  * @file    lv_font_provider.c
  * @brief   Font engine selection - see lv_font_cfg.h for the switch itself.
  ******************************************************************************
  */
#include "log.h"
#include "lv_font_provider.h"
#include "lv_font_gbk.h"
#include "lv_font_harmony.h"
#include "lvgl_font.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

static FontEngine_t s_engine = FONT_ENGINE_GBK;

/**
  * @brief  The always-available engine: compiled-in bitmaps + GBKxx.FON.
  */
static const lv_font_t *gbk_font(uint16_t size)
{
    switch (size)
    {
        case 12u:  return &lv_font_gbk_12;
        case 24u:  return &lv_font_gbk_24;
        case 32u:  return &lv_font_gbk_32;
        case 16u:
        default:   return &lv_font_gbk_16;
    }
}

/*---------------------------------------------------------------------------*/
/* CTF file discovery                                                         */
/*---------------------------------------------------------------------------*/

static int stem_cmp(const char *name, const char *stem)
{
    size_t i;

    for (i = 0u; stem[i] != '\0'; i++)
    {
        if (name[i] != stem[i])
        {
            return 0;
        }
    }
    return (name[i] == '\0');
}

/**
  * Swap a trailing ".ctf" for ".ttf" in place.
  * @retval RT_OK when the buffer really ended in .ctf
  */
static GlobalType_t ctf_to_ttf(char *path, uint32_t path_size)
{
    size_t len = strlen(path);

    if ((len < 5u) || ((len + 1u) > path_size))
    {
        return RT_FAIL;
    }

    if (strcmp(&path[len - 4u], ".ctf") != 0)
    {
        return RT_FAIL;
    }

    path[len - 3u] = 't';
    path[len - 2u] = 't';
    path[len - 1u] = 'f';
    return RT_OK;
}

GlobalType_t lv_font_provider_locate(char *ctf_path,
                                     char *ttf_path,
                                     uint32_t path_size)
{
    DIR           dir;
    FILINFO       fi;
    FIL           probe;
    char          found[128];
    int           have = 0;

    if ((ctf_path == NULL) || (ttf_path == NULL) || (path_size < 32u))
    {
        return RT_FAIL;
    }

    ctf_path[0] = '\0';
    ttf_path[0] = '\0';

    /* 1. The configured name. */
    if (snprintf(found, sizeof(found), "%s/%s.ctf", CTF_FONT_DIR, CTF_FONT_NAME)
        < (int)sizeof(found))
    {
        if (f_stat(found, &fi) == FR_OK)
        {
            have = 1;
        }
    }

    /* 2. Otherwise the first .ctf in the directory. */
    if (!have && (f_opendir(&dir, CTF_FONT_DIR) == FR_OK))
    {
        for (;;)
        {
            if (f_readdir(&dir, &fi) != FR_OK)
            {
                break;
            }
            if (fi.fname[0] == '\0')
            {
                break;    /* end of directory */
            }
            if ((fi.fattrib & AM_DIR) != 0u)
            {
                continue;
            }
            if (strlen(fi.fname) < 5u)
            {
                continue;
            }
            if (strcmp(&fi.fname[strlen(fi.fname) - 4u], ".ctf") != 0)
            {
                continue;
            }
            if (snprintf(found, sizeof(found), "%s/%s", CTF_FONT_DIR, fi.fname)
                < (int)sizeof(found))
            {
                have = 1;
            }
            break;
        }
        (void)f_closedir(&dir);
    }

    if (!have)
    {
        return RT_FAIL;
    }

    if (strlen(found) >= path_size)
    {
        return RT_FAIL;
    }

    strncpy(ctf_path, found, path_size - 1u);
    ctf_path[path_size - 1u] = '\0';

    strncpy(ttf_path, found, path_size - 1u);
    ttf_path[path_size - 1u] = '\0';

    /* The index is useless without the exact .ttf it was generated from. */
    if (ctf_to_ttf(ttf_path, path_size) != RT_OK)
    {
        return RT_FAIL;
    }

    if (f_open(&probe, ttf_path, FA_READ) != FR_OK)
    {
        return RT_FAIL;
    }
    (void)f_close(&probe);

    return RT_OK;
}

/*---------------------------------------------------------------------------*/
/* Engine selection                                                           */
/*---------------------------------------------------------------------------*/

static GlobalType_t start_ctf(void)
{
    static char ctf_path[128];
    static char ttf_path[128];

    if (lv_font_provider_locate(ctf_path, ttf_path, (uint32_t)sizeof(ctf_path)) != RT_OK)
    {
        PRINT_LOG("[FONT] CTF: no index under %s\r\n", CTF_FONT_DIR);
        return RT_FAIL;
    }

    if (lvgl_font_engine_init(ctf_path, ttf_path) != RT_OK)
    {
        PRINT_LOG("[FONT] CTF: %s unusable\r\n", ctf_path);
        return RT_FAIL;
    }

    s_engine = FONT_ENGINE_CTF;
    PRINT_LOG("[FONT] engine: CTF index + TTF\r\n");
    PRINT_LOG("[FONT]   ctf  %s\r\n", ctf_path);
    PRINT_LOG("[FONT]   ttf  %s\r\n", ttf_path);
    return RT_OK;
}

GlobalType_t lv_font_provider_init(void)
{
#if LV_FONT_ENGINE == LV_FONT_ENGINE_CTF
    if (start_ctf() == RT_OK)
    {
        return RT_OK;
    }
#elif LV_FONT_ENGINE == LV_FONT_ENGINE_HARMONYOS
    if (lv_font_harmony_init() == RT_OK)
    {
        s_engine = FONT_ENGINE_HARMONYOS;
        PRINT_LOG("[FONT] engine: HarmonyOS Sans TC (%s)\r\n",
               lv_font_harmony_file());
        return RT_OK;
    }
#endif

    s_engine = FONT_ENGINE_GBK;
    PRINT_LOG("[FONT] engine: GBK bitmaps\r\n");
    return RT_FAIL;
}

FontEngine_t lv_font_provider_engine(void)
{
    return s_engine;
}

const char *lv_font_provider_name(void)
{
    switch (s_engine)
    {
        case FONT_ENGINE_CTF:         return "鸿蒙CTF";
        case FONT_ENGINE_HARMONYOS:   return "鸿蒙TTF";
        case FONT_ENGINE_GBK:
        default:                      return "GBK点阵";
    }
}

const lv_font_t *lv_font_provider_get(uint16_t size)
{
#if LV_FONT_ENGINE == LV_FONT_ENGINE_CTF
    if (s_engine == FONT_ENGINE_CTF)
    {
        const lv_font_t *f = lvgl_font_get(size);

        if (f != NULL)
        {
            return f;
        }
    }
#elif LV_FONT_ENGINE == LV_FONT_ENGINE_HARMONYOS
    if (s_engine == FONT_ENGINE_HARMONYOS)
    {
        const lv_font_t *f = lv_font_harmony_get(size);

        if (f != NULL)
        {
            return f;
        }
    }
#endif
    return gbk_font(size);
}

const lv_font_t *lv_font_provider_default(void)
{
    return lv_font_provider_get(16u);
}
