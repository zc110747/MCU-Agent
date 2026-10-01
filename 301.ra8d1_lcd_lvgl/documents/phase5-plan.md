# Phase 5 实现计划：MIPI DSI 显示通路重做 + 菜单应用

> 目标：**先把屏点亮**（"先在屏幕上实现显示后再处理"），再在其上做菜单应用验证 LCD 通路，最后用宏屏蔽摄像头 / OpenMV 相关应用。

---

## 0. 根因确认（已取证，非推测）

| 项 | Phase 2/3/4 现网配置 | 实物硬件 | 结论 |
|---|---|---|---|
| 接口类型 | GLCDC **RGB666 并口**（RGB 4.3"） | **MIPI DSI**（2.0"） | ❌ 完全不匹配 |
| 分辨率 | 800×480 | **480×360** | ❌ |
| 时序 | htotal 1024 / vtotal 525 | htotal 514 / vtotal 382 | ❌ |
| 输出格式 | `DISPLAY_OUT_FORMAT_18BITS_RGB666` / BIG endian | `16BITS_RGB565` / LITTLE endian | ❌ |
| 面板初始化 | 无（RGB 屏无需 DCS 命令） | **~40 条 DCS 命令表**（FocusLCD） | ❌ 缺失 |
| `phy_layer` | `NULL` | `&g_mipi_dsi0` | ❌ 缺失 |

**现象吻合**：有背光但纯黑、Phase 2 彩色条就没显示过、SWD 断言只证明 SDRAM 内容写对（内存层 PASS 属**假绿**，从未证明面板点亮）。

**官方对口参考工程**（本机已存在，作为唯一真值源）：
```
E:/cnb/git/sdk-bsp-ra8d1-vision-board-master/projects/lvgl/vision_board_mipi_2.0inch_lvgl
```

---

## 1. 官方参考工程架构（已逐文件读取）

### 1.1 数据通路

```
LVGL v9.1.0 (2 个整屏 buffer, RENDER_MODE_DIRECT)
   │  disp_flush() → SCB_CleanInvalidateDCache_by_Addr + R_GLCDC_BufferChange(layer 0)
   ▼
GLCDC (R_GLCDC, base 0x40342000)  ── RGB565 内部管线 ── phy_layer = &g_mipi_dsi0
   │  ← 桥接点：g_display0_extend_cfg.phy_layer 指向 DSI 实例
   ▼
MIPI DSI (R_DSILINK)  ── Video Mode, 2 lane, 16RGB ── MIPI PHY (1 GHz)
   ▼
2.0" 480×360 FocusLCD 面板（DCS 命令表初始化）
```

**关键**：DSI 不是替代 GLCDC，而是 GLCDC 的**输出后端**（`phy_layer`）。22 个 `IOPORT_PERIPHERAL_LCD_GRAPHICS` 引脚**仍然存在**（GLCDC↔DSI 内部桥接引脚），额外新增 **P206 `IOPORT_PERIPHERAL_MIPI`**（MIPI PHY 专用）。

### 1.2 官方 verbatim 配置（`ra_gen/common_data.c`）

**MIPI PHY**：
```c
.pll_settings = { .div = 1 - 1, .mul_int = 50 - 1, .mul_frac = 0 }   /* 20MHz / 1 * 50 = 1000MHz */
.lp_divisor   = 5 - 1,
.p_timing     = &g_mipi_phy0_timing,   /* TINIT=71999, TCLKPREP=8, THSPREP=5 ... */
```

**MIPI DSI**：
```c
.sync_pulse               = 0,
.data_type                = MIPI_DSI_VIDEO_DATA_16RGB_PIXEL_STREAM,
.virtual_channel_id       = 0,
.vertical_active_lines    = 360,
.vertical_sync_lines      = 4,
.vertical_back_porch      = (10 - 4),                    /* = 6  */
.vertical_front_porch     = (382 - 360 - 10 - 4),        /* = 8  */
.horizontal_active_lines  = 480,
.horizontal_sync_lines    = 4,
.horizontal_back_porch    = (20 - 4),                    /* = 16 */
.horizontal_front_porch   = (514 - 480 - 20 - 4),        /* = 10 */
.video_mode_delay         = 206,
.num_lanes                = 2,
.ulps_wakeup_period       = 97,
.continuous_clock         = 1,
.ecc_enable               = 1,
.eotp_enable              = 1,
.p_extend                 = &g_mipi_dsi0_extended_cfg,   /* 6 个 DSI 中断向量 */
.p_callback               = mipi_dsi0_callback,
```

**GLCDC（DSI 桥接版）**：
```c
/* 注意：tcon 引脚映射与 RGB 版不同 */
.tcon_hsync = GLCDC_TCON_PIN_1,   /* RGB 版是 PIN_0 */
.tcon_vsync = GLCDC_TCON_PIN_0,   /* RGB 版是 PIN_1 */
.tcon_de    = GLCDC_TCON_PIN_2,   /* RGB 版是 PIN_3 */
.phy_layer  = (void*) &g_mipi_dsi0,   /* ← 桥接点 */
.clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_8,

.output.htiming = { .total_cyc=514, .display_cyc=480, .back_porch=20, .sync_width=4, LOACTIVE },
.output.vtiming = { .total_cyc=382, .display_cyc=360, .back_porch=10, .sync_width=4, LOACTIVE },
.output.format  = DISPLAY_OUT_FORMAT_16BITS_RGB565,
.output.endian  = DISPLAY_ENDIAN_LITTLE,
.output.color_order = DISPLAY_COLOR_ORDER_RGB,
```

**分辨率常量**：`DISPLAY_HSIZE_INPUT0 = 480`、`DISPLAY_VSIZE_INPUT0 = 360`、`DISPLAY_BITS_PER_PIXEL_INPUT0 = 16`。

**framebuffer**：`uint8_t fb_background[2][STRIDE_BYTES * 360] BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(".sdram")`（**双缓冲**）。

### 1.3 面板命令表（`board/ports/mipi_lcd/mipi_config.c`，已读全文）

- `lcd_table_setting_t { uint8 size; uint8 buffer[20]; mipi_dsi_cmd_id_t cmd_id; mipi_dsi_cmd_flag_t flags; }`
- 约 40 条：BK3/BK0/BK1 bank 切换（`0xFF,0x77,0x01,0x00,0x00,0x1X`）→ 时序/电压/伽马 → `0x3A=0x55`(16bpp) → `0x36=0x40`(RGB) → `0x11`(Sleep Out) + 120ms → `0x29`(Display On)
- 特殊 sentinel：`0xFE` = delay（`size` 字段即毫秒数）、`0xFD` = 表结束
- 驱动函数：`mipi_dsi_push_table()` 逐条 `R_MIPI_DSI_Command()` + 等 `g_message_sent` 回调；入口 `ra8_mipi_lcd_init()`
- 回调 `mipi_dsi0_callback()`：`MIPI_DSI_EVENT_SEQUENCE_0` + `DESCRIPTORS_FINISHED` → `g_message_sent = true`

### 1.4 初始化时序（`libraries/HAL_Drivers/drv_lcd.c`，已读全文）

```
rt_hw_lcd_init()  [INIT_DEVICE_EXPORT]
  ├─ 配 lcd_info（480×360, RGB565, framebuffer = lcd_framebuffer/.sdram）
  ├─ register "lcd" 设备
  ├─ gp_single_buffer = g_display0_cfg.input[0].p_base
  ├─ reset_lcd_panel()      ← LCD_RST_PIN = P1104，低 100ms → 高 100ms
  └─ ra_bsp_lcd_init()
       ├─ R_GLCDC_Open(&g_display0_ctrl, &g_display0_cfg)   ← 内含 DSI/PHY Open
       ├─ ra8_mipi_lcd_init()      ← 推 DCS 命令表
       ├─ g2d_drv_hwInit()         ← DRW/Dave2D（本项目无 DAVE2D，跳过）
       └─ R_GLCDC_Start(&g_display0_ctrl)
```

背光：`LCD_BL_PIN = BSP_IO_PORT_10_PIN_11`（与原工程**一致**，无需改）。

---

## 2. 施工步骤

### Step 1 — 引入上游 MIPI 驱动（原样拷贝）

从官方工程 `ra/fsp/` 拷入本工程 `Drivers/renesas/fsp/`：

| 源 | 目标 |
|---|---|
| `ra/fsp/src/r_mipi_dsi/r_mipi_dsi.c` (1096 行) | `src/r_mipi_dsi/` |
| `ra/fsp/src/r_mipi_phy/r_mipi_phy.c` (139 行) | `src/r_mipi_phy/` |
| `ra/fsp/inc/api/r_mipi_dsi_api.h` (555 行) | `inc/api/` |
| `ra/fsp/inc/instances/r_mipi_dsi.h` (120 行) | `inc/instances/` |
| `ra/fsp/inc/instances/r_mipi_phy.h` (155 行) | `inc/instances/` |
| `ra_cfg/fsp_cfg/r_mipi_dsi_cfg.h` (13 行) | `cfg/`（或 inc 目录） |

同时检查 `ra8d1_ek` board 的 `board.h` / `bsp_mcu_family_cfg.h` 是否已含 DSI 相关宏（RA8D1 与 RA8M85 共用 `R_DSILINK` 定义，预期已在）。

CMakeLists `FSP_MODULE_SOURCES` 增加：
```cmake
"${PROJ_ROOT}/Drivers/renesas/fsp/src/r_mipi_dsi/*.c"
"${PROJ_ROOT}/Drivers/renesas/fsp/src/r_mipi_phy/*.c"
```

### Step 2 — 新增 `applications/mipi_dsi_conf.c/.h`（承载 FSP 实例 + 命令表）

从 `common_data.c` + `mipi_config.c` 抽成本工程的等价文件，放在 **applications/**（我方代码，零警告目标）：

- `mipi_dsi_conf.h`：暴露
  - `extern const mipi_phy_instance_t  g_mipi_phy0;`
  - `extern mipi_dsi_instance_ctrl_t   g_mipi_dsi0_ctrl;`
  - `extern const mipi_dsi_instance_t  g_mipi_dsi0;`
  - `void ra8_mipi_lcd_init(void);`
- `mipi_dsi_conf.c`：
  - `g_mipi_phy0_timing` / `g_mipi_phy0_cfg` / `g_mipi_phy0_ctrl` / `g_mipi_phy0`
  - `g_mipi_dsi0_timing` / `g_mipi_dsi0_extended_cfg` / `g_mipi_dsi0_cfg` / `g_mipi_dsi0_ctrl` / `g_mipi_dsi0`
  - `mipi_dsi0_callback()` + `g_message_sent`
  - `g_lcd_init_focuslcd[]` + `mipi_dsi_push_table()` + `ra8_mipi_lcd_init()`
  - 注意：`R_DSILINK_*_Msk` 宏来自 device header，需确认 `bsp_mcu_device_pn_cfg.h` 已包含 `R7FA8D1BH` 的 `R_DSILINK` 定义。

> 命令表按**官方原样**移植，一条不改（上游代码原样拷贝原则）。

### Step 3 — 中断向量（`bsp/ra8d1-vision-board/gen/vector_data.c/.h`）

现表：`[0] SCI9_RXI`、`[1] CEU_CEUI`。需**扩展**为：

| slot | 事件 | ISR |
|---|---|---|
| 0 | `EVENT_SCI9_RXI` | `sci_b_uart_rxi_isr` |
| 1 | `EVENT_CEU_CEUI` | `ceu_isr` |
| 2 | `EVENT_GLCDC_LINE_DETECT` | `glcdc_line_detect_isr` |
| 3 | `EVENT_MIPI_DSI_SEQ0` | `mipi_dsi_seq0` |
| 4 | `EVENT_MIPI_DSI_SEQ1` | `mipi_dsi_seq1` |
| 5 | `EVENT_MIPI_DSI_VIN1` | `mipi_dsi_vin1` |
| 6 | `EVENT_MIPI_DSI_RCV` | `mipi_dsi_rcv` |
| 7 | `EVENT_MIPI_DSI_FERR` | `mipi_dsi_ferr` |
| 8 | `EVENT_MIPI_DSI_PPI` | `mipi_dsi_ppi` |

`VECTOR_DATA_IRQ_COUNT` 2 → 9。**slot 号可自定（必须连续从 0 起）**，与 `g_mipi_dsi0_extended_cfg` 中的 `.irq = VECTOR_NUMBER_*` 保持一致即可。

> DSI 的 6 个向量全部为**必需**：SEQ0 用于命令完成（`g_message_sent`），缺它命令表会死等；RCV/FERR/PPI/VIN1/SEQ1 用于错误上报。

### Step 4 — 引脚表（`bsp/ra8d1-vision-board/bsp_pin.c`）

- 新增 **P206**：`IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_MIPI`
- 复核现有 22 个 `LCD_GRAPHICS` 引脚（RGB 版已有）是否与官方 MIPI 版一致；差异项按官方表补齐（含 `IOPORT_CFG_DRIVE_MID` / `DRIVE_HIGH` 属性）
- 保持 `BSP_LCD_PIN_BL = P1011`、`BSP_LCD_PIN_RESET = P1104`（与官方一致）

### Step 5 — 改写 `bsp_lcd.c/.h`（本工程核心改动）

| 项 | 改前 | 改后 |
|---|---|---|
| 尺寸宏 | `800×480` | `480×360` |
| `stride` | 800 | 官方 `DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0` = **480 像素 / 960 字节**（见下方修正说明） |
| 输出格式 | `18BITS_RGB666` | `16BITS_RGB565` |
| endian | BIG | LITTLE |
| tcon | hsync=PIN_0 / vsync=PIN_1 / de=PIN_3 | hsync=**PIN_1** / vsync=**PIN_0** / de=**PIN_2** |
| phy_layer | NULL | `&g_mipi_dsi0` |
| timing | 1024/525, bp 46/23, sw 1 | 514/382, bp 20/10, sw **4** |
| 面板初始化 | 无 | `ra8_mipi_lcd_init()`（在 Open 之后、Start 之前） |
| 帧缓冲 | 单缓冲 `g_lcd_fb[800*480]` | **双缓冲**（DIRECT 模式需要） |
| 中断 | 全 disabled | `line_detect_irq = VECTOR_NUMBER_GLCDC_LINE_DETECT`，ipl 12，callback = `DisplayVsyncCallback` |

> ⚠️ **stride 是本次最容易算错的地方**：GLCDC 的 HSTRIDE 必须按官方公式算：
> `STRIDE_BYTES = ((HSIZE*BPP + 0x1FF) >> 9) << 6`，代入 480×16 得 **960 字节/行**，
> 反算 `STRIDE_PIXELS = 960*8/16 = 480` 像素 —— **与宽度相同，公式是恒等**。
> （480×16 = 7680 bit = 960 B，本身就是 64 B 对齐，向上取整到 512 B 块是 no-op。）
> 结论：**hstride 就填 480**，但保留公式写法并加注释，避免日后改分辨率时踩坑。
> 帧缓冲 480×360×2 B = **345,600 B/页**，双缓冲 **691,200 B**。

### Step 6 — `applications/lv_port.c/.h` 改 DIRECT 模式

官方做法（`lv_port_disp.c`）：

```c
lv_display_t *disp = lv_display_create(480, 360);
lv_display_set_flush_cb(disp, disp_flush);
lv_display_set_flush_wait_cb(disp, vsync_wait_cb);   /* 等 vsync 防撕裂 */
lv_display_set_buffers(disp, &fb_background[0][0], &fb_background[1][0],
                       sizeof(fb_background[0]), LV_DISPLAY_RENDER_MODE_DIRECT);

static void disp_flush(...) {
    if (!lv_display_flush_is_last(display)) return;
    SCB_CleanInvalidateDCache_by_Addr(px_map, sizeof(fb_background[0]));
    R_GLCDC_BufferChange(&g_display0_ctrl, (uint8_t *) px_map, 0);
}
```

我方改动点：
- `PARTIAL` + 逐行 memcpy → **`DIRECT` + `R_GLCDC_BufferChange`**
- 新增 vsync 信号量（`rt_sem` + `DisplayVsyncCallback`）
- lv_conf.h：`LV_HOR_RES_MAX 480`、`LV_VER_RES_MAX 360`（现为 800×480）
- **D-cache 一致性**：本工程 `BSP_CFG_DCACHE_ENABLED` 当前为 0，`SCB_CleanInvalidateDCache_by_Addr` 为 no-op 无害；若将来开启必须保留该调用。

### Step 7 — 验收判据重做（**核心**）

⚠️ 原判据（"SWD 读 SDRAM framebuffer 像素 == 期望值"）**不适用**——它只能证明内存写对，**证明不了屏亮**。这就是 Phase 2/3/4 假绿的来源。

新判据分三层：

**L1 寄存器层（SWD / msh）**
- `lcd stat` → `BG.EN=0x00010001`（EN=1 VEN=0）、`BG.HSIZE=480`、`BG.VSIZE=360`、`L1UNDF=0`
- 新增 `lcd dsi` 命令 → 读 `R_DSILINK` 状态寄存器：PHY 上电、PLL 锁定、Link Up、错误计数器为 0
- 新增 `lcd cmds` 命令 → 打印命令表推送到第几条（`g_message_sent` 计数）

**L2 内存层（SWD）**
- 双缓冲两页像素断言（保留现有能力，改为 480×360）

**L3 物理层（唯一能证明屏亮的）**
- **人眼/相机观察**：`lcd pattern 1`（全白）→ 屏应全白；`lcd pattern 2`（彩条）→ 8 条彩条
- 若 L1 全绿 + L3 仍黑 → 才是面板/连线问题，而非固件

`tools/verify/verify_phase5.py`：L1 + L2 自动化（PASS/FAIL 计数），L3 人工确认（脚本打印待确认项）。

### Step 8 — 摄像头 / OpenMV 宏屏蔽 ✅ 已完成

新增编译开关（`applications/app_config.h`）：
```c
#define APP_ENABLE_CAMERA   (0)   /* Phase 5: LCD-only, camera gated off */
#define APP_ENABLE_OPENMV   (0)
```
- `main.c`：`cam_cmd` 注册、`bsp_cam.h`/`bsp_sccb.h` include 用 `#if APP_ENABLE_CAMERA` 包裹
- `bsp_cam.c` / `bsp_ov5640.c` / `bsp_sccb.c` **及其 .h**：文件级 `#if APP_ENABLE_CAMERA` 整体包裹
  （关闭时每个文件退化为一个空编译单元 + 占位 typedef）
- **CEU 向量**（slot 1）同步 `#if` 屏蔽，`VECTOR_DATA_IRQ_COUNT` 9→8
- 保留 `bsp_pin.c` 中摄像头引脚（HAL 层静态表，屏蔽不影响）

**实证结论**：宏屏蔽而非删文件——用户明确要求"使用宏屏蔽"。

验收证据（`arm-none-eabi-nm` 硬证据，非仅"编译通过"）：
```
$ arm-none-eabi-nm build/firmware.elf | grep -i "bsp_cam\|bsp_ov5640\|bsp_sccb\|ceu_isr\|cam_cmd"
（0 命中 = 摄像头代码完全未进镜像）
$ arm-none-eabi-nm build/firmware.elf | grep -i "mipi_dsi\|glcdc"
02067668 T g_mipi_dsi  ...  02009490 T glcdc_line_detect_isr   （DSI 通路在位）
```
双构零警告：Debug `text 444252 / data 496 / bss 914660`；Release `347324 / 252 / 914680`。
bss 相较 Step 6 下降 **153,712 B**（CEU 帧缓冲 `320×240×2 = 153600 B` 随宏一并排除）。


### Step 9 — 菜单应用（参考 `003.stm32h743_lvgl_oled`）

屏点亮后，在 `applications/lv_app.c` 实现菜单：
- 深色主题（LVGL dark），主列表若干条目（占位 + 1~2 个已实现项）
- 无实体按键 → 复用现有 msh 串口命令驱动选中项（`menu up/down/enter`）
- 界面风格对齐 `003.stm32h743_lvgl_oled` 的实现（施工前先读该工程）

### Step 10 — 收尾

- Debug/Release **双构零警告**，显式报 FLASH/RAM 占比
- 真机烧录（OpenOCD + `.elf`）→ 串口 COM9 验证
- `tools/verify/verify_phase5.py` 给出 PASS/FAIL 计数
- `documents/phase5-report.md` + README 阶段表更新 + `.workbuddy/memory/` 日志
- commit（不做 push）

---

## 3. 风险与预案

| 风险 | 预案 |
|---|---|
| `R_DSILINK_*` 宏在本工程 device header 缺失 | 从官方工程 `bsp_mcu_device_pn_cfg.h` / `R7FA8D1BH.h` 对比补齐 |
| DSI 向量 slot 与 `bsp_irq.c` 冲突 | 读 `bsp_irq.c` 确认 `bsp_irq_cfg()` 按表编程；必要时改 slot 分配 |
| GLCDC `hstride` 用错（480 vs 256 像素） | 按官方公式硬算并加注释；L3 观察验证 |
| DSI 命令表死等（`g_message_sent` 不置位） | 先确认 SEQ0 向量已注册；加超时保护 + 错误日志 |
| DRW/Dave2D 依赖 | 本工程**不引入** DAVE2D（官方 g2d 仅用于加速，非必需） |
| 屏仍黑 | 按 L1→L2→L3 逐层排除，L3 黑则查排线/屏型/背光电流 |

---

## 4. 待确认

1. **是否同意上述 10 步施工顺序**（尤其 Step 5 的 stride=512 字节与 Step 7 判据重做）？
2. 触摸是否纳入本 Phase？（官方 2.0" 屏配 **CST812T** I2C 触摸，`lv_port_indev.c` 已有参考实现；本 Phase 可先不做，维持串口驱动菜单）
3. 菜单参考工程 `003.stm32h743_lvgl_oled` 是否按其现有代码风格 1:1 对齐？

---

*计划生成时间：Phase 4 完成后；依据官方 MIPI 参考工程 verbatim 配置，未做任何推测。*
