# Skills 能力表（MCU-Agent 工程 / 全局双份同步）

> 本表索引 `.workbuddy/skills/` 下 **18 个 MCU/嵌入式复用型 skill**，按域分组。
> 两份副本保持完全一致：工程 `.workbuddy/skills/` 与全局 `~/.workbuddy/skills/`。
> 另有 6 个 **SOC / Linux / 邮件域 skill** 仅存在于全局目录（非本 MCU 仓库产出）：
> `soc-camera-rtsp-agent`、`soc-video-streaming`、`soc-windows-gstreamer-build`、
> `soc-edge-ai-zerodep`、`soc-debug-verification`、`mail-weekly-digest`。

## 一、总方法论

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-vibe-coding-workflow` | AI Agent(Vibe Coding) 总方法论：环境要求 / 提示词模板 / 分阶段验收 / 人工干预时机 / **跨平台延伸(ESP32-S3)** | "用 AI 写 MCU""给 Agent 下嵌入式任务""规划 AI 开发流程" |

## 二、环境与工具链

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-ai-dev-environment` | 工具链安装与 PATH、west 调用、Git Bash / GBK 编码 / openocd swd、双 openocd 端口冲突、PowerShell stderr 误报、LIBUSB_ERROR_ACCESS、`.bat` 必须 CRLF+纯 ASCII、沙箱 build/ ACL 权限、**binutils 2.44 与 STemWin 预编译库不兼容的工具链版本锁定** | "配置 AI 能构建的嵌入环境""PowerShell 误报红字""ST-Link 被占用""厂商 .a 链接失败" |
| `stm32-keil-port` | CMake/GCC 工程 → **Keil MDK-ARM**(uvprojx + 分散加载 .sct) 并用 `UV4` 命令行构建 | "移植到 Keil""UV4 命令行编译""写分散加载文件" |

## 三、工程结构与内存架构

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-project-scaffold` | `app/bsp/Drivers/third_party` 分层、CMake+Ninja、OpenOCD、链接脚本、CMakePresets、Cortex-Debug、多工程 `.vscode` 批量统一、**`sys_startup/` 本地设备层替代 `Drivers/CMSIS/Device`**、CMSIS 启动文件禁止 GLOB_RECURSE、HAL Cube 标准命名、**LVGL 多页面 UI 拆分约定**、`build_oneclick.bat` 致命坑 | "搭建新 STM32 工程""批量统一 .vscode""sys_startup 迁移""写一键编译 .bat""拆分 LVGL 页面" |
| `soc-cache-mpu` | H7 缓存与 MPU 架构正确用法：**D-Cache 必须保持开启**，DMA 缓冲用 MPU 标 non-cacheable 而非全局关 Cache；SDIO 非 D-Cache 一致性处理 | "要不要关 D-Cache""DMA 数据不对""MPU 怎么配" |
| `zephyr-stm32-porting` | Zephyr+STM32+LVGL：west/设备树 overlay/25MHz HSE/SDMMC 卷名/GBK 字库/ST7789 DISPON/Zephyr shell/工具链自动探测/west build -b 全新构建 | "在 STM32 跑 Zephyr""设备树配置 ST7789/SD""west 删 build 后重编失败" |

## 四、外设驱动与显示

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-peripheral-drivers` | H7/F4 引脚速查、OV5640(DCMI `0x48020000`/极性/彩条)/ST7789/SD+GBK 字库/QSPI/USB OTG_FS **VDD33USB 供电**/LAN8720A RMII/I2C 锁死、F429 LCD 8080(NT35510)/GT911 触摸中断风暴三层防护、SDIO 4 字节对齐、emWin STemWin、**USB Host + 真正 exFAT**、**UART 物理层坑(7bit+校验位污染/RTS-CTS 悬空/环形缓冲二义性/Windows 不转发 RTS)**、USART1 非阻塞 PRINT_LOG | "查引脚""移植摄像头/OLED/SD""USB 枚举不上""GT911 风暴""exFAT""7E1 校验位错""流控不通" |
| `stm32-lvgl-font-engine` | SD 卡上 **CTF 索引 + 原 TTF** 实现中文/多语言显示的高性能字体引擎（硬约束、首绘 CPU 冷开销、Latin 预取误假设） | "LVGL 显示中文""字库太大""TTF 不裁剪" |
| `stm32-logging-print-log` | 把裸 `printf` 换成 **PRINT_LOG** 全局可控日志：编译期零成本关闭、栈缓冲格式化、RTOS 双 TX 路径+懒互斥量、裸机 TX 中断环形缓冲、CMake 开关、SWD 验证开关生效 | "统一日志开关""关日志省 FLASH""ISR 里不能打日志" |

## 五、调试、取证与验收

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-verification-acceptance` | 双构零警告、OpenOCD 烧录(**必须 .elf 非 .bin**)、串口/网络真机验证、verify 脚本 pass/fail、gdb 函数级验证、`mdw` 4 字节对齐直读、"没报错≠有数据"、COM code-31/LIBUSB_ERROR_ACCESS、ST-Link VCP 拒绝访问重试、**主机侧 `read(in_waiting or N)` 制造 30ms 假延迟**、PRINT_LOG 开关 SWD 验证、LVGL 首帧性能与离屏预热 | "验收固件""写自测脚本""串口抓不到""固件莫名很慢""首帧卡顿" |
| `stm32-swd-forensics` | 串口/板子不可用时的 **SWD+OpenOCD 内存取证**与中断链路诊断：CH340 code-31、`arm-none-eabi-nm` 取符号、`mdw` 对齐读 + Python 切字节 | "串口坏了怎么查""读内存取证""中断没进来" |
| `stm32-cmsis-dap-probe` | **自研 CMSIS-DAP/SWD/JTAG 探针**固件（STM32H7+TinyUSB 或 ESP32-S3+ESP-IDF）：DWT 延时替代 Keil 内联汇编、IRAM_ATTR 热点、**v1 over HID 1kHz 轮询限制(JTAG 下载慢不可救 → 用 SWD)**、JTAG `Invalid ACK(4)` 的 bool 提升坑、**USB/Wi-Fi 仲裁必须 `tud_mounted()` 而非 `tud_connected()`**、双主机冲突/空 Flash lockup 假故障 | "自研 DAP 探针""OpenOCD 找不到 CMSIS-DAP""Invalid ACK""下载慢""无线探针" |
| `esp32-cortex-debug` | VSCode + openocd-esp32 + xtensa-gdb 的 **ESP32 断点调试链路**：launch.json 写法、JTAG 驱动安装 | "ESP32 打断点""Cortex-Debug 配 ESP32-S3" |

## 六、ESP32 平台

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `esp32-arduino-cli-build` | **arduino-cli** 构建范式：单编译单元(根 .ino 同名 + `#include "xxx.cpp"`)、`#ifndef` 守卫、FQBN、core 3.3.x 新 LEDC、早期读 MAC 走 eFuse、路径三级回退、端口扫描、**gitignore 目录黑名单**、`-bat` 铁律、VSCode 配置、常见编译错误速修 | "arduino-cli 报 main file missing""子目录 cpp 没被编译""ledcSetup 不存在""双击 bat 找不到 COM""AP 名变 wifi-0000" |
| `esp32-board-hardware` | ESP32-S3 板级硬件：WS2812B GPIO48(RMT)、BOOT 键、COM 口识别、烧录 + 串口验证 | "板载 RGB 怎么驱动""识别 COM" |
| `esp32-web-ui-state-push` | 带内嵌网页时把界面从"命令回显"改成**设备真实状态驱动**：WebSocket 周期性推 state 快照、事件队列与快照分离、前端零轮询 | "ESP32 网页状态不实时""WebSocket 推送设计" |
| `esp-idf-windows-build` | Windows(沙箱) 上 **ESP-IDF / idf.py** 构建烧录踩坑：PowerShell 吞原生进程输出须用 Git Bash、MSYSTEM 兼容、sdkconfig/CMake 时序、串口与 USB | "idf.py 构建失败""PowerShell 无输出" |
| `esp-idf-whole-archive-link` | ESP-IDF 链接陷阱：`.a` 中被间接引用的目标文件被丢弃 → `undefined reference`；强定义 `__weak` 覆盖被弱桩静默取代 | "tud_descriptor 未定义""weak 覆盖不生效" |

## 七、使用建议

- **入口**：先读 `stm32-vibe-coding-workflow` 建立端到端认知，再按场景下钻。
- **主干链路：环境 → 结构 → 驱动 → 验收**，互相交叉引用。
- 平台分叉：Zephyr 工程看 `zephyr-stm32-porting`；ESP32 看第六节；ESP-IDF 走 `esp-idf-*`。
- 全新 skill `stm32-cmsis-dap-probe` 是调试器固件方向的总入口（被 `203` 项目 README 引用）。
- 各 skill 的 frontmatter `description` 即系统 `available_skills` 能力表，已与正文同步、无硬路径。

> **维护约定**：修改任一 skill 后同步更新本表对应行，并保持工程 / 全局两份副本一致。
> 双向同步操作：`diff -rq <proj>/.workbuddy/skills ~/.workbuddy/skills` 检查是否需要 merge。
