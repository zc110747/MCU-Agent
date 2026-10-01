# Phase 8 报告：移除复位静态彩条 + 启动动作条（boot splash）

## 1. 背景与问题

Phase 7 完成后，复位（reset）时面板会在两个时间点之间显示一段**冻结的 8 色彩条**：

```
bsp_lcd_init() 完成（GLCDC 开始扫描 SDRAM framebuffer）
        │
        │   ← 这段时间里 framebuffer 里是 bsp_lcd_pattern(2U) 画的彩条
        ▼
LVGL 线程建立并渲染出首帧
```

根因在 `applications/main.c`：

```c
if (g_lcd_started)
{
    bsp_lcd_pattern(2U);   /* colour bars until LVGL paints over them */
    lv_port_start();
}
```

`bsp_lcd_pattern(2U)` 是 Phase 2/4 引入的显示自查图案（8 色竖条），本意是"在
LVGL 起来前给面板一个可见信号"。但对最终产品而言，复位时看到一段测试彩条是
不合理的观感。

## 2. 目标

1. **移除**复位时的静态彩条界面。
2. **新增**"启动动作条界面"（boot splash）：板名 + 进度条 + 当前动作文字，
   跑满后自动切到 Phase 7 的单页状态页。

## 3. 实现

### 3.1 新增 `applications/ui/ui_page_boot.c/.h`

启动页布局（480×360）：

```
 0   ┌─────────────────────────────────────────┐
     │           RA8D1 VISION BOARD            │  36 px header
110  │              Booting...                 │  title  (28 px, 白)
160  │  ████████████████░░░░░░░░░░░░░░░░░░░░░  │  action bar (360x16)
196  │           Initializing RTC              │  caption (16 px, 灰)
222  │                    42 %                 │  percent (16 px, 绿)
300  │        RT-Thread Nano / LVGL v9.1       │  footer  (14 px, 暗)
     └─────────────────────────────────────────┘
```

- 进度条用 LVGL `lv_bar`：轨道 `COL_BAR_BG(0x2A2A2A)`、填充 `COL_ACCENT(0xFFA000)`、
  圆角 2 px、无边框。
- 40 ms `lv_timer` 自驱动，每 tick +2%，约 2 s 跑满；caption 按百分比映射到
  6 个步骤文案（Core clock → SDRAM → DSI → RTC → LVGL → Ready）。
- 对外接口：
  - `lv_obj_t *ui_page_boot_build(void)` — 构建并 arm 动画，返回 screen。
  - `void ui_page_boot_set(int32_t pct, const char *caption)` — 外部强制推进/命名。
  - `bool ui_page_boot_done(void)` — 交接判定。

### 3.2 `applications/lv_port.c`

- `build_boot_screen()` 包装 `ui_page_boot_build() + lv_screen_load()`。
- 线程 entry 启动时：`g_boot_active = true; build_boot_screen();`（替换原
  `build_main_screen()`）。
- `mode_timer_cb`（50 ms）优先判定交接：

```c
if (g_boot_active && ui_page_boot_done())
{
    g_boot_active = false;
    build_main_screen();
}
```

- `lv test` / `lv main` 请求会立即 `g_boot_active = false`，允许人工抢占。

### 3.3 `applications/main.c`

- `bsp_lcd_pattern(2U)` → `bsp_lcd_fill(0x0000U)`：黑色占位，LVGL 启动页
  会在下一帧立即接管（面板不再出现彩条）。
- 启动横幅：`Phase 7 (...)` → `Phase 8 (boot action bar + single-page UI + RTC)`。

> `bsp_lcd_pattern()` 函数本体保留 —— 它仍是 `lcd pattern N` 命令的调试图案，
> 只是不再在启动路径上被调用。

## 4. 踩坑

| 坑 | 现象 | 正解 |
|---|---|---|
| GLOB 不追踪新增文件 | 新增 `ui_page_boot.c` 后链接报 `undefined reference to ui_page_boot_build` | CMake `file(GLOB)` 在 configure 时展开；**清空并重新 configure** 构建目录（`rm -rf build_dbg build_rel && cmake -S . -B ...`） |
| 短命启动帧难抓 | 启动动作条约 2 s，串口看不到画面 | pyOCD `reset_and_halt()` → `resume()` → `sleep(0.55)` → `halt()`，读 framebuffer 取证 |
| 白字被误判为彩条 | "无纯白块"判据 `<500px` 误报（实际是标题抗锯齿 509px） | 彩条是**整块大面积纯色**；阈值放到 `<5000px`，只排除真实彩条带 |

## 5. 验收

### 5.1 双构零警告

| 配置 | text | data | bss |
|---|---|---|---|
| Debug   | 534604 B | 496 B | 914740 B |
| Release | 437676 B | 252 B | 914768 B |

两配置 `-Wall -Wextra` 下均无 warning。

### 5.2 真机取证（`tools/verify/verify_phase8.py`）

**启动帧**（复位后 0.55 s halt）：

| 元素 | 设计色 (RGB565) | 实测像素 |
|---|---|---|
| 背景 | `0x0000` | 146813 |
| 动作条轨道 | `0x2945` | **5514** |
| 动作条填充 | `0xFD00` | **228**（≈4% 进度）|
| 标题栏 | `0x09EC` | 15824 |
| 标题白字 | `0xFFFF` | 509（仅抗锯齿，无彩条）|

**主页面帧**（复位后 4.0 s halt）：

| 元素 | 设计色 | 实测像素 |
|---|---|---|
| 时钟 cyan | `0x073F` | 513 |
| 值绿 | `0x470E` | 1344 |
| 标签灰 | `0x8C51` | 517 |
| 动作条轨道 | `0x2945` | **0**（已交接）|

结果：**10 passed / 0 failed / 2 manual**。

## 6. 交付物

- `applications/ui/ui_page_boot.c/.h`（新增）
- `applications/lv_port.c`（启动流程改造）
- `applications/main.c`（去彩条 + 横幅）
- `tools/verify/verify_phase8.py`（新增）
- `README.md` / 本报告

## 7. 限制

- 启动动作条是**观感动画**：真实初始化工作（时钟/SDRAM/DSI/RTC）在 `main()`
  里、LVGL 线程起来之前就已经完成，动画步骤文案是示意性的（如需真实进度，
  可用 `ui_page_boot_set()` 在 main.c 各阶段推进）。
- 面板仍为只读（无触摸），交互仅串口 msh。
