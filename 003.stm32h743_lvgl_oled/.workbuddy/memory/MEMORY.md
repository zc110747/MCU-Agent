# 项目长期记忆：STM32H743 OLED 中文显示 (lvgl_oled)

## 目标
基于 STM32H743ZIT6 + SPI6(ST7789 240x240) 显示中文，字库在 SD 卡 `/SYSTEM/FONT/`(GBK12/16/24/32.FON + UNIGBK.BIN)，裸机(无 RTOS)。

## 硬件
- LED=PG7; 调试串口 USART1(PA9/PA10, 115200); SPI6: SCK=PG13, MOSI=PG14, NSS=PG8, DC=PG15, BL=PG12; SDMMC1: D0-D3=PC8-11, CK=PC12, CMD=PD2。
- **时钟源：25MHz 无源晶振**接 OSC_IN(PH0)/OSC_OUT(PH1) → 必须 `RCC_HSE_ON`，**绝不能用 `RCC_HSE_BYPASS`**（那是有源晶振/TCXO 模式）。PLL1: M=5→5MHz, N=192→960MHz VCO, P=2→SYSCLK 480MHz；HCLK 240MHz, APBx 120MHz。
- PH0/PH1 只使能 GPIOH 时钟、不做 `HAL_GPIO_Init`，保持复位后模拟态。

## 工具链（均在系统 PATH）
- arm-none-eabi-gcc 15.3.1、cmake 4.2.1、ninja、openocd 0.12（ST-Link）。

## 必须遵守的约定（来自用户）
- **生成的调试/构建文件不得含机器绝对路径** → 用 `${workspaceFolder}`、`CMAKE_SOURCE_DIR` 变量；编译加 `-fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.`。
- 构建用 cmake + ninja；仿真用 ST-Link + openocd + cortex-debug。

## 调试环境（已实测可用）
- **ST-Link 自带虚拟串口 = COM6**，且 VCP 就接在 PA9/PA10(USART1)，115200 8N1 可直接看 printf。
- openocd 可执行：`D:\software\ST\OpenOCD\bin\openocd.exe`（PATH 里直接 `openocd` 亦可）。
- SVD 已复制进工程 `debug/STM32H743.svd`（源自 STM32CubeIDE 插件目录），launch.json 用相对路径引用。
- `.vscode/launch.json` 两个配置：`Debug (OpenOCD + ST-Link)`（build→flash→停 main）与 `Attach (no reflash)`（附加运行中目标）。
- `scripts/runtime_check.gdb`：attach 后一键自检 tick / LED 心跳 / LCD 刷新 / SPI6 / GPIO。

## 易踩的坑
- 中文字面量：源码 UTF-8 + `-fexec-charset=GBK`（已验证 arm gcc 15.3 支持 GBK 转码）。
- 含 GBK 中文的源文件（如 drv_oled_fonts.c）需在提交前转成 UTF-8，否则 `-finput-charset=UTF-8` 报 "Illegal byte sequence"。
- `MCU_FLAGS` 必须写成 CMake list，不能直接一个带空格的字符串。
- FatFs `f_read` 第4参是 `UINT*`，不是 `uint32_t*`（32位下二者类型不同）。
- SDMMC IDMA 访问不了 DTCM → 主 RAM 放 AXI-SRAM，且 D-Cache 保持关闭。
- 抓串口 banner 必须**先开串口再复位**（banner 只在启动时打印一次）。
- PowerShell 里 `Start-Process openocd -ArgumentList ...`，`-c` 后的多词命令要再套一层引号：`'"init; reset run; exit"'`，否则被按空格拆参。
- 本机 `arm-none-eabi-gdb` 未链接 iconv，`set host-charset` 不可用，CP1252 警告可忽略。
- 从 Bash 调 `powershell.exe` 被安全策略拦截；PowerShell 工具 stdout 不回显，需写文件再读。

## 时钟加固（无源晶振专项，已实测）
- `SystemClock_Config()` 双路径：HSE 晶振起振失败 → 自动回退 HSI(64MHz, M=16/N=240/P=2 仍得 480MHz)，不再死在 Error_Handler；`g_clock_source` 记录实际时钟源，banner 打印。
- HSE 就绪后 `HAL_RCC_EnableCSS()`；`NMI_Handler` → `HAL_RCC_NMI_IRQHandler()` → `HAL_RCC_CSSCallback()` 只置 `g_hse_css_fault` 标志（**NMI 里不能调用任何轮询 HAL_GetTick 的 HAL 函数，SysTick 无法抢占 NMI 会死锁**），主循环负责打印告警。
- 校验手段：`RCC_CR`(0x58024400) bit18 HSEBYP=0 表示无源晶振、bit19 CSSHSEON=1 表示 CSS 已武装；`RCC_PLLCKSELR`(0x58024428) PLLSRC=2 为 HSE、DIVM1=5。
- **晶振真实频率交叉验证法**：openocd `sleep 20000` 作主机参考，前后各读一次 `uwTick`，比对得实测误差 0.010% → 反推 HSE=25.0025MHz。频率配错会成倍偏差，此法极灵敏。
- 链接告警 `LOAD segment with RWX permissions` 用 `-Wl,--no-warn-rwx-segments` 消除。

## 当前状态（2026-08-05 实机验证通过）
- 固件已烧录并 Verified OK，480MHz 运行正常。
- 串口输出全绿：`[LCD] ST7789 240x240 on SPI6 ready`、`[SD] mounted, fonts loaded`、UNIGBK+GBK12/16/24/32 全 OK。
- 中文渲染已端到端证实：从 SD 卡取出的 24x24 点阵 dump 后渲染出清晰的"中""文"字形。
- VSCode 仿真链路（reset/load/断点/单步/step into/变量监视/外设寄存器/attach）全部实测可用。
