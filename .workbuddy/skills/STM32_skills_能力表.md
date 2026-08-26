# STM32 Skills 能力表（全局）

> 本表汇总 `.workbuddy/skills/` 下 6 个 STM32 相关 skill 的能力与选型指引。
> 所有 skill 已**去除本机硬路径 / 项目专属内容**（COM 端口号、`D:/` 路径、`support_tools/*.zip` 包名、`001–010+101` 项目计数、悬空引用等），保留流程与经验参考，可直接复用或分享给其他人。

## 能力总览

| Skill | 一句话能力 | 何时用 | 配套 |
|---|---|---|---|
| stm32-vibe-coding-workflow | STM32 AI Agent(Vibe Coding) 总方法论：环境要求 / 提示词模板 / 分阶段验收 / 人工干预时机 | "用 AI 写 STM32""给 Agent 下嵌入式任务""规划 AI 开发流程" | 其余 5 个 |
| stm32-ai-dev-environment | 工具链安装与 PATH、west 调用、Git Bash 吞花括号 / GBK 编码 / openocd swd 等 Windows 平台坑 | "配置 AI 能构建的嵌入环境""排查 Agent 构建失败""west 不在 PATH" | workflow |
| stm32-project-scaffold | app/bsp/Drivers/third_party 分层、CMake+Ninja、OpenOCD、链接脚本、CMakePresets、Cortex-Debug | "搭建新 STM32 工程""规范化工程结构""修链接脚本 / 烧录配置" | environment / acceptance |
| stm32-peripheral-drivers | H7/F4 引脚速查、OV5640/ST7789/SD+GBK 字库/QSPI/USB/网络/I2C、Bootloader Flash 引擎坑 | "查 STM32 引脚""移植摄像头/OLED/SD 卡""USB 枚举不上""I2C 死锁" | scaffold |
| stm32-verification-acceptance | 双构零警告、OpenOCD 烧录、串口/网络真机验证、verify 脚本 pass/fail、gdb 函数级验证、Bootloader 验收 | "验收 STM32 固件""写自测脚本""定义验收标准""真机验证" | scaffold / environment |
| zephyr-stm32-porting | Zephyr+STM32+LVGL 移植：west/设备树 overlay/25MHz HSE/SDMMC 卷名/GBK 字库/ST7789 DISPON/Zephyr shell | "在 STM32 跑 Zephyr""Zephyr 移植 LVGL""设备树配置 ST7789/SD" | environment / scaffold |

## 使用建议

- **入口**：先读 `stm32-vibe-coding-workflow` 建立端到端认知，再按场景下钻。
- **环境 → 结构 → 驱动 → 验收** 是主干链路，互相交叉引用（见各 skill 顶部"配套"）。
- **Zephyr 工程**额外看 `zephyr-stm32-porting`（west / 设备树 / 卷名差异）。
- 各 skill 的 frontmatter `description` 即系统 `available_skills` 能力表，已与正文同步、无硬路径。

## 已清理的硬路径 / 项目专属内容（审计清单）

- 绝对路径：`D:/Software/openocd/...` → 占位符 `<openocd 安装目录>/...`；`D:/user_project/coding_git/.../7.stm32h7_iap` 外部参考路径已删除（仅保留样本名 `7.stm32h7_iap`）。
- 本机串口：`COM19` / `COM3` / `COM4` → "端口号依本机分配（如 COMx）"。
- 专属包名：`support_tools/env_support_for_stm32h743.zip`、`env_support_for_zephyr.zip` → 通用"打包 `Drivers/` / `third_party/` 随工程分发"。
- 项目计数：`9 个` / `11 个项目（001–010 + 101）` → "多个 STM32 项目"。
- 悬空引用：`stm32-bare-metal-bringup`（不存在的 skill）→ 替换为实际存在的 6 个 skill 交叉引用。
- 经验参考（引脚表、双段字库结构、Bootloader Flash 引擎坑、gdb 函数级验证、双固件验收等）全部保留。

> 维护约定：修改任一 skill 后，同步更新本表对应行；两份副本（`~/.workbuddy/skills/` 与工程 `.workbuddy/skills/`）保持一致。
