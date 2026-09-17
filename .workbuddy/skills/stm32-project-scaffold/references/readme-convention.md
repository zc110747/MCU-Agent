# 工程 README 说明文档规范

每个工程根目录 `README.md` 应**随代码同步更新**，让 AI / 人工一眼看懂「做什么 / 怎么搭 / 怎么调 / 怎么验」。建议固定章节：

1. **项目概述 / 处理内容**：一句话目标 + 功能范围。
2. **硬件与接口**：芯片、HSE 时钟、关键外设引脚（USB/显示/SPI/USART）、调试方式（SWD+ST-Link、SVD 文件）。
3. **工程结构**：目录树，标注 `app/bsp`/`Drivers`/`third_party` 分层与关键文件（`openocd.cfg`、链接脚本、`.svd` 置根）。
4. **开发流程**：分步实现建议（先 X 再 Y）与来源（指向 `prompter.md`）。
5. **构建与运行**：
   - 单工程 `build_oneclick.bat`、全仓库 `build_all.bat` 的使用位置与行为；
   - 手动命令（预设工程 `cmake --preset debug`；普通 CMake `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`），产物 elf 路径；
   - 依赖缺失处理（从 `..\support_tools\env_support_for_*.zip` 解压 `Drivers`/`third_party`）；
   - 零警告约束 + **实测资源占比（FLASH/RAM 数字显式列出）**。
6. **调试与烧录**：`.vscode/launch.json`（裸工具名 + 根级 cfg/svd + `preLaunchTask`）、命令行 `openocd -f openocd.cfg -c "program <elf> verify reset exit"`、单步/F5、gdb 脚本。
7. **验收与自测**：验收标准 + 自测脚本（Python `test_*.py` / `verify_*.py` 给出 pass/fail 计数）。
8. **常见问题**：表格列出典型坑与根因/处理（依赖缺失、`.ld` 改后 ninja `no work to do`、attach 超时、编码乱码等）。

**铁律**：路径一律相对；构建命令必须与 `tasks.json` / `build_oneclick.bat` 一致；资源占比、警告数等以**数字**显式给出；每次代码/配置变更同步更新 README。

---
