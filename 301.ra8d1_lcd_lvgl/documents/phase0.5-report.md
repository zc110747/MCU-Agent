# Phase 0.5 验收报告

日期：2026-09-30

## 1. 退出条件核对

| # | 条件 | 状态 | 证据 |
|---|---|---|---|
| 1 | Cortex-M85 编译参数实测确定 | ✅ | `-mcpu=cortex-m85 -mfpu=auto -mfloat-abi=hard -mthumb` 编译通过（fpv5-d16/fpv5-sp-d16/auto/mve.fp 全 OK，取 auto） |
| 2 | 裸机最小工程编译零警告 | ✅ | Debug/Release 双构，`grep -c warning\|error` = 0 |
| 3 | OpenOCD 对 RA8D1 支持实测 | ✅ | `SWD DPIDR 0x6ba02477`，`Cortex-M85 r0p2 processor detected`，8 breakpoints / 8 watchpoints |
| 4 | flash 烧录实测 | ✅ | pyOCD + DFP 官方 FLM（RA8D1_2M.FLM），`Program done. Target reset and running.` |
| 5 | LED 实测 | ✅ | P102 PmnPFS (0x40400848) 密集采样：`0x7 ↔ 0x4` 交替（PODR 翻转、PDR=输出） |
| 6 | 断点实测 | ✅ | `bp 0x02000116 2 hw` → resume → `halted due to breakpoint, pc=0x02000116` |

## 2. 资源占用（Release）

```text
text 4992  data 4  bss 24736
FLASH: 4996B / 2064384B (0x1F8000)  ≈ 0.24%
RAM:   24740B / 917504B  (0xE0000)  ≈ 2.70%
```

## 3. 工具链决策记录

### 3.1 烧录链：pyOCD（用户定案）

- sysprogs OpenOCD 0.12.0 无 RA8D1 flash 驱动（二进制内仅 renesas_rpchf）；上游亦无 RA8 flash 支持。
- pyOCD 0.45.1 + Renesas.RA_DFP 6.6.0 pack：
  - pack 目标 `r7fa8d1bh` 初始化在 ART-Link 上必失败（`DebugPortSetup` 序列 → `SWD/JTAG communication failure (No ACK)`），已试过 `pack.debug_sequences.enable=false`、`disabled_sequences=DebugPortSetup`、`deferred_transfers=false`、`limit_packets`、under-reset 等组合，全部无效；
  - **泛型 `cortex_m` 目标完全正常**（attach/halt/读写）；
  - 定案：泛型目标 + 手动注入 `FlashRegion(flm=RA8D1_2M.FLM)`，用官方算法擦写（`tools/flash/flash.py`）。
- pack 修补：RA_DFP 6.6.0 pdsc 引用 3 个不存在的 `_NS` FLM 导致 pyOCD 加载失败；另 SVD 的 dim 格式令 pyOCD 解析崩溃 → 修补脚本 `tools/flash/get_pack.py`（去 missing algorithm 行 + 去 svd 属性）。

### 3.2 调试链：OpenOCD

- `tools/openocd/artlink_ra8d1.cfg`：cmsis-dap + SWD + `swd newdap` + `cortex_m` 目标，实测 halt/断点/复位正常。
- gdb server 3333 端口可用（VSCode Cortex-Debug 接入留待后续需要时配置）。

## 4. 排错记录（真机取证）

| 现象 | 根因 | 修复 |
|---|---|---|
| 首次烧录 `FlashFailure: target was not halted (IPSR=3)` | 板载 RT-Thread 固件运行中开了 D-Cache，DAP 写入算法 RAM 后 CPU 读到陈旧缓存行 | 烧录前 `target.reset_and_halt()`（复位态缓存关闭），烧录后恢复正常 |
| 采样 PFS 读数恒定 | ① PFS 地址算错（PORT 步进 0x40、PIN 步进 4，P102=0x40400848，非 0x10/0x100 步进）；② 前次 OpenOCD 会话 halt 后未 resume，目标一直停着 | 修正地址 + `resume` 后密集采样（5ms 间隔）确认 0x4↔0x7 |
| PFS 写入无效 | RA8 PFS 写保护走 TZ 路径，解锁寄存器是 `R_PMISC->PWPRS @0x40400D14`（bit6 PFSWE），非 PWPR@0x0C | 手动写 PWPRS=0x40 后 PFS 写入生效（取证用，正常路径由 R_BSP_PinAccessEnable 完成） |

## 5. 交付物

| 文件 | 说明 |
|---|---|
| `cmake/arm_gcc.cmake` | M85 工具链文件（相对路径，裸程序名） |
| `CMakeLists.txt` | 自研码 `-Wall -Wextra` 严格，Drivers 宽松 |
| `bsp/ra8d1-vision-board/{bsp_led.c/h,bsp_syscalls.c,gen/,cfg/,linker/}` | 板级适配 + 最小向量表 |
| `applications/main.c` | entry() LED 闪烁 |
| `tools/build/build.bat` | configure/build/clean 一键（纯英文） |
| `tools/flash/{flash.py,flash.bat,get_pack.py}` | pyOCD 烧录 + pack 修补复现 |
| `tools/openocd/artlink_ra8d1.cfg` | OpenOCD 调试配置 |
| `Drivers/renesas/{fsp,board,arm/CMSIS_5}` | 上游原样（仅 board_cfg.h/bsp_mcu_family_cfg.h 两处生成头文件的相对路径改为本仓路径，见 cfg 注释） |

## 6. 遗留项

| # | 项 | 关闭阶段 |
|---|---|---|
| 1 | UART9 串口输出验证（P208/P209 冲突以生成代码为准） | Phase 1 |
| 2 | SDRAM 32MB 实测确认 | Phase 1/2 |
| 3 | VSCode Cortex-Debug 接入（OpenOCD cfg 已就绪） | 按需 |
| 4 | LED 有效电平（当前双态驱动，闪烁不受影响） | 查原理图 |
