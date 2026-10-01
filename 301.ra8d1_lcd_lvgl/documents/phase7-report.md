# Phase 7 报告：取消触摸 + 单页信息界面 + 硬件 RTC 实时时钟

> 项目：`301.ra8d1_lcd_lvgl`（Renesas RA8D1 Vision Board / R7FA8D1BH）
> 目标：依据 Phase 5/6 的触摸硬件故障定性，移除触摸实现，把多页菜单收敛为
> **单页面**，并显示**硬件 RTC 实时时钟**（每秒刷新）。

## 1. 背景与决策

| 项 | 结论 |
|---|---|
| 触摸（CST812T） | Phase 5/6 已定性为**硬件/面板层故障**：固件逐项 SWD 取证正确，控制器不响应任何 I2C 事务（SCI3 + 手工位翻转 I2C 均 NACK，全地址扫描 0 设备） |
| 用户指令 | “触摸应该是不支持的，先取消实现，只是构建单页面，显示当前的硬件信息和实时时间更新即可” |
| 时间源 | 用户明确选择 **硬件 RTC 真实时钟**（非运行时长） |

RTC 硬件可行性：原理图 p3 有 **Y2 = 32.768 kHz / 9pF 晶振**接 XCIN(J15)/XCOUT(J14)，
负载电容 C23/C24 = 15pF（已贴）；`cfg/bsp_cfg.h` 的 `BSP_CLOCK_CFG_SUBCLOCK_POPULATED = 1`，
启动期 `bsp_clock_init() → bsp_sosc_init()` 已启动并稳定子时钟。

## 2. 变更清单

### 2.1 删除触摸

| 文件 | 处理 |
|---|---|
| `bsp/ra8d1-vision-board/bsp_touch.c` / `.h`（537/113 行） | 删除 |
| `applications/app_config.h`（`APP_ENABLE_TOUCH`） | 删除 |
| `applications/ui/app_ui.c` / `.h` | 删除（页面系统移除） |
| `applications/ui/ui_page_boot.*` / `ui_page_menu.*` / `ui_page_info.*` | 删除 |
| `bsp_pin.c` 的 P000/P010/P408/P409 触摸引脚条目 + `PIN_PERIPH_NMOS` 宏 | 回退/移除 |
| `bsp/.../gen/vector_data.c` / `.h` 的 SCI3 TXI/TEI 两槽 | 移除（`VECTOR_DATA_IRQ_COUNT` 10→8） |
| `Drivers/renesas/fsp/src/r_sci_b_i2c/` + `r_i2c_master_api.h` + `r_sci_b_i2c*.h` | 删除，移出 CMake glob |
| `main.c` 的 `menu_cmd` / `touch_cmd`、`lv_port.c` 触摸 indev 块 | 删除 |

### 2.2 新增单页面

`applications/ui/ui_page_main.c` / `.h`：

```
 0   ┌─────────────────────────────────────────┐
     │           RA8D1 VISION BOARD            │  36 px 标题栏（COL_HDR）
36   ├─────────────────────────────────────────┤
     │              2026-10-01  Thu            │  日期（14px, 白）
     │              16:30:01                   │  时钟（28px, cyan COL_CLOCK）
128  ├─────────────────────────────────────────┤
     │  CPU                  Cortex-M85 @480MHz│
     │  Display           MIPI DSI 480x360 x2  │
     │  DSI             122 cmds / link 0x0110 │
     │  LVGL             fps 30 / flushes 4200 │
     │  Memory            31 KB / 128 KB (24%) │
     │  Uptime                       132 s      │
     └─────────────────────────────────────────┘
```

1Hz `lv_timer` 只 `lv_label_set_text_fmt()`，LVGL 仅重绘脏矩形。

### 2.3 手写 RTC 驱动

`bsp/ra8d1-vision-board/bsp_rtc.c` / `.h`（FSP 树无 `r_rtc`）：

- 直接编程 `R_RTC @ 0x40202000`；计数源 = 32.768 kHz 子时钟晶振。
- 初始化序列：`PRCR` 解锁（0xA502）→ `RCR4.RCKSEL=0`（子时钟）→ `RCR2.RESET`
  脉冲复位预分频 → `RCR2.HR24=1` → 写 7 个 BCD 计数器 → `RCR2.START=1` → 锁 `PRCR`。
- 计数器为 **BCD**，`to_bcd/from_bcd` 转换；读时秒值前后各读一次，翻转则重读。
- API：`bsp_rtc_init/get/set/is_running`。

## 3. 关键坑：`RCR4.RCKSEL` 语义与字面相反

| RCKSEL | 实际选择 |
|---|---|
| **0** | **子时钟（32.768 kHz 晶振）** ✅ |
| 1 | LOCO（内部低速 RC 振荡器，未校准） |

初版误置 `RCKSEL = 1`，RTC 跑在 LOCO 上，**实测快 10.63×**（60.61 s 真实时间走 644 s）。
改为 0 后 60.61 s 实走 60 s（ratio 0.9899）。

> 另一个取证陷阱：**SWD 直读 `0x40202000` 常返回 0**（RTC 位于 VBATT 域，
> 调试器 APB 访问不可靠），**不要用 SWD 读 RTC**；改用串口 `rtc info` 前后差值测走时。

## 4. 验收

### 4.1 双构零警告

| 构建 | FLASH | RAM |
|---|---|---|
| Debug | 534244 B (25.47% / 2MB) | 915204 B (87.26% / 1MB) |
| Release | 437032 B (20.84% / 2MB) | 914988 B (87.26% / 1MB) |

`-Wall -Wextra` 下 Debug/Release **均零警告**。

### 4.2 串口实测

```
RA8D1 Vision Board - Phase 7 (single-page UI + RTC)
RT-Thread Nano 5.0.2, CPU 480000000 Hz, tick 1000 Hz
[main] RTC on 32k sub-clock: 2026-01-01 00:00:00
[mipi] 44 DCS commands pushed, seq0=43, phy_status=0x0
[lcd] GLCDC 480x360 up, panel=480x360, fb=0x68000000+0x68054600
[main] MIPI panel 480x360: OK (0x0)
```

- `rtc info`：`2026-01-01 (wday 4) 00:00:04  running=1`，60 s 后 `00:01:05`（走时准确）
- `rtc set 2026 10 01 16 30 0` → 读回 `2026-10-01 (wday 0) 16:30:01`
- `lv info`：`running flushes=N fps=1 mem=9/128KB (8%)`（静态页 + 1Hz 时钟 → ~1 flush/s 正常）

### 4.3 SWD framebuffer 取证

| 期望色 | RGB888 | 帧缓冲 565 | 命中 |
|---|---|---|---|
| 背景 COL_BG | 0x000000 | 0x0000 | ✅ |
| 标题栏 COL_HDR | 0x0A3D62 | 0x09EC | ✅ |
| **时钟 COL_CLOCK** | **0x00E5FF** | **0x073F** | ✅ |
| 信息值 COL_VALUE | 0x40E070 | 0x470E | ✅ |
| 标题字 COL_HDR_TXT | 0xFFD966 | 0xFECC | ✅ |
| 标签 COL_LABEL | 0x8A8A8A | 0x8C51 | ✅ |

时钟文字包围盒：**x=191..288 / y=85..104**（97×20 px，水平居中于 480px 面板），
与设计坐标一致，确认实时时钟已正确渲染。

## 5. 交付物

- `applications/ui/ui_page_main.c` + `.h`（单页面）
- `bsp/ra8d1-vision-board/bsp_rtc.c` + `.h`（硬件 RTC 驱动）
- `applications/lv_port.c` / `main.c` / `ui_common.*`（去触摸、接单页）
- `bsp/.../gen/vector_data.*`（8 槽）
- 删除项见 §2.1

## 6. 已知限制

- 板上**无 RTC 备份电池**，冷复位/断电后时间回到编译期默认（2026-01-01 00:00:00）；
  运行期可用 `rtc set` 校准。若日后加 VBATT，可设 `VBTBER.VBAE` 使能备份域保持。
- 屏为**只读显示**（无触摸、无实体按键），交互仅经串口 msh 命令。
