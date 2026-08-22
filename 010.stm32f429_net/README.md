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
| IP | 192.168.10.99 / 255.255.255.0 / GW 192.168.10.1 |

## 软件架构

```
┌─ main ─────────────────────────────────────────────────┐
│  HAL_Init(TIM7 时基) → 时钟 → UART/LED/I2C → SDRAM      │
│  → MX_LWIP_Init(tcpip_thread) → httpd/httpsd 任务       │
│  → vTaskStartScheduler()                                │
└─────────────────────────────────────────────────────────┘
FreeRTOS 任务:
  tcpip_thread (prio 3)  LwIP 协议栈（tcpip_init 创建）
  EthLink      (prio 2)  LAN8720 链路监控 (500ms 轮询)
  httpd        (prio 3)  HTTP server (netconn, 端口 80)
  httpsd       (prio 3)  HTTPS server (netconn + mbedTLS, 端口 443)
  led          (prio 1)  LED 心跳
```

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
| `/api/network` | GET | ip/mask/gw/mac |
| `/api/network` | POST | 修改 ip/mask/gw/mac → 写 SD `netcfg.ini`，IP 立即生效（netif_set_addr），MAC 复位后生效 |
| `/api/control` | POST | `{"led":0\|1}` / `{"beep":0\|1}` |
| `/api/reset` | POST | 复位设备 |

### 传感器/IO 映射

- AP3216C：I2C2 (PH4/PH5)，0x1E（`bsp/bsp_ap3216.c`）
- MPU9250：I2C2，0x68（`bsp/bsp_mpu9250.c`，AK8963 磁力计经 I2C master）
- LED：PB0/PB1 低电平点亮；BEEP：PCF8574 **P0** 低电平发声（P0=L→蜂鸣器导通；`bsp_pcf8574.c`）
- 网络参数：运行时内存配置（`app/netcfg.c`，`netcfg_save` 为接口占位，当前不写外部存储）

### 已知待办

- MPU9250 磁力计当前读出为 0（加速度/陀螺仪正常）：AK8963 经 I2C master
  的 EXT_SENS_DATA 搬运时序待调
- SDIO 初始化实测需插入 SD 卡（无卡时 `SDIO: init FAILED` 属预期，功能回退 flash 页）
- 参数修改会写 SD `netcfg.ini`；无卡时修改仅本次运行生效



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

## 目录

```
app/            main / FreeRTOSConfig / HAL timebase(TIM7) / LwIP 移植 / HTTP(S) / SDRAM 堆定义
bsp/            bsp_uart / bsp_led / bsp_i2c / bsp_pcf8574 / bsp_sdram
Drivers/        ST HAL + CMSIS
third_party/    LwIP 2.1 / FreeRTOS-Kernel V11.1.0 / mbedtls 3.6.2
```
