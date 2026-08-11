# 实机验证记录 — STM32H743 LVGL 菜单 + NES 模拟器

**日期**：2026-08-11
**烧录器**：ST-Link V2（固件 V2J38M27，VID:PID 0483:374B）
**目标**：STM32H743ZIT6（Cortex-M7 r1p1，SWD DPIDR 0x6ba02477）
**工具链**：OpenOCD 0.12.0 / arm-none-eabi-gcc 15.3.1 / CMake+Ninja / pyserial 3.5

---

## 1. 构建（Release）
```
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
- 零错误零告警。
- 内存：DTCMRAM 82340 B / 128 KB = **62.82%**；RAM_D2 256 KB / 288 KB = **88.89%**；FLASH 256796 B / 2 MB = 12.24%。
- 产物：`build-release/nes_h743.elf`（text 256412 / data 376 / bss 461836）。

## 2. 烧录（OpenOCD）
```
openocd -f openocd.cfg -c "init" -c "program build-release/nes_h743.elf verify reset exit"
```
结果：
```
Info : STLINK V2J38M27 (API v2) VID:PID 0483:374B
Info : Target voltage: ~4.0 V
Info : SWD DPIDR 0x6ba02477
Info : [stm32h7x.cpu0] Cortex-M7 r1p1 processor detected
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```

## 3. 控制台连通性（双通道均验证）
- **COM19** = `STMicroelectronics STLink Virtual COM Port` → USART1 PA9/PA10
  ```
  fw     : H743-NES 1.0.0
  sysclk : 480000000 Hz
  clock  : HSE 25MHz
  console: uart only
  view   : menu
  ```
- **COM4** = `USB 串行设备` → STM32 USB CDC（PA11/PA12，TinyUSB）
  ```
  console: uart+usb
  ```
  > 两条串口链路共用同一解析器，命令完全一致。

## 4. Python 串口自测（`scripts/serial_test.py --port COM19`）
**28/28 checks passed**

| 测试组 | 结果 |
| --- | --- |
| basics（echo/status/pages） | PASS |
| navigation（open nes/clock/sysinfo/keytest/about → back） | PASS |
| keys（sel / key / down a 持键 / up a / release） | PASS |
| nes（rom list） | PASS（卡上无 .nes，优雅跳过） |
| error handling（非法命令/按键/页面/越界 ROM） | PASS（均回 ERR，控制台不卡死） |

固件关键确认：sysclk 480 MHz（H7 全速）、HSE 25 MHz 外部晶振锁定、LVGL 菜单框架运行、虚拟按键层工作、错误健壮。

## 5. NES 实机游玩（已验证 ✅）
- ROM 放置：**根目录 `1:`**（非 `1:/NES`），固件回退扫描生效，`rom list` 找到 4 个 `.NES`。
  - 中文 GBK 文件名现已在 `rom list` **正确显示**（UTF-8 转码已实装）。新增 `bsp/gbk_conv.c` + `bsp/gbk_unicode_tbl.c`（离线 `tools/gen_gbk_table.py` 生成，GBK→Unicode 23940 项/21791 映射，~48KB Flash）。仅显示层转码，存储名保持 GBK 以保证 `f_open` 字节级命中。
- 加载第 0 个 ROM：
  ```
  rom load 0
  OK rom load 0 <乱码>.NES
  [NES ] 1:/<乱码>.NES loaded, 40976 B, mapper 0 (NROM)
  ```
- 运行状态：
  ```
  rom info
    state  : running
    mapper : 0 (NROM)
    frames : 112
    fps    : 31
    pad    : 0x00
  status
    view   : nes (fullscreen)
    nes    : running, roms 4, fps 31
  ```
- 退出：
  ```
  rom stop
  [NES ] stopped after 442 frames
  ```
- 结论：Release 构建在 STM32H743@480MHz 跑出 **31 fps**（高于预估的 ≥20）。对 NES 原生 60fps 略慢但可玩；240×240 OLED 为降采样，CPU 瓶颈在 PPU 软件渲染。Mapper 0 (NROM) 验证通过。
