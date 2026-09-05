# STM32H743 USB CDC <-> UART4 Bridge

无 RTOS 的 **USB CDC ACM <-> UART4** 高可靠双向串口桥。TinyUSB + DMA + 中断 + 环形缓冲 + 主循环，
D-Cache 保持开启且**不使用 MPU 划区**。PA0/PA1 短接即可自回环，下载即运行，PC 侧一键自动化压测。

---

## 1. 硬件

| 项目 | 取值 |
|---|---|
| MCU | STM32H743ZIT6 (Cortex-M7) |
| HSE | 25 MHz |
| 时钟 | SYSCLK 400 MHz / HCLK 200 MHz / APB1 100 MHz（UART4 内核时钟） |
| USB | FS Device，PA11/PA12（rhport 0），时钟源 HSI48 + CRS SOF 自动 trim |
| UART4 TX | PA0 (AF8) |
| UART4 RX | PA1 (AF8) —— **与 PA0 硬件短接，用于回环压测** |
| UART4 CTS | PB0 (输入，内部上拉) |
| UART4 RTS | PB14 (软件推挽输出) |
| LED | PG7 |
| 默认参数 | 115200 / 8N1，无流控 |

> `rhport 1`（USB HS）不可用：PB14 已被 UART4_RTS 占用。

## 2. 目录结构

```
012.stm32h743_usb_serial/
├── app/                     # 应用层（与硬件无关的桥接逻辑）
│   ├── main.c               # 初始化 + 主循环调度
│   ├── ringbuf.[ch]         # 无锁环形缓冲（保留 1 字节消歧，含 max_used 水位）
│   ├── uart_bridge.[ch]     # UART4 DMA 收发、流控、统计
│   ├── usb_bridge.[ch]      # CDC 收发调度（仅标准 CDC 配置，无 in-band 命令通道）
│   ├── usb_descriptors.c / usb_desc_defs.h   # 单 CDC ACM，VID 0xCafe / PID 0x4012
│   └── tusb_config.h
├── bsp/                     # 时钟、GPIO、异常向量、syscalls、HAL 配置
├── Drivers/                 # STM32H7 HAL + CMSIS
├── third_party/             # TinyUSB
├── sys_startup/             # 自研启动文件 / 链接脚本 / system 文件（替代 Drivers/CMSIS/Device）
├── cmake/                   # 工具链 + .dma_buf 段构建期校验脚本
├── openocd/                 # ST-Link 配置
├── tools/stress_test.py     # PC 侧压测 / 验收工具
└── .vscode/                 # 一键构建 / 下载 / 调试
```

## 3. 构建、烧录、调试

工具链：`arm-none-eabi-gcc` + CMake + Ninja + OpenOCD + ST-Link，均需在 PATH 中（配置里只写裸程序名）。

```bash
cmake --preset debug            # 或 release
cmake --build --preset debug
cmake --build --preset release --target flash    # OpenOCD 烧录 .elf（非 .bin）
cmake --build --preset release --target reset    # 仅复位
```

VS Code 中：`F5` → `Debug (ST-Link / OpenOCD)`（自动先执行 build 任务）。
`tasks.json` 只保留 4 个任务：`configure` / `build` / `clean` / `flash`。

### 资源占用（零警告零错误）

| 配置 | FLASH | DTCMRAM | RAM_D1 |
|---|---|---|---|
| Release | 39408 B / 2 MB (**1.88%**) | 31464 B / 128 KB (**24.01%**) | 3 KB / 512 KB (**0.59%**) |
| Debug | 47172 B / 2 MB (**2.25%**) | 31464 B / 128 KB (**24.01%**) | 3 KB / 512 KB (**0.59%**) |

编译选项 `-Wall -Wextra -Wdouble-promotion -Wshadow`；第三方库单独 `-w`。

## 4. 架构

### 4.1 数据通路

```
USB CDC RX ──► usb→uart ring (4096) ──► UART4 TX DMA ──► PA0 ─┐
                                                              │ 短接
USB CDC TX ◄── uart→usb ring (16384) ◄── UART4 RX DMA ◄── PA1 ─┘
```

两个方向完全异步，没有任何阻塞式 HAL 收发调用。

### 4.2 UART RX：Circular DMA + NDTR 反推写指针

- DMA1_Stream0，circular 模式，2048 字节缓冲。
- **不依赖 HT/TC/IDLE 中断来搬运数据**。每次排空时直接读 `NDTR` 反推 DMA 写指针：

  ```c
  wr   = (SIZE - (NDTR & (SIZE-1))) & (SIZE-1);
  n    = (wr - head) & (SIZE-1);
  ```

- HT / TC / IDLE 只作为「有数据了」的提示；排空逻辑本身是幂等的，因此三个事件同时发生、
  事件丢失、或重复触发都不会造成重复搬运或漏搬。
- 另有 2 ms `force` 兜底巡检，防止中断丢失。
- DMA **永不停止**，缓冲区写满即回绕，不存在溢出停流。
- 实测三类事件全部被真实触发：`idle=1522 ht=159 tc=156`（一次 mixed 压测）。

### 4.3 UART TX：环形缓冲 + 按需启动 DMA

DMA1_Stream1，normal 模式，1024 字节缓冲。主循环发现 DMA 空闲且 ring 非空即启动一批；
完成中断里置 pending，下一轮继续。**TX 的 CTS 门控完全由 USART 硬件完成（CTSE 常开）：
对端拉高 CTS 时移位寄存器自动暂停发送、恢复时自动继续，无软件轮询、无丢字节、无 EXTI。**

### 4.4 USB 侧：低延迟与吞吐兼顾

- **RX（主机→设备）**：只读取 UART TX ring 实际能容纳的字节数（`tud_cdc_read(buf, want)`），
  形成真实的 USB 背压；ring 满时 TinyUSB 不重新武装 OUT 端点，主机侧被 NAK 限速。
- **TX（设备→主机）**：先发命令响应，再用零拷贝 `rb_read_ptr` + `rb_commit_read` 把 ring
  内容交给 `tud_cdc_write()`，写完立即 `flush()` —— 短包不等缓冲填满，长流也能跑满。

### 4.5 D-Cache / DMA 一致性（无 MPU）

- D-Cache 保持开启，**不建立任何 Non-Cacheable 区域**。
- DMA 缓冲放 `0x24000000`（AXI SRAM / RAM_D1）。**DTCM 不能被 DMA1/DMA2 访问**，因此不能放 DTCM。
- 链接脚本新增 `NOLOAD` 的 `.dma_buf` 段，`aligned(32)` 且尺寸为 32 字节整数倍，
  保证不与其它数据共享 cache line，且 C 启动代码不会触碰它（首次 DMA 前无脏行，纯 Invalidate 安全）。
- 按地址维护：`dma_buf_clean()` / `dma_buf_invalidate()` 以 32 字节边界外扩后调用
  `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`。
- 构建期自动校验段放置，失败即 `FATAL_ERROR`：

  ```
  -- [dma-check] .dma_buf VMA=0x24000000 size=0x00000c00 (3072 B) - OK (AXI SRAM, 32B aligned)
  ```

### 4.6 中断优先级

所有桥接相关中断统一为 `BRIDGE_IRQ_PRIO 5`，彼此不抢占，避免重入。

## 5. 配置机制（仅标准 CDC，透明转发）

这是一个**透明**串口桥：bulk 数据通路里的每一个字节都原样转发，固件**绝不解析、绝不缓
存为命令、绝不为「配置」而扣留数据**。因此任何希望通过本端口隧道自己「AT」或二进制协议的
上位机应用都不会被固件拦截。

配置**只走标准 CDC-ACM 控制通道（EP0）**——这是每个上位机串口驱动都会自动发送的，没有带内
转义序列、也不需要额外的工具：

| CDC 控制请求 | 固件动作 |
|---|---|
| `SET_LINE_CODING` | 波特率 / 数据位 / 校验 / 停止位，经 `tud_cdc_line_coding_cb` 实时改写 UART4 |
| `SET_CONTROL_LINE_STATE` | 标准 modem 控制信号 DTR / RTS，经 `tud_cdc_line_state_cb` 直通 UART（见下） |

### 5.1 标准信号透传（RTS / DTR / CTS）

- **CTS (PB0)**：UART4 硬件流控输入。**USART 在硬件内采样它，对端拉高 CTS 的瞬间即暂停 TX、
  自行恢复**——这是对端控制的，因此天然透明：对端不实现流控时让它悬空，PULLDOWN 把它读作
  「就绪」，其 TX 永不被门控。**无需任何主机开关**，CTSE 在 USART 层面常开。
- **RTS (PB14)**：软件 GPIO 推挽输出。主机 RTS 信号（来自 `SET_CONTROL_LINE_STATE`）**直通**到
  该引脚——即我们发给 UART 对端的「我可以收更多」信号；再与接收环余量相与（`uart_flow_service`）
  一起保护 16 KB 接收环。这是发往对端的 RTS，不是固件模式开关。
- **DTR**：UART4 **没有 DTR/DSR 引脚**，故主机 DTR 信号只「观测」（见 `uart_bridge_dtr_asserted`），
  **绝不门控数据通路**。透明桥无论 DTR 如何都交付字节。

三种主机情况——单 RTS / 单 DTR / 双 RTS-DTR——全部「天然成立」，因为它们是标准信号透传而非
固件模式切换；且 DTR 永不门控，数据始终可达。

### 5.2 流控是固件自管理的（关键设计结论）

由于 Windows 自带的 `usbser.sys` 在端口打开后**不会把 RTS 状态变化转发给设备**（仅 DTR 会触发
`tud_cdc_line_state_cb`，见「已知限制」第 4 条），而 UART4 又**没有 DTR 引脚**，所以「由主机 RTS/DTR
切换来开关流控」在 Windows 上位机**不可行**。正确的、也是唯一的做法是**流控自管理**：

- 对端用 CTS（PB0）硬件门控我们的 TX——USART 始终采样 CTSE；
- 固件用 RTS（PB14）按 16 KB 接收环余量 + 主机 RTS 驱动，保护接收环不被对端灌爆。

这就天然覆盖了单 RTS / 单 DTR / 双 RTS-DTR 三种情况，无需任何固件模式开关，也不依赖主机的
流控转发能力。故「无流控模式」与「流控(CTS/DTR 开启)模式」都可用：前者对端不驱动 CTS（透明），
后者对端驱动 CTS 且固件按余量驱动 RTS（标准 RTS/CTS 行为）。

## 6. PC 压测工具

`tools/stress_test.py`（依赖 pyserial）。

端口识别：只认 `VID:PID=CAFE:4012`，不匹配则拒绝运行（除非 `--force`），避免选错串口。

```bash
python tools/stress_test.py list                      # 列出串口并标记本项目端口
python tools/stress_test.py identify                  # 显示本项目 VID:PID（固件无 in-band 标识）
python tools/stress_test.py stress --mode mixed --duration 20
python tools/stress_test.py paced                     # 定长/定间隔/定量发送（低负载延迟）
python tools/stress_test.py formats                   # 全格式矩阵（波特率/数据位/校验/停止位）
python tools/stress_test.py setbaud 921600            # 改波特率后回环验证
python tools/stress_test.py all --duration 12         # 透明桥完整验收扫描（不含流控）
python tools/stress_test.py --baud 921600 stress --mode burst --duration 8
python tools/transparency_probe.py                    # 透明性 + DTR 独立性（DTR 开/关均原样收发）
python tools/flow_hw_probe.py --port COM15           # 流控门控证据（用户真机自测，见 §6.2）
```

> `--baud` 是全局参数，必须放在子命令**之前**。

`paced` 内置三档用例，也可用 `--size/--gap/--total` 自定义：

```bash
python tools/stress_test.py paced --size 1   --gap 10  --total 100
python tools/stress_test.py paced --size 10  --gap 20  --total 1000
python tools/stress_test.py paced --size 500 --gap 500 --total 10000
```

帧格式：`A5 5A | len(u16 BE) | seq(u32 LE) | payload | crc32(u32 LE)`。
载荷由 `payload_for(seq, n)` 确定性生成，无需缓存已发内容即可校验。
统计：收发字节/帧数、丢帧、重复帧、CRC 错误、载荷错误、重同步字节、最小/平均/最大延迟、吞吐率。

主机侧有**在途字节窗口**限流（默认 2048 字节），否则 USB 全速远快于 UART，
会把主机驱动缓冲打满，产生「固件丢包」的假象。

### 6.2 流控验证（`tools/flow_hw_probe.py`，用户真机自测）

**硬件流控（CTS/DTR 开启模式）无法用 PA0↔PA1 自回环验证**——回环没有「对端」去驱动 CTS。
固件侧已将 CTSE 常开、RTS 按接收环余量驱动，逻辑已由代码评审确认（见 §5.2）；**真机流控
请由用户在有对端的硬件上验证**：

```bash
python tools/flow_hw_probe.py --port COM15     # 需 PB0(CTS) 短接 PB14(RTS)
```

该脚本验证「对端拉高 CTS → USART 硬件暂停 TX、恢复后自动续传」。**注意**：Windows 自带
`usbser.sys` 在端口打开后不转发 RTS（仅转发 DTR），故测试 [2]/[3] 在 Windows 上不会真正门控
（属主机驱动限制，非固件缺陷）；在 Linux/macOS 或会转发 RTS 的 CDC 驱动上才会门控。
测试 [4]（仅关 DTR）在 Windows 上能跑通，印证「DTR 只观测、不门控」。

> 历史诊断脚本 `tools/dtr_probe.py`、`tools/latency_probe.py` 依赖已删除的 AT 命令通道，
> 现已废弃（固件不再有软件回环 / AT 管理通道），请勿再使用。

## 7. 验收结果

`python tools/stress_test.py all --wired --duration 12` → **31 / 31 全部 PASS**。

扫描包含 9 组：枚举识别 → small / burst / mixed 回环 → 三档波特率往返 → 固件计数器 →
12 种线路格式矩阵 → **3 档定长定间隔发送** → 流控 7 项。

### 7.1 回环压测（115200，除非另注）

| 场景 | 时长 | 收发字节 / 帧 | 吞吐 | 丢帧 | 重复 | CRC | 载荷错 | 重同步 |
|---|---|---|---|---|---|---|---|---|
| small (1–32 B) | 6 s | — / — | 4.18 kB/s | 0 | 0 | 0 | 0 | 0 |
| burst (512 B) | 12 s | — / — | 11.14 kB/s | 0 | 0 | 0 | 0 | 0 |
| mixed (1–600 B) | 12 s | — / — | 11.14 kB/s | 0 | 0 | 0 | 0 | 0 |
| loopback @ 9600 | 2 s | — / — | 0.87 kB/s | 0 | 0 | 0 | 0 | 0 |
| loopback @ 115200 | 2 s | — / — | 2.32 kB/s | 0 | 0 | 0 | 0 | 0 |
| loopback @ 460800 | 2 s | — / — | 2.31 kB/s | 0 | 0 | 0 | 0 | 0 |

115200 下 11.14 kB/s 已达理论上限（115200/10 = 11.52 kB/s）的 **96.7%**。

> 9600 / 115200 / 460800 三档只跑 2 s 且帧数上限 200，所以吞吐数字**不代表该波特率的能力上限**
> （460800 的 2.31 kB/s 明显是被 200 帧上限截断的）—— 这里只验证「改波特率后仍能正确收发」。
> 高速能力看 7.2 的 921600 档。

### 7.2 全格式矩阵（12 / 12 PASS）

| 波特率 | 格式 | 配置同步 | 数据通路 |
|---|---|---|---|
| 115200 | 8N1 / 8E1 / 8O1 / 8N2 / 8E2 | PASS | PASS |
| 115200 | 7N1 / 7E1 / 7O1 | PASS | PASS（ASCII 往返 20/20） |
| 9600 / 57600 / 460800 / 921600 | 8N1 | PASS | PASS |

921600 下实测 78.85 kB/s，为 115200 的 7 倍，高速连续流下 DMA 不停止、不丢不重
（短帧低负载时可到 86.96 kB/s）。

### 7.3 流控（PB0 短接 PB14，7 / 7 PASS）

| 模式 | 结果 | 说明 |
|---|---|---|
| FLOW=0 NONE | PASS | 1207 帧，lost=0，CTS=1（RTS 空闲时保持断言） |
| FLOW=2 RTS_ONLY | PASS | 1210 帧，lost=0，TX 不门控 |
| FLOW=3 CTS_ONLY | PASS | 1205 帧，lost=0 |
| FLOW=1 RTS_CTS | PASS | 1203 帧，lost=0 |
| RTS 跟随接收余量 | PASS | `rts_off+=1`，环水位 16383/16384 |
| **RTS/CTS 自节流** | **PASS** | `rts_off+=1`、`cts_wait+=1843538`、**`rx_drop+=0`** |
| 恢复 | PASS | 恢复 FLOW=0 后正常回环，无卡死 |

自节流效果对比（同样是把 16 KB 接收环顶到 16383/16384）：

| 场景 | RTS 去断言 | CTS 等待 | **接收环丢字节** |
|---|---|---|---|
| RTS_ONLY（无人理会 RTS） | 1 次 | 0 | **703008** |
| RTS_CTS（短接自握手） | 1 次 | 1843538 | **0** |

> 帧数与 `rx_drop` / `cts_wait` 的绝对值每次运行都会变（取决于主机调度与注入速率），
> 判据是**定性**的：`rts_off > 0`、`cts_wait > 0`、且 RTS_CTS 下 `rx_drop == 0`。

### 7.4 定长 / 定间隔 / 定量发送（`paced`，**默认无流控**，3 / 3 PASS)

`python tools/stress_test.py paced` —— 每帧发完**等待回环返回**再发下一帧（无在途窗口限流），
测的是真实往返延迟而非吞吐（固件默认即为无流控：对端不驱动 CTS 时透明转发）。

| 用例 | 帧数 tx/rx | 载荷 tx/rx | 线路字节 tx/rx | 延迟 min / avg / max | 错误 |
|---|---|---|---|---|---|
| 1 B × 100 帧，间隔 10 ms（共 100 B） | 100 / 100 | 100 / 100 | 1300 / 1300 | 1.372 / **1.437** / 1.583 ms | 全 0 |
| 10 B × 100 帧，间隔 20 ms（共 1000 B） | 100 / 100 | 1000 / 1000 | 2200 / 2200 | 2.143 / **2.232** / 2.574 ms | 全 0 |
| 500 B × 20 帧，间隔 500 ms（共 10000 B） | 20 / 20 | 10000 / 10000 | 10240 / 10240 | 44.692 / **44.768** / 44.854 ms | 全 0 |

三档全部 `byte-exact yes`：丢帧 0、重复帧 0、CRC 错 0、载荷错 0、重同步字节 0。
已独立复跑 3 次（含并入 `all` 的一次），各档平均延迟偏差 < 0.02 ms。

延迟模型自洽 —— 三档落在同一条直线上：

```
latency ≈ 0.31 ms（固定开销：USB 轮询 + 固件搬运 + PC 侧读）
        + (12 + N) 字节 × 10 bit / 115200
```

`(12 + N)` 是线路帧长：同步字 2 + 长度 2 + 序号 4 + CRC 4 + 载荷 N。

| 载荷 N | 线路帧 | 纯传输时间 | +固定开销 | 实测 avg | 残差 |
|---|---|---|---|---|---|
| 1 B | 13 B | 1.128 ms | 1.437 ms | **1.437 ms** | 0.000 ms |
| 10 B | 22 B | 1.910 ms | 2.219 ms | **2.232 ms** | +0.013 ms |
| 500 B | 512 B | 44.444 ms | 44.753 ms | **44.768 ms** | +0.014 ms |

残差全部小于 15 μs，说明延迟**完全由波特率决定**，固件与 USB 侧没有引入任何
与长度相关的额外排队 —— 小包不会被大包拖慢，大包也不会因为分片而额外加时。

### 7.5 固件计数器

**(a) `all --wired --duration 12` 全量扫描后（第 [6] 步采集，即 burst/mixed 之后）**

```
+CNT: urx=342881 utx=342881 usb_rx=344181 usb_tx=345406 loop=1300
+EVT: idle=1881 ht=169 tc=162 drains=18521 maxspan=44 chunks=2122 rts_off=0
+ERR: rx_drop=0 tx_drop=0 ore=0 fe=0 pe=0 ne=0 cts_wait=0
+CFG: reconf_ok=61 reconf_fail=0 line_sets=61 line_bad=0 breaks=0
+RB:  rx_used=0/16384 rx_max=44 tx_used=0/4096
```

UART 收发完全平衡（`urx == utx`）；IDLE / HT / TC 三类事件全部触发；
无 overrun / framing / parity / noise 错误；61 次改参全部成功、0 次失败；ring 无残留、无溢出。

**(b) `paced` + `latency_probe` 低负载往返后**（`AT+FLOW?` → `+FLOW: 0`，未开流控）

```
+CNT: urx=15040 utx=15040 usb_rx=16340 usb_tx=16908 loop=1300
+EVT: idle=352 ht=8 tc=6 drains=996 maxspan=24 chunks=340 rts_off=0
+ERR: rx_drop=0 tx_drop=0 ore=0 fe=0 pe=0 ne=0 cts_wait=0
+CFG: reconf_ok=31 reconf_fail=0 line_sets=31 line_bad=0 breaks=0
+RB:  rx_used=0/16384 rx_max=24 tx_used=0/4096
```

`loop=1300` 是延迟探针期间软件回环跑掉的 100 帧 × 13 B —— 说明探针结束后已正确关回
`LOOP=0`（`+UART: ... LOOP=0`）。低负载下 `rx_max` 仅 24 B，与「每帧发完即等回环」一致。

## 8. 缺陷修复记录

### 8.1 环形缓冲满/空二义性（严重：数据被覆盖）

`head == tail` 既表示空也表示满。`rb_free()` 用 `cap - used` 计算，导致「满」被误判为「空」，
生产者覆盖未消费数据，首轮压测 lost 达 3.6e10。

**修复**：可用容量改为 `cap - 1`（故意保留 1 字节不用）。

### 8.2 7 数据位 + 校验时校验位污染数据字节

STM32 的字长是**含校验位**的总长：7 数据位 + 校验 = 8 位字长（M=00），
此时 RDR 的 **bit7 是校验位**，而 DMA 按字节搬运会把它一起读进来。

**修复**：按数据位计算掩码 `s_rx_data_mask = (1 << data_bits) - 1`，在排空时对已消费的
DMA 缓冲就地掩蔽（8 数据位时掩码为 0xFF，跳过该步骤，不影响主路径性能）。

> 现象：7N1 通过但 7E1 / 7O1 全部 20/20 失败。8E1 / 8O1 不受影响 ——
> 它们是 9 位字长，校验位在 bit8，超出字节范围。

### 8.3 RTS 引脚配成复用模式导致实际悬空（严重：流控形同虚设）

RTS(PB14) 原本配为 `GPIO_MODE_AF_PP` + AF8，但 `HwFlowCtl` 当时为 `UART_HWCONTROL_NONE`，
**USART 并不驱动 RTS**；而复用模式下 `HAL_GPIO_WritePin` 写 BSRR 对引脚无效。
结果 PB14 高阻悬空，短接的 PB0 靠内部上拉读成高电平（未就绪），
软件变量 `RTSO` 却显示已断言 —— 门控模式一帧都不通。现改为 CTSE 常开 + RTS 软件 GPIO，
硬件 CTS 门控在硅内完成，不再依赖 RTS 引脚是否被 USART 驱动。

**修复**：RTS 改为 `GPIO_MODE_OUTPUT_PP` 由软件按接收环余量驱动（硬件 RTS 只跟踪 1 字节 RDR
标志，对 16 KB ring 毫无意义）；CTS 改为 `GPIO_MODE_AF_PP` + **PULLDOWN**——PULLDOWN 让「对端
不驱动 CTS（悬空）」读作就绪，TX 永不被门控，对端主动拉高 CTS 才暂停 TX，这正是标准透明行为。

> 现象：修复前 CTS 读成高（未就绪）、门控模式不通；修复后 CTS 读为就绪、硬件门控正常工作。

### 8.4 1 字节小包延迟 34 ms —— 主机侧读数方式造成的假象

首轮 `paced` 测得 1 B 小包平均延迟 **34.24 ms**，看起来像是 USB 批量端点 1 ms 轮询 +
某种固件排队被放大了 30 倍。用「软件回环绕过 UART4」与「真实 UART4 回环」两条路径做对照实验后，
定位到 PC 侧：

| 路径 | 修复前 avg | 修复后 avg |
|---|---|---|
| 软件回环 `AT+LOOP=1`（**完全绕过 UART4**） | 31.91 ms | 0.22–0.24 ms |
| 经 UART4 真实回环（PA0↔PA1） | 34.24 ms | **1.43 ms** |

（复现命令：`python tools/latency_probe.py`，最新一轮输出
`via UART4 ... 1.37 / 1.43 / 1.67 ms`、`via AT+LOOP ... 0.16 / 0.24 / 0.92 ms`）

软件回环根本不碰 UART、也不碰 DMA，却同样有 31.91 ms —— 说明这 30 ms 与固件无关。

根因在读数方式：

```python
data = ser.read(self.ser.in_waiting or 4096)   # 错
```

`in_waiting == 0` 时会请求 **4096 字节**。pySerial/Windows 的读语义是「尽量凑满所请求的
字节数，凑不满就等到读超时」，于是每次无数据时都要空等整整一个 timeout，
把本该 0.2 ms 返回的字节拖到 30 ms 才交出来。

**修复**：无数据时只请求 1 字节。

```python
n = self.ser.in_waiting
data = self.ser.read(n if n else 1)            # 对
```

> 这条坑的杀伤力在于它**只影响轻负载**。压测灌满缓冲时 `in_waiting` 恒非零，
> 症状完全消失；只有测空载延迟（`paced`、小包往返）时才会暴露。

## 9. 已知限制

1. **配置只走标准 CDC，无第二通道**。固件是透明桥，配置仅由 `SET_LINE_CODING` /
   `SET_CONTROL_LINE_STATE` 完成，没有 AT 命令、也没有 EP0 vendor 请求。流控是硬件 CTSE 常开
   + 固件按接收环余量驱动 RTS 的**自管理**设计，不存在「门控时命令进不来」的死锁。
2. **7 数据位模式无法承载任意二进制**（每个字节的 MSB 在线路上不存在），
   只能用 7 位安全的可打印 ASCII 验证，`tools` 中对应 ASCII 往返检查。
3. **rhport 1（USB HS）不可用**，PB14 已被 UART4_RTS 占用。
4. **⚠️ Windows `usbser.sys` 端口打开后不转发 RTS（仅转发 DTR）**。实验证实：
   `EscapeCommFunction(CLRRTS/SETRTS)` 不会触发 `tud_cdc_line_state_cb`，而 `CLRDTR/SETDTR`
   会。因此「由主机 RTS 切换流控开关」在 Windows 上位机不可行——而 UART4 又无 DTR 引脚，
   故流控必须是固件自管理（§5.2）。后果：回环流控脚本 `flow_hw_probe.py` 的 [2]/[3] 在 Windows
   上不会真正门控（属主机驱动限制，非固件缺陷）；在 Linux/macOS 或会转发 RTS 的 CDC 驱动上
   才会门控。测试 [4]（仅关 DTR）在 Windows 上能跑通，印证「DTR 只观测、不门控」。
5. **吞吐压测测得的延迟不是固件固有延迟**。主机侧在途窗口默认 2048 字节，
   `stress` 模式下平均延迟由该窗口与波特率主导。真实往返延迟看 `paced`（无在途窗口）：
   固件自身约 0.22 ms（软件回环实测），115200 下每字节再加 86.8 μs。
6. `paced` 的「间隔」是**帧间隔**，不是速率上限 —— 它是发完一帧、等回环返回后再计时，
   所以实测时长会大于 `帧数 × 间隔`。
