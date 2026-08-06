/**
 * @file    app_vision.c
 * @brief   Camera -> CMSIS-NN person detection -> display pipeline.
 *
 * Triple-buffer architecture (需求: 三缓冲, 不可拼接/撕裂)
 * ---------------------------------------------------------------------------
 *   OV5640 320x240 RGB565
 *     -> DCMI centre crop 192x192, DMA hardware double buffer s_frame[0]/[1]
 *        @0x24000000 (uncached, .dma_buffer)                      <- "采集" 侧
 *     -> memcpy into s_disp[] (cacheable AXI .axi_ram)            <- 快照 / "读取"
 *     -> 2x2 box filter + luma -> int8[96*96]  (pd_preprocess)   <- "计算"
 *     -> CMSIS-NN MobileNetV1-0.25 person detection (pd_run)
 *     -> single 192x192 blit to the 240x240 panel, centred(24,24) <- "显示"
 *
 * 关键点(三缓冲/防撕裂): s_frame[0]/[1]=CMOS 写的采集双缓冲; s_disp=第三块
 * *显示* 缓冲(仅 CPU 写, CMOS/DMA 永不写). 帧完成后立刻 memcpy 到 s_disp
 * (此刻 DMA 已切到另一块采集缓冲), LCD 随后刷新 s_disp. 刷新期间 CMOS 无法
 * 更新被显示区域 -> 不可能撕裂/拼接, 显示的永远是完整单帧.
 *
 * Display & detection (需求: 分数一直显示, 人在/不在用图标, 1s 检测一次, 去抖)
 * ---------------------------------------------------------------------------
 *   - 显示(相机预览)每帧实时刷新, 但 NN 检测节流到 1Hz (省 CPU)
 *   - 顶部左侧: 分数 "S:0.87" 始终显示 (每次检测更新)
 *   - 顶部右侧: 图标  -> 绿色实心人形 = 人在 ; 灰色空心人形 + 红斜杠 = 不在
 *       * 检测到人 -> 立即显示 ; 检测移除人 -> 延迟 2s 才切回 (去抖防闪)
 *   - 底部:     帧率 / NN 耗时
 */
#include <stdio.h>
#include <string.h>

#include "app_vision.h"
#include "drv_dcmi.h"
#include "drv_spi_oled.h"
#include "pd_infer.h"
#include "logger.h"

/* ------------------------------------------------------------------ layout */
#define VIEW_W          CAPTURE_WIDTH                       /* 192            */
#define VIEW_H          CAPTURE_HEIGHT                      /* 192            */
#define VIEW_X          ((LCD_WIDTH  - VIEW_W) / 2)         /* 24             */
#define VIEW_Y          ((LCD_HEIGHT - VIEW_H) / 2)         /* 24             */

/* 分数阈值: s_res.score(sigmoid, 0.0~1.0) >= 此值判为"人在".
 * 0.60 偏严(容易漏检真人), 降为 0.50 以减少漏检; 仍高于空场景基线(~0.30). */
#define APP_PERSON_THRESHOLD  0.50f

/* 检测节流(ms): NN 推理很贵, 每两次检测最小间隔 500ms => 2Hz 检测.
 * 显示(相机预览)仍按帧率实时刷新, 只有"计算/判定"降到 2Hz, 省 CPU. */
#define APP_DETECT_INTERVAL_MS  500

/* 人在/不在 去抖(ms): 检测到人 -> 立即显示(无延迟);
 * 检测移除人 -> 延迟 1s 才切回"不在"(避免模型瞬时掉分导致图标乱闪). */
#define APP_PERSON_HOLD_MS     1000

/* 顶部"人在/不在"图标 (贴在 24px 顶边栏右侧) */
#define ICON_W          24
#define ICON_H          24
#define ICON_X          (LCD_WIDTH - ICON_W - 4)   /* 右侧留 4px */
#define ICON_Y          0

/* RGB565 颜色 (与 LCD_CopyBuffer 期望的存储格式一致) */
#define RGB565_BLACK    0x0000u
#define RGB565_GREEN    0x07E0u
#define RGB565_GREY     0x7BEFu
#define RGB565_RED      0xF800u

#define TEXT_CACHE_LEN  31   /* 文字缓存安全长度(不含结束符) */

/* ------------------------------------------------------------------- state */
AXI_RAM static uint16_t s_disp[CAPTURE_PIXELS];   /* 3rd buffer: DISPLAY. CPU-only; CMOS/DMA never writes -> refresh can't tear */

/* 两块图标位图(人在 / 不在), 初始化时生成一次 */
static uint16_t s_icon_present[ICON_W * ICON_H];
static uint16_t s_icon_absent [ICON_W * ICON_H];

static uint8_t   s_pd_ready;
static uint32_t   s_overruns;
static uint32_t   s_stat_tick;
static uint32_t   s_infer_us;
static uint32_t   s_loop_count;
static uint8_t    s_loop_fps;
static char       s_line_top[TEXT_CACHE_LEN + 1];
static char       s_line_bottom[TEXT_CACHE_LEN + 1];

/* 检测节流: 上次 NN 检测的 tick; 上一帧消费的采集缓冲序号(确认乒乓交替) */
static uint32_t   s_last_detect_tick;
/* 去抖: 最近一次"检测到人"的 tick (用于 2s 移除延迟) */
static uint32_t   s_last_person_tick;
static uint8_t    s_cap_idx;

/* 当前 *已显示* 的"人在"状态(去抖后): 出现立即置 1, 移除延迟 2s 才清 0 */
static int        s_present = 0;
/* 图标当前显示状态(仅在变化时重绘, 防闪烁) */
static int        s_icon_state = -1;

/* NN 结果缓存(跨循环保留, 使非检测帧也能显示上一次分数) */
static pd_result_t s_res;

static void show_text(uint16_t x, uint16_t y, char *cache, size_t cap, const char *text);
static void icon_build(uint16_t *buf, int present);
static int  icon_in_silhouette(int x, int y);

/* -------------------------------------------------------------------- init */
GlobalType_t app_vision_init(void)
{
    if (driver_spi_oled_init() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "oled init failed");
        return RT_FAIL;
    }

    LCD_SetBackColor(LCD_BLACK);
    LCD_SetColor(LCD_WHITE);
    LCD_Clear();
    LCD_SetAsciiFont(&ASCII_Font24);
    LCD_DisplayText(VIEW_X, VIEW_Y + 80, "booting...");

    if (pd_init() != 0)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "pd_init failed");
        LCD_Clear();
        LCD_DisplayText(4, 100, "NN INIT FAIL");
        return RT_FAIL;
    }
    pd_set_threshold(APP_PERSON_THRESHOLD);   /* 与显示判定保持一致的阈值 */
    s_pd_ready = 1;
    PRINT_LOG(LOG_INFO, HAL_GetTick(),
              "Person detection ready: %dx%d int8-in (CMSIS-NN)",
              PD_INPUT_W, PD_INPUT_H);

    /* 预先生成"人在 / 不在"两块图标位图 */
    icon_build(s_icon_present, 1);
    icon_build(s_icon_absent, 0);

    if (drv_dcmi_init() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "camera init failed");
        LCD_Clear();
        LCD_DisplayText(4, 100, "CAM INIT FAIL");
        return RT_FAIL;
    }

    memset(s_disp, 0, sizeof(s_disp));
    LCD_Clear();

    if (drv_dcmi_start() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "camera start failed");
        return RT_FAIL;
    }

    s_overruns         = g_dcmi_overruns;
    s_stat_tick        = HAL_GetTick();
    s_last_detect_tick = 0;
    s_last_person_tick = 0;
    s_present          = 0;
    s_cap_idx          = 0xFF;
    s_icon_state       = -1;
    PRINT_LOG(LOG_INFO, HAL_GetTick(), "vision pipeline running (ping-pong)");
    return RT_OK;
}

/* -------------------------------------------------------------------- loop */
void app_vision_loop(void)
{
    uint16_t *frame = NULL;
    char      buf[32];
    uint32_t  now;
    int       detected = 0;

    now = HAL_GetTick();

    /* An overrun leaves the DCMI stalled: re-arm it. */
    if (g_dcmi_overruns != s_overruns)
    {
        s_overruns = g_dcmi_overruns;
        PRINT_LOG(LOG_WARN, now, "dcmi overrun #%lu, restarting",
                  (unsigned long)s_overruns);
        drv_dcmi_recover();
        return;
    }

    /* ---- 读取(乒乓之"另一块采集"): 取一帧刚完成、DMA 当前未写的缓冲 ---- */
    if (drv_dcmi_get_frame(&frame) != RT_OK)
    {
        return;                      /* 还没新帧: 相机仍在填另一块, 直接返回 */
    }
    s_cap_idx = g_dcmi_last_idx;     /* 确认乒乓交替用 (0 / 1) */

    /* 快照: 立刻把采集缓冲拷到可缓存 s_disp (此时 DMA 在写另一块, 不会撕裂) */
    memcpy(s_disp, frame, CAPTURE_BYTES);

    /* ---------- 1Hz 检测: NN 很贵, 每 APP_DETECT_INTERVAL_MS 才跑一次 ----------
     * 显示(相机预览)每帧都刷新(见下文), 只有"计算/判定"被节流到 1Hz. */
    if ((now - s_last_detect_tick) >= (uint32_t)APP_DETECT_INTERVAL_MS)
    {
        s_last_detect_tick = now;

        /* 计算: 192x192 RGB565 -> 2x2 box filter + luma -> int8[96*96] */
        int8_t *inp = pd_input();
        pd_preprocess_rgb565(s_disp, CAPTURE_WIDTH, inp, NULL);

        /* 运行 CMSIS-NN 行人检测 */
        if (pd_run(&s_res) != 0)
        {
            PRINT_LOG(LOG_ERROR, HAL_GetTick(), "pd_run failed");
        }
        else
        {
            s_infer_us = s_res.us;

            /* 原始判定(未去抖): 分数达阈值即为"检测到人" */
            detected = (s_res.score >= APP_PERSON_THRESHOLD) ? 1 : 0;

            if (detected)
            {
                s_last_person_tick = now;   /* 记录最近一次"有人"时刻 */
                if (s_present != 1)         /* 出现 -> 立即显示, 无延迟 */
                {
                    s_present = 1;
                    LED_ON();
                }
            }
            else
            {
                /* 移除延时: 距最近一次"有人"未满 APP_PERSON_HOLD_MS 时,
                 * 仍保持"人在"显示, 避免模型瞬时掉分导致图标乱闪. */
                if (s_present == 1 &&
                    (now - s_last_person_tick) >= (uint32_t)APP_PERSON_HOLD_MS)
                {
                    s_present = 0;
                    LED_OFF();
                }
            }
        }
    }

    /* ---------- 显示(每帧): 相机预览 + 分数常显 + 人在/不在图标 + 状态 ---------- */

    /* 把整帧(无检测框)贴到屏上 */
    LCD_CopyBuffer(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, s_disp);

    /* --------------------------------------------------------- 顶部: 分数常显 */
    snprintf(buf, sizeof(buf), "S:%.2f", s_res.score);
    LCD_SetAsciiFont(&ASCII_Font24);
    LCD_SetColor(s_present ? LCD_GREEN : LCD_WHITE);
    show_text(VIEW_X, 0, s_line_top, TEXT_CACHE_LEN, buf);

    /* ------------------------------------------- 顶部右侧: 人在/不在 图标 */
    if (s_present != s_icon_state)
    {
        LCD_CopyBuffer(ICON_X, ICON_Y, ICON_W, ICON_H,
                       s_present ? s_icon_present : s_icon_absent);
        s_icon_state = s_present;
    }

    /* ------------------------------------------------------ 底部: 帧率/耗时 */
    s_loop_count++;
    if ((now - s_stat_tick) >= 1000u)
    {
        s_stat_tick = now;
        s_loop_fps  = (uint8_t)s_loop_count;
        s_loop_count = 0u;

        PRINT_LOG(LOG_INFO, now,
                  "cam %2u fps | pipe %2u fps | nn %5lu us | score %.2f | cap%d",
                  g_dcmi_fps, s_loop_fps, (unsigned long)s_infer_us,
                  s_res.score, s_cap_idx);
    }

    snprintf(buf, sizeof(buf), "%2ufps nn%3lums",
             s_loop_fps, (unsigned long)((s_infer_us + 500u) / 1000u));
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetColor(LCD_WHITE);
    show_text(VIEW_X, LCD_HEIGHT - 20, s_line_bottom, TEXT_CACHE_LEN, buf);
}

/* --------------------------------------------------------------- internals */

/** Redraw a text line only when its content actually changed (anti flicker).
 *  @param cap 缓存区容量(不含结束符), 用于安全的 strncmp/strncpy, 防止越界。 */
static void show_text(uint16_t x, uint16_t y, char *cache, size_t cap, const char *text)
{
    if (strncmp(cache, text, cap) == 0) return;
    strncpy(cache, text, cap);
    cache[cap] = '\0';
    LCD_DisplayText(x, y, (char *)text);
}

/** 判断 (x,y) 是否落在"人形"轮廓内: 头(圆) + 肩宽向下的人像。 */
static int icon_in_silhouette(int x, int y)
{
    if (x < 0 || x >= ICON_W || y < 0 || y >= ICON_H) return 0;

    /* 头: 圆心(12,7) 半径 5 */
    int dx = x - 12, dy = y - 7;
    if (dx * dx + dy * dy <= 25) return 1;

    /* 身: y=13..22, 半宽 3..12 (向下变宽, 形成人像肩部) */
    if (y >= 13 && y <= 22)
    {
        int half = 3 + (y - 13);
        if (x >= 12 - half && x <= 12 + half) return 1;
    }
    return 0;
}

/** 生成一块图标位图: present=人在(绿实心), absent=不在(灰空心+红斜杠)。 */
static void icon_build(uint16_t *buf, int present)
{
    int x, y;
    for (y = 0; y < ICON_H; y++)
    {
        for (x = 0; x < ICON_W; x++)
        {
            uint16_t c = RGB565_BLACK;

            if (present)
            {
                if (icon_in_silhouette(x, y)) c = RGB565_GREEN;
            }
            else
            {
                /* 空心人像轮廓: 仅画边缘像素 */
                if (icon_in_silhouette(x, y))
                {
                    int inside = icon_in_silhouette(x - 1, y) &&
                                 icon_in_silhouette(x + 1, y) &&
                                 icon_in_silhouette(x, y - 1) &&
                                 icon_in_silhouette(x, y + 1);
                    if (!inside) c = RGB565_GREY;
                }
                /* 红色斜杠表示"不 / 无" */
                if (x >= 2 && x <= 21 && (x - y) >= -1 && (x - y) <= 1)
                {
                    c = RGB565_RED;
                }
            }
            buf[y * ICON_W + x] = c;
        }
    }
}

/* 检测框叠加绘制已移除: 需求要求人存在时不画框, 仅显示分数与人在/不在图标。 */
