---
name: stm32-verification-acceptance
description: STM32 嵌入式项目的端到端验收方法论：Debug/Release 双构零警告、OpenOCD 烧录、串口/网络真机验证、Python/C# verify 脚本 pass/fail 计数模式、增量交付清单。适用于"验收 STM32 固件""写嵌入式自测脚本""定义项目验收标准""真机烧录后如何确认功能正常""整理交付清单""串口抓取失败/拒绝访问""LVGL 首帧卡顿定位"。触发词：验收流程、零警告构建、OpenOCD 烧录验证、串口自测、verify 脚本、pass/fail 计数、交付清单、真机验证、嵌入式测试、snmp_verify、serial_test、openocd 烧录必须用 elf 非 bin、mdw 4字节对齐读取、没报错不等于有数据、COM code-31、LIBUSB_ERROR_ACCESS、PRINT_LOG 全局日志开关、SWD 验证日志开关行为、串口拒绝访问、ST-Link VCP 重试、COM 端口占用、gdb 计数直读、LVGL 首帧性能测量、离屏预热、脚本僵化回归、e2e 脚本假 FAIL、断言前提、零位移无状态帧、一条断言只证一件事。
agent_created: true
---

# STM32 端到端验收方法论

嵌入式代码"生成"只是第一步，**跑通验证闭环**才是迭代基础。本 skill 把多个 STM32 项目的
验收实践固化成可复制流程。配套：`stm32-project-scaffold`（构建）、
`stm32-ai-dev-environment`（环境）。

## 一、标准验收链（每个模块必走）

```
实现计划（先确认再动手）
   ↓
编码实现（app/ + bsp/ + tools/ 同步）
   ↓
Debug + Release 双构零警告        ← 用数字显式列出 RAM/FLASH 占比
   ↓
仿真 / 真机验证（openocd 烧录 + 串口/网络）
   ↓
扩展 verify 脚本（python / C#，pass/fail 计数）
   ↓
交付清单（增量汇报，✅ 状态收尾）
```

**铁律**：任何新模块动手前必须先出实现计划并获确认；每完成一模块立即增量汇报。
（这条链与 `stm32-vibe-coding-workflow` 的「分阶段验收节奏」是同一件事，
**以本 skill 为准**；总方法论 skill 只保留图示与非验收类内容。）

## 二、双构零警告（构建验收）

```bash
# Debug
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# Release
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
- 强约束 **零错误零警告**（`-Wall -Wextra`，见 `stm32-ai-dev-environment`）。
- 验收时**显式列出资源占用数字**：
  - FLASH：字节 / 总容量 / 百分比
  - RAM（内部）：字节 / 总容量 / 百分比
  - SDRAM 池：占用大小
  - 例：`FLASH <n>B / <总>KB (<p>%)`、`RAM <n>B / <总>KB (<p>%)`
- 仅 RWX 段良性提示可豁免（裸机/链接脚本特性），其余警告须清零。

### 2.1 构建目录清理（沙箱 safe-delete 拦截坑）
CI / 沙箱里 `rm -rf build` 可能触发 **safe-delete 批量确认拦截**（文件数超过阈值时命令被拦、实际未执行），
导致陈旧 `build/` 被复用、新增 `.c` 没进编译、出现「改了代码却没生效」的假象。
**正确做法**（二选一）：
- 用 `cmake --fresh -B build` 强制重配（最稳，保留同一目录名）；
- 或改用全新目录名（`build_dbg`/`build_rel`）避免复用陈旧产物。
⚠️ 只要动过 `CMakeLists.txt` 的 `GLOB` 源清单（新增 `app/ui/*.c` 等），**必须重跑 cmake 重新 GLOB**，
否则 ninja 增量不会自动重扫（见 `stm32-project-scaffold` 第二节）。

## 三、OpenOCD 烧录验证

```bash
openocd -f openocd/stm32h743_stlink.cfg \
  -c "init" -c "program build-release/xxx.elf verify reset exit"
```
成功标志：
```
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```
- 记录目标信息：ST-Link 固件版本、Target voltage、SWD DPIDR（H743=0x6ba02477，F429=0x2ba01477）。
- 烧录后通过串口确认系统起来（心跳 LED / 启动日志）。

⚠️ **OpenOCD 烧录必须用 `.elf`，不要用 `.bin`**：`.elf` 自带加载段与入口；`.bin` 会报
`no flash bank found for address 0x00000000` 且 `wrote 0 bytes`。命令：
`flash write_image erase build/xxx.elf` + `verify_image` + `reset run`
（注意 Git-Bash 里 `cd` 路径必须用正斜杠，反斜杠会吞掉目录分隔）。
- **改完代码后先烧录再抓串口**：只 `reset` 不复烧会看到旧固件行为，误判。

### 3.1 OpenOCD `mdw` 内存直读（正向验证）

**模板与全部时序细节的唯一起点在 `stm32-swd-forensics`**（`halt` 后必须 `wait_halt`、
`mdw` 只能 4 字节对齐整字读、一次性 `-c` 脚本的 stdout 会被 `shutdown` 前缓冲吞掉）。
验收侧只需要记住两条结论：

- `mdw` 读**非 4 字节对齐**地址会报 `Failed to read memory`；读 `uint8/uint16` 混排的静态变量
  要按 4 字节对齐整字读，再在 Python 里切字节。
- 用 `arm-none-eabi-nm` 取符号地址 → `mdw` 直读目标内存，是「没报错 ≠ 有数据」的正向验证手段（见 3.2）。

### 3.2 「没报错 ≠ 有数据」铁律（验收必守）
错误日志常被限流，且「调用返回 0」不等于「数据正确」。正向验证要用：
- `arm-none-eabi-nm <elf> | grep <symbol>` 取址；
- OpenOCD `mdw` / `arm-none-eabi-gdb` `x/...` 直读内存里的数据结构，比对预期值。
例：传感器采样值、`g_dcmi_last_idx` 交替、SRAM 池完整性，均靠直读内存而非只看日志。

## 四、真机功能验证（串口 / 网络）

### 4.1 串口控制台（最常用）
- H7 调试串口 USART1 PA9/PA10（ST-Link 虚拟串口，端口号依本机分配；或 TinyUSB CDC 虚拟串口）。
- 真机打印启动日志：时钟、版本、字库挂载状态、系统存活（LED 心跳）。
- 命令式测试：用 python `pyserial` 发命令、收响应、断言。

### 4.2 网络验证（F4 网络项目）
- `ping` 全尺寸扫一遍区分故障类型（1472 通/1473 断 = 分片路径失效，属 MTU/分片边界问题，
  非固件 bug）。
- Web：`curl http://IP/ | grep -q "<特征串>"`（避开 Git Bash 吞 `%{}`，见 `stm32-ai-dev-environment`）。
- `arp -a <IP>` 核对 MAC OUI（ST=00:80:E1）防假通。
- SNMP：用 PC 端工具或 `snmpget` 验证 Agent 响应（UDP 161）。

### 4.3 全局日志开关 PRINT_LOG（可 SWD 验证）
工程内所有应用日志统一走 `PRINT_LOG(...)`（编译期可整体关闭成 `((void)0)`），不再裸调
`printf`（完整方案见 `stm32-logging-print-log`）。这带来一个**可 SWD 直读验证**的特性：
关掉日志后，UART TX 环形缓冲写指针必须一个字节都没动过（用一次性脚本断言
`g_tx_head==0 && g_tx_busy==0`，应全 PASS）。
- 符号地址用 `arm-none-eabi-nm` 取，OpenOCD `mdw` 读（非 4 字节对齐先整字读再切字节，见 3.1）。
- 串口不可用（如 CH340 code-31）时，这条「日志关 = 串口零字节」正是用 **SWD 取证代替串口抓日志**的范例。
- 约定：ISR 内禁止调 `PRINT_LOG`（内部拿互斥量），中断上下文用 `uart_write()`。

## 五、verify 脚本模式（可复制模板）

**核心原则：脚本给出 pass/fail 计数**，而非人肉看日志。实测有效的几种形态：

### 5.1 Python 串口自测（例：某 NES 菜单项目，28/28 PASS）
```python
import serial, sys
port = sys.argv[sys.argv.index("--port")+1]
ser = serial.Serial(port, 115200, timeout=2)
checks = 0; passed = 0
def chk(name, cond):
    global checks, passed
    checks += 1; passed += 1 if cond else 0
    print(f"[{'PASS' if cond else 'FAIL'}] {name}")
# basics / navigation / keys / error handling ...
print(f"\n** {passed}/{checks} checks passed **")
sys.exit(0 if passed==checks else 1)
```

### 5.2 C# 批量验证（例：某 SNMP 项目，31/31 PASS）
- 构造 Get/GetNext/Set 请求，解析 VarBind，对每项错误计数。
- `dotnet run` 后打印 `31/31 PASS` 或失败项明细。

### 5.3 离线逻辑单测（PC 侧 gcc 编译 + ctypes）
- BER/MIB/报文编解码等纯逻辑，在 PC 用 gcc 编译成 .so/.dll，python ctypes 调用断言。
- 不依赖硬件，CI 友好（例：36/36 PASS）。

### 5.4 视觉渲染测试（桌面仪表盘）
- PrintWindow 截图 + Pillow 亮像素检测，验证每页非空白（例：6/6 PASS）。

### 5.5 ★ 验收脚本的「前提」纪律 —— 假 FAIL 先怀疑脚本自己

脚本报 FAIL 时，**先怀疑"前提没成立"，再怀疑产品代码**。最常见的四类自造 FAIL：

1. **动作不产生可观测变化**（目标 == 当前位置 / 已贴着边界）⇒ "等收敛"拿到 `NaN`/空集。
2. **造前提的常量写死**，没从真值（限位、容量、量程）里挑 ⇒ 撞边界后把"边界现象"
   误报成"机制缺陷"。
3. **一条断言证多件事** ⇒ 失败无法归因，只能猜；也别把"后来的合理变化"断言成"没收敛"。
4. **断言的机制窗口窄到脚本摸不着**（竞态/优先级闸门）⇒ 该由单测构造在途态来证，
   e2e 只证外部可观测后果。

> 完整的四条展开、实例与「写任何 e2e 脚本前过一遍」的检查表，见
> `robotics-multiphysics-vmodel-workflow/references/e2e-script-premise-discipline.md`。
> 与 3.2「没报错 ≠ 有数据」同源：**否定性结论一律先查"工具的观测能力"**，
> 别把"我没观测到"直接当成"它没发生"。

## 六、交付清单（增量汇报模板）

每个模块完成即汇报，用 ✅ 状态收尾：

```
## 交付清单 — <模块名>
- [x] 实现计划已确认
- [x] 编码完成（app/ + bsp/）
- [x] Debug/Release 双构零警告（FLASH xx% / RAM xx%）
- [x] 真机烧录验证（Verified OK）
- [x] verify 脚本 <n>/<n> PASS
- [x] README.md 已更新
```

## 七、稳定性验收（网络/长运行项目必做）

- 压测前后各查一次 CFSR/HFSR=0（无总线故障）。
- 连续数十次业务请求无失败（验 pbuf/pcb 无泄漏）。
- 长运行浸泡（如周期轮询某状态量，确认 `*_valid` 恒定不冻结）。
- I2C 锁死恢复与 SDRAM 初始化顺序见 `stm32-peripheral-drivers/references/lan8720a-rmii.md`。

## 八、双固件 Bootloader 端到端验证

Bootloader + App 是**两套独立构建、固定地址共存**，验收分三层：

1. **跳转验证（Golden path）**：直烧 App+配置 → 复位 → Bootloader 挂载 → 配置 CRC/向量/HMAC/版本全过 → 等待窗口；USB 连则 U-disk（符合设计），未连则跳 App。
2. **升级验证（U 盘路径）**：把 `*_test.bin` + `verify.json` 拷 U 盘 → 复位 → Bootloader 检测包 → `BFLASH_EraseApp` + `BFLASH_ProgramBlock` → 重启 → 打印新 App 版本。Flash 读回版槽/配置 CRC 确认。
3. **引擎级验证**：见「GDB 函数级调试」reference，gdb 直调 `BFLASH_ProgramBlock`，确认不再挂死、回读一致。

- 任何校验失败都**在擦写前 abort**，已运行 App 不会被破坏（防砖设计，验收时重点确认"坏包不破坏"）。
- 防砖设计与内存分区见 `stm32-project-scaffold` 的「双固件镜像 Bootloader」节。

## 九、LVGL 首帧/首绘性能测量与预热回归（STM32 + LVGL）

**结论先行**：首帧卡顿的常见根因**不是**字库缓存 miss，而是 **LVGL 一个 screen 第一次被实际
绘制时的一次性 CPU 开销**（样式计算 / label 排版 / draw-task 构建，可达 ~200 ms，纯 CPU、零 IO）。

- **判据要先排除"伪性能"**：光看 `refr` 变长会误判成缓存未命中。必须同时打印
  `glyph_cache` 的 `bmp_miss/hit/evict` 与 SD/TTF 侧 `ctf_sd / ttf_fill / ttf_read` **增量**；
  若增量为 0 → **零 IO**，问题在 CPU，加缓存无用。
- **正解是"抑制 flush 的离屏预热"**：把 `disp->flush_cb` 临时换成 dummy（吞帧 + 立即
  `lv_disp_flush_ready`），在启动加载页背后把每页渲染一次，把首绘开销提前吃掉。
- **回归脚本**：抓串口 → 解析每次 `[PAGE] switch` → 断言首帧 `bmp_miss == 0`、零 SD 读、
  `|首帧 refr − warm 均值| ≤ 阈值`，退出码 0/1（CI 友好），并支持 `--in <文件>` 离线复跑。

> 完整设计（CTF 索引 + TTF 块缓存的硬约束、Latin 预取误假设、字形缓存 LRU + epoch 钉扎）
> 见 `stm32-lvgl-font-engine`。**本节只保留验收判据；机制与实现以该 skill 为准。**

## 十、本 skill 的参考文件

- `references/gdb-function-level-debug.md` — **GDB 函数级调试配方**（常驻 openocd 服务器、
  gdb 直调函数做隔离验证、超时挂死检测抓 PC、Flash 回读、复用 running openocd 烧写、
  串口占用排查、运行时计数直读）。
  其它 skill 里写的「见 `stm32-verification-acceptance` 的 gdb 直调」指的就是它。

