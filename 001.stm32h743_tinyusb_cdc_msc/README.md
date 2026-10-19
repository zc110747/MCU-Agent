# 001 · STM32H743 TinyUSB CDC+MSC 复合设备

基于 STM32H743ZIT6 + TinyUSB 实现 **USB 复合设备**：CDC（虚拟串口）+ MSC（大容量存储，把板载 SD 卡暴露为 PC 可访问的 U 盘）。
SD 卡同时被固件侧 FatFs 与 PC 侧 USB 访问，CDC 与 MSC 并行工作、互不干扰。

> 需求与验收详见 `docs/prompter.md`。

---

## 1. 硬件与接口

| 项 | 说明 |
|----|------|
| MCU | STM32H743ZIT6 |
| 时钟 | HSE 外部无源晶振 25 MHz |
| USB | 默认 **OTG_FS**（PA11/PA12，TinyUSB rhport 0）；可选 **OTG_HS**（PB14/PB15，内置 FS PHY，rhport 1） |
| 存储 | 板载 microSD（SDIO），FatFs + TinyUSB MSC |
| 调试 | SWD + ST-Link；SVD = `STM32H743.svd` |

USB 控制器通过 CMake 选项切换：`-DUSB_PORT=FS`（默认）或 `-DUSB_PORT=HS`。

---

## 2. 工程结构

```
001.stm32h743_tinyusb_cdc_msc/
├── app/           应用层：main.c / stm32h7xx_it.c / stm32h7xx_hal_conf.h / syscalls.c
│   │              tusb_config.h（TinyUSB 配置）
│   └── usb/       usb_descriptors.c（复合设备描述符）/ msc_disk.c / sd_app.c / disk_interface.c
├── bsp/           板级：bsp.c / bsp_led.c / bsp_sdcard.c / bsp_log.c（PRINT_LOG 日志）
├── sys_startup/   启动与链接：gcc / arm / iar 三套启动文件 + system_stm32h7xx.c
│                  + stm32h743zi_flash.ld
├── Drivers/       CMSIS + STM32H7xx HAL（ST 官方，gitignore，不手改）
├── third_party/   tinyusb / FatFs（gitignore）
├── cmake/         arm-none-eabi.cmake 交叉工具链
├── MDK-ARM/       Keil 工程 stm32h743.uvprojx（ARMCLANG）+ mdk_target.c + stm32h743.sct
├── docs/          prompter.md / prompter-check.md / prompter-remaster.md
├── tools/         test_cdc.py / test_cdc_msc.py / test_roundtrip.py   PC 端自测脚本
├── CMakeLists.txt / CMakePresets.json   # 预设：debug / release / debug-hs
├── openocd.cfg / STM32H743.svd          # 均在根级
└── build_oneclick.bat                   # 单工程一键编译
```

> 模块化约定：应用逻辑 `app/`、用户驱动 `bsp/`、启动与链接 `sys_startup/`、
> HAL 只放 `Drivers/`、第三方库只放 `third_party/`。GCC（CMake）与 Keil（MDK-ARM）双构建后端并存。

---

## 3. 开发流程

1. 先实现 **CDC 设备**，测试虚拟串口收发正常。
2. 再叠加 **MSC**，把 SD 卡以 U 盘方式暴露，验证 PC 读写与固件侧 FatFs 内部读写共存。
3. 验收：CDC 与 MSC **同时工作、互不干扰**。

---

## 4. 构建与运行

### 一键编译
- **单工程**：双击 `build_oneclick.bat`
  - 检查工具 `cmake / ninja / openocd / arm-none-eabi-gcc`（缺失提示参考 `document/support.md`）；
  - 检查根目录 `Drivers`、`third_party`（缺失提示从 `..\support_tools\env_support_for_stm32h743.zip` 解压/拷贝）；
  - 流程 `configure → clean → build`；所有出口 `pause` 停留查看。
- **全仓库**：在仓库根目录双击 `build_all.bat`，顺序编译 `project.list` 中的全部 21 个工程；**某工程失败会暂停等你回车后继续**，末尾输出 `Passed/Failed/Skipped` 汇总。

### 手动（预设工程）
```bash
cmake --preset debug
cmake --build build/debug --target clean
cmake --build build/debug
```
产物：`build/debug/h743_tinyusb_cdc.elf`（同时生成 `.hex` / `.bin` 与 size 报告）。

### 约束与实测
- **零警告**：`-Wall -Wextra`（第三方库按 `-Wno-*` 豁免），构建应 0 warning。
- **依赖缺失**：报 `Missing dependency: third_party/tinyusb` → 解压 `env_support_for_stm32h743.zip`。
- **实测资源**（`arm-none-eabi-size`，2026-10-19 重新核对）：Debug FLASH 69,484 B / 2 MB ≈ **3.31%**；
  Release FLASH 58,796 B ≈ **2.80%**；`.bss` 14,552 B（主要落在 DTCMRAM）。
  <sub>早期记录的 65,668 B / 13,880 B 是重构前的数字，已过时。</sub>

---

## 5. 调试与烧录

- **VSCode（cortex-debug）**：`.vscode/launch.json` 用裸工具名（`openocdPath:"openocd"`、`gdbPath:"arm-none-eabi-gdb"`），
  `configFiles`/`svdFile` 经 `${workspaceFolder}` 指向根级 `openocd.cfg` 与 `STM32H743.svd`，`preLaunchTask:"build"`（F5 先编译再调试）。
- **命令行烧录**：
  ```bash
  openocd -f openocd.cfg -c "program build/debug/h743_tinyusb_cdc.elf verify reset exit"
  ```
- **单步 / 排错**：F5 进入调试；或用 `gdb_step_test.gdb` / `gdb_where.gdb` 等 gdb 脚本。
- SWD 速率 1800 kHz（stlink v2 clone 安全值；V3 可更高）。

---

## 6. 验收与自测

- ✅ CDC 串口正常通讯；PC 可正常访问 U 盘（SD 卡）。
- ✅ CDC 与 MSC 并行工作，互不干扰。
- PC 端脚本（需 Python，位于 `tools/`）：
  - `python tools/test_roundtrip.py` — CDC 回环测试
  - `python tools/test_cdc_msc.py` — CDC + MSC 并发测试

---

## 7. 常见问题

| 现象 | 根因 / 处理 |
|------|-------------|
| `Missing dependency: third_party/tinyusb` | 未解压第三方库 → 见第 4 节依赖处理 |
| 改 `.ld` 后 ninja 报 `no work to do` | 链接脚本未触发重配置 → 重跑 `cmake --preset debug` 或 `clean` |
| cortex-debug Attach 超时 | `openocd.cfg` **不要**加 `connect_assert_srst`（会把 core 一直 hold 在 reset），保持 `stm32h7x.cfg` 默认软件复位 |
| USB 枚举不稳定 | 确认 `USB_PORT` 与实际焊接的 USB 引脚（FS=PA11/PA12，HS=PB14/PB15）一致 |
