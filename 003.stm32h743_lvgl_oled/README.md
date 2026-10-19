# 003 · STM32H743 LVGL OLED 中文显示

STM32H743ZIT6 + LVGL v8，在 240×240 SPI OLED（ST7789 / SPI6）上显示中文界面。
中文字库放 SD 卡（`1:/SYSTEM/FONT/`），支持三种引擎：**GBK 点阵字库**、**CTF 索引 + TTF 随机访问**、
以及内置 Montserrat（仅 ASCII 回退）。

> 📖 **完整文档在 [`docs/README.md`](./docs/README.md)** —— 硬件接线、模块分层、字体引擎原理、
> 构建/烧录、验收记录与踩坑全部在那里。本文件只做入口索引。

---

## 快速索引

| 想了解 | 去哪 |
|---|---|
| 硬件与引脚、调试串口 | [docs/README.md §1](./docs/README.md#1-硬件与接口) |
| 目录结构 / 模块职责 | [docs/README.md §2](./docs/README.md#2-工程结构) |
| 构建与烧录 | [docs/README.md §4](./docs/README.md#4-构建与运行) |
| 字体系统（CTF + TTF 块缓存） | [docs/README.md §8](./docs/README.md#8-字体系统ctf-索引--ttf-块缓存重构进行中) |
| PRINT_LOG 日志系统 | [docs/README.md §9](./docs/README.md#9-日志系统重构print_log--tx-中断驱动2026-09-03) |
| TTF → CTF 索引生成器 | [`tools/ttf2ctf/README.md`](./tools/ttf2ctf/README.md) |

## 一句话上手

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # 本工程无 CMakePresets
cmake --build build
openocd -f openocd.cfg -c "program build/lvgl_oled.elf verify reset exit"
```

产物：`build/lvgl_oled.elf` / `.hex` / `.bin`。Debug/Release 双构零警告。

## 关键约定

- 目录分层：`app/`（应用与界面）→ `bsp/`（驱动、字体引擎）→ `{Drivers, third_party}`（上游，不改）。
- 字库是**磁盘资源**：整个 TTF / CTF 都不进 RAM，固件按需读小记录（`bsp/font/`）。
- 双构建后端：GCC（CMake）与 Keil（`MDK-ARM/`），源集两侧对齐；Keil 侧排除 `syscalls.c`。
- 本工程同时是 009（Zephyr + LVGL）的裸机**参考实现**，用于对齐 SPI6 / ST7789 / 字库加载路径。
