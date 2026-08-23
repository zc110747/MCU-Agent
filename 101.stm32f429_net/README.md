# STM32F429 网络工程 — 需求 / 框架 / 开发流程 / 问题解决

基于 **STM32F429IGT6** 的网络工程：**FreeRTOS V11.1.0 + LwIP 2.1 多线程 + mbedTLS 3.6 HTTPS**，
**HTTP(80) 与 HTTPS(443) 双协议**，内存池全部走**外部 SDRAM**；并通过 **SNMP v2c Agent（UDP 161）**
把全部硬件/网络/控制状态暴露给 PC 端管理工具。

---

## 一、需求

### 1.1 核心功能需求

| 编号 | 需求 | 状态 |
|---|---|---|
| R1 | 静态 IP 联网（LAN8720A RMII），ping / ICMP 分片重组正常 | ✅ |
| R2 | 内嵌 Web 服务器，HTTP(80) 与 HTTPS(443) 同时发布 | ✅ |
| R3 | 网页 JSON API：硬件信息、网络参数读写、LED/BEEP 控制、复位 | ✅ |
| R4 | 硬件信息采集（AP3216C 光距感 + MPU9250 九轴 IMU），web/snmp 多接口并发读 | ✅ |
| R5 | SNMP v2c Agent（UDP 161），MIB 覆盖系统/网络/传感器/控制/统计，支持 Get/GetNext/Set | ✅ |
| R6 | PC 端 SNMP 管理工具：命令行验证、WinForms 客户端/服务器、**桌面仪表盘**（主交付物） | ✅ |
| R7 | 长期运行稳定性：I2C 总线锁死后自动恢复，传感器数据不冻结为 0 | ✅ |

> **说明**：原需求（联网 + 双协议 Web + 硬件采集 + SNMP + PC 工具）未包含命令行调试接口。
> **串口 Shell（UART）与 Telnet(23) 是开发过程中为便于调试、验证多接口并发读模型而逐步扩展的能力**
> （详见 §2.8），并非原始需求条目。其命令解析逻辑（`shell_exec` / `shell_feed_line`）被 SNMP / Web 复用，
> 属于实现层面的基础设施，不在上述 R1~R7 需求范围内。

### 1.2 非功能需求

- **零警告构建**：Debug / Release 双构零警告（仅 RWX 段良性提示）。
- **端到端验收**：每次功能变更后必须真机烧录 + 自动化脚本（python / C#）验收 PASS。
- **开发节奏**：实现 → 双构零警告 → 仿真/真机验证 → 扩展 verify 脚本（pass/fail 计数）→ 交付清单。
- **模块化**：`app/` `bsp/` `Drivers/` `third_party/` 分层，调试配置一律相对路径。

---

## 二、框架

### 2.1 硬件

| 项目 | 配置 |
|---|---|
| MCU | STM32F429IGT6 (Cortex-M4 @ 180MHz, HSE 25MHz, PLL M=25 N=360 P=2 Q=8) |
| SRAM | 192KB 连续 @0x20000000 (SRAM1 112K + SRAM2 16K + SRAM3 64K)；CCM 64K 不可用于 ETH DMA |
| FLASH | 1024KB @0x08000000 |
| SDRAM | W9825G6KH-6, 32MB @0xC0000000 (FMC Bank1, 16bit, 90MHz) |
| PHY | LAN8720A (RMII, addr 0)，复位由 PCF8574T(I2C 0x20) P7 控制 |
| 串口 | USART1 PA9/PA10 @115200 (COM3) |
| LED | PB0/PB1 低电平点亮（PB0=受控 LED，PB1=心跳） |
| BEEP | PCF8574 P0 低电平发声 |
| 传感器 | AP3216C (I2C2, 0x1E) 光距 IR；MPU9250 (I2C2, 0x68) 九轴 IMU（含 AK8963 磁力计） |
| EEPROM | AT24C02 (I2C2, 0xA0)，存网络参数，重启生效 |
| IP | 192.168.10.99 / 255.255.255.0 / GW 192.168.10.1（默认值，EEPROM 可改） |

### 2.2 分层架构

```
┌──────────────────────────────────────────────────────────────────────┐
│ 应用层 (app/)                                                          │
│  shell (UART 命令行)   Telnet server(23, 每连接独立任务)               │
│  HTTP server(80)   HTTPS server(443, mbedTLS)                          │
│  SNMP agent(161, snmpd 任务)        hwinfo (传感器采集 AP3216C/MPU9250)│
│  led 心跳                                                            │
├──────────────────────────────────────────────────────────────────────┤
│ 协议 / RTOS 层                                                         │
│  LwIP 2.1 (tcpip_thread)   mbedTLS 3.6 (TLS)   FreeRTOS V11.1.0       │
│  → 任务: tcpip / EthLink / httpd / httpsN / telnetd / telnetN /      │
│         snmpd / shell / hwinfo / led                                  │
├──────────────────────────────────────────────────────────────────────┤
│ 系统层 (内存布局)                                                      │
│  SDRAM ucHeap (FreeRTOS 512KB)  @0xC0020000                           │
│  LwIP mem heap (128KB)        @0xC0000000                             │
│  mbedTLS pool (256KB)         @0xC00A0000                             │
│  ETH RX 零拷贝缓冲 (SRAM, DMA 可达)                                    │
├──────────────────────────────────────────────────────────────────────┤
│ 外设 / 驱动层 (bsp/)                                                   │
│  USART1+IRQ  ETH RMII(LAN8720)  FMC SDRAM  I2C2(PCF8574+24C02+传感器)  │
└──────────────────────────────────────────────────────────────────────┘
        ↑ 工具链: arm-none-eabi-gcc + CMake + Ninja + OpenOCD + ST-Link
```

### 2.3 任务清单（FreeRTOS）

| 任务 | 优先级 | 职责 |
|---|---|---|
| tcpip_thread | 3 | LwIP 协议栈（tcpip_init 创建） |
| EthLink | 2 | LAN8720 链路监控（500ms 轮询） |
| httpd | 3 | HTTP server（netconn, 端口 80） |
| httpsN | 3 | 每 HTTPS 连接一个任务（netconn + mbedTLS, 端口 443，上限 HTTPS_MAX_CONNS=4） |
| telnetd | 2 | Telnet 监听（netconn, 端口 23，accept 后派生 telnetN） |
| telnetN | 2 | 每 Telnet 客户端一个任务（行编辑 + 回显 + shell_feed_line） |
| snmpd | 3 | SNMP Agent（netconn UDP, 端口 161） |
| hwinfo | 2 | 硬件信息采集（每 200ms 读传感器 → 写共享结构体） |
| shell | 2 | UART 命令行（队列收字节 → 行编辑 → shell_exec） |
| led | 1 | LED 心跳 |

### 2.4 启动引导流程（boot sequence）

`main()` 严格按"先建内存、再建对象"的顺序执行，任何一步失败走 `Error_Handler()`（LED1 闪烁）：

```
① HAL_Init()                      复位外设、TIM7 作为 HAL 1ms 时基（SysTick 让给 FreeRTOS）
② SystemClock_Config()            HSE 25MHz → PLL → 180MHz（OverDrive + FLASH_LATENCY_5）
③ bsp_sdram_init()  ★必须先于一切 FreeRTOS 对象
   └─ FMC 配置 → SDRAM 初始化序列 → 刷新率 → 内存自测
④ BSP_UART_Init()                 创建 TX mutex + RX 队列
⑤ shell_init()                   创建 shell_task（等待 RX 队列）
⑥ BSP_LED_Init()/Off(1)
⑦ BSP_I2C_Init() → BSP_I2C_Recover() → BSP_ETH_PHY_Reset()
   └─ Recover 在 PHY 复位前清锁死 I2C 总线（防启动期死等）
⑧ web_serve_init()               运行时网络配置（仅 RAM，无 SD 持久化）
⑨ 传感器 init                    AP3216C / MPU9250（I2C2）
⑩ MX_LWIP_Init()                  tcpip_init → 创建 tcpip_thread + EthLink
   └─ __set_BASEPRI(0); __enable_irq()  ← 清 V11 调度器前残留 BASEPRI
⑪ http_server_init() / https_server_init() / snmp_agent_init()
⑫ hwinfo_init()                   创建 hwinfo_task
⑬ PRINT_LOG("FreeRTOS scheduler starting...")
⑭ xTaskCreate(led_task) → vTaskStartScheduler()
```

**关键约束（引导期三大硬规则）**
1. **SDRAM 最先**：`ucHeap`/`LwIP pools`/`mbedTLS pool` 都在 SDRAM，任何 `xTaskCreate` 在 SDRAM 前会写未初始化内存 → `heap_4.c:269` 下溢断言。
2. **UART 前零打印**：`bsp_sdram_init()` 在 `BSP_UART_Init()` 之前，内部不准任何 PRINT_LOG（失败靠 LED）。
3. **调度器前清 BASEPRI**：V11 的 `xTaskCreate` 残留 `BASEPRI=0x50`，不清除则 TIM7/HAL_Delay 卡死。
4. **I2C 启动前 Recover**：调度器启动前 HAL tick 不推进，`HAL_I2C_*` 超时永不触发，总线锁死会死等；`BSP_I2C_Recover()` 纯 `__NOP()` 延时，启动期安全。

### 2.5 内存布局（SDRAM 32MB 仅用 ~800KB）

| 区域 | 地址 | 大小 | 用途 |
|---|---|---|---|
| LwIP mem heap (ram_heap) | 0xC0000000 | 128KB | LwIP 动态内存 (MEM_SIZE) |
| FreeRTOS heap (ucHeap) | 0xC0020000 | 512KB | 任务/队列/信号量 (heap_4) |
| mbedTLS pool | 0xC00A0000 | 256KB | mbedTLS 堆（memory_buffer_alloc） |
| 其余 | — | ~31MB | 预留 |

**ETH RX 零拷贝缓冲（RX_POOL）必须留在内部 SRAM**：ETH DMA 写 SDRAM 在分片突发时
丢包（实测 `ping -l 1473` 起不稳定）。memp 池（PBUF 等）也在 SRAM。

### 2.6 关键设计

- **HAL 时基 = TIM7**（`app/stm32f4xx_hal_timebase_tim.c`）：SysTick 让给 FreeRTOS。
- **LwIP 多线程**：`NO_SYS=0`、`LWIP_NETCONN=1`、`tcpip_input()` 从 ETH ISR 喂包。
- **sys_arch**（`app/lwip/sys_arch.c`）：FreeRTOS 实现 mbox/sem/mutex/thread；ISR 内用 `xPortIsInsideInterrupt()` 跳过临界区。
- **校验和**：软件（`ETH_USE_HW_CHECKSUM=0`）——硬件卸载破坏 IP 分片。
- **HTTPS 架构（每连接任务）**：listener 只 accept，每连接 `xTaskCreate` `https_conn_task`（栈 4096、优先级 3、上限 4）；握手经 `s_hs_gate` 互斥串行化（mbedTLS 非线程安全）；请求循环支持 keep-alive 复用同一 TLS 会话；60s idle 超时（`netconn_set_recvtimeout` 轮询）才优雅关闭。mbedTLS 堆池在 SDRAM（256KB）。
- **共享状态模型**（`app/hwinfo.c`）：采集线程 `hwinfo_task` 每 200ms 整体 `memcpy` 进 `g_dyn`（临界区内原子发布），读接口整体 `memcpy` 出；控制接口 `hwinfo_set_led/beep` 临界区内改单字段 + 临界区外驱动硬件。web / telnet / snmp 复用同一路径，零状态分散。

### 2.7 硬件信息采集架构

**背景**：原 `GET /api/hardware` 在 web 线程内实时 I2C 读取，每次请求阻塞 100+ms，且 `led_on`/`beep_on` 为裸 `static` 无并发保护。后续要接入 telnet / snmp 多接口并发读，必须把"采集"与"访问"解耦。

**设计要点**：
1. 静态 / 动态拆两个结构体——静态信息初始化后基本不变；动态信息周期刷新。
2. 200ms 周期后台采集，web 访问不阻塞采集。
3. 整体拷贝 + 单字段原子——读整体 memcpy 出、写整体 memcpy 进，均在临界区内。

#### 数据结构

```c
/* 静态：init 后基本不变 */
typedef struct {
  const char *mcu;            /* "STM32F429IGT6" */
  const char *clock;          /* "180 MHz" */
  char ip[NETCFG_IP_LEN];
  char mask[NETCFG_IP_LEN];
  char gw[NETCFG_IP_LEN];
  char mac[NETCFG_MAC_LEN];
  uint32_t freertos_tasks;
} hwinfo_static_t;

/* 动态：每 200ms 刷新 */
typedef struct {
  uint16_t lux, ps, ir;       /* AP3216C */
  float ax, ay, az;           /* MPU9250 加速度 */
  float gx, gy, gz;           /* 陀螺仪 */
  float mx, my, mz;           /* 磁力计 (AK8963) */
  uint8_t sensor_valid;       /* 0 = 上次读取失败 */
  uint8_t led_on;
  uint8_t beep_on;
  uint32_t i2c_recover;       /* I2C 总线恢复累计次数 */
  uint32_t updated_ms;
} hwinfo_dynamic_t;
```

#### 并发模型

- **采集线程 `hwinfo_task`（prio 2，200ms）**：`web_i2c_lock()` → 读传感器 → 失败则 `BSP_I2C_Recover()` 重试一次 → 填局部 `dyn` → 拷贝前从 `g_dyn` 回填 `led_on/beep_on` → 临界区内整体 `memcpy` 进 `g_dyn`。
- **读接口** `hwinfo_static_copy()` / `hwinfo_dynamic_copy()`：临界区内整体 `memcpy` 出。
- **控制入口** `hwinfo_set_led()` / `hwinfo_set_beep()`：临界区内改单字段 + 临界区外驱动硬件（I2C 不在临界区）。
- 主存 `g_sta` / `g_dyn` 为单变量，`taskENTER_CRITICAL/taskEXIT_CRITICAL` 只包 memcpy，安全快速。

### 2.8 串口 Shell 与 Telnet

**Shell 硬件**：USART1 (PA9/TX, PA10/RX)，115200 8N1，接板载 USB 转串（CP210x）。

**并发与收发模型**
- **TX**：中断驱动 + `g_tx_buf` 环形缓冲（`volatile` 计数），`uart_write()` 在临界区内完成原子入队；调度器启动前走 `HAL_UART_Transmit` 阻塞发送。
- **RX（FreeRTOS 队列）**：`xQueueCreate(256,1)` + `xQueueSendFromISR`，彻底消除 ISR 与任务共享 head/tail 计数器导致的丢字节；调度器启动前仅清 RXNE 丢弃。
- **回显 / 行编辑**：收到即回显；`BS/DEL` 退格回显 `\b \b`；方向键 ANSI 转义序列静默丢弃。
- **命令历史**：缓存最近 3 条。
- **解析线程**：`shell_task` 调 `shell_exec(line, out)`；`telnet` 复用同一解析函数（`shell_feed_line`），零改动。
- **Telnet**：端口 23，每连接独立任务，支持最多 3 并发；`exit` 仅 telnet 生效；30s idle 超时主动关闭；IAC 协商状态机（5 态）按 RFC854 对称应答，过滤 NUL(0) 字节避免命令截断。

**命令集**
| 命令 | 功能 |
|---|---|
| `hw` | FreeRTOS 任务数、芯片型号、时钟 |
| `dev` | 硬件设备信息：sensor_valid / i2c_recover / AP3216C lux,ps,ir / MPU9250 九轴（含磁力计） |
| `net` | 无参显示待生效网络参数；`net ip/mask/gw/mac <值>` 修改并写入 EEPROM（重启生效） |
| `version` | 固件版本 + 编译时间 |
| `beep on\|off` / `led on\|off` | 控制实际硬件 |
| `help` / `history` / `reboot` | 帮助 / 历史 / NVIC 系统复位 |

### 2.9 SNMP Agent（嵌入式端，UDP 161）

基于 lwIP `netconn` UDP API，独立 FreeRTOS 任务 `snmpd` 实现 **SNMP v2c** Agent（community `public`），
手写轻量 BER/ASN.1 编解码（无外部依赖、无堆分配），MIB 节点取值经 `hwinfo_*_copy()` 共享层，
控制节点经 `hwinfo_set_*()` 写回（与 web/telnet 复用同一控制路径）。

企业根节点：`1.3.6.1.4.1.32`。**5 大类共 28 个叶子节点**：

| 分支 | 含义 | 代表节点 |
|---|---|---|
| `32.1.x` 系统 | MCU/时钟/任务数/运行时间 | `32.1.1` sysMcu, `32.1.3` sysTasks, `32.1.4` sysUptime |
| `32.2.x` 网络 | IP/掩码/网关/MAC | `32.2.1~3` netIp/netMask/netGw（**读写**，经 netcfg 重启生效）, `32.2.4` netMac |
| `32.3.x` 传感器 | AP3216C + MPU9250 九轴 | `32.3.1~3` 光/距/IR，`32.3.4~12` 加速度/陀螺/磁力 9 轴，`32.3.13` sensor_valid |
| `32.4.x` 控制 | LED / BEEP / 复位 | `32.4.1` led, `32.4.2` beep, `32.4.3` ctrlReset（Set=1 → 软复位） |
| `32.5.x` 统计 | 请求/错误/更新/I2C恢复 | `32.5.1` statReq, `32.5.2` statErr, `32.5.3` statLastUpd, `32.5.4` statI2cRecover |

支持 PDU：`GetRequest` / `GetNextRequest`（Walk）/ `SetRequest`；错误 community 静默丢弃。

**源码布局**
```
app/snmp/ber.h  ber.c          BER/ASN.1 编解码（TLV 读写、OID、INTEGER、OCTET、IPAddress、TimeTicks）
app/snmp/mib.h  mib.c          自定义 MIB 树（节点表 + get/set 回调，绑定 hwinfo）
app/snmp/snmp_msg.h snmp_msg.c 报文解码/调度/响应构建（纯逻辑，可在 PC 离线单测）
app/snmp/snmp_agent.h snmp_agent.c  UDP 161 监听 + 独立线程 + netconn 收发
```

### 2.10 PC 端工具（C# / .NET 9 / WinForms，tools/ 下）

| 工具 | 路径 | 功能 | 状态 |
|---|---|---|---|
| SNMP 客户端 | `tools/snmp_client` | WinForms 客户端，构造 Get/GetNext/Set，解析显示 VarBind，支持 Walk | ✅ |
| SNMP 代理服务器 | `tools/snmp_server` | WinForms 代理，监听 UDP 161，解析报文，可手动构造响应/Trap 联调 | ✅ |
| 共享编解码 | `tools/snmp_common/SnmpCore.cs` | 两工具共用的 BER v2c 编解码（单源维护） | ✅ |
| **桌面仪表盘** | `tools/snmp_desktop` | **主交付物**：WinForms 桌面软件，6 页（系统概念/设备信息/硬件监控/传感器监控/网络状态/参数设置），周期轮询 + LED/BEEP 管理 | ✅ |
| 命令行验证 | `tools/snmp_verify` | 无 GUI 批量验证（31 项断言，pass/fail 计数） | ✅ |

**snmp_desktop 设计要点**
- 复用 `SnmpCore.cs` 编解码，直接 UDP 通信（不依赖 snmp_server）。
- 左侧 6 项菜单（自绘 Panel+Label，`AutoScroll` 防裁切），右侧 `pnlContent` 绝对定位 + 每页 `MakeScroll()` 纵向可滚。
- 传感器监控页 13 路卡按窗口宽度自适应 3~4 列网格；参数设置页含设备控制/网络设置/系统维护/硬件信息/运行统计/轮询设置 6 张卡。
- 深色主题（深底 + 深卡片 + 统一浅色文字，无彩色点缀），符合项目 UI 风格。
- 周期轮询：`System.Threading.Timer` 按 NumericUpDown 间隔（默认 3000ms）刷新当前页；ToggleSwitch 下发 SetRequest 并回读。
- 发布：`dotnet publish -c Release -r win-x64 --self-contained false` → `tools/snmp_desktop/publish/snmp_desktop.exe`。

### 2.11 网页 + JSON API

**SD 卡部署（网页默认页）**：前端工程 `web/`（Vue 3 + Vite）`vite build` 产物 `web/dist/` 拷入 SD 卡 `web/` 目录，插卡重启 → HTTP(S) 默认页从 `0:/web/index.html` 读取（无卡回退 flash 内嵌页）。

**JSON API（HTTP 80 与 HTTPS 443 均支持）**

| 端点 | 方法 | 说明 |
|---|---|---|
| `/api/hardware` | GET | MCU/时钟/AP3216C(lux,ps,ir)/MPU9250(九轴)/LED/BEEP/sensor_valid |
| `/api/network` | GET | ip/mask/gw/mac（待生效值） |
| `/api/network` | POST | 修改 → 写 EEPROM，重启生效 |
| `/api/control` | POST | `{"led":0\|1}` / `{"beep":0\|1}` |
| `/api/reset` | POST | 软复位 |

### 2.12 资源占用（最新，第四十二波）

| 配置 | FLASH | RAM (内部) | SDRAM 池 |
|---|---|---|---|
| Debug | 286292B / 27.46% | 85992B / 43.74% | ~800KB |
| Release | 310808B / 29.79% | 85992B / 43.74% | ~800KB |

> 注：仅 RWX 段良性提示，无其他警告。

---

## 三、开发流程

### 3.1 标准工作流（端到端）

```
需求确认（用户给完整上下文：路径 + 硬件细节 + 验收标准）
   ↓
实现计划（先出计划获确认，再动手）
   ↓
编码实现（app/ + bsp/ + tools/ 同步）
   ↓
Debug + Release 双构零警告（ninja）
   ↓
仿真 / 真机验证（openocd 烧录 + GDB 栈回溯 / 串口 / 网络）
   ↓
扩展 verify 脚本（python / C#，给出 pass/fail 计数）
   ↓
交付清单（增量汇报，✅ 状态收尾）
```

### 3.2 构建

```bash
cmake -G Ninja -B build                  # Debug
cmake -G Ninja -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build-release
```

产物 `build/stm32f429_net.elf/.hex/.bin`。**注意**：本工程无 `CMakePresets.json`，构建目录是 `build/` 与 `build-release/`；新增 `.c` 源后必须重跑 `cmake ..` 重新 `GLOB`（`file(GLOB app/*.c)` 在配置时展开并缓存，ninja 增量不会自动重扫）。

### 3.3 验证脚本清单

| 脚本 | 用途 | 最新结果 |
|---|---|---|
| `tools/snmp_verify` (C#) | SNMP 端到端 31 项断言 | 31/31 PASS |
| `tools/snmp_desktop_headless` (C#) | 桌面仪表盘 6 页数据通路 + 稳定性轮询 | 26/26 PASS |
| `tools/snmp_desktop_visualtest` + `verify_snmp_desktop_visual.py` | 6 页视觉渲染（PrintWindow 截图 + Pillow 亮像素检测） | 6/6 PASS |
| `tools/telnet_test.py` | Telnet IAC 协商 / NUL 过滤 / 命令 / exit / idle 超时 | 全 PASS |
| `tests/shell_stress/stress_shell.py` | shell 解析逻辑压测（PC 侧 gcc 编译 + ctypes） | 41/41 PASS |
| `tests/shell_stress/verify_uart_hw.py` | 物理串口端到端（COM3） | 10/10 PASS |
| `tests/snmp_offline/` | BER/MIB/报文逻辑离线 gcc 单测 | 36/36 PASS |
| `tests/verify_netcfg_block.py` | EEPROM 块布局 + CRC16 复刻 | 9/9 PASS |

### 3.4 迭代记录（按波次）

| 波次 | 内容 | 关键产出 |
|---|---|---|
| 1~29 | 工程基线：SDRAM/ETH/LwIP/FreeRTOS 移植、HTTP、HTTPS(mbedTLS)、Shell、Telnet、hwinfo 共享层、EEPROM 持久化 | 双协议联网、JSON API、多命令行 |
| 30 | Telnet IAC 协商修复 + Python 测试脚本 | telnet_test.py 11/11 PASS |
| 31 | Telnet 接收 NUL 字节截断修复 | — |
| 32 | HTTPS 卡顿修复（每连接任务 + 握手串行化 + keep-alive + 60s 保活） | https_test.py 3/3 PASS |
| 33 | 放弃 TLS/Web 效率优化（保持 HEAD 现状） | — |
| 34 | 嵌入式 SNMP v2c Agent + PC 端 C# 三工具（client/server/verify） | 离线 36/36、真机 19/19 PASS |
| 35 | snmp_client/server 布局修复（TableLayoutPanel） | — |
| 36~37 | snmp_desktop 桌面仪表盘（C# WinForms，6 页）+ 崩溃修复 | headless 10/10 PASS |
| 38 | 嵌入式端 MIB 扩展（可写网络 32.2.1~3 + 复位 32.4.3） | — |
| 39 | 硬件修复后真机端到端验收（readback 同步 g_netcfg 修复） | snmp_verify 31/31、headless 15/15 PASS |
| 40 | snmp_desktop UI 重构（传感器独立页 + 参数设置页硬件信息区） | headless 24/24 PASS |
| 41 | snmp_desktop 全量布局重写（绝对定位 + 视觉测试工具） | visualtest 6/6 PASS |
| 42 | **SNMP 硬件数据运行后变 0 的根因与修复（I2C 总线锁死恢复）** | 双构 0 警告、headless 26/26 PASS |

---

## 四、问题解决

### 4.1 移植踩坑记录（重要）

1. **FreeRTOS V11 `INCLUDE_*` 默认全 0**：`vTaskDelay`/`vTaskDelete` 等必须显式开启。
2. **V11 ARM_CM4F port 需要 softfp 编译**：port.c 单独 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16`。
3. **V11 向量表必须直指 port 函数**：startup `.word vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler`。
4. **调度器启动前 xTaskCreate 残留 BASEPRI=0x50**：`tcpip_init()` 后 `__set_BASEPRI(0)` 恢复。
5. **SYS_ARCH_PROTECT 不能在 ISR 调 vPortEnterCritical**：`xPortIsInsideInterrupt()` 跳过。
6. **netconn close 前必须读完请求**：未读数据 close 发 RST。
7. **mbedTLS BIO 必须缓冲 netbuf 余量**：netconn_recv 返回整个 netbuf，但 mbedTLS 按小块请求——BIO 需内部读缓冲。
8. **ETH DMA 缓冲不能放 SDRAM**：分片重组丢包；RX 零拷贝池留 SRAM。
9. **mbedTLS 3.x 配置**：`MBEDTLS_CONFIG_FILE=<mbedtls_config.h>` 用尖括号；PEM parse 传 `strlen+1`；`MEMORY_BUFFER_ALLOC_C` 需配 `PLATFORM_MEMORY`。
10. **`file(GLOB app/*.c)` 新增源必须重跑 `cmake ..`**：GLOB 在配置时展开并缓存。本工程无 CMakePresets.json。
11. **共享状态用整体 memcpy + 临界区**：采集线程整体 memcpy 进、读接口整体 memcpy 出，均在临界区内；采集线程写整体前须从全局回填控制态（led/beep）。
12. **SDRAM 初始化必须最先**：FreeRTOS heap(ucHeap)、LwIP ram_heap、mbedTLS 池都在 SDRAM，所有 `xTaskCreate`/`xSemaphoreCreate*`/`xQueueCreate` 都从 ucHeap 分配，SDRAM 晚于对象创建会触发 `heap_4.c:269` 下溢断言。
13. **串口 RX 必须用 FreeRTOS 队列**（非环形缓冲）：ISR 与任务共享 head/tail 在连发字节时错位丢字节；调度器启动前 RX 数据必须丢弃（`xQueueSendFromISR` 在调度器前不安全）。
14. **RX 分支禁用 `portYIELD_FROM_ISR`、统一在函数末尾 yield**：否则同一字节被 `xQueueSendFromISR` 两次（输入翻倍串）。
15. **应用层全面弃用 `printf`，改用 `PRINT_LOG`**：`printf` 经 newlib `_write` 走 `uart_write` 互斥路径，在任务上下文与 `g_tx_mutex` 交互时死机；`PRINT_LOG` 按调度器状态分流（RUNNING→互斥 ring 入队；未运行→`HAL_UART_Transmit` 阻塞），两种状态都安全。
16. **`-specs=nano.specs` 默认不链浮点 printf**：MPU9250 九轴显示空白，需加 `-u _printf_float`。
17. **Telnet NUL(0) 字节导致命令截断**：`str*` 遇 `\0` 终止，需在写入 line 前过滤 NUL。
18. **Telnet IAC 协商状态机**：旧实现把转义字节漏进数据路径，需 5 态机按 RFC854 对称应答。

### 4.2 SNMP 硬件数据运行后变全 0（第四十二波，重点）

**现象**：SNMP 运行一段时间后，13 路传感器硬件数据全为 0 且不再更新，网页 / shell / snmp 三处都变 0。

**根因（双层面）**
1. **运行时 I2C 总线锁死无恢复**：`hwinfo_task` 每 200ms 经 `web_i2c_lock` 读 AP3216C/MPU9250（HAL `I2C_Mem_Read` 仅 10ms 超时）。一旦从设备把 SDA 拉低锁死（噪声 / 复位中途打断事务），HAL 读永久超时返回 -1，collector 持续发布 0 + `sensor_valid=0`，且无看门狗、无总线恢复 → 数据永久冻结为 0。
2. **启动期 I2C 卡死（闪烧后暴露）**：烧录时用 openocd `reset halt;program;reset` 打断在飞 I2C 事务，留下 SDA 锁死。下次启动 `main()` 在调度器启动前调 `BSP_ETH_PHY_Reset()→BSP_PCF8574_Write→HAL_I2C_Master_Transmit`，因 BUSY/SDA 低卡在 `I2C_WaitOnFlagUntilTimeout`；而 HAL tick（TIM7）在调度器启动前不推进，超时条件永不成立 → **无限死等**，整板起不来（ping 100% 丢包）。GDB 栈确认：`main:82 → BSP_ETH_PHY_Reset → BSP_PCF8574_Write → HAL_I2C_Master_Transmit → I2C_WaitOnFlagUntilTimeout`。

**修复**
- `bsp/bsp_i2c.c` 新增 `BSP_I2C_Recover()`：检测 SDA 低 / SR2.BUSY / SR1 错误标志 → 把 SCL 配成 GPIO 推挽翻转 ≥9 次释放被卡从设备 → 发 STOP → `HAL_I2C_DeInit+Init` 清锁存错误 → 还原 AF_OD 引脚 → 复测 BUSY/SDA。纯 `__NOP()` 延时，**调度器启动前也可安全调用**。
- `app/main.c`：`BSP_I2C_Init()` 后、`BSP_ETH_PHY_Reset()` 前插入 `BSP_I2C_Recover()`，启动即清锁死总线。
- `app/hwinfo.c`：`hwinfo_task` 读失败即调 `BSP_I2C_Recover()` 重试一次，仅恢复后仍失败才发布 0 + valid=0；新增 `i2c_recover` 计数（g_dyn 累加并跨周期保留）；任务栈 256→384。
- `app/snmp/mib.c` 新增 `32.5.4 statI2cRecover`（Counter32）；`app/shell.c` `cmd_dev` 打印 `i2c_recover`。
- `tools/snmp_desktop/SnmpEngine.cs` 补 `Oids.Stats.Recover`；`tools/snmp_desktop_headless` 扩展 10 次稳定性轮询（`sensor_valid` 恒为 1）。

**验证**：Debug+Release 双构 0 警告；烧录后设备从卡死恢复在线；`snmp_desktop_headless` 26/26 PASS（稳定性轮询 `sensor_valid=1`、lux 随环境波动、无冻结）；`curl /api/hardware` 30s 浸泡 `valid` 恒为 1。

### 4.3 HTTPS 握手性能（实测优化）

| 配置 | TLS 握手耗时 |
|---|---|
| Debug 未开 ECP 优化（初期） | 4.88s |
| Release 未开 ECP 优化 | 3.67s |
| Debug + `MBEDTLS_ECP_NIST_OPTIM` + `WINDOW_SIZE=6` + `FIXED_POINT_OPTIM` | **0.83s** |
| Debug + 上述 + `MBEDTLS_HAVE_ASM`（M4 umull 汇编） | **0.79s** |
| Release + 全部优化 | **0.64s** |

瓶颈：mbedTLS 最小化配置缺 ECP 加速宏，P-256 走通用 bignum 模约减（慢 5-10 倍）且每次握手重建固定基点表。代价为 FLASH +18KB（window 6 预计算表）。

**浏览器首访 1.46s 之谜**：浏览器并发请求 `/` + `/favicon.ico` 两个连接排队串行握手。修复：HTML `<head>` 加 `<link rel="icon" href="data:,">` 内联图标，首访降到单握手 ~0.8s。

**X25519 反而更慢（实测否决）**：mbedTLS 3.6 已移除 Everest 快速实现，X25519 走通用 Montgomery ladder，实测 1.07s，比 NIST 优化的 P-256（0.79s）慢 35%。保持 P-256。

### 4.4 其他调试记录

- **BER 编解码**：`BER_TAG_SEQUENCE` 误定义为 `0x10`（正确 `0x30`）；长度字段预留少 1 字节（long-form 需 5 字节占位）；`handle_message` 外层 SEQUENCE 解码后需 `ber_dec_init` 重建解码器。
- **C# SnmpCore**：`BuildRequest` 曾把 message 内容用 `Wrap(msg, SEQUENCE, msg)` 自引用追加导致报文顺序颠倒（头部变 `A0` 而非 `30`），改为 `BuildMessage()` 按 `version→community→pdu` 顺序组装。
- **mib.c use-after-free**：`gnet_*` `v->oct` 指向栈局部数组，改 `static` 缓冲；newlib sscanf 不支持 `%hhu`，改 `%u` 到 `unsigned` 再赋 `uint8_t`。
- **SNMP readback 假阳性**：`gnet_ip/mask/gw` 原读 `g_sta.ip`（hwinfo 一次性快照），与 Set 写的 `g_netcfg.ip` 不同步 → 改实时 `sscanf(g_netcfg.*)`。
- **Telnet 退出 RST**：`telnet_conn_cleanup` 先排空残包 → `netconn_close` 发 FIN → 延时 200ms 让 Bye! 与 FIN 落线 → `netconn_delete`，消除 Python 收到 10054。

---

## 五、目录

```
app/            main / FreeRTOSConfig / HAL timebase(TIM7) / LwIP 移植 / HTTP(S) / SDRAM 堆定义
  app/lwip/     ethernetif / lan8720 / sys_arch
  app/snmp/     ber / mib / snmp_msg / snmp_agent
  app/web/      http_server / https_server / web_assets / cert
  hwinfo.c/.h   shell.c/.h / telnet_shell.c/.h / netcfg.c/.h / mbedtls_pool.c/.h
bsp/            bsp_uart / bsp_led / bsp_i2c / bsp_pcf8574 / bsp_sdram / bsp_ap3216 / bsp_mpu9250 / bsp_eeprom_24c02
Drivers/        ST HAL + CMSIS
third_party/    LwIP 2.1 / FreeRTOS-Kernel V11.1.0 / mbedtls 3.6.2 / FatFs
tools/          snmp_client / snmp_server / snmp_common / snmp_desktop / snmp_verify / https_test.py / telnet_test.py / verify_snmp_desktop_visual.py
tests/          shell_stress/ / snmp_offline/ / verify_netcfg_block.py / i2cdiag.gdb / i2cscan.gdb
web/            Vue 3 + Vite 前端（build 产物拷 SD 卡）
```
