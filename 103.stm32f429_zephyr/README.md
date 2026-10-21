# 103.stm32f429_zephyr — Zephyr HTTP 服务器（STM32F429IGT6 + LAN8720A + SDRAM）

在 **STM32F429IGT6** 上用 **Zephyr 3.7.0** 跑一个自带 IP 栈的 HTTP 服务器：

- 一颗灯（PB1）由固件心跳闪烁；
- 另一颗灯（PB0）与蜂鸣器（PCF8574T P0）由网页开关控制；
- 外部 **32 MB SDRAM**（FMC bank 1）已接入：自检通过后开放 8 MB 应用堆，
  HTTP 线程栈优先从 SDRAM 分配；
- 静态 IP `192.168.10.8/24`，浏览器打开 `http://192.168.10.8/` 即可。

参考工程：[`009.stm32h743_zephyr`](../009.stm32h743_zephyr)（Zephyr 工程骨架）、
[`101.stm32f429_net`](../101.stm32f429_net)（同一块板的裸机 HAL + LwIP + Web/SNMP，硬件与外设配置以它为准）。

> 当前状态：**仅验证编译通过**（干净全量构建，0 警告）。硬件未连接，烧录与真机联调尚未执行。

---

## 1. 硬件

| 项目 | 配置 | 出处 |
|---|---|---|
| MCU | STM32F429IGT6，LQFP176，1 MB Flash / 192 KB SRAM + 64 KB CCM | 101 README |
| 系统时钟 | HSE 25 MHz 晶振，PLL M=25 N=336 P=2 Q=7 → **168 MHz** | 见 §7 |
| 外部 SDRAM | **W9825G6KH-6，32 MB，FMC bank 1 @ 0xC0000000**，16 bit，13 行 × 9 列 × 4 bank | 101 `bsp/bsp_sdram.c` |
| PHY | LAN8720A，RMII，PHY 地址 0，复位由 PCF8574T P7 经三极管控制 | 101 `bsp_pcf8574.h` |
| ETH 引脚 | PC1 MDC / PC4 RXD0 / PC5 RXD1 / PA1 REF_CLK / PA2 MDIO / PA7 CRS_DV / **PB11 TX_EN** / PG13 TXD0 / **PG14 TXD1** | 101 `app/lwip/ethernetif.c` |
| FMC 引脚 | 数据 PD0/1/8/9/10/14/15 + PE7..15；地址 PF0..5、PF12..15、PG0..2；**BA0/BA1 = PG4/PG5（即 FMC A14/A15）**；控制 PC0/PC2/PC3/PG8/PF11/PG15；掩码 PE0/PE1 | 101 `bsp/bsp_sdram.c` |
| LED0（心跳） | **PB1**，低电平点亮 | 101 `bsp/bsp_led.h` |
| LED1（受控） | **PB0**，低电平点亮 | 同上 |
| 蜂鸣器 | PCF8574T **P0**，低电平发声 | 101 `bsp/bsp_pcf8574.h` |
| I2C2 | PH4 SCL / PH5 SDA，100 kHz，PCF8574T 地址 0x20 | 101 `bsp/bsp_i2c.h` |
| 调试串口 | USART1 PA9 TX / PA10 RX，115200 8N1 | 101 README |
| 网段 | 192.168.10.8 / 255.255.255.0 / GW 192.168.10.1 | 101 `netcfg`（本工程写死在 `prj.conf`） |

板级定义在 **`boards/stm32f429ig/`**（项目内自定义 board，用 `-DBOARD_ROOT=<项目根>` 引入）。
没有直接复用 `nucleo_f429zi`：那颗板是 144 脚 / 2 MB Flash / 8 MHz ST-Link 时钟源，
与本板的 176 脚 / 1 MB Flash / 25 MHz 晶振 / PCF8574 网络复位全都对不上。

---

## 2. Zephyr 支持的网络接口与协议

### 2.1 先分清两套 IP 栈

| 方案 | 位置 | 说明 |
|---|---|---|
| **Zephyr 原生栈**（本工程） | `zephyr/subsys/net/` | 与内核同仓，Kconfig 直接开；`CONFIG_NETWORKING=y` 即可 |
| LwIP | 独立模块 `modules/lib/lwip` | 需额外 west 下载；101 那种「HAL + LwIP + FreeRTOS」组合用它是自然选择 |

两套二选一（`CONFIG_NET_LWIP`）。本工程用原生栈，所以仓库里**没有** lwip 模块。

### 2.2 链路层（`subsys/net/l2/`）

| L2 | 说明 |
|---|---|
| `ethernet` | 802.3 + VLAN(802.1Q)；本工程使用 |
| `wifi` | 802.11（需配套 Wi-Fi 驱动 / hostap） |
| `ieee802154` | 802.15.4，配 6LoWPAN |
| `openthread` | Thread |
| `ppp` | 串口 / 蜂窝模块拨号 |
| `canbus` | CAN 上的 IP（ISO-TP 之上的 6LoCAN） |
| `dummy` / `virtual` / `offloaded_netdev` | 测试、native_sim、把整套栈交给外部芯片 |

以太网驱动（`drivers/ethernet/`）里与 ST 相关的是 **`eth_stm32_hal.c`**，覆盖 F2 / F4 / F7 / H5 / H7
的片上 MAC（外接 PHY）。本工程即走这条路：MAC 在片内，PHY 是 LAN8720A。

### 2.3 网络层

IPv4、IPv6、ICMPv4/ICMPv6、ARP、ND/MLD、IP 分片重组、
DHCPv4 **客户端 + 服务端**、DHCPv6、IPv4 链路本地地址自动配置、静态地址、
多播、网络管理与事件（`net_mgmt`）、promiscuous / `AF_PACKET` 抓包。

### 2.4 传输层

TCP（含拥塞控制、快速重传、TIME_WAIT 可配置）、UDP、TLS/DTLS（mbedTLS，`tls_credentials` 存凭据）。

### 2.5 Socket 与应用层库（`subsys/net/lib/`）

| 层 | 能力 |
|---|---|
| Socket | BSD 风格 `zsock_*` API、`poll`/`select`、`getaddrinfo`/`netdb`、`socketpair`(socks) |
| HTTP | **client** + **server**（HTTP/1.1，含 HTTP/2 与 WebSocket 服务端支持） |
| WebSocket | 客户端 + 服务端 |
| CoAP | 客户端/服务端、observe、block-wise |
| LwM2M | 设备管理 |
| MQTT / MQTT-SN | 3.1.1 / 5.0 |
| DNS | 解析器 + mDNS responder |
| SNTP | 时间同步 |
| TFTP | 文件传输 |
| PTP | 精密时钟 |
| zperf | 吞吐测试 |
| net shell | `net iface` / `net conn` / `net stats` / `ping` 等命令 |

> 注意：Zephyr **没有**官方 SNMP / Telnet 实现。101 里的 SNMP Agent 与 telnet shell 是项目自己写的，
> 在 Zephyr 上要迁移得自己带代码。

### 2.6 本工程实际启用的部分

```
L2      : ethernet (st,stm32-ethernet + LAN8720A, RMII)
L3      : IPv4 + ARP + 静态地址（NET_CONFIG_*）
L4      : TCP（+ UDP 打开，留给后续扩展）
Socket  : CONFIG_NET_SOCKETS（zsock_*）
应用    : 自写 HTTP/1.1 服务器（未使用 Zephyr 内置 http_server）
```

---

## 3. Web 服务器实现

### 3.1 为什么手写而不是用 `CONFIG_HTTP_SERVER`

Zephyr 3.7 的 `subsys/net/lib/http/` 提供现成的 HTTP 服务器（`HTTP_SERVICE_DEFINE` /
`HTTP_RESOURCE_DEFINE` 宏 + 静态资源）。这里选择手写 BSD socket 版本，原因：

- 路由、响应头、JSON 格式完全可控，便于和前端约定；
- 行为与 RAM 占用一眼看得见（一个 1 KB 接收缓冲 + 4 KB 线程栈）；
- 不依赖 3.7 刚重构过的 resource 宏 API。

后续若要 HTTP/2、WebSocket、静态文件系统，再切回内置服务器更划算。

### 3.2 启动顺序（这里有一个坑）

```
POST_KERNEL prio 0  : Zephyr FMC 驱动完成 SDRAM 控制器初始化
POST_KERNEL prio 50 : Zephyr STM32 I2C 驱动就绪
POST_KERNEL prio 55 : SYS_INIT 钩子释放 LAN8720 复位（PCF8574 P7: 1→0）
POST_KERNEL prio 60 : Zephyr STM32 以太网驱动探测 PHY ID
main()              : board_io_init() → sdram_init(自检 + 建堆) → 等链路 up
                      → 起 HTTP 线程（栈取自 SDRAM）→ 心跳循环
```

**为什么不用设备树的 `reset-gpios`**：`st,stm32-ethernet` 绑定里没有这个属性，驱动也不处理；
而 PCF8574 走 I2C，只能软件复位。复位必须发生在**以太网驱动读 PHY ID 之前**，否则 MAC 起来后
PHY 还在复位态，表现为「网口不亮、无任何报错」这类静默故障。所以把复位钉在
`CONFIG_I2C_INIT_PRIORITY`(50) 与 `CONFIG_ETH_INIT_PRIORITY`(60) 之间的 55 号优先级上
（`app/board_io.c`）。

PCF8574 没有内部寄存器，一次写就是全部 8 个引脚，因此软件维护一份影子字节，
翻 P0（蜂鸣器）时不会误改 P7（网络复位）。

### 3.3 HTTP 路由

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/`、`/index.html` | 内嵌单页（`app/web_index.h`，编进 Flash） |
| GET | `/api/status` | `{"led":0,"beep":1,"uptime_ms":12345,"ip":"192.168.10.8","mac":"00:80:E1:42:10:03","sdram_free_kb":8188}` |
| POST | `/api/led` | body `toggle` / `1` / `0` |
| POST | `/api/beep` | 同上 |
| GET | `/favicon.ico` | 204，避免浏览器反复 404 |

页面每秒轮询 `/api/status`，按钮 POST `toggle`，返回最新状态直接刷新界面（无需前端自己算状态）。

### 3.4 代码结构

```
app/main.c          启动流程、链路等待、心跳闪烁
app/board_io.c/h    LED + PCF8574 蜂鸣器 + LAN8720 复位时序
app/sdram.c/h       SDRAM 自检 + 8 MB k_heap
app/web_server.c/h  HTTP/1.1 服务器（zsock_*）
app/web_index.h     内嵌网页（深色单页，纯静态，无外部依赖）
boards/stm32f429ig/ 自定义板级定义（dts / defconfig / yaml / board.cmake）
```

---

## 4. 外部 SDRAM（FMC bank 1）

### 4.1 控制器初始化交给 Zephyr 驱动

`&fmc` 与 `&sdram` 节点写在 `boards/stm32f429ig/stm32f429ig.dts` 里，
由 `drivers/memc/memc_stm32.c` + `memc_stm32_sdram.c` 在 **POST_KERNEL priority 0** 完成
时钟、pinmux 与上电序列（CLK_ENABLE → PALL → AUTOREFRESH ×8 → LOAD_MODE → 刷新计数）。
进入 `main()` 时芯片已经可用。

### 4.2 必须显式声明 MPU 属性（否则会随机 fault）

Cortex-M4 的默认内存映射把 **`0xA0000000–0xDFFFFFFF` 归为 Device memory**，
`0xC0000000` 正好落在里面：不可缓存、非对齐访问直接 BusFault。

所以 SDRAM 窗口按 Zephyr 的 memory-region 写法声明，并带上 ARM MPU 属性：

```dts
sdram1: sdram@c0000000 {
    compatible = "zephyr,memory-region", "mmio-sram";
    device_type = "memory";
    reg = <0xc0000000 DT_SIZE_M(32)>;
    zephyr,memory-region = "SDRAM1";
    zephyr,memory-attr = <DT_MEM_ARM_MPU_RAM_NOCACHE>;
};
```

`arch/arm/core/mpu/arm_mpu.c` 会据此建立一条 Normal / non-cacheable 的 region。
证据：链接输出里出现独立的 `SDRAM1` 区域（32 MB），说明 memory-region 已进链接脚本。

### 4.3 容量与时序

| 项 | 值 | 依据 |
|---|---|---|
| 器件 | W9825G6KH-6，32 MB = 4 bank × 13 行 × 9 列 × 16 bit | 101 `bsp_sdram.c` |
| FMC 内核时钟 | HCLK = 168 MHz | `&rcc clock-frequency` |
| SDCLK | HCLK / 2 = **84 MHz**（`SDCLK_PERIOD_2`） | dts |
| mode register | **0x230**（burst=1、sequential、CAS=3、single write burst） | 与 101 同款计算 |
| 刷新计数 | **636** = 64 ms ÷ 8192 行 ÷ 11.9 ns − 20 | 按 84 MHz 重算 |
| 时序 `st,sdram-timing` | `2 8 6 6 2 2 2`（tMRD/tXSR/tRAS/tRC/tWR/tRP/tRCD） | 沿用 101 的 90 MHz 值 |

> 沿用 90 MHz 那组数不会不够：SDCLK 从 90 MHz 降到 84 MHz，同样的周期数对应**更长**的纳秒时间。

### 4.4 自检 + 8 MB 应用堆

`app/sdram.c`：

1. **自检**：3 个窗口（起始 / 中间 / 末尾）× 4 种 pattern × 64 个字，覆盖数据线与部分地址线；
   破坏性写读，因此必须在分发堆之前执行（代码里就是这个顺序）。
2. **建堆**：在 `0xC0000000` 上建 8 MB `k_heap`，余下 24 MB 保持可寻址，留给后续帧缓冲/大池。
3. **HTTP 线程栈优先放 SDRAM**：`k_thread_stack` 要求 32 字节对齐（MPU 开启时的
   `Z_THREAD_STACK_OBJ_ALIGN`），而 `k_heap` 只保证 8 字节，所以多申请 32 字节再手工对齐；
   SDRAM 不可用时自动退回静态 SRAM 栈。

网页上的「外部 SDRAM 可用」来自 `/api/status` 的 `sdram_free_kb`
（`CONFIG_SYS_HEAP_RUNTIME_STATS` + `sys_heap_runtime_stats_get()`），
这是「SDRAM 真的在读写」最直接的线上证据。

### 4.5 为什么 RAM 占用数字没降

静态后备栈（4 KB）仍然保留：SDRAM 自检失败时回退到它。
如果确认板上 SDRAM 稳定，可以删掉 `app/web_server.c` 里的 `http_stack_fallback`
（或把 `HTTP_THREAD_STACK` 调小），换回约 4 KB 内部 SRAM。

---

## 5. 构建

### 5.1 依赖

- `arm-none-eabi-gcc` 15.3.1（PATH 中即可，CMake 会自动推导 `GNUARMEMB_TOOLCHAIN_PATH`）
- CMake ≥ 3.20（实测 4.2.1）、Ninja
- Python 需能 `import pykwalify, yaml, elftools`（Zephyr 构建脚本依赖）

### 5.2 一键构建（Windows）

```bat
build_oneclick.bat
```

若 PATH 上的 Python 缺依赖，先指定一个可用的：

```bat
set PYTHON3_EXECUTABLE=D:\Software\Python3\python.exe
build_oneclick.bat
```

### 5.3 手工构建

```bash
cmake -B build -GNinja -DBOARD=stm32f429ig -DBOARD_ROOT="$PWD" \
      -DPython3_EXECUTABLE="D:/Software/Python3/python.exe"
ninja -C build
```

### 5.4 构建结果（本次实测，干净全量）

```
FLASH:   92312 B / 1 MB      8.80%
RAM:     64232 B / 192 KB   32.67%
CCM:         0 B / 64 KB     0.00%
SDRAM1:      0 B / 32 MB     0.00%   <- 区域已登记，内存在运行时分配
警告:    0
产物:    build/zephyr/zephyr.elf / .bin / .hex
```

RAM 偏高主要来自网络缓冲区（`NET_PKT_*` / `NET_BUF_*` 各 8/16 个、128 B 数据块）。
若后续要压内存，先动这三个 Kconfig，再考虑 `NET_BUF_DATA_SIZE`。

### 5.5 烧录（未执行，硬件到位后用）

```bash
openocd -f openocd.cfg -c "program build/zephyr/zephyr.elf verify reset exit"
```

调试串口 115200 8N1（USART1），启动后应看到：

```
<inf> board_io: LAN8720 reset released (PCF8574 P7 = 0)
<inf> main: STM32F429IGT6 Zephyr web server demo
<inf> sdram: SDRAM ready: 32 MB at 0xc0000000 (self-test OK), heap 8 MB
<inf> main: network up: 192.168.10.8/255.255.255.0 gw 192.168.10.1
<inf> web_server: HTTP thread stack: SDRAM (4096 B)
<inf> web_server: HTTP server listening on 192.168.10.8:80
```

---

## 6. 与 009 / 101 的关系

| | 009.stm32h743_zephyr | 101.stm32f429_net | 103（本工程） |
|---|---|---|---|
| 平台 | STM32H743 + Zephyr | STM32F429 + 裸机 HAL | STM32F429 + Zephyr |
| 网络 | 无 | LwIP + FreeRTOS | Zephyr 原生栈 |
| Web | 无 | HTTP + HTTPS（mbedTLS）+ SNMP | HTTP（纯 socket） |
| SDRAM | 无 | 32 MB，FreeRTOS heap 落 SDRAM | 32 MB，Zephyr FMC 驱动 + 8 MB k_heap |
| 板级 | 直接用 `nucleo_h743zi` | 自研板 | 项目内自定义 board |

Zephyr 工作区（`zephyr/`）与 009 同版本 3.7.0，但 **009 的源码树是按 H7 裁剪过的**
（`dts/arm/st` 与 `hal_stm32/dts` 只有 `h7`，`stm32cube` 只有 `stm32h7xx`），
无法直接支撑 F429。本工程的 `zephyr/` 已补齐：

- Zephyr v3.7.0 官方 tag 源码（补 `dts/`，全系列）
- `hal_stm32` v3.7-branch 的 `stm32cube/stm32f4xx`（F4 HAL）与 `dts/st/f4`（F4 pinctrl）

若换机器重建工作区，按同样方式取这两块即可，不要指望 H7 裁剪树。

---

## 7. 与 101 的时钟差异（重要）

101 裸机工程跑 **180 MHz**（PLL M=25 N=360 P=2 Q=8，并调用 `HAL_PWREx_EnableOverDrive()`）。
Zephyr 3.7 的 F4 时钟驱动 **只在 STM32F7 上实现 over-drive**（`drivers/clock_control/clock_stm32f2_f4_f7.c`
里的 `CONFIG_SOC_SERIES_STM32F7X` 分支），F4 走 180 MHz 没有 over-drive 使能，属于超规格运行。

所以本工程按 **168 MHz** 配置（M=25 N=336 P=2 Q=7）。若要 180 MHz，需要自己在 `main()` 早期
补 `PWR->CR |= PWR_CR_ODEN` 的时序，不建议在未验证前做。

---

## 8. 已知限制 / 未验证项

| 项 | 状态 |
|---|---|
| 编译（干净全量、0 警告） | ✅ 已验证 |
| 设备树（MAC/RMII/I2C2/GPIO/FMC/SDRAM） | ✅ 已核对生成结果 `build/zephyr/zephyr.dts` |
| SDRAM 控制器初始化 / 自检 | ⬜ 未上板（需要真机读写） |
| 烧录、串口启动日志 | ⬜ 未执行（硬件未连） |
| LAN8720 链路、静态 IP 连通性 | ⬜ 未验证 |
| 网页开关 LED / 蜂鸣器 | ⬜ 未验证 |
| SDRAM 只开放前 8 MB 作堆 | 后 24 MB 可寻址但未纳入管理，需要时改 `SDRAM_HEAP_SIZE` |
| SDRAM 未做 DMA 场景验证 | 以后若把 Ethernet/LTDC 缓冲放进去，需单独评估带宽与一致性 |
| HTTP 并发 | 单连接串行处理（`accept` 后同步处理再关闭），够演示用 |
| 无 TLS | 按需求只做 HTTP |
| 无认证 | 局域网演示用，未做 Basic Auth |
