# 一键编译 bat 规范（每个工程根目录 `build_oneclick.bat`）

> 通用 `.bat` 坑的**唯一主副本**在外层 skill：
> `soc-debug-verification/references/bat-pitfalls.md`（二十七大坑 + 一族一族的归纳）。
> 本节只讲 **ST 工程脚本特有**的部分。**手工写 `.bat` 前先读那份。**

## 一、需求与流程

① 全英文输出；② 先检查工具（ST 工程检查 `cmake / ninja / openocd / arm-none-eabi-gcc`，
ESP 走各自工具），ST 工程还要检查根目录 `Drivers` / `third_party`，缺失则提示从支持包 zip 解压；
③ 流程 `configure → clean → build`（ESP 按自身流程：`arduino-cli compile` / `idf.py build`）；
④ 失败立即 `goto END` 中止；⑤ **所有出口（成功/失败/中止）都 `goto END → pause → exit /b %ERR%`**，
双击即可停留查看错误。

> 各构建机制的 `configure` / `build` / `clean` 命令必须与该工程 `tasks.json` **逐字一致**
> （预设工程 `cmake --preset debug` + `build/debug`；普通 CMake
> `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` + `build`；Zephyr 走
> `python -m west build -b <board>/<soc> ...`）。

## 二、ST 工程脚本特有的三个坑

### 坑 1 —— 禁止用 `!MISSING!` 延迟展开打印缺失工具名

旧写法先 `set "MISSING=!MISSING! cmake"` 累积、再 `echo ...:!MISSING!`。一旦该分支被触发
且延迟展开未生效，`!MISSING!` 会被**原样打印成字面量** `:!MISSING!`。
正确做法：`for %%T` 逐个 `where`，缺失时**直接 `echo` 出该工具名**，用 `set "TOOLMISS=0/1"`
标记，循环结束后在**顶层**用 `%TOOLMISS%` 判定（顶层 `%VAR%` 在执行时展开，无需延迟展开）：

```bat
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc) do (
    where %%T > nul 2>nul
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Please refer to the project support doc for installation instructions.
    set "ERR=1"
    goto END
)
```

### 坑 2 —— 用 Python 生成 `.bat` 时 `%` 格式化会把 `%%` 吞成 `%`

Python `"for %%T in (%s)" % tools` 中 `%%` 是 `%` 转义，结果变成单 `%T` → `for` 循环语法错误。
避开 `%` 格式化，用字符串拼接：
`"for %%T in (" + " ".join(tools) + ") do ("`（其余 `%%T` 在普通字符串里保留 `%%`，不受影响）。

### 坑 3 —— 生成器必须清洗 `echo`/`REM` 行里的 `& ( ) | < >`

`(` 出现在 `echo` 参数**行首**会被当成命令组起始；`&` 是命令分隔符（`echo A & B` 把 `B`
当外部命令跑）；`( ) | < >` 同理会在文本里被 cmd 解析。
→ 生成脚本时对**所有 `echo`/`REM` 行**清洗：`&`→`and`，`| < > ( )`→删除。
**只清洗这两类行**，`if` / `for` / `cmake` 等命令行的括号必须保留。

```python
def sanitize(line):
    s = line.lstrip()
    if s.startswith("echo") or s.startswith("REM"):
        return (line.replace("&", "and").replace("|", " ").replace("<", " ")
                    .replace(">", "").replace("(", "").replace(")", ""))
    return line
```

反斜杠 `\`（如 `..\support_tools\...` 路径）在 cmd 的 `echo` 中是字面量，**不受影响**。

> 配套 `lint_bat.py`（本 skill 目录下）可做 preflight：把 `echo` 行的特殊字符列为 ERROR，
> `REM` / `cd %~dp0` / `2>&1` 降为 INFO。

### 已撤销的「坑」—— `cd /d "%~dp0"` 尾随反斜杠

曾把「`cd /d "%~dp0"` 的尾随 `\` + `for` 块内 `2>&1` 导致 `此时不应有 .`」列为致命坑 4，
**实测（Win11 cmd）不触发**：保留原始写法的脚本仍能一路跑过
`Configure → Clean → Build`。故**不要再为此改 `cd` / `2>&1`**。
若 `.bat` 报 `此时不应有 . / into。`，**第一嫌疑永远是 `echo` 行里的括号/`&` 等特殊字符**（坑 3）。

## 三、全仓编排层

- `<repo>/build_all.bat`：`for %%P in (工程列表)` → `call "%~dp0<proj>\build_oneclick.bat" < nul`。
  - 子脚本末尾 `pause` 经 `< nul` 喂 EOF 立即返回，由父脚本统一掌控暂停时机。
  - **出错才暂停**：`exit /b` 非 0 → 打印 `[ERROR]` + `pause` 等回车后继续；成功直接继续。
  - 缺 `build_oneclick.bat` 的工程标记 `[SKIP]`，不中断整体流程。
  - 末尾汇总 `Passed / Failed / Skipped` 并 `pause`（避免双击后窗口关闭丢结果）。
  - cmd 老坑：`for` 循环内**不能用 `goto`**（会中断整循环）→ 改 `call :子例程`；
    计数器用 `setlocal enabledelayedexpansion` + `!VAR!`。
- `<repo>/support_all.bat`：把 `support_tools/` 下的支持包（`Drivers` / `third_party` / RTOS 源码等）
  按需同步进各工程——目录不存在就解压 zip、逐条目对比、目标缺失才拷贝、已存在则 `[SKIP]` 跳过。
  - 解压优先 `tar -xf "<zip>" -C "<dir>"`（Windows 自带 bsdtar），失败回退
    `powershell -NoProfile -Command "Expand-Archive -Path '<zip>' -DestinationPath '<dir>' -Force"`。
  - 拷贝用 `robocopy "<pkg>\<item>" "<proj>\<item>" /E`（目录/文件通用）。
  - **不合并、不覆盖已有条目**；重复运行只补缺失项（幂等安全）。

> 三者组成「依赖补齐 → 单工程编译 → 全工程编译」的完整工具链。
