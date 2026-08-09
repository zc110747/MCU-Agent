# 项目长期记忆：STM32H743 CMSIS-DAP v1 调试探针

## 目标
用 STM32H743ZIT6 + tinyusb 实现 CMSIS-DAP v1（USB HID）调试器固件，GPIO 位操作模拟 SWD/JTAG，
验收：通过本板仿真 STM32F429（OpenOCD 读 IDCODE + 下载 + 调试）。

## 硬件接线（GPIOA）
SWDIO=PA0, SWCLK=PA1, NRST=PA2, nTRST=PA3, TDI=PA5, TDO=PA7；状态 LED=PG7。
USB 用 OTG_FS(PA11/PA12, rhport0) —— **已用户确认板子 USB 座即 FS 口**，debug 预设直接对应；OTG_HS(PB14/PB15) 仅备用，未实测。

## 构建
- 预设：`debug`/`release`/`debug-hs`（CMakePresets）。`cmake --preset debug && cmake --build build/debug`
- CMSIS-DAP 源码(DAP.c/SW_DP.c/JTAG_DP.c)强制 -O2（SWD/JTAG 位操作时序）。
- 不含 SWO（SWO.c 已剔除，SWO_UART/SWO_MANCHESTER=0）。
- 便捷目标：`flash`/`erase`/`reset`（ST-Link 烧本板）、`dap-test`（本板->F429）。

## 移植到 GCC/M7 的关键修改（详见 README §6）
1. `PIN_DELAY_SLOW` 由 Keil `__asm{SUBS/BNE}` 改为 DWT 周期计数器自旋；`DELAY_SLOW_CYCLES=1U`。
   DWT 在 `DAP_SETUP()` 中解锁+使能。
2. `DAP_SWJ_Clock` 中 `delay` 初始化为 `1U`（消除未初始化 UB）。
3. `dap_port.c` 的 `MODE_INPUT/MODE_OUTPUT` 用 `#ifndef` 守卫（HAL 同名）。
4. `syscalls.c` 补 `_write` 空实现。

## 验收状态
- 软件：固件编译通过、ELF 含 "STM32H743 CMSIS-DAP v1" 产品串、OpenOCD 配置/目标就绪。
- 硬件：未验证（本环境无板）。需 ST-Link 烧写 H743 + 接线 F429 + openocd 读 IDCODE 0x2BA01477。

## 工具链路径
- arm-none-eabi-gcc 15.3.1: E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin
- OpenOCD: D:/software/ST/OpenOCD/bin/openocd.exe
