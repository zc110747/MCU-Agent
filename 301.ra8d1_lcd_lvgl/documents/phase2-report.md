# Phase 2 报告 — GLCDC + RGB 面板（800×480，无 LVGL）

> 目标：不引入 LVGL，把 GLCDC + SDRAM framebuffer + RGB 面板点亮到可编程测试图案，
> 并用串口命令 + SWD 读回做端到端验收。验收脚本：`tools/verify/verify_phase2.py` → **14 passed, 0 failed**。

## 1. 交付内容

| 模块 | 文件 | 说明 |
|---|---|---|
| LCD BSP | `bsp/ra8d1-vision-board/bsp_lcd.c/.h` | GLCDC Open/Start、framebuffer 管理、fill/rect/pixel、5 种测试图案、状态读出 |
| 引脚集中初始化 | `bsp/ra8d1-vision-board/bsp_pin.c/.h` | IOPORT 一次 Open，GLCDC/SDRAM/UART/LED/背光/复位全部引脚一张表 |
| 上游驱动（原样） | `Drivers/renesas/fsp/src/r_glcdc/r_glcdc.c` 等 | FSP r_glcdc 模块 + `r_display_api.h` + instances 头 + `board/ra8d1_ek`（SDRAM 控制器初始化） |
| 应用 | `applications/main.c` | `lcd` 命令（`init|info|stat|pattern N|fill HEX|bl on/off`），`led` 命令沿用 |
| 验收脚本 | `tools/verify/verify_phase2.py` | 串口 + SWD 双通道，14 项 PASS/FAIL |
| 取证探针 | `tools/verify/probe_gr1.py`、`probe_stmon.py` | L2UNDF 调查所用的一次性 SWD 探针（保留备查） |

## 2. 数据通路

```text
CPU 写 g_lcd_fb[] (0x68000000, .sdram NOLOAD, 768000B)
        │
        ▼
GLCDC GR[0] 层：RGB565 800x480, hstride=800 ──► 逐行取数（64B burst）
        │  PCLK = LCDCLK(240MHz) / 8 = 30MHz
        ▼
TCON: hsync=PIN_0, vsync=PIN_1, de=PIN_3；时序 1024x525, bp 46/23, syncw 1
        │  RGB666 大端输出，DE 高有效，同步上升沿
        ▼
4.3" RGB 面板（P1011 背光, P1104 复位）
```

关键参数（与官方 `ra_gen/common_data.c` 一致）：

| 参数 | 值 | SWD 实测 |
|---|---|---|
| PCLK | 240MHz/8 = **30MHz** | `PANEL_CLK.DCDR=8 CLKEN=1 CLKSEL=1` |
| 刷新率 | 30e6/(1024×525) = **55.8Hz** | — |
| H 时序 | HSS=47 / HSW=800 | `BG.HSIZE=0x2F0320` |
| V 时序 | VSS=24 / VSW=480 | `BG.VSIZE=0x001801E0` |
| framebuffer | 0x68000000 | `GR[0].FLM2.BASE=0x68000000` |
| 层1取数 | 使能 | `GR[0].FLMRD.RENB=1` |

## 3. 时钟链

```text
XTAL 20MHz ×48 ─► PLL 960MHz ─/2─► PLL1P 480MHz ─/2─► LCDCLK 240MHz ─/8(DCDR)─► PCLK 30MHz
                                     └─/1─► CPU 480MHz（ICLK /2 = 240MHz）
```

配置头：`bsp/ra8d1-vision-board/gen/bsp_clock_cfg.h`（`BSP_CFG_LCDCLK_SOURCE=PLL1P, DIV=2`）。

## 4. 实现要点与坑

| 坑 | 现象 | 根因 / 修法 |
|---|---|---|
| `ioport_pin_cfg_t has no member named 'BSP_IO_PORT_02_PIN_08'` | 编译报错 | 宏参数命名 `pin` 会把 `.pin` 指定符一起替换。宏参数一律改 `_pin`/`_periph` |
| `-Wmissing-braces` ×8 | `g_display_cfg` 编译警告 | `.input[1] = {0}` 首成员是聚合体；改全展开指定初始化（`.p_base=NULL` 等） |
| pyOCD 读 GLCDC 寄存器得到乱码 | `GR1.CLUTINT=0x535ED3F7` 之类不可能值 | **地址进位错误**：`0x40342000+0x1100 = 0x40343100`，误写成 `0x40342100`（落在 GR1_CLUT0 未初始化区）。BG/SYSCNT 一直读得对，只有 GR[] 读成乱码 |
| `BG.EN_b.VEN` 写 1 后读回 0 | 疑似没启动 | 正常：VEN 反射完成后硬件自清零；`EN` 保持 1。`lcd stat` 显示 `EN=1 VEN=0` 即健康态 |

## 5. L2UNDF 调查（本阶段的取证案例）

**现象**：`lcd stat` 恒显 `STMON=0x00000005`（`VPOS=1` + `L2UNDF=1`），固件里
`STCLR_b.L2UNDFCLR=1` 清不掉。

**取证过程（全部 SWD 实测，脚本在 `tools/verify/`）**：

1. **驱动源码定位**（`r_glcdc.c`）：
   - layer 2 `p_base==NULL` → `r_glcdc_graphics_layer_set` 只写
     `AB1=TRANSPARENT` + `FLMRD=0` 后提前 return（1557-1564 行）——**`GR[1].EN` 从未置 1**，
     也没有走到底部本该把 `CLUTINT` 清零的那一行；
   - `R_GLCDC_Open` 尾部**无条件**编程 `R_GLCDC->GR[1].CLUTINT_b.LINE = back_porch + display_cyc + 1`（436 行）。
2. **GR[] 寄存器真值**（修正地址后 `probe_gr1.py`）：

   ```text
   GR1.VEN=0x00000000  GR1.FLMRD.RENB=0  GR1.FLM2.BASE=0x00000000
   GR1.AB1=0x00000001(TRANSPARENT)   GR1.CLUTINT=0x000001F8(LINE=504,SEL=0)
   GR1.MON.UNDFLST=1                 ← 粘滞
   GR0.FLMRD.RENB=1  GR0.FLM2.BASE=0x68000000  GR0.MON.UNDFLST=0
   ```

   LINE=504 = 23+480+1，与驱动公式逐位吻合；layer 2 **从不访问内存**（RENB=0、BASE=0）。

3. **行为曲线**（`probe_stmon.py`）：

   ```text
   自由观察 2s        : STMON 恒 0x05，无振荡
   STCLR=0x7 后       : +100ms 读到 0x00000000，+200ms 回到 0x00000005
   仅写 L2UNDFCLR     : STMON=0x01，GR1.MON.UNDFLST 仍 =1（清不掉）
   ```

**结论**：
`GR[1].MON.UNDFLST` 是硬件粘滞位（仅 SWRST/反射能复位），驱动为 layer 2 的
line-detect 机制武装了 LINE=504，而 layer 2 又是透明+不取数的空层 —— 二者叠加使
`STMON.L2UNDF` 永久回显 1，任何 STCLR 清除在 1-2 帧内被重新拉起。
**这是 FSP 驱动的固有良性伪影，零功能影响**（无中断挂接、layer 1 健康未欠载、画面正确）。

**验收判据相应修正**：`L1UNDF==0` 为硬判据（层 1 真的在取数），`L2UNDF` 不作要求。
`bsp_lcd.c` 的 STCLR 注释已同步更新。

## 6. 实测数据

双构零警告（`-Wall -Wextra`，自研代码；上游以 `-w` 原样编译）：

| 构建 | FLASH(text+data) | 内部 RAM(bss+data+noinit+heap+stack) | SDRAM(.sdram NOLOAD) |
|---|---|---|---|
| Debug (`-g`) | 47328B / 2MB (**2.26%**) | 92128B / 1MB (**8.79%**) | 768000B / 32MB (2.29%) |
| Release (`-O3`) | 41016B / 2MB (**1.96%**) | 91992B / 1MB (**8.77%**) | 同上 |

- framebuffer 768000B = 800×480×2，位于 `.sdram`（NOLOAD，不占 Flash、不占内部 RAM）
- SDRAM 读写已验证（0x680C0000 处 0x12345678 回读一致）

## 7. 验收输出

```text
Phase 2 acceptance - COM9 @ 115200
[PASS] boot banner                found 'RA8D1 Vision Board - Phase 2'
[PASS] heartbeat present          3 samples
[PASS] tick advances ~2000        ticks=[4207, 6210, 8213]
[PASS] msh prompt
[PASS] help lists 'lcd'           lcd registered
[PASS] lcd init OK                'lcd init: OK (0x0)'
[PASS] lcd info geometry          'lcd 800x480 bpp16 fb=0x68000000 (768000 bytes, .sdram)'
[PASS] lcd stat EN/timing/L1UNDF  EN=1 HSW=800 HSS=47 VSW=480 VSS=24 L1UNDF=0 L2UNDF=1(artifact)
[PASS] lcd pattern 3              'lcd pattern 3'
[PASS] lcd pattern 2              'lcd pattern 2'
[PASS] fb colour bars (SWD)       6 samples exact
[PASS] lcd fill reaches SDRAM     fb[0]=0x07E0 fb[mid]=0x07E0 fb[end]=0x07E0
[PASS] P1011 backlight SWD        PODR off=0 on=1
[PASS] GR0 RENB/BASE/UNDFLST      RENB=1 BASE=0x68000000 UNDFLST=0
RESULT: 14 passed, 0 failed
```

面板目视确认：colour bars（红橙黄绿青蓝紫白）、RGB 渐变、棋盘格均正确显示。

## 8. 遗留与下一阶段

- `L2UNDF` 良性伪影（已定性，不修上游）
- Phase 3：Camera（CEU 8-bit DVP + SCCB）—— 摄像头缓冲需 `.nocache_sdram`
  （`system.c` 已映射）；如启用 D-Cache 需重估 framebuffer 一致性
- Phase 4：LVGL v9.1.0 直接以 `g_lcd_fb` 作为 draw buffer，渲染完待 VEN 时机刷新
