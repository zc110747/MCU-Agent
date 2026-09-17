# GDB 函数级调试：隔离验证与挂死定位

当某个函数（如 Flash 擦写引擎）在真机上一跑就死、靠加日志难以定位时，用 **gdb 直调该函数**做
隔离验证，并用 **超时挂死检测**自动抓 PC。这是 Bootloader 项目验证编程函数是否修复的核心手段
（配合 `stm32-peripheral-drivers/references/h7-flash-bootloader.md`）。

> 下文出现的 `BFLASH_*` / `stm32h7_boot.elf` / `0x08021000` 等均为**示例符号与地址**，
> 按自己工程的链接脚本与符号名替换。

### 1. 起常驻 openocd 调试服务器（gdb :3333 / tcl :6666 / telnet :4444）
```bash
openocd -s <openocd scripts dir> -f openocd.cfg > ocd.log 2>&1 &
# openocd.cfg: interface/stlink.cfg + transport select swd + target/stm32h7x.cfg
```
后续 gdb / telnet 烧写都连这个服务器，**不要重复起 openocd**（ST-Link 被独占，第二个实例会失败）。

### 2. gdb 直调函数（先 relocate RAM 引擎再 call）
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
x/8xw 0x08040000                     # 回读 8 字，应等于写入 pattern
```
若 `call` 后 gdb 长时间不返回 → 真机挂死（见第 3 节抓 PC）。

⚠️ **GDB 可靠性边界（来自真机教训）**：`-O2`/Release 下 GDB 读取局部变量不可靠，
且 Cortex-M **勿用 `call` 触发复杂函数**（易 HardFault，尤其涉及 OS 调度/中断/浮点）。隔离验证
优先用 hw-bp + `finish` + `call` 简单函数；复杂路径改用"直调 + 第 3 节超时挂死检测"而非交互式 `call`。

⚠️ **GDB 断点打在「含 FreeRTOS 互斥量获取」的函数会死锁**（真机教训）：这类函数内部
 `xSemaphoreTake` 在调度器未起或已有任务持锁时 `call` 会永久阻塞 gdb。隔离验证时：
- 断点打在 `vTaskStartScheduler()` **之前**，或确认该函数此刻无持锁窗口；
- 否则改用「hw-bp 命中该函数入口 → `finish` 跑完 → 看返回值」而非交互式 `call`，避免 gdb 卡死。

### 3. 挂死自动检测驱动（python 包 gdb）
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
- **铁律**：Cortex-M7 的 DTCM(0x20000000) 不可执行代码（I-Code 总线取不到指令）→ 从 DTCM 跑函数立即 BusFault → `Default_Handler`(Infinite_Loop)。凡"从 RAM 执行"的引擎**绝不放 DTCM**，必须放 AXI SRAM(0x24000000)。

### 4. Flash 回读校验（gdb 直接读内存）
升级/跳转后，用 gdb 读关键区确认结果，不依赖串口：
```gdb
x/1xw 0x08021000     # 版本槽（示例地址），应等于刚烧进去的版本号
x/2xw 0x08020000     # App 向量：SP / reset
x/4xw 0x081E0000     # 配置区 magic + crc32
```
gdb 会剥前导零，比对时按 32 位值判断。

### 5. 复用 running openocd 烧写（telnet 4444）
```python
s = socket.create_connection(('127.0.0.1', 4444))
send('reset halt')
send('flash write_image erase build/stm32h7_boot.bin 0x08000000')
send('verify_image build/stm32h7_boot.bin 0x08000000')   # 期望 verified N bytes
send('reset run')
```
避免再起 openocd 冲突 ST-Link。

### 6. 串口捕获与端口占用排查
```python
import serial
s = serial.Serial('<COM端口>', 115200, timeout=0.3)   # H7: ST-Link VCP
```
- 若 `serial.Serial` 抛 `PermissionError` → 端口被**残留 python/捕获进程**占用。先 `tasklist` / `wmic process` 找占用者并结束，再抓。曾因后台 capture 进程未退出导致串口抓不到。
- 抓日志要在 `reset run` **之后**开始，否则错过启动 banner。
- **ST-Link 被 openocd/gdb 残留占用**：烧录报 `Error: init mode failed` / `ST-Link not found` →
  `tasklist | findstr openocd`（或 `findstr arm-none-eabi-gdb`）找残留 PID，`taskkill /F /PID <pid>`
  结束后再起新实例。同一时刻只能有一个 openocd 持有 ST-Link（见第 1 节常驻服务器做法可避免冲突）。
- **`libusb_open() failed with LIBUSB_ERROR_ACCESS`**：反复用 openocd/gdb 后 USB 被残留进程占用，
  先 `Get-Process openocd | Stop-Process -Force`（PowerShell）或 `taskkill /F /IM openocd.exe` 再烧。
- **COM 口 `code-31 / PermissionError(13)`**：CH340 等会周期性进入「设备未发挥作用」状态，需重新插拔 USB
  才能恢复；串口挂掉时可用 SWD 读内存取证（见 `stm32-swd-forensics`）代替串口抓日志。

### 6.1 ST-Link 虚拟串口「拒绝访问」重试坑（真机教训，高概率）
当用 OpenOCD 经 libusb 触碰过 ST-Link 后，**首次打开其 VCP（COMx，即 USART1 PA9/PA10）常报
「拒绝访问 / PermissionError」**——即使 `tasklist` 查无占用者、没有残留 python/捕获进程（与上面
"端口被残留进程占用"是**两种不同根因**）。根因是 ST-Link 的 VCP 在 OpenOCD 释放后仍被 Windows 短暂持锁。
**解法**：串口捕获脚本对 `serial.Serial(...)` 做 **5~7 次重试（指数退避 0.2~1.5s）**，重试几次后必然成功；
若仍失败，重插 ST-Link 或重启 OpenOCD 后再重试即可。切勿误判为"端口被占用去 kill 进程"——那种做法无效。
- 端口号依本机分配；Git Bash 下近似 `/dev/ttyS<N-1>`，原生 python 用 `COM<N>`。
- 抓日志要在 `reset run` **之后**开始，否则错过启动 banner（见本节上文顺序铁律）。

### 6.2 ⚠️ 主机侧读数方式会造成"固件很慢"的假象（30 ms 假延迟）
压测/延迟脚本里最常见的写法藏着一个数量级陷阱：
```python
data = ser.read(self.ser.in_waiting or 4096)   # ❌ 错
```
`in_waiting == 0` 时此处会**请求 4096 字节**；pySerial/Windows 的读语义是
「尽量凑满所请求字节数，凑不满就等到读超时」，于是**每次无数据时都要空等整整一个 timeout**，
把本该 0.2 ms 返回的字节拖到 ~30 ms 才交出来。

实测对照（某 CDC↔UART 桥工程，1 字节小包延迟）：

| 路径 | 错误读数 | 修正后 |
|---|---|---|
| 软件回环（**完全绕过 UART**） | 31.91 ms | 0.22–0.24 ms |
| 经 UART 真实回环 | 34.24 ms | **1.43 ms** |

关键点：**软件回环根本不碰 UART/DMA，却同样有 31.91 ms** → 这 30 ms 与固件无关。

**正确写法**：`data = ser.read(self.ser.in_waiting or 1)`（有读多少，无则最多等 1 字节）。
- 方法论：任何"固件延迟异常"先做**双路径对照**（绕过外设 vs 经过外设），
  两者差异才是外设真实开销；两者共同的开销一定在主机侧
  （与 `stm32-verification-acceptance` 的 LVGL 首帧「先排除伪性能」同理）。
- 吞吐压测的 `平均延迟` 还受**在途窗口**与波特率主导，不代表固件固有延迟：
  真实往返延迟要看 pacing 模式（无在途窗口）。

### 7. 沙箱 / 环境局限（验收设计必知）
- **QSPI 直写不可行**：openocd `stmqspi` 在本类环境常拉不起 H743 QSPI（probe 后 timeout / No QSPI）。升级包改走**设计的 U 盘路径**（QSPI FatFs + TinyUSB MSC，用户机器拷包）。
- **COM 映射**：Windows 下串口号近似 `/dev/ttyS<N-1>`（Git Bash）；原生 python 用 `COM<N>`，端口号依本机分配。

### 8. 运行时计数直读（验证缓存命中率 / 算法行为，无需串口命令接口）
当固件没有 UART 命令接口、却要确认某个模块（如字形缓存）的 hit/miss/evict 计数是否真实生效时，
**烧录 Debug 构建 + gdb 直读静态变量** 是最硬的证据。比串口打印更准（不受日志时序/缓冲干扰）。

```bash
# 1) 烧录 Debug 构建（带 -g 符号），让目标跑起来
openocd -f openocd.cfg -c "program build-debug/xxx.elf verify reset exit"
# 2) 起常驻 openocd 服务器（见第 1 节）
openocd -f openocd.cfg > ocd.log 2>&1 &
# 3) gdb 脚本（或 -x）：reset 让目标跑 N 秒触发业务，再 halt 直读
arm-none-eabi-gdb -batch -x read_counters.gdb build-debug/xxx.elf
```
```gdb
set pagination off
target remote :3333
monitor reset run          # 重新启动，跑自动业务（如页面切换）
shell sleep 20             # 让缓存/算法充分运行
monitor halt               # 冻结
x/1uw &'your_module.c'::s_hits      # 直读符号真实地址（换成自己的符号）
x/1uw &'your_module.c'::s_misses
x/1uw &'your_module.c'::s_evicts
detach
quit
```
- 读数即权威：`s_hits=1319 / s_misses=210 → 命中率 86%` 这类数字直接证明缓存生效；
  `s_evicts=0` 若符合预期（缓存未填满）也一并坐实。
- **必须用 `x/1uw &'file.c'::symbol` 直读符号真实地址**，不要 `call func(&$h)`：
  gdb 便利变量 `$h` 不能取地址，会报 `Attempt to take address of value not located in memory`；
  `x/1uw` 直接剥符号地址读内存，100% 可靠。
- gdb 偶发 `This normally should not happen, please file a bug report` 多为 `printf` 路径噪声，
  不影响 `x/1uw` 结果，可忽略。
- 验证完把板子刷回 Release 构建（生产态），并删掉临时 `.gdb` 脚本。
