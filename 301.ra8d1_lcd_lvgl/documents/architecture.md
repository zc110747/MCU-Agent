# 工程架构（Phase 0）

## 1. 目标目录结构

```text
301.ra8d1_lcd_lvgl/            （= 项目根，等同规范的 ra8d1-nano-vision/）
├── applications/              应用层：main、demos（led/lcd/lvgl/camera）
├── bsp/
│   └── ra8d1-vision-board/    板级适配：startup/ linker/ clock/ uart/ led/ lcd/ camera/ rtthread/ lvgl/
├── Drivers/
│   └── renesas/               Renesas FSP/HAL 原始驱动 + CMSIS 设备文件（从官方 BSP 拷贝，保持原样）
├── third_party/
│   ├── rt-thread/rt-thread-nano/
│   └── lvgl/                  （Phase 4 从 BSP libraries/components/LVGL-v9.1.0 拷贝）
├── documents/                 本 Phase 0 产出的分析文档
├── tools/
│   ├── build/ flash/ debug/   CLI 脚本（.bat 纯英文）
│   └── openocd/               OpenOCD cfg（ART-Link + RA8D1 target）
├── CMakeLists.txt
├── README.md
└── .gitignore
```

## 2. 依赖方向（强制）

```text
applications → bsp → { Drivers/renesas, third_party }
禁止任何反向依赖（Drivers/RT-Thread/LVGL 不得 include applications 或 bsp）。
```

## 3. 拷贝来源计划（只拷需要的，不依赖官方 BSP 目录构建）

| 目标 | 来源（官方 BSP 内） | 阶段 |
|---|---|---|
| Drivers/renesas/fsp/{bsp,inc} + cmsis Device(R7FA8D1BH.h, system.c, 启动) | `projects/*/ra/fsp/src/bsp` | Phase 0.5 |
| Drivers/renesas/fsp/src/r_ioport、r_sci_b_uart | `projects/vision_board_blink_led/ra/fsp/src/` | Phase 0.5 |
| Drivers/renesas/fsp/src/r_glcdc、r_drw、r_gpt（+LCD 所需） | `projects/lcd/vision_board_rgb_4.3inch` / camera 工程 | Phase 2 |
| Drivers/renesas/fsp/src/r_ceu、r_mipi_dsi、r_mipi_phy | camera 工程 | Phase 3 |
| third_party/rt-thread-nano | RT-Thread Nano（Phase 1 确定获取方式；BSP 内是 full 5.0.2，仅作移植参考） | Phase 1 |
| third_party/lvgl | `libraries/components/LVGL-v9.1.0`（v9.1.0） | Phase 4 |
| 生成代码参考（pin_data/hal_data/时钟/链接脚本） | 对应官方工程 `ra_gen/ ra_cfg/ script/fsp.ld` | 各 Phase |

原则：**Drivers/ 与 third_party/ 保持 upstream 原始状态**；本项目适配全部进 `bsp/`。

## 4. 构建与调试链

```text
arm-none-eabi-gcc 15.3.1 + CMake + Ninja
    ↓ firmware.elf/bin/hex/map + size.txt
OpenOCD 0.12.0 + CMSIS-DAP(ART-Link)
    ↓ SWD
RA8D1 (Cortex-M85)
```

- 工具从系统 PATH 查找，**禁止硬编码绝对路径**
- Cortex-M85 编译参数（-mcpu/-mfpu/-mfloat-abi）在 Phase 0.5 用 `arm-none-eabi-gcc` 实测确认后固定，不照搬 M4/M7
- OpenOCD 对 RA8D1 支持情况 Phase 0.5 实测；缺配置则基于官方/上游最小化补充，不凭空写 flash algorithm

## 5. 阶段推进（每阶段：分析→实现→编译→静态检查→硬件验证→报告→commit）

| 阶段 | 内容 | 退出条件 |
|---|---|---|
| Phase 0 ✅ | 硬件/BSP 分析，本目录文档 | 文档齐、事实带来源 |
| Phase 0.5 | GCC 裸机最小工程（启动+时钟+LED）+ OpenOCD/CMSIS-DAP 调试链 | 编译零警告；halt/reset/断点/flash 实测通过 |
| Phase 1 | RT-Thread Nano + UART9 + LED | 串口输出 + LED 10 分钟稳定 |
| Phase 2 | GLCDC RGB LCD（800×480 RGB565）+ Cache/DMA 方案 | 色块/像素测试 10 分钟无异常 |
| Phase 3 | CEU Camera | 采图上屏正确 |
| Phase 4 | LVGL v9.1.0 | demo 流畅运行 |
