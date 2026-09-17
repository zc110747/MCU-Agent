# sys_startup 本地设备层约定（强约束，替代 `Drivers/CMSIS/Device`）

**背景**：ST 官方 `Drivers/CMSIS/Device/ST/STM32H7xx/` 是一棵极庞大的树（~60 个 startup `.s`
变体 × arm/gcc/iar × 每个 H7 型号、所有 H7 头文件、DSP/NN 源）。每个工程实际只用到其中
**一份** device 头 + 一份 `system_stm32h7xx.c` + 一份对应型号的 startup + 工程自己的链接脚本。
把整棵树拖进每个工程既臃肿又让 `CMakeLists.txt` / `c_cpp_properties.json` 的引用路径冗长易错。
**结论**：每个 STM32H7 CMake 工程必须自带本地 `sys_startup/`，只放真正需要的设备层文件，
**不得**从 `Drivers/CMSIS/Device/` 引用任何东西（DSP/NN 在 `Drivers/CMSIS/DSP`、`/NN`，与 Device 平级，保留）。

### 1. `sys_startup/` 固定布局（以 H743 为例）

```
<project>/sys_startup/
├── stm32h743xx.h          # device 头（从官方 Device/.../Include 取，全工程唯一）
├── stm32h7xx.h            # top-level CMSIS 设备聚合头
├── system_stm32h7xx.h     # system 头
├── system_stm32h7xx.c     # 时钟初始化（从官方 Device/.../Source/Templates 取）
├── <本工程链接脚本>.ld     # 例：STM32H743ZITx_FLASH.ld / stm32h743zi_flash.ld
├── gcc/startup_stm32h743xx.s   # gcc 版向量表（add_executable 实际编译）
├── arm/startup_stm32h743xx.s   # armclang 版（备用，不编译）
└── iar/startup_stm32h743xx.s   # IAR 版（备用，不编译）
```

- **device 头 / `system_*` / 三份 startup 是型号级通用件**：同型号所有工程逐字节相同，
  可由任一已落地工程直接复制（迁移前先 `diff -q` 验证全一致，零风险）。
- **链接脚本是工程级专属件**：每个板的 Flash/RAM/SDRAM 布局不同，**绝不跨工程复制**，
  必须保留各工程自己的 `.ld` 内容（仅改存放位置到 `sys_startup/`）。
- 早期工程的旧位置（`Core/Src/system_stm32h7xx.c`、`Core/Startup/startup_*.s`、`ldscript/`、
  `Drivers/CMSIS/Device/...`）一律作废，集中到 `sys_startup/` 后删除冗余副本。

### 2. `CMakeLists.txt` 引用方式（强约束写法）

```cmake
set(CMSIS_DEVICE sys_startup)   # 语义化变量，指向本地设备层目录
# ① include 路径：只给 device 头，不再指向 Drivers/CMSIS/Device/ST/STM32H7xx/Include
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMSIS_DEVICE}                                  # stm32h743xx.h / stm32h7xx.h / system_stm32h7xx.h
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Core/Include   # CMSIS-Core（保留）
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
)
# ② 源文件：startup(gcc) + system 都从 sys_startup 取
set(STARTUP_SOURCE   ${CMSIS_DEVICE}/gcc/startup_stm32h743xx.s)
set(SYSTEM_SOURCE    ${CMSIS_DEVICE}/system_stm32h7xx.c)
# ③ 链接脚本：用本工程那份（内容已拷进 sys_startup）
set(LINKER_SCRIPT    ${CMSIS_DEVICE}/STM32H743ZITx_FLASH.ld)
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections")
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
# ④ add_executable 必须显式列出这两份源（即便 app/ 用 GLOB，也别靠 GLOB 漏掉）
target_sources(${PROJECT_NAME} PRIVATE ${STARTUP_SOURCE} ${SYSTEM_SOURCE})
```

**验收红线**：迁移/新建后，整份 `CMakeLists.txt` 中 `Drivers/CMSIS/Device` 出现次数必须为
**0**；`sys_startup` 至少出现 3 次（include / startup / linker）。

### 3. `.vscode/c_cpp_properties.json` 同步

若存在 `${workspaceFolder}/Drivers/CMSIS/Device/ST/STM32H7xx/Include` 这一条 includePath，
**必须改**为 `${workspaceFolder}/sys_startup`（与 CMake 的 include 指向一致，否则 IntelliSense
找不到 `stm32h743xx.h`）。未引用官方 Device 树的工程无需改动。

### 4. 新工程落地步骤（保持全仓一致）

1. 工程根 `mkdir sys_startup`，从任一已落地工程的 `sys_startup/` 复制
   `stm32h743xx.h` / `stm32h7xx.h` / `system_stm32h7xx.h` / `system_stm32h7xx.c` /
   `gcc|arm|iar/startup_stm32h743xx.s`（型号不同则换对应头与 startup 名）。
2. 把本工程的链接脚本放进 `sys_startup/`，**保留原文件名与内容**。
3. 按第 2 节改写 `CMakeLists.txt`；按第 3 节修正 `c_cpp_properties.json`。
4. 删除旧位置的设备层冗余副本（`Core/Src/system_*.c`、各处 `startup_*.s`、`ldscript/`）。
5. 验收：`cmake --preset debug`（或等价 `cmake -S . -B build ...`）必须 `exit 0`、0 error；
   且 `grep -c "Drivers/CMSIS/Device" CMakeLists.txt` == 0。

> `Drivers/CMSIS/Device/` 整棵树的删除建议人工执行（体积大、需逐工程确认）；
> 删除后 `Drivers/CMSIS/` 仅保留 `Core` / `Include` / `DSP` / `NN`。
