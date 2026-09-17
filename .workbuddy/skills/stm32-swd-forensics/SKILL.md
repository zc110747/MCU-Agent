---
name: stm32-swd-forensics
description: 串口/板子不可用时的 SWD+OpenOCD 内存取证与中断链路诊断：**读内存前必须先 verify_image 锁定"读的是谁的内存"**（Debug/Release 的 .bss 布局不同，符号地址错位会产出"看似合理实则错位"的假象）、HardFault 现场解码（ICSR/CFSR/BFAR + 异常压栈帧 + objdump 反汇编核对）、CH340 code-31 驱动故障、arm-none-eabi-nm 取符号地址、mdw 4 字节对齐读 + Python 切字节、"没报错≠有数据"正向验证、EXTI_SWIER 软件中断注入验证中断链路、中断风暴定位与防护、I2C 总线锁死恢复、无相机/无示波器时用"传输字节计数器 + 两次采样看增量"做量化验证。适用于"串口打不开""COM5 驱动故障""读内存验证变量""中断不进""触摸不响应""I2C 死锁""中断风暴""EXTI 不触发""用 SWD 抓数据""读到的内存不像是这个程序写的""I2C/SPI 外设没反应""屏幕只显示一行""固件跑一会儿就死""HardFault 定位""libusb_open 失败""板子像死了/串口完全没反应但固件看着没问题"。触发词：SWD 取证、mdw、arm-none-eabi-nm、OpenOCD 读内存、verify_image、符号地址错位、Debug Release bss 布局、HardFault、CFSR、BFAR、异常压栈帧、硬件监视点 wp、uwTick 不涨、CH340 code-31、串口打不开、中断不进、EXTI_SWIER、软件中断注入、中断风暴、T_PEN 不响应、I2C 锁死、BSP_I2C_Recover、正向验证、没报错但有数据、无相机验证、bit-bang 计数、reset halt 陷阱、.data 搬运判定、内存异常字节指纹搜索、硬件写监视点 wp、**监视点选址（只压只初始化一次的字段）/假命中**、LIBUSB_ERROR_ACCESS、探针被占用、VSCode Cortex-Debug 占 ST-Link、**板子像死了但不一定是固件**、RCC_CSR 复位标志、uwTick 回退判复位、间歇性故障定性。
agent_created: true
---

# SWD 内存取证 + 中断链路诊断

## 一、何时用本 skill

- 串口打不开（CH340 `code-31` / `PermissionError(13)`，需重新插拔）、COM 口占错；
  **日志看不到了，但代码还在跑**，要用 SWD 读内存验证行为。
- "日志没报错"但怀疑根本没数据——需要**正向验证**：直接读目标 RAM 里的数据结构。
- 中断不进 / 触摸不响应 / EXTI 不触发——用软件中断注入验证链路，不靠人手点屏。
- 中断风暴（IRQ 计数飙到几万/秒）把低优先级任务饿死、锁死 I2C 总线。

## 二、探针开不了：CH340 code-31 / 残留 openocd / **VSCode Cortex-Debug**

- `code-31` 时**不要反复重试串口**：改用 SWD 抓内存（见下）。物理重插拔 USB 串口芯片可恢复。
- 烧录前先清残留 openocd：它会独占 ST-Link，让后续报
  `libusb_open() failed with LIBUSB_ERROR_ACCESS`。
- 机器上若有多个同款 USB 串口（如两个 CH340），**先抓字节确认哪个是本板串口**，占错口会
  一直读到空。

### 2.1 `LIBUSB_ERROR_ACCESS` 的两条成因 —— 先查谁占着，别怀疑板子

同一条报错，两种完全不同的成因，**必须先分清**：

| 现象 | 成因 |
|---|---|
| 设备列表里**找不到** VID_0483 | 线/驱动/掉电（见本节上文 code-31、末节供电） |
| 设备状态 **`OK`** 却 open 失败 | **另一个进程占着探针** |

**排查配方（PowerShell 工具的 stdout 会被吞 → 落文件 + Read 取回）：**

```powershell
# 谁的命令行里有 openocd / stlink
Get-CimInstance Win32_Process | Where-Object { "$($_.CommandLine)" -match 'openocd|stlink' } |
  ForEach-Object { "$($_.ProcessId) $($_.CommandLine)" }
# 设备本身在不在
Get-PnpDevice -PresentOnly | Where-Object { "$($_.InstanceId)" -match '0483' }
```

**最常见的"隐形占用者"是 VSCode 的 Cortex-Debug（F5 调试会话）**：它一直持有 ST-Link，
进程是 `openocd.exe` + `arm-none-eabi-gdb.exe`，父进程 `Code.exe`，命令行里能看到
`marus25.cortex-debug-*/support/openocd-helpers.tcl`。
（⚠️ 按**进程名**过滤可能什么都看不到 —— 用的是 `D:\Software\openocd\bin\openocd.exe`，
必须按**命令行**匹配才抓得到。）

> ⚠️ **这不只是挡住你，还会直接制造"板子工作异常"的假象**：
> 调试会话若停在断点上，CPU 就是被 **halt** 的 —— 串口不出字、屏幕不刷新、按键无响应。
> **任何"板子像死了 / 串口收不到数据"的判断之前，先做这一步。** 本次实测：固件侧寄存器、
> 引脚、`halt` 下手写 `DR` 全部正常、PC 发 68 B 时 RX 环指针增量 0 —— 排查半天，
> 最后发现有 VSCode 调试会话在跑。


## 三、SWD 读内存通用模板（必背）

OpenOCD `mdw` **只能 4 字节对齐整字读**；读 `uint8`/`uint16` 混排的静态变量要先按
4 字节对齐整字读，再在 Python 里按字节位置切出来。

> **halt 时序与读数落盘（真机易踩）**：
> - `halt` 后**务必 `wait_halt`** 再 `mdw`：刚发 `halt` 时目标可能还在跑，立刻读 SRAM 会报
>   `Failed to read memory`；`wait_halt` 等真正停稳再读即正常。
> - 用 `-c "init; ...; mdw ...; shutdown"` 一次性脚本时，**成功 mdw 行在 `shutdown` 前常被缓冲丢弃**
>   （只透出错误/PC 行）。稳法：把输出 `> ocd.log 2>&1` 落盘再 `grep`，或先把 mdw 做完、最后单独 `shutdown`。

```
# 1) 取符号地址（不同构建地址不同，不要硬编码）
arm-none-eabi-nm build/xxx.elf | findstr s_data
# 2) OpenOCD 烧录并 boot 后 halt，按 4 字节对齐整字读
openocd -s <scripts> -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c init -c "reset halt" \
  -c "flash write_image erase build/xxx.elf" -c "verify_image build/xxx.elf" \
  -c "reset run" -c "sleep 15000" -c "halt" \
  -c "mdw 0x20001000" -c "mdw 0x20001004" ...
```

Python 侧切字节（参考 `verify_sensors.py` / `verify_log_switch.py`）：

```python
import re, subprocess
def read_struct(elf, addr, size):
    words = range(addr & ~3, (addr + size + 3) & ~3, 4)
    cmds = ["init", "reset halt", "flash write_image erase %s" % elf,
            "verify_image %s" % elf, "reset run", "sleep 15000", "halt"]
    for w in words: cmds.append("mdw 0x%08X" % w)
    cmds.append("shutdown")
    p = subprocess.run([OCD, "-s", SCR, "-f", "interface/stlink.cfg",
                        "-f", "target/stm32f4x.cfg"] + sum((["-c", c] for c in cmds), []),
                       capture_output=True, text=True, timeout=300)
    vals = {}
    for line in (p.stdout + p.stderr).splitlines():
        m = re.match(r"\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$", line)
        if m: vals[int(m.group(1), 16)] = int(m.group(2), 16)
    raw = bytearray()
    for a in range(addr, addr + size):
        raw.append((vals[a & ~3] >> (8 * (a & 3))) & 0xFF)
    return bytes(raw)
```

## 四、"没报错 ≠ 有数据"——正向验证模式

串口日志只在失败时打印、且失败还被限流，所以"没有 FAILED"不等于"有数据"。
直接读目标内存里的采样结构，拿到物理量（lux、g、dps、uT）确认量纲合理。

`verify_sensors.py` 判据（真机）：
- `s_data` 结构用 `arm-none-eabi-nm` 取址，`LAYOUT` 列出每个字段的偏移与 `<` 格式。
- AP3216C `ap3216_ok==1`；MPU9250 `mpu_ok==1`；`samples>0`；`errors==0`。
- 静止时加速度模长 `|a| ∈ [0.5, 1.5] g`（板平放应≈1.00 g）。
- AK8963 `WIA`（即 `g_mag_id`）读 `0x00` → 本模块**未装配可用磁力计**（硬件事实，
  非故障），页面显示「AK8963 未装配」，**绝不把 0.0 uT 当成功上报**；读到 `0x48` 才是有磁力计。

## 五、EXTI_SWIER 软件中断注入——免手指验证中断链路

`EXTI->SWIER`（F4 上 `0x40013C10`）写 1 产生**与引脚边沿完全等价**的中断请求。
验证 T_PEN (PH7, line 7) 链路：`EXTI -> NVIC -> HAL_EXTI_IRQHandler -> 回调 ->
xSemaphoreGiveFromISR -> touch_task 被唤醒`。

```bash
openocd ... -c "reset run" -c "sleep 14000" -c "halt" \
  -c "mww 0x40013C00 0x80"   /* IMR 强制使能 line 7（ISR 自屏蔽时需先开）*/ \
  -c "mww 0x40013C10 0x80"   /* SWIER 注入 line 7 */ \
  -c "resume" -c "sleep 4000" -c shutdown
```

> ⚠️ `verify_touch_irq.py` 的特殊点：touch ISR 在入口**自屏蔽** EXTI line 7（防噪声），
> 所以注入前必须先用 `mww EXTI_IMR 0x80` 把 mask 位打开，否则软件中断被丢掉。
> 这条验证的是**链路通断**，不验证引脚电平——仍需手指点一次看 `[TOUCH] raw=` 行。

回读 `SYSCFG->EXTICR[1]`（= `EXTICR2`）：`0x00007000` 表示 line 7 已复用到 GPIOH（PH7）。

## 六、中断风暴定位与防护（速查）

> **完整配方（三层防护表 + ISR 重武装代码 + 速率看门狗）的唯一主副本在
> `stm32-peripheral-drivers/references/lan8720a-rmii.md`。** 本节只留"怎么认出它"。

**现象指纹**：中断计数几秒内爬到几万；ISR 抢占低优先级的轮询式 I2C 传输 → HAL 超时 →
从机拉住 SDA → I2C 总线 BUSY 锁死，之后每次读都失败。
**典型根因**：中断引脚配成浮空（`GPIO_NOPULL`），且紧邻一条高速翻转的信号线
（如位绑定 I2C 的 SCL），串扰耦合出边沿。

**定位手段**：给该中断加 **1s 窗口速率计数**，把"风暴"变成 `N/s` 的数字再判断——
这是这类问题最划算的诊断投入。

## 七、I2C 总线锁死恢复（速查）

> **配方（`BSP_I2C_Recover()` 六步 + 调用方防护）唯一主副本在
> `stm32-peripheral-drivers/references/lan8720a-rmii.md`。**

要点：HAL 超时后从机仍拉住 SDA、外设锁在 BUSY；**不恢复则一次失败变永久失败**。
判据是 SDA 低 / BUSY / AF-ARLO-BERR **任一**成立才动手；SCL 改推挽翻转 ≥9 次释放从机
→ 发 STOP → `HAL_I2C_DeInit + Init` 清锁存标志 → 还原 AF_OD。

## 八、**读内存前先证明"你读的是谁的内存"**（本 skill 最重要的一条）

**症状**：dump 出 `SSD1306_Buffer`，看到 page0 有内容、page1~7 全空、文字在行中间断掉
→ 结论"OLED 只渲染了第一行"。**这个结论是假的。**

**根因**：板上跑的是 **Debug** 构建，而脚本的符号来自 **Release** ELF。
两个构建的 `.bss` 布局不同，**偏移 8 字节**：

| 符号 | Debug | Release |
|---|---|---|
| `SSD1306`（结构体） | 0x20000228 | 0x20000230 |
| `SSD1306_Buffer` | 0x20000230 | 0x20000238 |

按 Release 的 `0x20000238` 去读 Debug 固件 = 从缓冲区中间开始读 → 结构体头 6 字节
和 page0 前 122 字节拼在一起 → "文字断掉 + 后面全空"。**一个看起来极其合理的假象。**

**规矩**：任何"读内存 → 下结论"的动作之前，先跑：
```bash
openocd -f openocd.cfg -c "init" -c "halt" \
  -c "verify_image D:/<绝对路径>/build/Debug/xxx.bin 0x08000000" \
  -c "resume" -c "shutdown" 2>&1 | grep -E "^diff|checksum"
```
- 输出 `Error: checksum mismatch - attempting binary compare` +
  `diff 0 address 0x08000004. Was 0x29 instead of 0x0d` 就是铁证。
- **差异从 `0x08000004`（复位向量）就开始** = 板上固件与你以为的不是同一份。
- ⚠️ `verify_image` / `dump_image` 的路径必须给 **Windows 绝对路径 + 正斜杠**（`D:/...`）：
  MSYS 风格的 `/d/...` OpenOCD 打不开（会报 `couldn't open`），
  而相对路径会被它写到谁也找不到的地方（Tcl 解释器有自己的 CWD）。

**判据不要靠看图猜**：把期望值在 Python 里**复现**（从源码解析字库表 + 照抄
`snprintf` 格式串 + 同款位语义），逐字节比对，输出差异的**页/列/坐标**。
本次这样定位到"唯一差异是 row4 里 ADC 原始码的低 2 位十六进制" →
一眼判定是采样抖动而非渲染错误（还反证了刷新是活的）。
注意 ASCII 点阵图人眼极易误判（本次就误读了一次）。

## 九、HardFault 现场解码配方（从"跑飞了"收敛到某条指令）

```bash
openocd -f openocd.cfg -c "init" -c "halt" \
  -c "reg pc" -c "reg lr" -c "reg sp" \
  -c "mdw 0xE000EDF0 1"   # DHCSR : bit1 C_HALT / bit17 S_HALT / bit19 S_LOCKUP
  -c "mdw 0xE000ED04 1"   # ICSR  : VECTACTIVE(bits 8:0) = 当前异常号（3 = HardFault）
  -c "mdw 0xE000ED28 1"   # CFSR  : BFSR(bits15:8) bit7 PRECISERR + bit1 BFARVALID
  -c "mdw 0xE000ED2C 1"   # HFSR  : bit30 FORCED（说明被升级成 HardFault）
  -c "mdw 0xE000ED38 1"   # BFAR  : 出错访问地址
  -c "mdw <MSP> 8"        # 异常压栈帧
  -c "shutdown"
```

- **异常压栈帧**（handler 未再压栈时就在 MSP）：
  `+0x00 R0 +0x04 R1 +0x08 R2 +0x0C R3 +0x10 R12 +0x14 LR +0x18 PC +0x1C xPSR`
  → `PC` 才是**出错指令**，`xPSR` 的 bit24(T) 应为 1。
- **一定要用 objdump 核对，不要用 `nm` 的"最近符号"推断**：
```bash
arm-none-eabi-objdump -d --start-address=0x08001d38 --stop-address=0x08001d60 xxx.elf
```
  本次 `PC=0x08001d48` 处的指令是 `ldr r3, [r0, #8]`（`HAL_GPIO_ReadPin` 读
  `GPIOx->IDR`），而 **`BFAR − R0 = 8` 正好等于该结构体偏移** →
  一句话证明"R0 是被当成 GPIO 端口用的野指针"，无需猜测。
- ⚠️ **`nm` 的 weak 别名会骗人**：一堆 `*_IRQHandler` 全指到 `Default_Handler` 同一
  地址，`nm | awk '$1<=PC'` 会返回 `WWDG_IRQHandler` 之类毫不相关的符号。
  看到 `PC` 落在 `Default_Handler` 上，就去解压栈帧反推真正出错点。
- 常见组合判据：
  | 现象 | 含义 |
  |---|---|
  | `uwTick` 不涨 + `S_HALT=1` | 被调试器停住（不是固件 bug），`resume` 即可 |
  | `uwTick` 不涨 + `S_HALT=0` + ICSR VECTACTIVE≠0 | 卡在某个异常（多为 HardFault） |
  | CFSR 全 0 + VECTACTIVE=0 但 `uwTick` 不涨 | 卡在普通 `while(1)` / 关中断临界区 |

## 十、"固件是否在跑"必须先判定，不要猜

本环境 `halt` / `resume` 不可靠：`init` 后直接 `resume` 会报
`Error: [xxx.cpu] not halted / context restore failed, aborting resume`；
而未真正停稳时 `mdw` 可能读到"看起来合理"的活跃内存，导致误判。
所以 `halt` 之后建议配 `wait_halt`，并且**判活要用双采样**：

```bash
openocd -f openocd.cfg -c "init" -c "halt" -c "mdw <uwTick> 1" \
  -c "resume" -c "sleep 500" -c "halt" -c "mdw <uwTick> 1" -c "shutdown"
```
Δ=0 → 卡死，配合 §九 判是否 HardFault。

**验收脚本要把这一步做成前置判定**：否则"uptime 不推进 / LED 不翻转 / 传输计数不增"
会一起 FAIL，三个红叉把真正的根因（一次 HardFault）盖住。
正确做法是：判死不通过时**直接打印故障现场**，并**跳过**所有运行时段判据。

## 十一、无相机 / 无示波器的量化验证（bit-bang 外设）

GPIO 软件 SPI / 自研 bit-bang 外设"看不到波形"时，用**计数器 + 增量**替代示波器：

```c
/* 非 static，否则 nm 看不到符号；volatile 防被优化掉 */
volatile uint32_t g_xfer_bytes, g_cmd_bytes, g_data_bytes;
```

判据（SSD1306 OLED 真机实测值）：
- `xfer == cmd + data` **严格相等**（允许 ≤1 的差，因为 halt 可能停在两个自增之间）。
- **必须采两次看增量**：`Δdata % 1024 == 0` → 每帧 8 页 × 128 字节整帧发完。
  只看绝对值会被"halt 停在半帧中间"误判成"刷新被截断"。
- `Δcmd == 24 × Δ帧数`（每页 3 条寻址命令：`0xB0+i` / 列低 / 列高）。
- `初始化开销 = cmd − 24×帧数` 应为常量，且**要与 `Init()` 里逐条数出来的命令数相等**
  （本次数出 28、实测 28 → 逐条命令都真的发出去了）。
- **交叉验证（很划算）**：末次 `WriteData` 结束时 SDA 停在最后一位，
  所以 `GPIOB->ODR` 的该位应 == `Buffer[1023] & 1`；
  又若 `init` 时把 SCK 置低、事后读回高，则**反向证明 bit-bang 确实执行过**。

## 十二、`.data` 真值三处对照法（怀疑"RAM 被写坏"时）

同时取三处内容对照，可一刀切开"Flash 初值错"还是"搬运/运行期被改写"：

```bash
arm-none-eabi-objdump -h xxx.elf | grep -E "Idx|\.text|\.data|\.bss"  # VMA / LMA / size
arm-none-eabi-nm xxx.elf | grep -E "_sidata|_sdata|_edata|_sbss|_ebss"
arm-none-eabi-objdump -s -j .data xxx.elf | head    # ELF 里的初值
# 板上：Flash @LMA 处内容  与  RAM @VMA 处内容
```
- 三处一致 → 搬运没问题，问题在运行期。
- 若**只有 RAM 与另两处不同**，再指向"运行期被改写"。
- 若 Flash@LMA 与 ELF 初值不同 → 段的 LMA 摆放 / 镜像生成有问题
  （此时先看 `objcopy -O binary` 出来的 `.bin` 长度是否覆盖到 `.data` 末尾）。

> ⚠️ 本次踩坑：`halt` 未真正停稳时两次读到的 RAM 内容不一样，差点得出
> "`.data` 搬运被写坏"的错误结论。**RAM 内容在两次读之间变化，先怀疑读取
> 时刻/对象不对，而不是立刻下"被改写"的结论。**

## 十三、`reset halt` 的陷阱 + 疑似"内存被写坏"的三步定性

### 13.1 `reset halt` 停在**复位向量**，`.data` 还没搬运

```bash
openocd -f openocd.cfg -c "init" -c "reset halt" -c "mdw <VMA> 12" ...   # 读到的是【上次运行的残留】
openocd ... -c "reset halt" -c "resume" -c "sleep 3000" -c "halt" -c "mdw <VMA> 12"   # 这才是搬运后的真值
```

**本次实测**：`reset halt` 立即读 → 一片垃圾；`resume` 3 s 后读 → 完全正确。
差一步就会误判成"启动搬运把 RAM 写坏了"。**判定搬运问题必须带 `resume` 等待。**

### 13.2 指纹搜索：异常字节是不是"代码被拷进来"

把异常字节（`mdw` 输出按**小端**转成字节流）当指纹，在 `.bin` 里多位移搜多窗口长度：

```python
raw = b''.join(struct.pack('<I', w) for w in words)     # mdw 值 -> 字节流
for n in (32, 24, 16, 12, 8):
    for shift in range(4):
        if (i := data.find(raw[shift:shift+n])) >= 0: print(n, shift, hex(0x08000000+i))
```
- **命中** → 某段 Flash 被拷到了 RAM（查 memcpy/搬运循环/段摆放）。
- **全 0 命中** → 内容是**运行期生成的**，别再往"拷贝错"方向查（本次即如此）。
- 顺带一句判据：**多次崩溃的垃圾字节几乎逐字节相同（个别字节差异）⇒ 确定性写入**，
  不是随机噪声/失控执行；反之若每次都不同，先怀疑供电/EMI 导致的跑飞。

### 13.3 硬件写监视点：**选址决定成败**，以及"0 命中"意味着什么

```bash
# boot 之后再设，否则会被 .data 拷贝本身触发（必须先 resume 跳过启动）
openocd -f openocd.cfg -c "init" -c "reset halt" -c "resume" -c "sleep 3000" -c "halt" \
  -c "wp <addr0> 4 w" -c "wp <addr1> 4 w" ... \
  -c "resume" -c "sleep 600000" -c "halt" \
  -c "reg pc" -c "reg lr" -c "reg sp" -c "mdw <VMA> 24" \
  -c "mdw 0xE000ED04 1" -c "mdw 0xE000ED28 1" -c "shutdown"
```

**⚠️ 铁律：只能压「只在启动时写一次」的字段。**

本次真实翻车：我把监视点撒在 `0x20000000 / 0x18 / 0x2C / 0x44`（想均匀覆盖 48 字节的
`s_keys`）。结果 `resume` 后**立刻"命中"**，PC 落在 `bsp_key_scan` 内：

```
8001846: strb r0, [r5, #6]   ; s_keys[i].last
800184a: strb r2, [r5, #8]   ; s_keys[i].count   ← 0x2000002C 正好压在这个字段上
800184c: b.n 800182a
```

`.last/.count` 是**每次扫描都可能写**的去抖状态字段 → 假命中。更糟的是命中后 OpenOCD 去读
DWT 数据地址，常常失败并刷一片 `Fail reading CTRL/STAT register`，**整个长跑窗口作废**，
最后读到"`.data` 正确、CFSR=0"还会让人误判成"没崩"。

**正确做法**：先反汇编/看结构体，找出**只初始化一次**的字段（本例是 `s_keys[i].port/.pin`，
只有 `bsp_key_init()` 写），4 个监视点精确压在 4 个 `.port` 字上，正好用满 Cortex-M3 的
4 个 DWT 比较器。**撒点式"覆盖整段"是错的。**

其它要点：
- 长度只能 1/2/4 字节，无 mask；命中后目标自动停住，`reg pc/lr` 就是写入者现场 → `objdump` 核对。
- **必须同时读满整段**（如整个 `.data` 0xA4 字节）——只读开头 48 字节**得不到跨度**，
  而"从哪个绝对地址开始、覆盖多少字节"往往就是定位写入者的关键线索。
- **跑几倍于历史崩溃时间仍 0 命中 ⇒ 问题间歇性**：别急着下"没有这个 bug"，
  写进未决项并注明已排除项（本次 240 s 0 命中，而历史上 31 s / 50 s / 154 s 都崩过）。

### 13.4 长跑必须用**常驻单会话**，且要能区分"没崩 / 崩了没看出来 / 被复位了"

每轮重开一次 openocd 会**丢监视点**，还更慢、更容易撞上 USB 占用。正确姿势是常驻一个
openocd 进程、用 stdin 喂命令，用 `echo <唯一标记>` 做输出同步（等到标记出现即表示该批命令跑完）：

```python
proc = subprocess.Popen([OPENOCD, "-f", cfg], stdin=PIPE, stdout=PIPE,
                        stderr=STDOUT, text=True, bufsize=1)
# 起线程按行收 stdout；cmd() = 写命令 + 写 "echo MARK" + 等 MARK 出现，返回其间输出
```

每轮采样这 5 类判据，缺一类就会误判：

| 判据 | 读什么 | 说明 |
|---|---|---|
| 监视点命中 | halt 输出含 `due to watchpoint` | 命中即拿到了写入者 PC |
| 故障 | `CFSR` != 0 | 总线/用法故障（配合 `HFSR`/`BFAR`） |
| 内存被改写 | 目标变量 vs **ELF 初值** | 期望值取自 `objcopy --only-section=.data`，别在脚本里复写数字 |
| **被复位** | `RCC_CSR` 新增标志位 | bit26 PINRSTF / bit27 PORRSTF / bit28 SFTRSTF / bit29 IWDGRSTF / bit30 WWDGRSTF / bit31 LPWRRSTF（bit24 是清除位） |
| **被复位（兜底）** | `uwTick` 回退 | 计数器倒回去 = 目标重启过。**"内存被写坏"和"掉电/看门狗复位后跑了新的一轮"现象极像，必须分开** |

> **本项目成品**：`tools/soak_watch.py`（4 个 `.port` 监视点 + 上表 5 类判据，
> 启动后先自检"`.data` == ELF 初值"不符即退出，命中即存 `build/soak_capture.txt`）。
>
> **顺带一个静默坑**：`objdump -h` 的列序是 `Idx Name **Size VMA LMA** FileOff Algn`。
> 按 `VMA/LMA/Size` 取会得到 `.data VMA=0x0000008C` 这种荒谬值 —— 加一条
> "VMA 必须落在 RAM 区间"的自检即可当场拦住。

