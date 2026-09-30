# Phase 4 报告 — LVGL v9.1.0 集成（GLCDC RGB565 framebuffer）

> 目标：LVGL v9.1.0 跑在 Phase 2 的 GLCDC framebuffer 上，tick/flush 对接
> RT-Thread Nano，动画 demo + 确定性测试屏做端到端验收。
> 验收脚本：`tools/verify/verify_phase4.py` → **7 passed, 0 failed**。

## 1. 交付内容

| 模块 | 文件 | 说明 |
|---|---|---|
| LVGL 上游源码（原样） | `third_party/lvgl/`（src/ + lvgl.h + LICENCE.txt） | v9.1.0 官方 release，未做任何修改 |
| 配置 | `third_party/lv_conf.h` | 只写覆盖项：RGB565、builtin malloc 128KB、OS_NONE、默认主题（dark）+ Montserrat 14 |
| 移植层 | `applications/lv_port.c/.h` | display 创建（PARTIAL 模式 800×40 行缓冲）、flush=memcpy 进 `bsp_lcd_framebuffer()`、tick 回调、demo/test 双屏、LVGL 线程 |
| 应用 | `applications/main.c` | `lv start / test / demo / info` 命令；上电自启 LVGL |
| 构建 | `CMakeLists.txt` | `lvgl_core` 静态库（`-w`，上游不修警告；纯 API 调用无 section 表，archive 提取安全） |
| 验收脚本 | `tools/verify/verify_phase4.py` | 串口 + SWD 双通道，7 项 PASS/FAIL |

## 2. 架构

```text
LVGL v9.1.0（PARTIAL 渲染，g_draw_buf 800×40×2B = 64KB 内部 SRAM）
   │  flush_cb: 逐行 memcpy（rt_memcpy）到 SDRAM framebuffer
   ▼
g_lcd_fb @ 0x68000000 (.sdram, 768000B)  ──►  GLCDC 层1 ──► RGB 面板 800×480
   ▲
   │  lv_tick_set_cb() → rt_tick_get_millisecond()（1kHz 内核 tick）
   │  "lvgl" 线程（8KB 栈，heap 分配）：lv_timer_handler() 循环，间隔≤33ms
```

线程模型：只有 LVGL 线程碰对象；msh 命令只置 volatile 标志（切屏）或读 32 位
计数器（flushes/fps/mem），Cortex-M85 上原子，无锁。

两块屏幕：
- **demo**（默认）：深色底 + 标题/uptime/fps 标签（1s 刷新）+ 0↔100 循环 bar +
  旋转 arc + RGBW 色块行（ASCII 文案——Montserrat 无 CJK 字形）。
- **test**：纯色矩形屏（无圆角/边框/阴影），4 个已知坐标色块供 SWD 像素断言。

## 3. 实测数据

- **双构零警告**：Debug FLASH 449060B(21.41%) / 内部 RAM 263384B(25.12%)；
  Release FLASH 350344B(16.70%) / RAM 263176B(25.10%)
- SDRAM 不变：921600B（LCD fb 768000 + camera fb 153600，2.75%）
- LVGL 堆占用 6-8KB / 128KB 池（demo 屏稳态）
- fps 实测 30-57（动画重绘面积相关），达标 30fps 目标
- 验收：`python tools/verify/verify_phase4.py` → **7 passed, 0 failed**
  （SWD 像素断言：(60,40)=0xF800 红、(170,40)=0x07E0 绿、(280,40)=0x001F 蓝、
  (390,40)=0xFFFF 白、背景 (700,240)=0x0000，全部精确命中）

## 4. 踩坑记录（v8 → v9 迁移陷阱）

| 坑 | 现象 | 根因 / 修法 |
|---|---|---|
| **LV_TICK_CUSTOM 已删除** | 首屏渲染恰好 12 次 flush（480/40 全屏一遍）后完全冻结：fps=0、timer/动画全停 | v8 的 `LV_TICK_CUSTOM` 宏在 v9 不存在（lv_conf_internal.h 里根本没有这个门控），LVGL 默认 tick 恒 0 → 动画/timer 永不到期。v9 正解：运行时 `lv_tick_set_cb(cb)`，cb 返回毫秒 |
| **lv_color_hex() 是 RGB888** | 测试屏 4 个色块颜色全部"错位"：红 0xF800 读回 0x07C0、绿 0x07E0→0x003C、蓝 0x001F→0x0003、白 0xFFFF→0x07FF | `lv_color_hex()` 参数按 **0xRRGGBB** 解释；传 565 值等于传了一组 RGB888（0xF800 = G=0xF8 的亮绿！）。四组读回值与该模型逐位吻合（几何/行结构完全正确）。修法：传 0xFF0000/0x00FF00/0x0000FF/0xFFFFFF，565 转换交给 LVGL |
| `lv_theme_default` 不存在 | 编译错 | v9 theme API：`lv_theme_default_init(...)` 返回主题并已作用于 display，无需再 `lv_display_set_theme` |
| `lv_mem_monitor_t.used_size` 字段不存在 | 编译错 | v9.1 字段为 `total_size/free_size/used_pct/max_used`，used = total-free |
| anim exec 回调签名 | 潜在 UB | `lv_bar_set_value` 带第三参（anim_enable），直接 cast 成 `lv_anim_exec_xcb_t` 会把垃圾值传入第三参触发重入；包一层 `bar_anim_cb(void*, int32_t)` |
| 空翻译单元 | 第三方 `-w` 抑制 | src/drivers/ 下 SDL 等文件整体被 config 宏清空 |

## 5. 设计取舍

- **静态库而非编译进固件**：LVGL 是纯 API 调用（无 RT-Thread 式 section 表注册），
  archive 按未定义符号提取成员，`--gc-sections` 裁掉未用控件/驱动/库（thorvg、
  freetype 等 1.1MB+ 源码因 `LV_USE_VECTOR_GRAPHIC=0` 等默认关闭根本不参与编译）。
- **PARTIAL 模式 40 行缓冲**：64KB 内部 SRAM（1MB 总量的 6%），flush 双拷贝成本
  换实现简单与低内存占用；DIRECT 直渲可省 memcpy，但渲染进 SDRAM 慢且整屏
  buffer 占 768KB，Phase 4 无性能需求。
- **无触摸输入**：本 Phase 范围外（板上触摸控制器走 I2C，Phase 2/3 未引入），
  demo 为纯输出。如后续需要接 GT911/FT 系列，加 `lv_indev` 驱动即可。
- **深色主题**：与项目 UI 约定一致（`lv_theme_default_init(..., dark=true, ...)`）。

## 6. 全 Phase 总验收状态

| Phase | 内容 | 验收 | commit |
|---|---|---|---|
| 0/0.5/0.6 | 硬件分析 / GCC 最小工程 / VSCode pyOCD 仿真 | ✅ | 9dc9c34 |
| 1 | RT-Thread Nano + LED + UART | verify_phase1 15/15 | 478df28 |
| 2 | GLCDC + RGB 面板 | verify_phase2 14/14 | 2ef9e25 |
| 3 | Camera（OV5640 + CEU） | verify_phase3 14/14 | 4cc3ffb |
| 4 | LVGL v9.1.0 | verify_phase4 7/7 | 本提交 |
