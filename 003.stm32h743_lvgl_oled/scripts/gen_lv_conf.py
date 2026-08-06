# -*- coding: utf-8 -*-
"""Generate Middlewares/lvgl/lv_conf.h from the upstream lv_conf_template.h.

Keeping this as a script (instead of a hand-written 780-line header) means the
config can be regenerated verbatim after an LVGL version bump: only the small
REPL / OFF tables below carry project-specific intent.
"""
import io
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
LVGL = os.path.join(HERE, '..', 'Middlewares', 'lvgl')
TEMPLATE = os.path.join(LVGL, 'lv_conf_template.h')
OUTPUT = os.path.join(LVGL, 'lv_conf.h')

src = io.open(TEMPLATE, 'r', encoding='utf-8').read()

REPL = [
    # ---- enable the whole file ------------------------------------------
    ('#if 0 /*Set it to "1" to enable content*/',
     '#if 1 /*Set it to "1" to enable content*/'),

    # ---- memory ----------------------------------------------------------
    ('#define LV_MEM_SIZE (48U * 1024U)',
     '#define LV_MEM_SIZE (40U * 1024U)'),

    # ---- tick source: reuse the HAL SysTick ------------------------------
    ('#define LV_TICK_CUSTOM 0',
     '#define LV_TICK_CUSTOM 1'),
    ('#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"         '
     '/*Header for the system time function*/',
     '#define LV_TICK_CUSTOM_INCLUDE "stm32h7xx_hal.h"   '
     '/*Header for the system time function*/'),
    ('#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())    '
     '/*Expression evaluating to current system time in ms*/',
     '#define LV_TICK_CUSTOM_SYS_TIME_EXPR (HAL_GetTick()) '
     '/*Expression evaluating to current system time in ms*/'),

    # ---- fonts: everything comes from the SD card ------------------------
    ('#define LV_FONT_MONTSERRAT_14 1',
     '#define LV_FONT_MONTSERRAT_14 0'),
    ('#define LV_FONT_CUSTOM_DECLARE\n',
     '#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(lv_font_gbk_12) \\\n'
     '                                LV_FONT_DECLARE(lv_font_gbk_16) \\\n'
     '                                LV_FONT_DECLARE(lv_font_gbk_24) \\\n'
     '                                LV_FONT_DECLARE(lv_font_gbk_32)\n'),
    ('#define LV_FONT_DEFAULT &lv_font_montserrat_14',
     '#define LV_FONT_DEFAULT &lv_font_gbk_16'),

    # ---- theme -----------------------------------------------------------
    ('    #define LV_THEME_DEFAULT_DARK 0',
     '    #define LV_THEME_DEFAULT_DARK 1'),
    ('    #define LV_THEME_DEFAULT_GROW 1',
     '    #define LV_THEME_DEFAULT_GROW 0'),
    ('    #define LV_THEME_DEFAULT_TRANSITION_TIME 80',
     '    #define LV_THEME_DEFAULT_TRANSITION_TIME 0'),
    ('#define LV_USE_THEME_BASIC 1', '#define LV_USE_THEME_BASIC 0'),
    ('#define LV_USE_THEME_MONO 1', '#define LV_USE_THEME_MONO 0'),
]

# Widgets this UI never instantiates -> compile them out.
# Kept ON: ARC, BAR, BTN, IMG, LABEL, LINE, LED, FLEX, GRID.
OFF = [
    'LV_USE_BTNMATRIX', 'LV_USE_CANVAS', 'LV_USE_CHECKBOX', 'LV_USE_DROPDOWN',
    'LV_USE_ROLLER', 'LV_USE_SLIDER', 'LV_USE_SWITCH', 'LV_USE_TEXTAREA',
    'LV_USE_TABLE', 'LV_USE_ANIMIMG', 'LV_USE_CALENDAR', 'LV_USE_CHART',
    'LV_USE_COLORWHEEL', 'LV_USE_IMGBTN', 'LV_USE_KEYBOARD', 'LV_USE_LIST',
    'LV_USE_MENU', 'LV_USE_METER', 'LV_USE_MSGBOX', 'LV_USE_SPAN',
    'LV_USE_SPINBOX', 'LV_USE_SPINNER', 'LV_USE_TABVIEW', 'LV_USE_TILEVIEW',
    'LV_USE_WIN',
]

missing = []
for old, new in REPL:
    if old not in src:
        missing.append(old.split('\n')[0])
        continue
    src = src.replace(old, new, 1)

for sym in OFF:
    pat = re.compile(r'^(#define\s+' + sym + r'\s+)1(\s*(?:/\*.*)?)$', re.M)
    src, n = pat.subn(r'\g<1>0\g<2>', src)
    if n == 0:
        missing.append(sym)

HEADER = u'''/**
 * @file lv_conf.h
 * LVGL v8.3.11 configuration for the STM32H743ZIT6 / ST7789 240x240 board.
 *
 * DO NOT EDIT BY HAND - regenerate with  scripts/gen_lv_conf.py
 *
 * Project specific decisions baked in here:
 *   - RGB565 without byte swap.  LCD_CopyBuffer() switches SPI6 to 16-bit
 *     frames and sends them MSB first, which is exactly the big-endian RGB565
 *     the ST7789 expects, so LV_COLOR_16_SWAP stays 0.
 *   - LV_TICK_CUSTOM = 1 bound to HAL_GetTick(), so LVGL rides on the SysTick
 *     the HAL already owns and lv_tick_inc() never has to be called.
 *   - No built-in font is compiled in.  Every glyph, ASCII and Chinese alike,
 *     is served at run time by lv_font_gbk_xx which reads the GBKxx.FON files
 *     from the SD card (see Bsp/lv_font_gbk.c).
 *   - Only label / bar / arc / btn / img / line / led are enabled; the rest of
 *     the widget set is switched off to keep the image small.
 */
'''

# Replace the upstream doc comment with ours, keep everything from the
# "clang-format off" marker onwards untouched.
marker = u'/* clang-format off */'
src = HEADER + marker + src.split(marker, 1)[1]

io.open(OUTPUT, 'w', encoding='utf-8', newline='\n').write(src)
print('written: %s  (%d lines)' % (os.path.normpath(OUTPUT), src.count('\n') + 1))
print('substitutions not applied: %s' % (missing if missing else 'none'))
