---
name: stm32-verification-acceptance
description: STM32 嵌入式项目的端到端验收方法论：Debug/Release 双构零警告、OpenOCD 烧录、串口/网络真机验证、Python/C# verify 脚本 pass/fail 计数模式、增量交付清单。适用于"验收 STM32 固件""写嵌入式自测脚本""定义项目验收标准""真机烧录后如何确认功能正常""整理交付清单"。触发词：验收流程、零警告构建、OpenOCD 烧录验证、串口自测、verify 脚本、pass/fail 计数、交付清单、真机验证、嵌入式测试、snmp_verify、serial_test、openocd 烧录必须用 elf 非 bin、mdw 4字节对齐读取、没报错不等于有数据、COM code-31、LIBUSB_ERROR_ACCESS、PRINT_LOG 全局日志开关、SWD 验证日志开关行为。
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
  - 例：`FLASH 310808B / 1024KB (29.79%)`、`RAM 85992B / 192KB (43.74%)`
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
- **`mdw` 读非 4 字节对齐地址会报 `Failed to read memory`**。读 `uint8/uint16` 混排的静态变量时
  按 4 字节对齐整字读（`mdw <addr_aligned>`），再在 Python 里切字节。
- 用 `arm-none-eabi-nm` 取符号地址 → OpenOCD `mdw` 直读目标内存里的数据结构，是「没报错≠有数据」的
  正向验证手段（见 8.6）。

⚠️ **一次性 `-c "init; ...; mdw ...; shutdown"` 读数会被缓冲吞掉**：在 `shutdown` 前 OpenOCD 常不 flush
 stdout，成功 mdw 行在管道里丢失，只透出错误/PC 行。两种稳法：
- 落盘：`openocd ... -c "...; mdw 0x..; resume; shutdown" > ocd.log 2>&1`，再 `grep 0x2000 ocd.log`；
- 或分两步：先 `halt` 做完 mdw，最后单独 `shutdown`，不要在一行里紧跟 mdw 后 shutdown。
- **`halt` 后必须 `wait_halt` + 短 `sleep` 再 `mdw`**：刚 halt 瞬间目标还在跑，读 SRAM 会报
  `Failed to read memory`；`wait_halt` 等停稳再读即正常（详见 `stm32-swd-forensics` 第三节）。

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
`printf`（约定见 `102.stm32f429_tinyusb_ui`）。这带来一个**可 SWD 直读验证**的特性：
关掉日志后，UART TX 环形缓冲写指针必须一个字节都没动过（见 `102` 的 `verify_log_switch.py`，
`6/6 PASS`：`g_tx_head==0 && g_tx_busy==0`）。
- 符号地址用 `arm-none-eabi-nm` 取，OpenOCD `mdw` 读（非 4 字节对齐先整字读再切字节，见 3.1）。
- 串口不可用（如 CH340 code-31）时，这条「日志关 = 串口零字节」正是用 **SWD 取证代替串口抓日志**的范例。
- 约定：ISR 内禁止调 `PRINT_LOG`（内部拿互斥量），中断上下文用 `uart_write()`。

## 五、verify 脚本模式（可复制模板）

**核心原则：脚本给出 pass/fail 计数**，而非人肉看日志。本项目实测有效的几种形态：

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
- 长运行浸泡（如 SNMP 30s 轮询 `sensor_valid` 恒为 1），确认无数据冻结。
- I2C 锁死恢复与 SDRAM 初始化顺序见 `stm32-peripheral-drivers` 第八节。

## 八、高级调试：openocd + gdb 函数级验证与挂死定位

当某个函数（如 Flash 擦写引擎）在真机上一跑就死、靠加日志难以定位时，用 **gdb 直调该函数** 做隔离验证，并用 **超时挂死检测** 自动抓 PC。这是 Bootloader 项目验证 `BFLASH_ProgramBlock` 是否修复的核心手段（详见 `stm32-peripheral-drivers` 第九节）。

### 8.1 起常驻 openocd 调试服务器（gdb :3333 / tcl :6666 / telnet :4444）
```bash
openocd -s <openocd scripts dir> -f openocd.cfg > ocd.log 2>&1 &
# openocd.cfg: interface/stlink.cfg + transport select swd + target/stm32h7x.cfg
```
后续 gdb / telnet 烧写都连这个服务器，**不要重复起 openocd**（ST-Link 被独占，第二个实例会失败）。

### 8.2 gdb 直调函数（先 relocate RAM 引擎再 call）
很多函数依赖"从 RAM 执行的引擎"，必须先让 relocate 跑完。用 hw-breakpoint 命中 relocate 函数，`finish` 执行完它，再 `call` 目标函数：
```gdb
target extended-remote :3333
monitor reset halt
file build/stm32h7_boot.elf          # 载入符号表
break BFLASH_Relocate                # hw-bp 命中 relocate
continue                             # 跑到 relocate
finish                               # 执行完 relocate（引擎已搬到 AXI SRAM）
call flash_erase_sector(1,2)         # 返回 0 = OK
call BFLASH_ProgramBlock(0x08040000, 0x24070000, 32)
x/8xw 0x08040000                    # 回读 8 字，应等于写入 pattern
```
若 `call` 后 gdb 长时间不返回 → 真机挂死（见 8.3 抓 PC）。

⚠️ **GDB 可靠性边界（来自真机教训）**：`-O2`/Release 下 GDB 读取局部变量不可靠，
且 Cortex-M **勿用 `call` 触发复杂函数**（易 HardFault，尤其涉及 OS 调度/中断/浮点）。隔离验证
优先用 hw-bp + `finish` + `call` 简单函数；复杂路径改用"直调 + 8.3 超时挂死检测"而非交互式 `call`。

⚠️ **GDB 断点打在「含 FreeRTOS 互斥量获取」的函数会死锁**（真机教训）：这类函数内部
 `xSemaphoreTake` 在调度器未起或已有任务持锁时 `call` 会永久阻塞 gdb。隔离验证时：
- 断点打在 `vTaskStartScheduler()` **之前**，或确认该函数此刻无持锁窗口；
- 否则改用「hw-bp 命中该函数入口 → `finish` 跑完 → 看返回值」而非交互式 `call`，避免 gdb 卡死。

### 8.3 挂死自动检测驱动（python 包 gdb）
```python
p = subprocess.Popen([GDB, "-q", "-batch", "-ex", "target extended-remote :3333", ...])
try:
    p.wait(timeout=45)               # 超时则视为挂死
except subprocess.TimeoutExpired:
    p.kill()
    # 再起一个 gdb 连服务器，monitor halt，读 PC
    # info registers / bt 定位卡在哪个函数
```
- 读 `pc` / `lr` / `sp` 定位。本例卡在 `BFLASH_ProgramBlock` → 引擎放 DTCM 不可执行 / 手搓寄存器序列错。
- **铁律**：Cortex-M7 的 DTCM(0x20000000) 不可执行代码（I-Code 总线取不到指令）→ 从 DTCM 跑函数立即 BusFault→`Default_Handler`(Infinite_Loop)。凡"从 RAM 执行"的引擎**绝不放 DTCM**，必须放 AXI SRAM(0x24000000)。

### 8.4 Flash 回读校验（gdb 直接读内存）
升级/跳转后，用 gdb 读关键区确认结果，不依赖串口：
```gdb
x/1xw 0x08021000     # 版本槽，应 == 0x06000001 (v1.0.0.6)
x/2xw 0x08020000     # App 向量：SP / reset
x/4xw 0x081E0000     # 配置区 magic(0xB0075EED) + crc32
```
gdb 会剥前导零，比对时按 32 位值判断。

### 8.5 复用 running openocd 烧写（telnet 4444）
```python
s = socket.create_connection(('127.0.0.1', 4444))
send('reset halt')
send('flash write_image erase build/stm32h7_boot.bin 0x08000000')
send('verify_image build/stm32h7_boot.bin 0x08000000')   # 期望 verified N bytes
send('reset run')
```
避免再起 openocd 冲突 ST-Link。

### 8.6 串口捕获与端口占用排查
```python
import serial
s = serial.Serial('<COM端口>', 115200, timeout=0.3)   # H7: ST-Link VCP
```
- 若 `serial.Serial` 抛 `PermissionError` → 端口被**残留 python/捕获进程**占用。先 `tasklist` / `wmic process` 找占用者并结束，再抓。曾因后台 capture 进程未退出导致串口抓不到。
- 抓日志要在 `reset run` **之后**开始，否则错过启动 banner。
- **ST-Link 被 openocd/gdb 残留占用**：烧录报 `Error: init mode failed` / `ST-Link not found` →
  `tasklist | findstr openocd`（或 `findstr arm-none-eabi-gdb`）找残留 PID，`taskkill /F /PID <pid>`
  结束后再起新实例。同一时刻只能有一个 openocd 持有 ST-Link（见 8.1 常驻服务器做法可避免冲突）。
- **`libusb_open() failed with LIBUSB_ERROR_ACCESS`**：反复用 openocd/gdb 后 USB 被残留进程占用，
  先 `Get-Process openocd | Stop-Process -Force`（PowerShell）或 `taskkill /F /IM openocd.exe` 再烧。
- **COM 口 `code-31 / PermissionError(13)`**：CH340 等会周期性进入「设备未发挥作用」状态，需重新插拔 USB
  才能恢复；串口挂掉时可用 SWD 读内存取证（见 3.2）代替串口抓日志。

### 8.7 沙箱 / 环境局限（验收设计必知）
- **QSPI 直写不可行**：openocd `stmqspi` 在本类环境常拉不起 H743 QSPI（probe 后 timeout / No QSPI）。升级包改走**设计的 U 盘路径**（QSPI FatFs + TinyUSB MSC，用户机器拷包）。
- **COM 映射**：Windows 下串口号近似 `/dev/ttyS<N-1>`（Git Bash）；原生 python 用 `COM<N>`，端口号依本机分配。

## 九、双固件 Bootloader 端到端验证

Bootloader + App 是**两套独立构建、固定地址共存**，验收分三层：

1. **跳转验证（Golden path）**：直烧 App+配置 → 复位 → Bootloader 挂载 → 配置 CRC/向量/HMAC/版本全过 → 8s 窗口；USB 连则 U-disk（符合设计），未连则跳 App。
2. **升级验证（U 盘路径）**：把 `stm32h7_test.bin`+`verify.json` 拷 U 盘 → 复位 → Bootloader 检测包 → `BFLASH_EraseApp`+`BFLASH_ProgramBlock` → 重启 → 打印新 App 版本。Flash 读回版槽/配置 CRC 确认。
3. **引擎级验证**：见第八节 gdb 直调 `BFLASH_ProgramBlock`，确认不再挂死、回读一致。

- 任何校验失败都**在擦写前 abort**，已运行 App 不会被破坏（防砖设计，验收时重点确认"坏包不破坏"）。
- 黄金外部参考样本：`7.stm32h7_iap`（同芯片已验证的 `drv_flash.c` / `upload_frame.c`，可作为外部参考，非本仓 skill）。
- 防砖设计与坏包验证见 `stm32-project-scaffold` 第八节。
