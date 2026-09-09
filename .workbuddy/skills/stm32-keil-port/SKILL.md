---
name: stm32-keil-port
description: 把 CMake/GCC 的 STM32 工程移植到 Keil MDK-ARM（uvprojx + 分散加载 sct）并用 UV4 命令行构建的端到端流程。涵盖：从 CMake 源集清点真实文件、写分散加载文件（.framebuffer 固定到 AXI SRAM 0x24000000）、重写 uvprojx（Groups/IncludePath/Define/Thumb）、排除 GCC 专用文件（syscalls.c）、ARMCLANG 警告参数（单横杠 -Wno-）、UV4 -b 批构建与 build_log.htm 解析、常见坑（非法 ARM 态、链接符号、GUI 日志不回显）。适用于"用 Keil 打开/编译 STM32 工程""移植 uvprojx""UV4 命令行构建""让 GCC 工程能在 MDK 编译""Keil 编译报错排查"。触发词：Keil、uvprojx、MDK-ARM、ARMCLANG、UV4、分散加载、scatter、sct、STM32H7 DFP、syscalls.c 排除、TinyUSB Keil、把 GCC 工程迁到 Keil。
agent_created: true
---

# 把 STM32 工程移植到 Keil MDK-ARM（UV4 命令行构建）

当项目主体是 CMake + arm-none-eabi-gcc，但需要一套能在 Keil μVision 里打开、或用
`UV4.exe -b` 做 CI/命令行构建的工程时，用本流程。核心矛盾：GCC/newlib 工程里有些
文件/链接符号是工具链专用的，直接塞进 Keil 会编不过——**正确做法是「排除」而非「改造」**。

## 一、前置确认（动手前必查）

1. **UV4 路径**：常见 `C:\Keil_v5\UV4\UV4.exe`。不要用 `UV4 -h`（GUI 程序，会弹界面卡住）。
2. **ARMCLANG 版本**：`C:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe --version`。
   把 `pCCUsed=6140000::V6.14::ARMCLANG` 与 `uAC6=1` 写进 uvprojx，版本号要和实际安装一致。
3. **DFP 器件包**：STM32H7 用 `Keil.STM32H7xx_DFP.4.1.3`（新机可能装在
   `%LOCALAPPDATA%\Arm\Packs\Keil\STM32H7xx_DFP\4.1.3`，不在 `C:\Keil_v5\ARM\PACK`）。
   uvprojx 里 `Device=STM32H743ZITx`、`PackID=Keil.STM32H7xx_DFP.4.1.3` 必须装好对应版本，否则设备解析失败。
4. **启动文件语法**：确认 `sys_startup/arm/startup_stm32h7xx.s` 是 **MDK/Keil 语法**
   （`AREA | DCD | EXPORT __initial_sp | __heap_base/__heap_limit`），不是 GCC `.word/.section`。
   本机启动文件已导出 `__HeapBase`/`__HeapLimit`/`__initial_sp`，Keil 堆管理直接用它。

## 二、从 CMake 清点真实源集（别信旧的 uvprojx）

旧 uvprojx 经常指向另一套 app（FatFs / MSC / CDC 等），直接重写更稳：

```bash
# 看 CMake 里都编了哪些 .c，以及 IncludePath / 宏定义
grep -nE "add_executable|target_include_directories|target_compile_definitions|HSE_VALUE|CFG_TUSB" CMakeLists.txt
ls Core/Src BSP/* Drivers/STM32H7xx_HAL_Driver/Src third_party/tinyusb/src
```

据此重组 uvprojx 的 Groups/Files。**关键：把 `Core/Src/syscalls.c` 排除在 Keil 工程之外。**

## 三、分散加载文件 `stm32h743.sct`

镜像 GCC 的 `*.ld`，把摄像头帧缓冲 `.framebuffer` 钉在 **AXI SRAM 0x24000000**
（DCMI DMA 只能访问该域；DTCM 放 .data/.bss/heap/stack，SRAM_D2/D3 兜底溢出）：

```
LR_IROM1 0x08000000 0x00200000
{
  ER_IROM1 0x08000000 0x00200000  { *.o (RESET, +First) * (InRoot$$Sections) .ANY (+RO) }
  RW_IRAM2 0x24000000 0x00080000  { *.o (.framebuffer) *.o (.ram_d1) }   /* AXI: 帧缓冲 */
  RW_IRAM1 0x20000000 0x00020000  { .ANY (+RW +ZI) }                     /* DTCM: 快内存 */
  RW_IRAM3 0x30000000 0x00048000  { .ANY (+RW +ZI) }                     /* SRAM_D2 */
  RW_IRAM4 0x38000000 0x00010000  { .ANY (+RW +ZI) }                     /* SRAM_D3 */
}
```

> 不要试图用 scatter 的 `EXPORT _end/_estack/_Min_Stack_Size` 给 newlib `_sbrk`——
> ARMCLANG 不认这些符号，且根本不需要（它用自己的堆）。**排除 syscalls.c 才是正解。**

## 四、重写 uvprojx（关键字段）

- `<TargetName>` / `<Device>STM32H743ZITx</Device>` / `<PackID>Keil.STM32H7xx_DFP.4.1.3</PackID>`
- `<pCCUsed>6140000::V6.14::ARMCLANG</pCCUsed>`、`<uAC6>1</uAC6>`
- **Thumb 必开**：`<Cads><uThumb>1</uThumb>` 且 `<Aads><thumb>1</thumb>`。
  旧工程若误设 `uThumb=0` 会生成非法 ARM 态代码（Cortex-M 只认 Thumb）。
- **IncludePath**（相对路径，随工程移动）：
  `..\Core\Inc;..\BSP\ov5640;..\Drivers\STM32H7xx_HAL_Driver\Inc;..\Drivers\STM32H7xx_HAL_Driver\Inc\Legacy;..\Drivers\CMSIS\Include;..\sys_startup;..\third_party\tinyusb\src`
- **Define**（沿用 CMake，补齐缺失项）：
  `STM32H743xx,USE_HAL_DRIVER,USE_PWR_LDO_SUPPLY,HSE_VALUE=25000000,CFG_TUSB_MCU=OPT_MCU_STM32H7,CFG_TUSB_OS=OPT_OS_NONE,BOARD_TUD_RHPORT=0,BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED`
- **MiscControls（C）**：`-Wno-unused-parameter -Wno-sign-compare`
  ⚠️ **单横杠**。ARMCLANG 不接受 GCC 的 `--Wno-...`（报 `unknown option`）。
- **ScatterFile**：`stm32h743.sct`；勾 `<useFile>1</useFile>`。
- **After Build**：`fromelf --bin !L --output Objects\stm32h743.bin`（生成 .bin）。
- **Groups/Files**：Application/Startup（startup .s + system_stm32h7xx.c）、Application/User/Core（main 等）、
  Drivers/STM32H7xx_HAL_Driver（按需 HAL .c）、BSP/ov5640、Middlewares/TinyUSB。
  **`syscalls.c` 不在此列表。**

## 五、UV4 命令行构建与日志解析

```bat
"C:\Keil_v5\UV4\UV4.exe" -b -j0 -t stm32h743 ^
  -x "D:\proj\MDK-ARM\stm32h743.uvprojx" ^
  -o "D:\proj\MDK-ARM\build_log.htm"
```

- `-b` 批构建；`-j0` 不限并发；`-t` 指定 target；`-o` 写 HTML 日志。
- **stdout 不一定回显**，判断成败靠 `build_log.htm` 末行：
  `"Objects\stm32h743.axf" - 0 Error(s), 0 Warning(s).`
- 退出码 `0` = 成功；`2` = 有错误。
- 用 `fromelf --info=totals Objects\stm32h743.axf` 取尺寸：
  `Total ROM Size`（= .bin 大小）、`Total RW Size`（RW+ZI，含 AXI 帧缓冲）。
- 产物：`Objects/stm32h743.axf` / `.hex` / `.bin`。

## 六、常见坑（按出现频率排序）

| 现象 | 根因 | 修法 |
|------|------|------|
| `error: unknown option '--Wno-...'` | ARMCLANG 用单横杠 | 改 `-Wno-...` |
| `_sbrk` / `_end` / `_estack` 未定义 | 把 GCC 的 `syscalls.c` 加进了 Keil | **从工程移除 syscalls.c**，ARMCLANG 自带堆 |
| `<sys/stat.h>` / `<sys/types.h> not found` | 同上（syscalls.c 引用 POSIX 头） | 移除即可，勿改文件 |
| 生成非法 ARM 态指令 | `uThumb=0` | 设 `uThumb=1`、`thumb=1` |
| 分散加载 EXPORT 不生效 | ARMCLANG 不认 `_end` 等 | 别 EXPORT，排除 syscalls.c |
| `UV4` 卡住不返回 | 误加 `-h` 或弹 GUI | 只用 `-b` 批模式，日志走 `-o` |
| 设备解析失败 | DFP 版本/路径不对 | 装对 `STM32H7xx_DFP.4.1.3`，确认 `PackID` |

## 七、验收标准

- [ ] `build_log.htm` 末行 `0 Error(s), 0 Warning(s)`
- [ ] `Objects/` 下 `.axf` / `.hex` / `.bin` 均生成
- [ ] `fromelf --info=totals` 的 ROM 大小与预期一致（本工程 90.13 KB / 2 MB）
- [ ] 帧缓冲落在 AXI SRAM（ZI 约 345728 B = 3×240×240，符合 `.framebuffer` 段）
- [ ] 真机烧录/运行验证为可选后续（与 GCC 构建共用同一份源码逻辑）
