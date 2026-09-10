---
name: esp32-cortex-debug
description: >
  ESP32-S3 (Arduino/ESP-IDF) 通过 VSCode Cortex-Debug + openocd-esp32 + xtensa-gdb
  搭建断点调试链路的完整配方与排错清单。涵盖 launch.json 正确写法、JTAG 驱动安装
  (eim install-drivers)、以及 15 类常见报错（Arg list too long / LIBUSB_ERROR_ACCESS /
  GDB Server Quit / No symbols for FreeRTOS 等）的根因与修复。适用于"ESP32 怎么用
  VSCode 调试""openocd 报 Arg list too long""F5 断连""ESP32-S3 JTAG 驱动"等请求。
---

# ESP32-S3 Cortex-Debug 调试链路配方

适用组合（已验证）：
- ESP32-S3（含 N16R8：OPI PSRAM + 16MB Flash）
- VSCode + Cortex-Debug 1.12.1（mcu-debug 全家桶：debug-tracker / memory-view /
  peripheral-viewer / rtos-views）
- openocd-esp32（espressif fork，随 Arduino Core 内置，**不是** STM32 的 sysprogs openocd）
- xtensa-esp-elf-gdb 17.1（随 Arduino Core 内置）

## 1. 工具链路径（随 ESP32 Core 内置，无需另装）

以 `esp32:esp32@3.3.11`、data 目录 `D:\software\arduino-cli\data` 为例，按实际版本号替换：

| 组件 | 路径 |
|------|------|
| openocd-esp32 | `...\esp32\tools\openocd-esp32\v0.12.0-esp32-20260424\bin\openocd.exe` |
| xtensa gdb | `...\esp32\tools\xtensa-esp-elf-gdb\17.1_20260402\bin\xtensa-esp32-elf-gdb.exe` |
| binutils (nm/objdump) | `...\esp32\tools\esp-x32\2601\bin` —— **nm/objdump 在此，不在 gdb 目录** |
| openocd scripts | `...\openocd-esp32\v0.12.0-esp32-20260424\share\openocd\scripts` |

> ⚠️ Cortex-Debug 的 `armToolchainPath` 要指向 **esp-x32 binutils 目录**（取 nm/objdump），
> 而 `gdbPath` 指向 **gdb 目录**。两者不同，少了前者会报 `xtensa-esp-elf-nm.exe ENOENT`。

## 2. 调试接口（硬件）

- **内置 USB-Serial-JTAG**：VID `0x303a` / PID `0x1001`，板载 USB 口（非 CH343 串口口）。
  用 `board/esp32s3-builtin.cfg`，免外接调试器。
- **CH343 串口**：只能下载（esptool/arduino-cli upload），**不能调试**。
- **外接 ESP-Prog**：用 `board/esp32s3-ftdi.cfg`。

## 3. JTAG 驱动安装（Windows，必需）

Windows 默认把 USB-Serial-JTAG 绑到串口驱动 `usbser`，libusb 无法原生打开 →
openocd 报 `LIBUSB_ERROR_NOT_FOUND` 或 `LIBUSB_ERROR_ACCESS`。

**正确解法（官方，自动按标准描述符把 JTAG Interface 0 装 WinUSB 并保留 CDC 串口）：**

```
# 管理员 PowerShell
eim install-drivers
```

（`eim` = Espressif Installation Manager。不要用旧 `idf-env driver install`，
也不要用 Zadig 手动装——社区案例把 Interface 2（CDC）误装成 WinUSB 反而倒退，
标准描述符里 JTAG 在 Interface 0。）

验证驱动成功：`openocd -f board/esp32s3-builtin.cfg -c "init; reset halt; shutdown"`
应看到 `esp_usb_jtag: Device found` + `Examination succeed` + 退出码 0。

## 4. 正确的 launch.json 配方（三配置一致）

```jsonc
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "ESP32-S3 Debug (内置USB-Serial-JTAG)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}\\.build\\<project>.ino.elf",
            "toolchainPrefix": "xtensa-esp32-elf",
            "armToolchainPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\esp-x32\\2601\\bin",
            "gdbPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\xtensa-esp-elf-gdb\\17.1_20260402\\bin\\xtensa-esp-32-elf-gdb.exe",
            "serverpath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\bin\\openocd.exe",
            "configFiles": ["board/esp32s3-builtin.cfg"],
            "searchDir": ["D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\share\\openocd\\scripts"],
            "overrideLaunchCommands": [],
            "runToEntryPoint": "app_init",
            "showDevDebugOutput": "none",
            "serverArgs": ["-d2"]
        },
        {
            "name": "ESP32-S3 Debug (内置USB-Serial-JTAG - 安全手动)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}\\.build\\<project>.ino.elf",
            "toolchainPrefix": "xtensa-esp32-elf",
            "armToolchainPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\esp-x32\\2601\\bin",
            "gdbPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\xtensa-esp-elf-gdb\\17.1_20260402\\bin\\xtensa-esp-32-elf-gdb.exe",
            "serverpath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\bin\\openocd.exe",
            "configFiles": ["board/esp32s3-builtin.cfg"],
            "searchDir": ["D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\share\\openocd\\scripts"],
            "overrideLaunchCommands": [],
            "showDevDebugOutput": "none",
            "serverArgs": ["-d2"]
        },
        {
            "name": "ESP32-S3 Debug (ESP-Prog/FTDI)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}\\.build\\<project>.ino.elf",
            "toolchainPrefix": "xtensa-esp32-elf",
            "armToolchainPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\esp-x32\\2601\\bin",
            "gdbPath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\xtensa-esp-elf-gdb\\17.1_20260402\\bin\\xtensa-esp-32-elf-gdb.exe",
            "serverpath": "D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\bin\\openocd.exe",
            "configFiles": ["board/esp32s3-ftdi.cfg"],
            "searchDir": ["D:\\software\\arduino-cli\\data\\packages\\esp32\\tools\\openocd-esp32\\v0.12.0-esp32-20260424\\share\\openocd\\scripts"],
            "overrideLaunchCommands": [],
            "runToEntryPoint": "app_init",
            "showDevDebugOutput": "none",
            "serverArgs": ["-d2"]
        }
    ]
}
```

### 关键字段铁律

| 字段 | 值 | 原因 |
|------|-----|------|
| `overrideLaunchCommands` | `[]`（空数组，**必写**） | 覆盖 Cortex-Debug 默认的 `monitor reset halt`，否则必触发 `Arg list too long` 断连 |
| `serverArgs` | `["-d2"]`（**紧贴**，不要 `["-d","2"]`） | esp32 版 openocd 不认 `-d 2`，报 `Unexpected command line argument: 2` |
| `runToEntryPoint` | `"app_init"` 或 `"app_main"`（**不要 `"main"`**） | Arduino elf 无 `main` 符号，`thb main` 失败→continue 跑飞→断连 |
| `rtos` | **不要写** | Arduino elf 无 FreeRTOS 符号 → openocd 线程查询拖垮 gdb 连接（`GDB server session ended`） |
| `armToolchainPath` | esp-x32 binutils 目录 | nm/objdump 在此，Cortex-Debug 拼 `toolchainPrefix-nm` 取符号 |

> 入口符号核对：`nm <elf> | grep -E "app_init|app_main"`。本项目 elf 同时存在
> `app_init`(`_Z8app_initv`) 与 `app_main`(`_Z8app_mainv`) 两个合法符号，任选其一即可。

## 5. 15 类报错根因速查表

| # | 报错 | 根因 | 修复 |
|---|------|------|------|
| 1 | `Unexpected command line argument: 2` | `serverArgs: ["-d","2"]` 空格分隔 | 改 `["-d2"]` |
| 2 | `xtensa-esp-elf-nm.exe ENOENT` | nm/objdump 在 esp-x32 binutils 目录，不在 gdb 目录 | `armToolchainPath` 指向 `esp-x32/2601/bin` |
| 3 | `LIBUSB_ERROR_NOT_FOUND` | Windows 串口驱动 usbser 绑死 USB-Serial-JTAG，libusb 打不开 | `eim install-drivers`（管理员） |
| 4 | `LIBUSB_ERROR_ACCESS` | 设备被独占——常是**残留 openocd/esptool 进程占 USB 复合设备** | `taskkill /F /IM openocd.exe` 清残留后再 F5 |
| 5 | `Arg list too long` (from `monitor reset halt`) | 来自 **Cortex-Debug 默认启动命令**；attach 后重复复位触发 gdb/openocd 协议崩 | 写 `"overrideLaunchCommands": []` 覆盖默认 |
| 6 | `Arg list too long` (from `flushregs`) | gdb 17.1 下 `flushregs` 是废弃别名，触发寄存器重读崩 | 删除该命令（空 override 即规避）|
| 7 | `Arg list too long` (from `reset init`) | `reset init` 停在 ROM 态、Flash MMU 未映射，后续 Flash 断点崩 openocd | 不用 `reset init`，用 `reset halt` 或空 override |
| 8 | `Function "main" not defined` | Arduino elf 无 `main` 符号 | `runToEntryPoint` 改 `app_init`/`app_main` |
| 9 | `No hardware breakpoint support` | gdb 17.1 误判 xtensa 硬件断点数为 0 | 一般无需处理；空 override + 正确入口即可 |
| 10 | `Warn: No symbols for FreeRTOS!` | Arduino elf 不带 FreeRTOS 符号 | 删 `rtos: "FreeRTOS"`（**必删，否则线程查询拖垮连接**）|
| 11 | `GDB server session ended` / `GDB Server Quit Unexpectedly` | `rtos: "FreeRTOS"` 导致 openocd 线程遍历失败断连（决定性真凶） | 删除 `rtos` 键 |
| 12 | `probe-rs-debug: Received unknown custom event` | 误装 probe-rs-debug 扩展与 Cortex-Debug 抢 DAP 事件 | 卸载 probe-rs-debug 扩展 |
| 13 | `0x40000400 in ?? ()` + 服务端崩溃 | `reset init` 停在 ROM 早于 Flash 映射 | 同 #7，不用 `reset init` |
| 14 | openocd 启动即退出（无板子） | 板子没插在内置 USB-Serial-JTAG 口（VID 0x303a PID 0x1001） | 插对板载 USB 口（非 CH343 口）|
| 15 | 调试正常但 RTOS 视图空 | 删 `rtos` 后无任务线程列表 | 预期代价；需线程视图须换 ESP-IDF 工程导出 FreeRTOS 符号 |

## 6. 推荐排错顺序

1. **驱动**：先 `eim install-drivers`，命令行 `openocd -f board/esp32s3-builtin.cfg -c "init; reset halt; shutdown"` 验证 `Device found`。
2. **残留进程**：F5 前 `tasklist | findstr openocd`，有则 `taskkill /F /IM openocd.exe`，否则 `LIBUSB_ERROR_ACCESS`。
3. **launch.json**：严格按第 4 节铁律——`overrideLaunchCommands: []` + `serverArgs: ["-d2"]` + `runToEntryPoint: "app_init"` + **无 `rtos` 键**。
4. **扩展冲突**：确认未装 probe-rs-debug。
5. **看日志**：DEBUG CONSOLE 看不到服务端崩溃原因，必须看 **TERMINAL 标签页 gdb-server 输出**。

## 7. 实测验证结论（本项目 201.esp32s3_rtos）

- 构建体积：程序 314768 B / 24%，动态内存 22880 B / 6%。
- F5 选 `ESP32-S3 Debug (内置USB-Serial-JTAG)` → openocd 自动 halt（Flash 已映射）→
  `runToEntryPoint: app_init` 自动停在入口 → 断点/单步/变量/调用栈全部正常 ✅。
- 代价：无 RTOS 线程视图（删 `rtos` 所致），对应用调试无影响。
