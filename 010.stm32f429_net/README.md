# STM32F429 + FreeRTOS + LwIP + mbedTLS + SDRAM 网络工程

基于 STM32F429IGT6 的网络工程：**FreeRTOS V11.1.0 + LwIP 2.1 多线程 + mbedTLS 3.6 HTTPS**，
**HTTP(80) 与 HTTPS(443) 双协议**，内存池全部走**外部 SDRAM**。

## 硬件

| 项目 | 配置 |
|---|---|
| MCU | STM32F429IGT6 (Cortex-M4 @ 180MHz, HSE 25MHz, PLL M=25 N=360 P=2) |
| SRAM | 192KB 连续 @0x20000000 (SRAM1 112K + SRAM2 16K + SRAM3 64K)；CCM 64K 不可用于 ETH DMA |
| FLASH | 1024KB @0x08000000 |
| SDRAM | W9825G6KH-6, 32MB @0xC0000000 (FMC Bank1, 16bit, 90MHz) |
| PHY | LAN8720A (RMII, addr 0)，复位由 PCF8574T(I2C 0x20) P7 控制 |
| 串口 | USART1 PA9/PA10 @115200 (COM3) |
| LED | PB0/PB1 低电平点亮 |
| EEPROM | AT24C02 (I2C2 PH4/PH5, 器件地址 0xA0)，存网络参数 |
| IP | 192.168.10.99 / 255.255.255.0 / GW 192.168.10.1（默认值，EEPROM 可改） |

## 软件架构

### 分层架构图

```
┌──────────────────────────────────────────────────────────────────────┐
│ 应用层 (app/)                                                          │
│  shell (UART 命令行)   HTTP server(80)   HTTPS server(443, mbedTLS)   │
│  hwinfo (传感器采集 AP3216C/MPU9250)        led 心跳                   │
├──────────────────────────────────────────────────────────────────────┤
│ 协议 / RTOS 层                                                         │
│  LwIP 2.1 (tcpip_thread)   mbedTLS 3.6 (TLS)   FreeRTOS V11.1.0       │
│  → 9+ 任务: tcpip / EthLink / httpd / httpsd / shell / hwinfo / led   │
├──────────────────────────────────────────────────────────────────────┤
│ 系统层 (内存布局)                                                      │
│  SDRAM ucHeap (FreeRTOS 512KB)  @0xC0020000                           │
│  LwIP mem heap (128KB)        @0xC0000000                             │
│  mbedTLS pool (32KB)          @0xC00A0000                             │
│  ETH RX 零拷贝缓冲 (SRAM, DMA 可达)                                    │
├──────────────────────────────────────────────────────────────────────┤
│ 外设 / 驱动层 (bsp/)                                                   │
│  USART1+IRQ  ETH RMII(LAN8720)  FMC SDRAM  I2C2(PCF8574+24C02)  AP3216/MPU9250│
└──────────────────────────────────────────────────────────────────────┘
        ↑ 工具链: arm-none-eabi-gcc + CMake + Ninja + OpenOCD + ST-Link
```

### 任务清单（FreeRTOS）

| 任务 | 优先级 | 职责 |
|---|---|---|
| tcpip_thread | 3 | LwIP 协议栈（tcpip_init 创建） |
| EthLink | 2 | LAN8720 链路监控（500ms 轮询） |
| httpd | 3 | HTTP server（netconn, 端口 80） |
| httpsd | 3 | HTTPS server（netconn + mbedTLS, 端口 443） |
| hwinfo | 2 | 硬件信息采集（每 200ms 读传感器 → 写共享结构体） |
| shell | 2 | UART 命令行（xQueue 收字节 → 行编辑 → shell_exec） |
| led | 1 | LED 心跳 |

### 启动引导流程（boot sequence）

`main()` 严格按"先建内存、再建对象"的顺序执行，任何一步失败走 `Error_Handler()`（LED1 闪烁）：

```
① HAL_Init()                      复位外设、TIM7 作为 HAL 1ms 时基（SysTick 让给 FreeRTOS）
② SystemClock_Config()            HSE 25MHz → PLL → 180MHz（OverDrive + FLASH_LATENCY_5）
③ bsp_sdram_init()  ★必须先于一切 FreeRTOS 对象
   └─ FMC 配置 → SDRAM 初始化序列 → 刷新率 → 内存自测
   └─ 失败则 Error_Handler（此时 UART 尚未初始化，不打印，仅靠 LED）
④ BSP_UART_Init()                 创建 TX mutex + RX 队列(s_rx_queue)
   └─ 此后才允许 PRINT_LOG（调度器前走 HAL_UART_Transmit 阻塞发送）
⑤ shell_init()                   创建 shell_task（等待 s_rx_queue）
⑥ BSP_LED_Init()/Off(1)          LED1 交还 web API 控制
⑦ BSP_I2C_Init()→BSP_ETH_PHY_Reset()  PCF8574 P7=0 释放 LAN8720 复位
⑧ web_serve_init()               运行时网络配置（仅 RAM，无 SD 持久化）
⑨ 传感器 init                    AP3216C / MPU9250（I2C2），结果 PRINT_LOG
⑩ MX_LWIP_Init()                  tcpip_init → 创建 tcpip_thread + EthLink
   └─ __set_BASEPRI(0); __enable_irq()  ← 清 V11 调度器前残留 BASEPRI
⑪ http_server_init() / https_server_init()
   └─ 每创建一个 server 任务后重复清一次 BASEPRI（防御性）
⑫ hwinfo_init()                   创建 hwinfo_task（调度器启动后运行）
⑬ PRINT_LOG("FreeRTOS scheduler starting...")
⑭ xTaskCreate(led_task) → vTaskStartScheduler()
   └─ 调度器运行后，s_rx_queue 投递的字节才被 shell_task 消费
```

**关键约束（引导期三大硬规则）**
1. **SDRAM 最先**：`ucHeap`/`LwIP pools` 都在 SDRAM，任何 `xTaskCreate` 在 SDRAM 前会写未初始化内存 → `heap_4.c:269` 下溢断言。
2. **UART 前零打印**：`bsp_sdram_init()` 在 `BSP_UART_Init()` 之前，内部不准任何 PRINT_LOG（失败靠 LED）。
3. **调度器前清 BASEPRI**：V11 的 `xTaskCreate` 残留 `BASEPRI=0x50`，不清除则 TIM7/HAL_Delay 卡死。

### 引导流程评价

引导序列设计体现了**"依赖序优先于调用序"**的工程原则，整体成熟度较高：

- ✅ **时序正确性**：SDRAM → UART → FreeRTOS 对象的依赖链打断风险已彻底消除（早期曾因 BSP_UART_Init 在 SDRAM 前建 mutex 触发 heap 断言，后按"SDRAM 提前、不拆分 BSP"根治）。
- ✅ **失败可诊断**：每步失败有 LED 指示 + 可选 PRINT_LOG，不会出现"黑屏无信息"。
- ✅ **V11 移植陷阱已闭环**：BASEPRI 残留、向量表直指、FPU flag 三处均已落实。
- ⚠️ **可改进点**：`__set_BASEPRI(0); __enable_irq()` 在 ⑩⑪ 处重复出现 4 次（防御性但冗余）。可收敛为 `MX_LWIP_Init()` 返回后清一次 + 封装 `rtos_pre_scheduler_unmask()` 宏，降低维护认知负担。
- ⚠️ **可改进点**：`Error_Handler()` 在 SDRAM 失败路径会 `BSP_LED_Toggle(1)` —— 但 `BSP_LED_Init()` 在 ⑥ 才调用，若 ③ 失败则 LED 未初始化，闪烁无效。建议在 `main()` 最顶端（HAL 之后）先 `BSP_LED_Init()`，或 `Error_Handler` 自带最小 GPIO 初始化。

### HTTPS 实现（mbedTLS 3.6.2）

- 单任务阻塞式：BIO 回调直连 `netconn_write/recv`，握手/IO 阻塞在 httpsd 任务
  （无需 WANT_READ 状态机）。证书 EC P-256 自签名（CN=stm32f429.local），
  TLS 1.2 + ECDHE-ECDSA-AES128-GCM-SHA256，兼容 curl/浏览器 TLS 1.3 ClientHello 降级。
- **mbedTLS 堆池在 SDRAM**（32KB，`app/mbedtls_pool.c`），实测峰值 13.4KB。
- RNG 为 dev 级 xorshift64（自签名开发服务器场景）。

### 内存布局（SDRAM 32MB 仅用 ~672KB）

| 区域 | 地址 | 大小 | 用途 |
|---|---|---|---|
| LwIP mem heap (ram_heap) | 0xC0000000 | 128KB | LwIP 动态内存 (MEM_SIZE) |
| FreeRTOS heap (ucHeap) | 0xC0020000 | 512KB | 任务/队列/信号量 (heap_4) |
| mbedTLS pool | 0xC00A0000 | 32KB | mbedTLS 堆（memory_buffer_alloc） |
| 其余 | — | ~31MB | 预留 |

**ETH RX 零拷贝缓冲（RX_POOL）必须留在内部 SRAM**：ETH DMA 写 SDRAM 在分片突发时
丢包（实测 `ping -l 1473` 起不稳定）。memp 池（PBUF 等）也在 SRAM。

### 关键设计

- **HAL 时基 = TIM7**（`app/stm32f4xx_hal_timebase_tim.c`）：SysTick 让给 FreeRTOS。
- **LwIP 多线程**：`NO_SYS=0`、`LWIP_NETCONN=1`、`tcpip_input()` 从 ETH ISR 喂包。
- **sys_arch**（`app/lwip/sys_arch.c`）：FreeRTOS 实现 mbox/sem/mutex/thread。
- **校验和**：软件（`ETH_USE_HW_CHECKSUM=0`）——硬件卸载破坏 IP 分片。
- **HTTP/HTTPS**：netconn API + 独立任务，连接后先读请求再响应（close 前不读会发 RST）。

## 构建

```bash
cmake -G Ninja -B build              # Debug
cmake -G Ninja -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物 `build/stm32f429_net.elf/.hex/.bin`。Debug/Release 双构零警告（仅 RWX 良性提示）。

## 验收结果（真机）

| 项目 | 结果 |
|---|---|
| SDRAM 自检 | 32MB @ 0xC0000000 OK (FMC 90MHz) |
| ping 192.168.10.99 | 20/20，RTT 1-2ms |
| ICMP 32B→16000B（含 11 分片重组） | 全通 |
| HTTP GET / (80) | 200 OK，压测 20/20，响应 ~2ms |
| HTTPS GET / (443, curl -k) | 200 OK，压测 20/20，**握手 ~0.83s (Debug) / 0.64s (Release)** |
| openssl s_client | TLSv1.2 + ECDHE-ECDSA-AES128-GCM-SHA256 |
| mbedTLS 堆峰值 | 13376 / 32768 B（SDRAM 池） |
| CFSR / HFSR | 0 / 0 |
| 资源占用 | FLASH 156KB (Debug) / 174KB (Release，含 ECP window6 预计算表) + SRAM 80KB + SDRAM 672KB |

## HTTPS 握手性能（实测优化）

| 配置 | TLS 握手耗时 |
|---|---|
| Debug 未开 ECP 优化（初期） | 4.88s |
| Release 未开 ECP 优化 | 3.67s |
| Debug + `MBEDTLS_ECP_NIST_OPTIM` + `WINDOW_SIZE=6` + `FIXED_POINT_OPTIM` | **0.83s** |
| Debug + 上述 + `MBEDTLS_HAVE_ASM`（M4 umull 汇编） | **0.79s** |
| Release + 全部优化 | **0.64s** |

瓶颈定位：curl 时间分解（`time_connect`/`time_appconnect`）显示 4.88s 全部花在 TLS 握手
（mbedTLS 内部计算），**非任务优先级问题**（httpsd=3 与 tcpip_thread=3 同级，时间片轮转不饿死；
tcpip_thread 无包时阻塞让出 CPU）。根因是 mbedTLS 最小化配置缺 ECP 加速宏：P-256 走通用
bignum 模约减（慢 5-10 倍）且每次握手重建固定基点表。代价为 FLASH +18KB（window 6 预计算表）。

**浏览器首访 1.46s 之谜**：不是单握手慢，而是浏览器并发请求 `/` + `/favicon.ico` **两个连接**
排队串行握手（httpsd 单任务 accept，实测 `0.80s + 0.82s = 1.62s`）。修复：HTML `<head>` 加
`<link rel="icon" href="data:,">` 内联图标，浏览器不再请求 favicon → 首访降到单握手 ~0.8s。

**X25519 反而更慢（实测否决）**：mbedTLS 3.6 已移除 Everest 快速实现，X25519 走通用
Montgomery ladder，实测握手 1.07s，比 NIST 优化的 P-256（0.79s）慢 35%。保持 P-256 仅。

## Vue 网页 + SD 卡 + JSON API

### SD 卡部署（网页默认页）

1. 前端工程：`web/`（Vue 3 + Vite，`vite build` 产物在 `web/dist/`）
2. 把 `web/dist/` 下 **index.html + assets/** 拷入 SD 卡 `web/` 目录
3. 插卡重启 → HTTP(S) 默认页从 SD `0:/web/index.html` 读取
   （无卡/无文件 → 自动回退 flash 内嵌页）

### JSON API（HTTP 80 与 HTTPS 443 均支持）

| 端点 | 方法 | 说明 |
|---|---|---|
| `/api/hardware` | GET | MCU/时钟/AP3216C(lux,ps,ir)/MPU9250(9轴)/LED/BEEP |
| `/api/network` | GET | ip/mask/gw/mac（当前待生效值，EEPROM 中持久化） |
| `/api/network` | POST | 修改 ip/mask/gw/mac → 写 EEPROM，校验 head+crc16，**重启后生效**（响应 `{"ok":true,"apply":"reboot"}`） |
| `/api/control` | POST | `{"led":0\|1}` / `{"beep":0\|1}` |
| `/api/reset` | POST | 复位设备 |

### 硬件信息采集架构（`app/hwinfo.h` / `app/hwinfo.c`）

**背景**：原 `GET /api/hardware` 在 web 线程内实时 I2C 读取 AP3216C/MPU9250，每次请求阻塞
web 线程 100+ms，且 `led_on`/`beep_on` 为裸 `static uint8_t` 无并发保护。后续要接入
**telnet / snmp** 多接口并发读，必须把"采集"与"访问"解耦。

**设计要点（用户确认）**：
1. **静态 / 动态拆两个结构体**——静态信息初始化后基本不变；动态信息周期刷新。
2. **200ms 周期**后台采集，web 访问不阻塞采集。
3. **整体拷贝 + 单字段原子**——读整体 memcpy 出、写整体 memcpy 进，均在临界区内，
   保证任意单个字段赋值原子、读者永不见半更新结构体。

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
  uint32_t freertos_tasks;    /* uxTaskGetNumberOfTasks() @ init */
} hwinfo_static_t;

/* 动态：每 200ms 刷新 */
typedef struct {
  uint16_t lux, ps, ir;       /* AP3216C */
  float ax, ay, az;           /* MPU9250 加速度 */
  float gx, gy, gz;           /* 陀螺仪 */
  float mx, my, mz;           /* 磁力计 */
  uint8_t sensor_valid;       /* 0 = 上次读取失败 */
  uint8_t led_on;             /* 网页控制的 LED (PB0) */
  uint8_t beep_on;            /* BEEP (PCF8574 P0) */
  uint32_t updated_ms;        /* 采集时间戳 */
} hwinfo_dynamic_t;
```

#### 并发模型

- **采集线程 `hwinfo_task`（prio 2，200ms）**：
  `web_i2c_lock()` → 读 AP3216C/MPU9250 → `web_i2c_unlock()` → 填局部 `dyn`
  → **拷贝前从全局 `g_dyn` 回填 `led_on/beep_on`**（不被传感器刷新冲掉）
  → 临界区内整体 `memcpy` 进 `g_dyn`（原子发布）。
- **读接口** `hwinfo_static_copy()` / `hwinfo_dynamic_copy()`：临界区内整体 `memcpy` 出，
  单条 memcpy 极短，读者不阻塞采集。
- **控制入口** `hwinfo_set_led()` / `hwinfo_set_beep()`：临界区内改单字段 + 临界区外驱动硬件
  （I2C 不在临界区）；web / telnet / snmp 复用同一控制路径，避免状态分散。
- 主存 `g_sta` / `g_dyn` 为单变量，`taskENTER_CRITICAL/taskEXIT_CRITICAL` 只包 memcpy，安全快速。

#### 接入方式

- web：`api_hardware` 不再实时 I2C，改为 `hwinfo_dynamic_copy()` + `hwinfo_static_copy()`；
  `api_control` 改调 `hwinfo_set_led/beep()`（删原 `g_led_on`/`g_beep_on` 裸变量）。
- 后续 telnet / snmp：直接调用 `hwinfo_*_copy()` / `hwinfo_set_*()`，**零改动 web**。

#### 构建与验证过程

1. 新建 `app/hwinfo.h`、`app/hwinfo.c`；`web_serve.c` 删除实时 I2C 读取与裸 `g_led_on`/`g_beep_on`，
   改用 `hwinfo_*` 接口；`main.c` 在 `https_server_init()` 后、`vTaskStartScheduler()` 前调 `hwinfo_init()`。
2. **CMake 关键坑**：源文件用 `file(GLOB app/*.c)` 在配置时展开，新建 `hwinfo.c` 后必须重跑
   `cmake ..` 重新 GLOB（ninja 增量不会自动重扫），否则链接报 `undefined reference to hwinfo_*`。
   注意：本工程**无 CMakePresets.json**，`cmake --preset` 会静默失败，真实构建目录是 `build/` 与 `build-release/`。
3. `hwinfo.c` 需 `#include <string.h>`（`strncpy`/`memset`）。
4. Debug/Release 双构零警告（仅 RWX 良性提示）：
   - Debug  text = 255160 B / FLASH 24.33%
   - Release text = 275988 B / FLASH 26.36%
5. ST-Link 烧录 `Verified OK`。
6. 真机验证：
   - `GET /api/hardware` 返回完整嵌套 JSON：`ap3216c{lux,ps,ir,valid}`、`mpu9250` 9 轴、
     `led`/`beep`、`tasks`（=4）。
   - 间隔 250ms 取样 4 次，`lux` 持续变化（128→14→15→…）→ 证明 200ms 后台刷新且 web 访问不阻塞。
   - `POST /api/control {"led":1/0}` 连续切换，`/api/hardware` 回读 `led` 0↔1 正确。
   - `verify_all.py` **6/6 PASS**。

> 注：`/api/hardware` 不再实时读 I2C，而是读取 `app/hwinfo.c` 的共享快照。独立 `hwinfo_task`
> 每 **200ms** 周期采集（受共享 I2C 锁保护），整体 memcpy 到 `g_dyn`（临界区内原子发布）；
> 静态信息存 `g_sta`，初始化后不变。此设计面向后续 **telnet / snmp** 多接口并发访问。

### 串口 Shell（`bsp/bsp_uart.c` / `app/shell.c` / `app/shell.h`）

将调试打印串口升级为**命令行 Shell**：中断收发 + 环形缓冲 + 回显 + 回车解析 + 命令历史 +
独立解析线程，解析函数与传输解耦，后续 telnet 可复用同一套指令解析。

**硬件**：USART1 (PA9/TX, PA10/RX)，115200 8N1，接板载 USB 转串（CP210x）。

#### 并发与收发模型
- **TX（发送）**：中断驱动 + `g_tx_buf` 环形缓冲（`g_tx_head/tail/busy` 均 `volatile`）。
  `uart_puts()/uart_write()` 在 **`taskENTER_CRITICAL/EXIT_CRITICAL` 临界区**内完成
  "计算 next + 拷字节 + 置 busy + 开 TXEIE" 的原子操作，避免与 `USART1_IRQHandler`（TXE 分支）
  并发改写计数导致错位；再叠加 `g_tx_mutex` 保证多线程（`web`/`shell`/任务）入队互斥。
  调度器启动前（boot 日志阶段）走 `HAL_UART_Transmit` 阻塞发送，避免
  `xSemaphoreTake(..., portMAX_DELAY)` 在调度器未运行时死锁/断言。
- **RX（接收）：FreeRTOS 队列（非环形缓冲）**。`BSP_UART_Init()` 中 `xQueueCreate(UART_RX_BUF_SIZE=256, 1)`；
  `RXNE` 中断用 `xQueueSendFromISR` + `portYIELD_FROM_ISR` 把字节投入队列，`uart_getc()` 用
  `xQueueReceive(.., 0)` 非阻塞取字节。**彻底消除 ISR 与 `shell_task` 共享 head/tail 计数器
  在连发字节时的错位丢字节问题**（早期串显示 `hheehehehl` 被截成 `he` 的根因）。
  - **调度器启动前丢弃 RX**：`BSP_UART_IRQHandler` 的 RX 分支先判
    `xTaskGetSchedulerState()==taskSCHEDULER_RUNNING`，未启动时仅读 DR 清 RXNE 后丢弃，不碰队列
    （`xQueueSendFromISR` 在调度器前不安全）。
- **回显 / 行编辑**：`shell_task`（prio 2）从队列取字节，收到即回显；`BS/DEL`（0x08/0x7F）退格
  时 `len--` 且回显 `\b \b`（BS+空格+BS）让终端同步擦除；`\r`/`\n` 触发整行解析。
  - **方向键（上下左右）静默丢弃**：终端方向键发出 ANSI 转义序列 `ESC [ A/B/C/D`。
    `shell_task` 内置轻量转义状态机（`esc_state` 0→1→2），识别到整串后**直接丢弃、不回显、不存入
    line buffer**，避免光标键污染命令行（如 `dev` 前误按方向键导致指令被吞/乱码）。
- **命令历史**：缓存最近 **3 条**指令，`history` 命令查看（先进先出环形，旧→新打印）。
- **解析线程**：`shell_task` 在收到整行后调用 `shell_exec(line, out)`；`shell_exec` 仅做字符串
  解析 + 调 `hwinfo_*` / `hwinfo_set_*`，输出经 `out` 回调（UART 用 `uart_out`）。**同一函数后续
  telnet 直接调用，零改动解析逻辑。**
- **串口中断向量挂载**：启动文件 `startup_stm32f429xx.s` 中 `USART1_IRQHandler` 仅是 `.weak
  Default_Handler`，本工程在 `bsp_uart.c` 提供真实的 `USART1_IRQHandler(void)` 并 `forward` 到
  `BSP_UART_IRQHandler()`，否则 RX/TX 中断会全落 `Default_Handler` 导致串口不工作。

#### 命令集
| 命令 | 功能 |
|---|---|
| `hw` | FreeRTOS 任务数、芯片型号(STM32F429IGT6)、时钟(180MHz) |
| `dev` | 硬件设备信息（`hwinfo_dynamic_copy`：AP3216C lux/ps/ir、MPU9250 9 轴、led/beep 状态、sensor_valid） |
| `net` | 无参数显示待生效网络参数；`net ip <a.b.c.d>` / `net mask <掩码>` / `net gw <a.b.c.d>` / `net mac <XX:XX:XX:XX:XX:XX>` 或 `net mac random` 修改并写入 EEPROM（重启生效）。格式不合规打印 `ERR: ...` |
| `version` | 固件版本（`v1.0.0` + 编译日期时间） |
| `beep on\|off` | 控制实际硬件（`hwinfo_set_beep`） |
| `led on\|off` | 控制实际硬件（`hwinfo_set_led`，控制 PB0 非心跳灯） |
| `help` | 命令帮助 |
| `history` | 显示最近 3 条指令 |
| 未知命令 | 回显 `Unknown command: xxx` |

#### 关键坑（新增）
- **RXNEIE 必须用寄存器直接置位**：`SET_BIT(huart1.Instance->CR1, USART_CR1_RXNEIE)` 或
  `__HAL_UART_ENABLE_IT(..., UART_IT_RXNE)` 在本工程 HAL 版本下 CR1 位未生效，最终用
  `huart1.Instance->CR1 |= USART_CR1_RXNEIE`（0x20）才可靠使能 RX 中断。
- **应用层全面弃用 `printf`，改用 `PRINT_LOG`（见 `app/log.h`/`app/log.c`）**：`printf`
  经 newlib `_write` 最终也走 `uart_write` 的互斥路径，在任务上下文与 `BSP_UART_Init` 创建的
  `g_tx_mutex` 交互时出现过启动后死机。现统一用 `printf_log()`：栈上 `vsnprintf` 格式化后
  经 `uart_write()` 发出，`uart_write()` 自身按调度器状态分流（RUNNING→互斥 ring 入队；
  未运行→`HAL_UART_Transmit` 阻塞），**RTOS 已启动与未启动两种状态都安全**。
  - `PRINT_LOG_ENABLE` 为编译期总开关：置 0 时 `PRINT_LOG(...)` 编译为空、`printf_log()` 直接
    return，可一键关闭全部应用打印且无任何运行时开销。
  - `syscalls.c` 的 `_write` 仍直接调 `uart_write`（不经 printf），作为任何漏网 stdio 调用的
    安全兜底通道。
  - **RX 接收改用 FreeRTOS 队列、弃用环形缓冲**：原 `g_rx_buf/g_rx_head/g_rx_tail`（ISR 写、
    `shell_task` 读）在连发字节时 head/tail 被并发观察为不一致态 → 丢字节（实测 `hheehehehl`
    被截成 `he`）。改用 `xQueueCreate`(256,1) + `xQueueSendFromISR`/`xQueueReceive`，队列独占
    缓冲所有权，ISR 与任务不再共享计数器。
  - **调度器启动前 RX 数据必须丢弃**：`xQueueSendFromISR` 在 `vTaskStartScheduler` 前调用不安全，
    `BSP_UART_IRQHandler` 的 RX 分支加 `xTaskGetSchedulerState()==RUNNING` 守卫，未启动仅清 RXNE。
  - **TX 入队必须临界区保护**：`uart_write` 入队段包 `taskENTER_CRITICAL/EXIT_CRITICAL`，保护
    `g_tx_head/tail/g_tx_busy/CR1.TXEIE` 不被 `USART1_IRQHandler`(TXE) 并发改写；三者标 `volatile`。
  - **RX 分支禁用 `portYIELD_FROM_ISR`、统一在函数末尾 yield（修复输入字节翻倍）**：
    原 `BSP_UART_IRQHandler` 在 RX 分支内**每收到一字节就调用一次 `portYIELD_FROM_ISR`**。
    该宏只是向 NVIC `INT_CTRL` 的 PendSV 位做**无 `dsb/isb` 屏障的 store**；与"读 DR 清 RXNE"
    紧邻时，会在 ISR 退出瞬间重新采样/重踢 Pending 的 RXNE，**同一字节被 `xQueueSendFromISR`
    两次** → shell 收到翻倍串（实测用户输入 `he` 被刷成 `hhelphehe`，并误判成未知命令）。
    修复：RX 分支只 `xQueueSendFromISR` + 读 DR 后 `__DSB()`，把 yield 累积为单个 `xWoken`、
    **仅在函数最末**调用 `portYIELD_FROM_ISR(xWoken)`（RX 路径本不需 yield，只 TX 完成可能
    解锁任务，故保留末位一次）。另 shell 回显原用 `uart_puts((char*)&c)`（依赖 `strlen` 易越界），
    改为 `uart_write(&c, 1)` 显式长度。
  - **真机串口回归（COM3 @115200，Python pyserial）14/14 PASS**：其中关键用例
    `输入 he → MCU 精确收到 he（不出现 hhelphehe 翻倍串）` 通过，确认字节不再重复投递。
  - **`dev` 命令 MPU9250 9 轴显示空白（newlib-nano 浮点 printf 被裁剪）**：
    链接用 `-specs=nano.specs`（newlib-nano），其 `printf` **默认不链接浮点格式化**，
    `snprintf(..., "%.2f", ...)` 输出为空（实测 `MPU9250 ax/ay/az :  /  /` 全空格，而
    `sensor_valid=1` 证明驱动/采集均成功，只是打印层丢数字）。修复：链接器加 `-u _printf_float`
    显式拉入浮点 printf（Flash 增 ~8.6KB，余量充足：Debug 25.58% / Release 27.59%）。
    若追求极致体积，可改用定点整数显示（×100）避免该符号。
  - **方向键（ESC[A/B/C/D）静默丢弃**：终端方向键发出 `ESC [ A/B/C/D`，`shell_task` 用
    `esc_state` 状态机识别整串后直接丢弃（不回显、不存入 line buffer），避免光标键污染命令行。
  - **EEPROM(AT24C02) 网络参数持久化，重启生效**：
    - 驱动 `bsp/bsp_eeprom_24c02.c` 基于 I2C2（PH4/PH5，器件地址 7 位 0x50 / 写 0xA0），实现随机读
      `EEPROM24_Read` 与页写 `EEPROM24_Write`（8 字节/页，写后 5ms 延时；HAL `Mem_Read/Write` 自动处理地址）。
    - 块布局（EEPROM 地址 0 起，共 **69 字节**，24C02 容量 256B 富余）：
      `[head=0xAA][ip 16][mask 16][gw 16][mac 18][crc16(2)]`。CRC16-CCITT（poly 0x1021, init 0xFFFF）
      覆盖 `[head..mac]` 全部数据区（`netcfg_crc16`）。
    - 加载顺序：`main()` 在 `BSP_I2C_Init()`（释放 PHY 复位）之后调 `web_serve_init()`，其内部先
      `netcfg_init_defaults()` 填默认，再 `netcfg_load()` 读 EEPROM：head≠0xAA 或 CRC16 失配则回退默认。
      随后 `MX_LWIP_Init()` / `hwinfo_init()` 使用的就是 EEPROM（或默认）值。
    - **不立即生效**：`net` 指令与网页 POST `/api/network` 只更新 `g_netcfg` RAM + 写 EEPROM，
      **不调 `netif_set_addr`**，重启后 netif 才用新值。前端用 JS RegExp 校验、固件用 `valid_ip/
      valid_mask/valid_mac` 校验，不合规拒绝写入。
    - **校验测试**：`tests/verify_netcfg_block.py`（Python 复刻 CRC16+布局）9/9 PASS；真机 `net`
      指令端到端用例已加入 `tests/shell_stress/verify_uart_hw.py`（待 COM3 端口释放后复跑）。
- **`heap_4.c:269` 断言根因 = SDRAM 晚于 FreeRTOS 对象创建**：本工程 FreeRTOS heap(ucHeap)
  在 SDRAM，任何 `xTaskCreate`/`xSemaphoreCreate*` 都从 ucHeap 分配。原 `main()` 在
  `bsp_sdram_init()` **之前**就调 `BSP_UART_Init()`(建 mutex) 与 `shell_init()`(建任务)，
  把控制块写进未初始化的 SDRAM → 空闲链表元数据损坏 → 269 下溢断言。加 shell 后首次在
  SDRAM 就绪前建任务才暴露。**根治：把 `bsp_sdram_init()` 提前到 `HAL_Init`+时钟之后、所有
  硬件初始化之前**（见踩坑 12）。从此任何初始化顺序创建 FreeRTOS 对象都安全，无需拆分。
- 新增 `app/shell.c` 同样被 `file(GLOB app/*.c)` 覆盖，但每次新增 `.c` 仍需重跑 `cmake ..`
  重新展开 GLOB。

#### 构建与验证过程
1. `bsp_uart.c` 改为中断收发（删原阻塞 `_write`，`syscalls.c` 的 `_write` 改调 `uart_puts`）；
   新增 `app/shell.c`/`app/shell.h`，`main.c` 在 `BSP_UART_Init()` 后调 `shell_init()`。
2. Debug/Release 双构零警告（仅 RWX 良性提示）：
   - Debug  text = 259292 B / FLASH 24.73% / RAM 40.82%
   - Release text = 280304 B / FLASH 26.73% / RAM 40.82%
3. ST-Link 烧录 `Verified OK`。
4. **gdb 仿真验证**（openocd + arm-none-eabi-gdb）：
   - `CR1 = 0x202c` → `RXNEIE`(0x20) 已使能，TX/RX 中断路径就绪。
   - 直接调用 `shell_exec("hw"/"dev"/"net"/"version"/"beep on"/"led on"/"history", uart_out)`
     全部返回 `0`（解析/输出链路无崩溃）。
6. **`PRINT_LOG` 日志改造 + 启动验证**：应用层全面以 `PRINT_LOG` 替代 `printf`，新增
   `app/log.h`/`app/log.c`（`printf_log`/`vprintf_log` + `PRINT_LOG_ENABLE` 编译期开关）。
   双构零警告（仅 RWX）：Debug **text=259276B / FLASH 24.73%**，Release **text=280256B / FLASH 26.73%**。
   gdb 仿真（openocd 烧录后 `reset run` 4s 读 `uxCurrentNumberOfTasks`）= **9**，证明调度器
   正常启动、所有任务创建成功、**未死机/未触发 heap_4 断言**，`BSP_UART_Init` 后 `PRINT_LOG`
   走 `uart_write` 双状态路径工作正常。
5. **物理串口真机验证（COM3 / CP210x ↔ USART1 PA9/PA10）**：用户插好 CP210x 后，用
   Python pyserial（`tests/shell_stress/verify_uart_hw.py`，COM3 @115200）做端到端交互验证，
   **10 PASS / 0 FAIL**：
   - 启动横幅 `=== STM32F429 Shell ===` + 提示符 `STM32> ` 正常出现
   - `hw` 回显且输出含 `STM32F429IGT6`；`net`/`version`/`dev` 顺序不乱（net<version<dev）
   - **backspace 行编辑**：先打 `hhee` 再 BS×2，MCU 最终收到 `hh`（非 `hhee`），终端同步擦除
   - 未知命令 `foobar` → `Unknown command: foobar`
   - 空行不崩溃，提示符正常返回
   - 超长行 80 字节（>64 上限）截断不崩溃，后续 `help` 仍正常执行（恢复能力）
   - 配合 PC 侧逻辑压测（见下）合计 **41 + 10 = 51 PASS / 0 FAIL**。

#### 串口指令压测（`tests/shell_stress/`）
双层压测：

**(A) PC 侧解析逻辑压测**（编译真实 `app/shell.c` + 硬件依赖桩，覆盖传输无关的逻辑层）：
用 gcc 把 `app/shell.c` 与桩（`tests/shell_stress/inc/*` 桩头 + `shell_stub.c`）编成共享库，
Python（ctypes）驱动 `shell_exec` / `shell_feed_line`。

- **重构**：从 `shell_task` 抽出 `shell_feed_line(line)`（history_push + shell_exec），回车路径
  与未来 telnet 共用，行为不变；`app/shell.h` 导出声明。
- 覆盖维度：
  1. 正常指令功能正确性（hw/dev/net/version/help/history/beep/led 输出与返回码）
  2. 边界：空行、前后空格、Tab 包裹
  3. 边界：超长行 63/64/65/128/200 字节（64 上限安全截断不崩溃）
  4. 异常：未知命令（`rc==-1` + `Unknown`）、缺参/错参（`rc==0` + `Usage`）
  5. history 环形缓冲（真实回车路径 `shell_feed_line`）：保留最近 3 条、挤出最旧、空行不进历史
  6. 大规模随机压测：10000 条（正常/异常/边界混合，含随机长后缀）
- **结果：`41 PASS / 0 FAIL`**，随机负载 10000 条 ≈ **105 万 cmd/s**，无崩溃/越界。
- 运行：`python tests/shell_stress/stress_shell.py`（自动 gcc 编译 + 加载 + 压测）。

**(B) 真机端到端验证**（COM3 / CP210x 物理串口，逻辑层 + 传输层 + 中断全链路）：
Python pyserial（`tests/shell_stress/verify_uart_hw.py`，COM3 @115200）模拟终端打字
（含 backspace 编辑），逐条发指令并核对回显与执行结果。
- **结果：`10 PASS / 0 FAIL`**（横幅/提示符、hw 回显+MCU 型号、backspace 编辑收到 `hh`、
  未知命令、多指令顺序、空行、超长行恢复）。
- 运行：板子连 CP210x→COM3 后 `python tests/shell_stress/verify_uart_hw.py`。

### 传感器/IO 映射

- AP3216C：I2C2 (PH4/PH5)，0x1E（`bsp/bsp_ap3216.c`）
- MPU9250：I2C2，0x68（`bsp/bsp_mpu9250.c`，AK8963 磁力计经 I2C master）
- LED：PB0/PB1 低电平点亮。**网页 `/api/control` 控制的 LED 是 LED1/PB0（非心跳）**；PB1 是心跳灯（500ms 闪烁，由 `led_task` 驱动，不受网页控制）。BEEP：PCF8574 **P0** 低电平发声（P0=L→蜂鸣器导通；`bsp_pcf8574.c`）
- 网络参数：EEPROM(AT24C02) 持久化（`app/netcfg.c` + `bsp/bsp_eeprom_24c02.c`）。块布局：`[head=0xAA][ip][mask][gw][mac][crc16]`，共 69 字节；CRC16-CCITT(0x1021,init 0xFFFF) 覆盖全部数据区。启动 `web_serve_init()` 先填默认再从 EEPROM `netcfg_load()`，校验失败回退默认值。修改经 `net` 指令 / 网页写入，重启生效。

### 已知待办

- MPU9250 磁力计当前读出为 0（加速度/陀螺仪正常）：AK8963 经 I2C master
  的 EXT_SENS_DATA 搬运时序待调
- SDIO 初始化实测需插入 SD 卡（无卡时 `SDIO: init FAILED` 属预期，功能回退 flash 页）
- 参数修改已落地 EEPROM（AT24C02），重启后从 EEPROM 加载生效；网页与 shell 改动不立即应用（前端/串口均有格式校验，不合规拒绝写入）



## 移植踩坑记录（重要）

1. **FreeRTOS V11 `INCLUDE_*` 默认全 0**：`vTaskDelay`/`vTaskDelete` 等必须显式开启。
2. **V11 ARM_CM4F port 需要 softfp 编译**：port.c 单独 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16`。
3. **V11 向量表必须直指 port 函数**：startup `.word vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler`。
4. **调度器启动前 xTaskCreate 残留 BASEPRI=0x50**：`tcpip_init()` 后 `__set_BASEPRI(0)` 恢复。
5. **SYS_ARCH_PROTECT 不能在 ISR 调 vPortEnterCritical**：`xPortIsInsideInterrupt()` 跳过。
6. **netconn close 前必须读完请求**：未读数据 close 发 RST。
7. **mbedTLS BIO 必须缓冲 netbuf 余量**：netconn_recv 返回整个 netbuf，但 mbedTLS 按小块
   （如 5B 记录头）请求——`netbuf_copy` 只拷请求量、剩余被 delete 丢弃会导致握手卡死。
   BIO 需内部读缓冲（见 https_server.c g_rxbuf）。
8. **ETH DMA 缓冲不能放 SDRAM**：分片重组丢包；RX 零拷贝池留 SRAM。
9. **mbedTLS 3.x 配置**：`MBEDTLS_CONFIG_FILE=<mbedtls_config.h>` 用尖括号；
   PEM parse 传 `strlen+1`；`MEMORY_BUFFER_ALLOC_C` 需配 `PLATFORM_MEMORY`；
   `common.h` 的 -Warray-bounds 假阳性在 config 里 pragma 屏蔽。
10. **`file(GLOB app/*.c)` 新增源必须重跑 `cmake ..`**：GLOB 在配置时展开并缓存，ninja 增量
    不会重新扫描目录，新建 `.c` 后只 `ninja` 会链接报 `undefined reference`。本工程无
    CMakePresets.json，`cmake --preset` 静默失败，构建目录是 `build/` 与 `build-release/`。
11. **共享状态用整体 memcpy + 临界区**：多接口（web/telnet/snmp）并发读写时，采集线程整体
    memcpy 进、读接口整体 memcpy 出，均在 `taskENTER/EXIT_CRITICAL` 内，保证单字段原子、
    读者永不见半更新。采集线程写整体前须从全局回填控制态（led/beep），避免被传感器刷新覆盖。
12. **SDRAM 初始化必须最先（`bsp_sdram_init()` 放在 `HAL_Init`+时钟之后、任何硬件初始化之前）**：
    FreeRTOS heap(ucHeap)、LwIP ram_heap、mbedTLS 池都在 SDRAM。**所有 `xTaskCreate` /
    `xSemaphoreCreate*` / `xQueueCreate` 都从 ucHeap 分配**，若在任何使用 SDRAM 的对象创建
    之后才初始化 SDRAM，控制块会写进未初始化的 SDRAM，破坏 heap_4 空闲链表，触发
    `heap_4.c:269`（`heapSUBTRACT_WILL_UNDERFLOW`）断言。**约束（后续实现必须遵守）**：
    新增任何在 `main()` 里创建 FreeRTOS 对象的初始化（BSP/驱动/中间件），都**不能早于**
    `bsp_sdram_init()`；若初始化函数内部建了 FreeRTOS 对象，调用点必须在 SDRAM 就绪后。
    不要为规避此问题而把对象创建拆到 SDRAM 之后——直接保证 SDRAM 最先起即可，否则后续
    维护时容易再次踩雷。

## 目录

```
app/            main / FreeRTOSConfig / HAL timebase(TIM7) / LwIP 移植 / HTTP(S) / SDRAM 堆定义
bsp/            bsp_uart / bsp_led / bsp_i2c / bsp_pcf8574 / bsp_sdram
Drivers/        ST HAL + CMSIS
third_party/    LwIP 2.1 / FreeRTOS-Kernel V11.1.0 / mbedtls 3.6.2
```
