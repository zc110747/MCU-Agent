# 003 · STM32H743 LVGL OLED 中文显示

基于 STM32H743ZIT6 + **LVGL**，在 240×240 SPI OLED（ST7789，SPI6 接口）上显示中文标签与界面。
中文字库存放于 SD 卡 `1:/SYSTEM/FONT/`（GBK 点阵字库），经 FatFs 读取并由 GBK→Unicode 映射驱动 LVGL 字体渲染。
调试串口 USART1（PA9/PA10）。

> 需求与验收详见本目录 `prompter.md`。本工程亦作为 Zephyr + LVGL 项目（009）的裸机**参考实现**，用于对齐 SPI6 / ST7789 / 字库加载路径。

---

## 1. 硬件与接口

| 项 | 说明 |
|----|------|
| MCU | STM32H743ZIT6 |
| 时钟 | HSE 外部无源晶振 25 MHz（`HSE_VALUE=25000000`） |
| 显示 | 240×240 SPI OLED（ST7789，SPI 接口） |
| 存储 | 板载 microSD（SDIO）：中文字库 + FatFs |
| 调试串口 | USART1 PA9/PA10，115200 8N1 |
| 调试 | SWD + ST-Link；SVD = `STM32H743.svd` |

---

## 2. 工程结构

```
003.stm32h743_lvgl_oled/
├── Core/          startup_stm32h743xx.s / system_stm32h7xx.c / main.c
│                 stm32h7xx_hal_msp.c / stm32h7xx_it.c / syscalls.c
├── Bsp/          drv_spi_oled.c / drv_oled_fonts.c / drv_oled_text.c
│                 drv_sdio.c / drv_rtc.c / disk_interface.c
│                 lv_port_disp.c（LVGL 显示对接）/ lv_font_gbk.c / lv_gbk_map.c（GBK 字体）
├── Application/  app_main.c / app_ui.c（LVGL 界面）
├── Drivers/      CMSIS + STM32H7xx HAL
├── third_party/  lvgl / FatFs
├── cmake/        gcc-arm-none-eabi.cmake
├── CMakeLists.txt          # 普通 CMake（无 CMakePresets）
├── STM32H743ZITX_FLASH.ld  # 根级链接脚本
├── openocd.cfg             # 根级，stlink + swd
├── STM32H743.svd
└── build_oneclick.bat      # 单工程一键编译
```

> 模块化约定：应用逻辑 `Application/`、用户驱动 `Bsp/`、HAL `Drivers/`、第三方 `third_party/`。

---

## 3. 开发流程

1. 先实现 **SD 卡读取**（FatFs）。
2. 再实现 **OLED 显示输出**（SPI 驱动）。
3. 集成 **LVGL**，实现基于 LVGL 的显示应用与 SD 卡 GBK 字库渲染。

---

## 4. 构建与运行

### 一键编译
- **单工程**：双击 `build_oneclick.bat`
  - 检查工具 `cmake / ninja / openocd / arm-none-eabi-gcc`；检查根目录 `Drivers`、`third_party`
    （缺失提示从 `..\support_tools\env_support_for_stm32h743.zip` 解压/拷贝）；
  - 流程 `configure → clean → build`；所有出口 `pause` 停留查看。
- **全仓库**：仓库根目录双击 `build_all.bat`，顺序编译全部 14 个工程；失败暂停等你回车后继续，末尾输出汇总。

### 手动（普通 CMake，无预设）
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target clean
cmake --build build
```
产物：`build/lvgl_oled.elf`（同时生成 `.hex` / `.bin` 与 `.map`）。

### 约束
- **零警告**：`-Wall -Wextra`，构建应 0 warning。
- **编码**：源码为 UTF-8 字面量（`-finput-charset=UTF-8`），字库内部再转 GBK 索引；**切勿**改写 `-fexec-charset`。
- **HAL 模板排除**：`*_template.c` 已在 CMake 中排除，避免与自定义 `HAL_MspInit` / `HAL_InitTick` 冲突。

---

## 5. 调试与烧录

- **VSCode（cortex-debug）**：`.vscode/launch.json` 用裸工具名，`configFiles`/`svdFile` 指向根级 `openocd.cfg` 与 `STM32H743.svd`，`preLaunchTask:"build"`（F5 先编译再调试）。
- **命令行烧录**：
  ```bash
  openocd -f openocd.cfg -c "program build/lvgl_oled.elf verify reset exit"
  ```
- **运行日志 / 交互**：调试串口 PA9/PA10（115200 8N1）。

---

## 6. 验收

- ✅ 仿真确定 OLED 上显示的界面符合预期（中文标签正确渲染、无乱码）。

---

## 7. 常见问题

| 现象 | 根因 / 处理 |
|------|-------------|
| 中文乱码 / 方框 | 确认源码 UTF-8、`lv_font_gbk.c` / `lv_gbk_map.c` 映射正确、SD 字库路径 `1:/SYSTEM/FONT/` 存在且可读 |
| 编译报 `HAL_InitTick` 重复定义 | 已排除 `*_template.c`；确认仅 `stm32h7xx_hal_msp.c` 提供 Msp/时基 |
| 改 `.ld` 后 ninja 报 `no work to do` | 链接脚本未触发重配置 → 重跑 `cmake -S . -B build ...` 或 `clean` |
| OLED 不亮 | 核对 SPI 引脚/时钟、`drv_spi_oled.c` 初始化时序与 ST7789 复位/背光线 |
| 显示偏移 / DMA 异常 | 参考 009 工程的 SPI6/ST7789/字库对齐结论，保持刷新缓冲与 DCache 一致性 |
