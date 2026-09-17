# 多工程 .vscode 批量统一规范

仓库下有多个 STM32 工程（不同芯片 / 不同构建机制）需统一 `.vscode` 与仿真配置时，遵循以下铁律：

### 规则 1 — 工具走系统 PATH（裸程序名）
`cmake` / `ninja` / `openocd` / `arm-none-eabi-gcc` / `arm-none-eabi-gdb` 已加入系统 PATH。
所有引用只写**裸程序名**，不加安装目录、不带 `.exe`：
- `tasks.json`：`command: "cmake ..."`、`"openocd ..."`、`"arm-none-eabi-gdb ..."`
- `settings.json`：`"cortex-debug.openocdPath": "openocd"`、`"cortex-debug.gdbPath": "arm-none-eabi-gdb"`、`"C_Cpp.default.compilerPath": "arm-none-eabi-gcc"`
- `launch.json`：`"openocdPath": "openocd"`、`"gdbPath": "arm-none-eabi-gdb"`
- ❌ 严禁写死 `<openocd 安装目录>/bin/openocd.exe`、`${env:ARM_GNU_TOOLCHAIN_BIN}/...` 等本机/环境变量路径。

### 规则 2 — tasks.json 只保留 4 个任务
统一为 `configure` / `build` / `clean` / `flash`，其余（release / erase / reset / dap-test / serial /
OpenOCD server / 模型导出等）全部删除。
- `build` 设为默认构建组：`"group": {"kind":"build","isDefault":true}`，供 `preLaunchTask` 调用。
- `flash` 用 `"dependsOn": "build"` 先编译再烧录。
- **构建机制按工程现状保留**（下表），不要强行统一成一种：

| 工程类型 | configure | build | clean | flash 的 elf 路径 |
|---|---|---|---|---|
| 预设工程（有 CMakePresets.json） | `cmake --preset debug` | `cmake --build build/debug` | `cmake --build build/debug --target clean` | `build/debug/xxx.elf` |
| 普通 CMake（无预设） | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build` | `cmake --build build --target clean` | `build/xxx.elf` |
| Zephyr（west） | `python -m west build -b <board>/<soc> -d build -s .` | `python -m west build -d build` | `python -m west build -t clean -d build` | `build/zephyr/zephyr.elf` |

- ⚠️ 预设工程的 build 子目录（如 `build/debug`）必须与 CMakePresets 的 `binaryDir` 一致。
- ⚠️ 旧 `CMakeCache.txt` 路径不匹配（大小写 / 旧仓库路径）会导致 regenerate 失败；
  跑一次 `clean`（或删 `build/debug`）即可恢复——保留 `clean` 任务正是为此。

### 规则 3 — *.svd / *.cfg 放工程根目录（与 .vscode 同级），不再需要 openocd/ 子目录
- `openocd.cfg`（及可选的 `openocd_cmsisdap.cfg`）直接放工程根，与 `CMakeLists.txt` / `.vscode/` 同级。
- `*.svd`（如 `STM32H743.svd` / `STM32F429.svd`）也放工程根。
- `launch.json` 用 `${workspaceFolder}/openocd.cfg`、`${workspaceFolder}/STM32H743.svd` 引用。
- 原散落在 `openocd/`、`debug/`、`tools/` 下的同名文件应迁移到根并删除已清空的子目录。
- 缺失 `openocd.cfg` 的工程按芯片补建（H7：`stm32h7x.cfg`；F4：`stm32f4x.cfg`；均 `interface/stlink.cfg` + `transport select swd`）。
- 经自研 CMSIS-DAP 探针调目标板时，保留额外 `openocd_cmsisdap.cfg` + 一条 `attach` 配置（`cmsis_dap` interface）。

### 批量检索时排除 .gitignore
- 处理 / 搜索文件时，排除 `.gitignore` 中声明的路径（如 `third_party/`、`Drivers/STM32H7xx_HAL_Driver/`、`build/`），
  它们多是未检出的子模块 / 第三方库，缺失是正常的，不要当作错误。
- 校验脚本应只对「源码完整、可独立构建」的工程判定 PASS/FAIL，依赖缺失的标记为「环境依赖缺失」。
- 示例：`git -C <proj> check-ignore <path>` 可快速判定某缺失文件是否属被忽略项。

### 统一模板

`tasks.json`（4 任务，普通 CMake 示例；预设工程把命令换成 `cmake --preset debug` / `cmake --build build/debug`）：
```json
{
  "version": "2.0.0",
  "tasks": [
    {"label":"configure","type":"shell","command":"cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug","problemMatcher":[],"group":"build"},
    {"label":"build","type":"shell","command":"cmake --build build","group":{"kind":"build","isDefault":true},"problemMatcher":["$gcc"],"presentation":{"reveal":"always","panel":"shared","clear":true}},
    {"label":"clean","type":"shell","command":"cmake --build build --target clean","problemMatcher":[]},
    {"label":"flash","type":"shell","command":"openocd -f openocd.cfg -c \"program build/xxx.elf verify reset exit\"","dependsOn":"build","problemMatcher":[],"presentation":{"reveal":"always","panel":"shared"}}
  ]
}
```

`settings.json`：
```json
{
  "cmake.generator": "Ninja",
  "cmake.configureOnOpen": false,
  "cortex-debug.openocdPath": "openocd",
  "cortex-debug.gdbPath": "arm-none-eabi-gdb",
  "cortex-debug.armToolchainPath": "",
  "C_Cpp.default.compilerPath": "arm-none-eabi-gcc",
  "C_Cpp.default.intelliSenseMode": "gcc-arm",
  "cmake.useCMakePresets": "always"
}
```

`launch.json`（cortex-debug，根级 cfg/svd）：
```json
{
  "version": "0.2.0",
  "configurations": [
    {"name":"Debug (OpenOCD + ST-Link)","type":"cortex-debug","request":"launch","servertype":"openocd",
     "cwd":"${workspaceFolder}","executable":"${workspaceFolder}/build/debug/xxx.elf",
     "configFiles":["${workspaceFolder}/openocd.cfg"],"openocdPath":"openocd","gdbPath":"arm-none-eabi-gdb",
     "device":"STM32H743ZI","interface":"swd","runToEntryPoint":"main","preLaunchTask":"build",
     "showDevDebugOutput":"none","svdFile":"${workspaceFolder}/STM32H743.svd"},
    {"name":"Attach (no reflash)","type":"cortex-debug","request":"attach","servertype":"openocd",
     "cwd":"${workspaceFolder}","executable":"${workspaceFolder}/build/debug/xxx.elf",
     "configFiles":["${workspaceFolder}/openocd.cfg"],"openocdPath":"openocd","gdbPath":"arm-none-eabi-gdb",
     "device":"STM32H743ZI","interface":"swd","showDevDebugOutput":"none","svdFile":"${workspaceFolder}/STM32H743.svd"}
  ]
}
```

### 批量验证脚本思路（一次性，给出 pass/fail 计数）
- **仿真链路**：对每个工程 `openocd -f openocd.cfg -c "init; exit"`，预期「脚本/路径解析 OK」；
  仅缺物理探针的 adapter 报错属正常（需真机才能 flash）。
- **编译链路**：跑 `configure` + `build`，记录 PASS/FAIL + 警告数；`third_party`/`Drivers` 缺失
  （被 gitignore）导致的 FAIL 标记为「环境依赖缺失」，与「配置改动导致」区分开。

---
