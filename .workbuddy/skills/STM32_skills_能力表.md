# Skills 能力表（全局权威 `~/.workbuddy/skills/` ↔ 工程镜像）

> 本表索引全局 `~/.workbuddy/skills/` 下 **32 个 skill**，按域分组。
> **权威副本在全局**；工程侧 `<proj>/.workbuddy/skills/` 是 **MCU 域 18 个 skill 的完整镜像备份**。
>
> 同步与漂移检查统一走脚本（**不要手敲 `cp` / `diff`**）：
>
> ```bash
> support_tools/sync_skills.sh            # 全局 -> 工程镜像（覆盖式刷新）
> support_tools/sync_skills.sh --check    # 只报告漂移，不改动
> support_tools/sync_skills.sh --prune    # 先清理镜像侧残留再同步（全局删过文件时用）
> support_tools/sync_skills.sh --restore  # 工程镜像 -> 全局（换机 / 灾后恢复）
> support_tools/sync_skills.sh --list     # 打印纳管清单
> ```
>
> 纳管范围 = **MCU 域 18 个**（`stm32-*` / `zephyr-stm32-porting` / `soc-cache-mpu` / `esp32-*` /
> `esp-idf-*`）+ 本表。其余域（`soc-*`、`lvgl-*`、`mail-*`、`robotics-*` 等）不镜像，
> 避免把无关域塞进 MCU 工程仓库。

## 一、总方法论

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-vibe-coding-workflow` | AI Agent(Vibe Coding) 总方法论：环境要求 / 提示词模板 / 分阶段验收 / 人工干预时机 / **跨平台延伸(ESP32-S3)** | "用 AI 写 MCU""给 Agent 下嵌入式任务""规划 AI 开发流程" |

## 二、环境与工具链

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-ai-dev-environment` | 工具链安装与 PATH、west 调用、Git Bash / GBK 编码 / openocd swd、双 openocd 端口冲突、PowerShell stderr 误报、LIBUSB_ERROR_ACCESS、`.bat` 必须 CRLF+纯 ASCII、沙箱 build/ ACL 权限、**binutils 2.44 与 STemWin 预编译库不兼容的工具链版本锁定** | "配置 AI 能构建的嵌入环境""PowerShell 误报红字""ST-Link 被占用""厂商 .a 链接失败" |
| `stm32-keil-port` | CMake/GCC 工程 → **Keil MDK-ARM**(uvprojx + 分散加载 .sct) 并用 `UV4` 命令行构建；**源集铁律**（禁加 `syscalls.c`、必留 `mdk_target.c`、HAL 按需引入） | "移植到 Keil""UV4 命令行编译""写分散加载文件""_sbrk 未定义" |

## 三、工程结构与内存架构

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-project-scaffold` | `app/bsp/Drivers/third_party` 分层、CMake+Ninja、OpenOCD、链接脚本、CMakePresets、Cortex-Debug、多工程 `.vscode` 批量统一、**`sys_startup/` 本地设备层替代 `Drivers/CMSIS/Device`**、CMSIS 启动文件禁止 GLOB_RECURSE、**HAL 按需引入（禁整包）**、**LVGL 多页面 UI 拆分约定**、`build_oneclick.bat` 致命坑 | "搭建新 STM32 工程""批量统一 .vscode""sys_startup 迁移""写一键编译 .bat""拆分 LVGL 页面" |
| `soc-cache-mpu` | H7 缓存与 MPU 架构正确用法：**D-Cache 必须保持开启**，DMA 缓冲用 MPU 标 non-cacheable 而非全局关 Cache；SDIO 非 D-Cache 一致性处理 | "要不要关 D-Cache""DMA 数据不对""MPU 怎么配" |

## 四、外设驱动与显示

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-peripheral-drivers` | H7/F4 引脚速查、OV5640(DCMI)/ST7789/SD+GBK 字库/QSPI/USB OTG_FS **VDD33USB 供电**/LAN8720A RMII/I2C 锁死、F429 LCD 8080(NT35510)/GT911 触摸中断风暴三层防护、SDIO 4 字节对齐、emWin STemWin、**USB Host + 真正 exFAT**、**UART 物理层坑(7bit+校验位污染/RTS-CTS 悬空/环形缓冲二义性/Windows 不转发 RTS)**、USART1 非阻塞 PRINT_LOG | "查引脚""移植摄像头/OLED/SD""USB 枚举不上""GT911 风暴""exFAT""7E1 校验位错""流控不通" |
| `stm32-lvgl-font-engine` | SD 卡上 **CTF 索引 + 原 TTF** 实现中文/多语言显示的高性能字体引擎（硬约束、首绘 CPU 冷开销、Latin 预取误假设） | "LVGL 显示中文""字库太大""TTF 不裁剪" |
| `stm32-logging-print-log` | 把裸 `printf` 换成 **PRINT_LOG** 全局可控日志：编译期零成本关闭、栈缓冲格式化、RTOS 双 TX 路径+懒互斥量、裸机 TX 中断环形缓冲、CMake 开关、SWD 验证开关生效；**单文件 `bsp_log.c` 双工具链范式 + `mdk_target.c` 必须保留** | "统一日志开关""关日志省 FLASH""ISR 里不能打日志" |

## 五、调试、取证与验收

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `stm32-verification-acceptance` | 双构零警告、OpenOCD 烧录(**必须 .elf 非 .bin**)、串口/网络真机验证、verify 脚本 pass/fail、gdb 函数级验证、`mdw` 4 字节对齐直读、"没报错≠有数据"、COM code-31/LIBUSB_ERROR_ACCESS、**主机侧 `read(in_waiting or N)` 制造 30ms 假延迟**、PRINT_LOG 开关 SWD 验证、LVGL 首帧性能与离屏预热 | "验收固件""写自测脚本""串口抓不到""固件莫名很慢""首帧卡顿" |
| `stm32-swd-forensics` | 串口/板子不可用时的 **SWD+OpenOCD 内存取证**与中断链路诊断：CH340 code-31、`arm-none-eabi-nm` 取符号、`mdw` 对齐读 + Python 切字节、**`verify_image` 锁定"读的是谁的内存"**、HardFault 解码、`EXTI_SWIER` 软件中断注入 | "串口坏了怎么查""读内存取证""中断没进来""跑飞了定位" |
| `stm32-cmsis-dap-probe` | **自研 CMSIS-DAP/SWD/JTAG 探针**固件（STM32H7+TinyUSB 或 ESP32-S3+ESP-IDF）：DWT 延时替代 Keil 内联汇编、IRAM_ATTR 热点、**v1 over HID 1kHz 轮询限制(JTAG 下载慢不可救 → 用 SWD)**、JTAG `Invalid ACK(4)` 的 bool 提升坑、**USB/Wi-Fi 仲裁必须 `tud_mounted()` 而非 `tud_connected()`**、双主机冲突/空 Flash lockup 假故障 | "自研 DAP 探针""OpenOCD 找不到 CMSIS-DAP""Invalid ACK""下载慢""无线探针" |
| `esp32-cortex-debug` | VSCode + openocd-esp32 + xtensa-gdb 的 **ESP32 断点调试链路**：launch.json 五条铁律、JTAG 驱动安装、15 类报错速查 | "ESP32 打断点""Arg list too long""Cortex-Debug 配 ESP32-S3" |

## 六、ESP32 / ESP-IDF 平台

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `esp32-arduino-cli-build` | **arduino-cli** 构建范式：单编译单元(根 .ino 同名 + `#include "xxx.cpp"`)、`#ifndef` 守卫、FQBN、core 3.3.x 新 LEDC、早期读 MAC 走 eFuse、路径三级回退、端口扫描、**gitignore 目录黑名单**、`.bat` 铁律、VSCode 配置、常见编译错误速修 | "arduino-cli 报 main file missing""子目录 cpp 没被编译""ledcSetup 不存在""双击 bat 找不到 COM""AP 名变 wifi-0000" |
| `esp32-board-hardware` | ESP32-S3 板级硬件：WS2812B GPIO48(RMT/Adafruit NeoPixel)、BOOT 键、COM 口识别、烧录 + 串口验证 | "板载 RGB 怎么驱动""识别 COM" |
| `esp32-web-ui-state-push` | 带内嵌网页时把界面从"命令回显"改成**设备真实状态驱动**：WebSocket 周期性推 state 快照、事件队列与快照分离、前端零轮询 | "ESP32 网页状态不实时""WebSocket 推送设计" |
| `esp-idf-windows-build` | Windows(沙箱) 上 **ESP-IDF / idf.py** 构建烧录踩坑：PowerShell 吞原生进程输出须用 Git Bash、MSYSTEM 兼容、sdkconfig/CMake 时序、串口与 USB 验证 | "idf.py 构建失败""PowerShell 无输出" |
| `esp-idf-whole-archive-link` | ESP-IDF 链接陷阱：`.a` 中被间接引用的目标文件被丢弃 → `undefined reference`；强定义 `__weak` 覆盖被弱桩静默取代 | "tud_descriptor 未定义""weak 覆盖不生效" |

## 七、RTOS 移植

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `zephyr-stm32-porting` | Zephyr+STM32+LVGL：west 工作区/设备树 overlay/25MHz HSE 覆盖/SDMMC 卷名/GBK 字库/ST7789 DISPON/Zephyr shell/工具链自动探测/删 build 后全新构建 | "在 STM32 跑 Zephyr""设备树配置 ST7789/SD""west 删 build 后重编失败" |

## 八、非 MCU 域（LVGL 通用 / SOC / 邮件 / 机器人 / 取证）

| Skill | 一句话能力 | 何时用 |
|---|---|---|
| `lvgl-init-and-porting-traps` | LVGL(v8/v9) 初始化顺序与移植崩溃陷阱：`lv_init()` 必须早于注册任何 display/indev、PC 模拟器(SDL2)后端自写姿势、`LV_TICK_CUSTOM` 与 `lv_tick_inc()` 双计 | "LVGL 一启动就崩""SIGSEGV 在 lv_mem_alloc""SDL2 模拟器怎么接" |
| `lvgl-render-silent-failures` | LVGL **静默失败**四类：字体压缩标志不匹配(整屏有框无字)、`lv_coord_t`(int16) 存样本致低价曲线塌陷、样本撞 `LV_CHART_POINT_NONE` 被跳过、**「事件到了/UI 重建了但模型没重新加载」+「只比集合忘了顺序」**；配离屏探针(dummy display+像素断言+BMP 样张) | "LVGL 屏幕没东西""曲线不见了""字体不显示""界面不刷新""重排无效" |
| `soc-debug-verification` | Windows 调试验证方法论：bat 二十七坑、一键启动脚本范式、故障定位决策树、ffplay/mediamtx 低延迟、**「Permission denied」三成因辨析(沙箱 vs exe 文件锁 vs 真实链接错)**、**长驻 GUI 程序"状态外置"验证配方** | "bat 无输出""一键启动""ffplay 打不开""链接报 Permission denied""验证常驻程序" |
| `soc-camera-rtsp-agent` | PC 端摄像头→H.264→RTSP 推流 Agent：可插拔后端(SIM/GStreamer)、断线退避、YAML 配置、零依赖单测 | "摄像头推流""RTSP Agent" |
| `soc-video-streaming` | video-server(Go)+MediaMTX+WebRTC 信令+HLS 反代；前端 Vue3+hls.js(WebRTC 优先/HLS 兜底) | "视频服务端""WebRTC 播放" |
| `soc-edge-ai-zerodep` | 端侧 AI 检测叠加：GStreamer tee+appsink、有界队列、YOLOv8 ONNX 手写解码，**仅依赖 ONNX Runtime(不引 OpenCV/Eigen)** | "端侧 AI 检测""不装 OpenCV 跑 YOLO" |
| `soc-windows-gstreamer-build` | Windows+MSVC+Ninja 构建 GStreamer C++ 工程：禁 MSBuild 生成器、`/utf-8` 消 C4819、MSVC devel 探测 | "GStreamer 构建失败" |
| `mail-weekly-digest` | 邮箱周报：QQ 邮箱 MCP 调用序列、UTC→+08:00 时区换算、长正文截断规避、深色极简 HTML 产出 | "邮件汇总""邮箱周报""上周邮件" |
| `robotics-multiphysics-vmodel-workflow` | 「虚拟模型↔真实机械臂」多物理场项目的工程流程：**唯一真值源**、冻结基线 + 语义核心哈希、三条实现互证、e2e 脚本前提纪律 | "虚拟模型与真机不一致""验收脚本假 FAIL" |
| `robotics-urdf-mjcf-vendoring` | 引入厂商官方机器人模型(URDF+MJCF+STL)并让自有配置与官方模型长期不漂移：以引擎真正执行的描述裁决、TCP 帧朝向/限位差异 | "导入 URDF/MJCF""模型数值不一致" |
| `arm-tcp-client-link` | 给既有上位机服务**新增 TCP Client 传输**（保留原串口/WS 链路），驱动另一后端；JSON Lines 跨服务直连验收 | "默认走 TCP 控制另一服务""新增 transport 不改原链路" |
| `duplicated-truth-forensics` | 诊断「同一个量在两处被独立实现、数值悄悄不一致」类 bug：轻微偏差/随机报错/不崩溃，多层实现（前后端各算一遍、UI 预检 vs 引擎内部） | "明明在范围内却被拒""改了参数反而更差" |
| `schematic-pdf-netlist` | 从 Altium 等 EDA 导出的原理图 PDF 提取可读网表与引脚映射，产出硬件规格文档 | "从原理图 PDF 提网表""写硬件规格书" |
| `webgl-first-frame-forensics` | 诊断「首帧幽灵/重影/半透明副本」这类**只在首次加载出现、一交互就消失**的 WebGL/three.js/React 渲染现象：逐帧记录器 + 残差定位 | "three.js 首帧重影""第一帧不对" |

## 九、使用建议

- **入口**：先读 `stm32-vibe-coding-workflow` 建立端到端认知，再按场景下钻。
- **主干链路：环境 → 结构 → 驱动 → 验收**，互相交叉引用。
- 平台分叉：Zephyr 工程看 `zephyr-stm32-porting`；ESP32 看第六节；ESP-IDF 走 `esp-idf-*`。
- `stm32-cmsis-dap-probe` 是调试器固件方向的总入口。
- 各 skill 的 frontmatter `description` 即系统 `available_skills` 能力表，已与正文同步、无硬路径。

> **维护约定**：
> 1. 修改任一 skill 后同步更新本表对应行。
> 2. 改完立刻跑 `support_tools/sync_skills.sh` 刷新工程镜像（脚本自带 IDENTICAL 自校验）。
> 3. 提交前用 `--check` 确认无漂移；换机器/灾后恢复用 `--restore`。
> 4. 全局 `~/.workbuddy/skills/` 是权威副本，**不要在工程镜像上直接改**（会被下次同步覆盖）。
