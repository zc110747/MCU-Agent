# 302.ra8d1_lcd_nes — RA8D1 Vision Board NES 模拟器

基于 **Renesas RA8D1 Vision Board**（R7FA8D1BH，Cortex-M85 @480 MHz）的 NES 模拟器工程：
RT-Thread Nano + GLCDC/MIPI-DSI 面板 + **DRW2D (D/AVE 2D)** 硬件 blit + W25Q64 OSPI 闪存。
工程沿用 301 的构建/调试链（Arm GNU GCC + CMake/Ninja + pyOCD/ART-Link），
本文件只描述 302 自身的实现与当前状态。

> ⚠️ 与早期计划书的差异：**USB MSC 与 W25Q64 上的 FatFs 尚未落地**。
> `CMakeLists.txt` 顶部的 `tinyusb` 与 README 历史描述属于计划项；当前代码里没有
> TinyUSB 组件，ROM 走"主机经 SWD 写入 staging 窗口"的路径（见 §5）。
> 这一条是有意写明的——不然接手的人会去找一个不存在的 U 盘。

---

## 1. 硬件速览

| 项 | 说明 |
|---|---|
| MCU | R7FA8D1BH（Cortex-M85，480 MHz，2 MB Flash / 1 MB SRAM） |
| 显示 | **MIPI DSI 480×360**（GLCDC layer-0 framebuffer 在 SDRAM），DRW2D 做 blit |
| 外部 RAM | SDRAM 32 MB @0x68000000 |
| 外部 Flash | W25Q64-class SPI NOR（8 MB）挂 xSPI/OSPI @0x60000000 |
| 调试串口 | SCI9（P208 = TXD9 / P209 = RXD9），115200 8N1——同时是 RT-Thread msh 控制台 |
| 调试 | ART-Link（CMSIS-DAP）：pyOCD（主）/ OpenOCD（备） |
| 输入 | **无实体十字键/按键接到模拟器**：用串口控制台的虚拟手柄（见 §4.3） |

---

## 2. 分层

```
applications/             应用层：main.c（msh 命令、线程、NES 生命周期）、mipi_dsi_conf
bsp/ra8d1-vision-board/   板级：bsp_lcd / bsp_gpu(DRW2D) / bsp_ospi / bsp_uart / bsp_pin
                          bsp_isr / bsp_rtthread / bsp_syscalls / bsp_diag / bsp_led / bsp_rtc
                          gen/（中断向量与 IRQ 配置表）
                          cfg/ linker/（FSP 配置与链接脚本）
bsp/nes/                  模拟器移植层：nes_port（呈现/计时）、nes_heap（SDRAM 池）、
                          rt_key_scan（串口虚拟手柄）
Drivers/renesas/fsp/      上游 FSP（-w，不改）
third_party/              rt-thread-nano、dave2d（DRW2D 库）、nes（上游内核）
cmake/                    工具链与构建片段
tools/                    构建 / 烧录 / 调试 / 验证脚本（见 §6）
```

依赖方向：`applications → bsp → { Drivers, third_party }`，禁止反向。
`-Wall -Wextra` 只加在本仓库自己的三层；上游代码一律 `-w`。

---

## 3. 运行时模型

- `entry()` 由 RT-Thread Nano 提供（`third_party/rt-thread-nano/src/components.c`），
  创建 `main` 线程；`main()` 里再做板级初始化。
- **NES 核心跑在独立线程**：`nes_run()` 是永不返回的逐帧循环，由 `nes_deinit()`
  置 `nes_quit` 让它自己退出，线程资源随之回收。
- `nes_t`（6502 + PPU 上下文）从 **SDRAM 专用堆**（`nes_heap`）分配，带水位与
  失败计数；`nes info` 可查 total / used / peak / nalloc / nfail。
- 呈现路径固定：`nes_draw()` → `bsp_gpu_blit_array()`（DRW2D `d2_blitcopy`）→
  `d2_flushframe`，目标就是 GLCDC layer-0 的 SDRAM 缓冲。

### 实测（480 MHz，8,000,000 cycles = 一帧 60 Hz 预算）

| 环节 | 每帧周期 | 占预算 |
|---|---|---|
| 呈现（DRW2D blit） | 0.61 M | 7% |
| 模拟（6502 + PPU） | 27.0 M | 337% → 约 **13 fps** |

> 瓶颈在解释器本身，不在显示或存储。串口 `nes bench [S]` 可重新测稳态帧周期。

---

## 4. 控制台命令（RT-Thread msh，SCI9）

### 4.1 板级 / 诊断

| 命令 | 作用 |
|---|---|
| `led on\|off\|blink` | 板载 LED（P102） |
| `lcd init\|info\|stat\|dsi\|vsync\|pattern N\|fill HEX` | MIPI DSI 面板、时序、自检图案 |
| `gpu init\|info\|fill HEX\|blit` | DRW2D 设备打开与 blit 自检 |
| `ospi init\|id\|regs\|pfs\|cs\|ints\|bb\|miso\|stat\|r ADDR LEN\|e ADDR` | W25Q64 读写/寄存器/ID |
| `hb on\|off` | 2 s 心跳（默认 **off**：与 shell 抢串口会让脚本化抓取不可靠） |
| `dbg threads` | 遍历内核线程表（排查 shell 卡死） |

### 4.2 NES

| 命令 | 作用 |
|---|---|
| `nes test [N]` | 用合成帧走**真实呈现路径**（不需 ROM、不需模拟线程），验证 blit 链路 |
| `nes stage` | 打印 ROM staging 窗口地址/大小，供主机脚本写入 |
| `nes runmem` | 从 staging 窗口加载 iNES（校验 `NES\x1a` magic）并开跑 |
| `nes load PATH` | 从文件系统加载 ROM——**依赖尚未落地的 DFS/FatFs**，当前会失败 |
| `nes run` / `nes stop` / `nes info` / `nes bench [S]` | 运行控制与统计 |

### 4.3 虚拟手柄（`bsp/nes/rt_key_scan.c`）

板子没有接到 NES 的实体按键，上游的 `rt_key_scan` 是空桩。本工程改成**串口字符驱动**：
无锁位掩码，`nes_read_joypad()` 每帧采样一次（轮询，不用 `rt_event`，无法与 shell 读线程死锁）。

| 键 | 功能 | 键 | 功能 |
|---|---|---|---|
| `w/s/a/d` | 上 / 下 / 左 / 右 | `k` / `j` | A / B |
| `n` / `m` | SELECT / START | 空格 | 全部释放 |

`nes keys on|off` 开关捕获；`nes key <mask>` 直接注入掩码，是验收脚本用的无人工路径。

---

## 5. ROM 加载（当前唯一可用路径）

```text
PC ──SWD/pyOCD──▶ nes_heap 的 staging 窗口（RAM）
                        │  nes runmem  校验 iNES magic
                        ▼
                   nes_load_rom() ──▶ SDRAM 中的 nes_t ──▶ 逐帧模拟
```

1. `nes stage` 拿到 `addr` / `size`；
2. 主机侧用 pyOCD 把 `tools/testdata/SuperMary.nes`（或任意 iNES）写进该窗口；
3. `nes runmem` 开跑；`nes stop` 结束并释放。

W25Q64 的 `ospi` 读写通路已经打通（ID/寄存器/扇区读写在 `ospi` 命令里可验），
但**文件系统与 U 盘暴露仍是计划项**，所以 `nes load <path>` 暂时不可用。

---

## 6. 构建 / 烧录 / 验证

```bat
tools\build\build.bat            :: Debug（默认），产物 build\firmware.elf
tools\build\build.bat release    :: Release
tools\build\build.bat clean

tools\flash\flash.bat            :: pyOCD + ART-Link 烧 build\firmware.elf
tools\flash\flash.bat PATH\image.elf
```

- 工具链文件 `cmake/arm_gcc.cmake`；无 `CMakePresets.json`，构建目录固定 `build/`。
- 烧录走 `tools/flash/flash.py` + `RA8D1_2M.FLM`（pyOCD 注入闪存算法，
  见 skill `pyocd-flm-inject-debug`）；`pyocd.yaml` / `pyocd_user.py` 提供会话配置。
- `openocd.cfg` 是备用调试链（CMSIS-DAP，gdb :3333）。

| 工具 | 用途 |
|---|---|
| `tools/verify/dump_fb.py` | 经 SWD 抓 SDRAM framebuffer，落盘比对/看图 |
| `tools/verify/probe_ospi_regs.py` | 读 OSPI 控制器与 Flash 状态寄存器 |
| `tools/dbg/*.py` | 现场取证：`read_regs` / `read_ospi_vars` / `probe_validity` / `thread_dump` / `uart_check` / `sci9_check` / `miso_level` / `ccr0_track` / `soak_regs` / `diag_read` |
| `tools/testdata/SuperMary.nes` | 最小可跑 ROM（staging 路径用） |

VSCode：`.vscode/` 内为 build / flash / clean 任务与 cortex-debug 配置
（pyOCD + ART-Link，`R7FA8D1BH.svd` 外设视图）。

---

## 7. 已知问题

1. **`-O1` / `-O2` 会破坏串口控制台**（已在 `CMakeLists.txt` 顶部记录）：
   每行 `rt_kprintf("... tick=%u", n)` 在 `tick=` 处被截断，格式化不再执行。
   `-Og` 完全正常，上游参考工程在 MDK 高优化下也正常——触发点在本工具链 + RT-Thread
   varargs 格式化的组合，尚未定位。因此 Debug/Release 都用 `-Og`，两者只差调试信息。
2. **FatFs / USB MSC 未实现**：见文首说明。`nes load <path>` 暂不可用，ROM 走 staging。
3. **无实体游戏按键**：只能用串口虚拟手柄；触摸屏归 GUI（301）使用，不接模拟器。
4. **帧率约 13 fps**：解释器开销，非显示或存储瓶颈；`nes bench` 可复测。
5. **心跳默认关闭**：`hb on` 才打印，避免与 msh 命令回显抢 SCI9。

---

## 8. 参考

- 同平台的显示/触摸/调试链细节见 [`../301.ra8d1_lcd_lvgl`](../301.ra8d1_lcd_lvgl/README.md) 与
  skill `ra8d1-fsp-platform-notes` / `ra8d1-vscode-pyocd-debug` / `pyocd-flm-inject-debug`。
- NES 内核为上游项目（`third_party/nes/README_upstream.md`），本工程只做移植层。
