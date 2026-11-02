# 001 · STM32H743 TinyUSB CDC+MSC 复合设备

基于 STM32H743ZIT6 + TinyUSB 实现 **USB 复合设备**：CDC（虚拟串口）+ MSC（大容量存储，把板载 SD 卡暴露为 PC 可访问的 U 盘）。
SD 卡同时被固件侧 FatFs 与 PC 侧 USB 访问，CDC 与 MSC 并行工作、互不干扰。

> 需求与验收详见 `docs/prompter.md`。

---

## 1. 硬件与接口

| 项 | 说明 |
|----|------|
| MCU | STM32H743ZIT6 |
| 时钟 | HSE 外部无源晶振 25 MHz |
| USB | 默认 **OTG_FS**（PA11/PA12，TinyUSB rhport 0）；可选 **OTG_HS**（PB14/PB15，内置 FS PHY，rhport 1） |
| 存储 | 板载 microSD（SDIO），FatFs + TinyUSB MSC |
| 调试 | SWD + ST-Link；SVD = `STM32H743.svd` |

USB 控制器通过 CMake 选项切换：`-DUSB_PORT=FS`（默认）或 `-DUSB_PORT=HS`。

---

## 2. 工程结构

```
001.stm32h743_tinyusb_cdc_msc/
├── app/           应用层：main.c / stm32h7xx_it.c / stm32h7xx_hal_conf.h / syscalls.c
│   │              tusb_config.h（TinyUSB 配置）
│   └── usb/       usb_descriptors.c（复合设备描述符）/ msc_disk.c / sd_app.c / disk_interface.c
├── bsp/           板级：bsp.c / bsp_led.c / bsp_sdcard.c / bsp_sdcache.c / bsp_log.c（PRINT_LOG 日志）
├── sys_startup/   启动与链接：gcc / arm / iar 三套启动文件 + system_stm32h7xx.c
│                  + stm32h743zi_flash.ld
├── Drivers/       CMSIS + STM32H7xx HAL（ST 官方，gitignore，不手改）
├── third_party/   tinyusb / FatFs（gitignore）
├── cmake/         arm-none-eabi.cmake 交叉工具链
├── MDK-ARM/       Keil 工程 stm32h743.uvprojx（ARMCLANG）+ mdk_target.c + stm32h743.sct
├── docs/          prompter.md / prompter-check.md / prompter-remaster.md
├── tools/         test_cdc.py / test_cdc_msc.py / test_roundtrip.py   PC 端自测脚本
├── CMakeLists.txt / CMakePresets.json   # 预设：debug / release / debug-hs
├── openocd.cfg / STM32H743.svd          # 均在根级
└── build_oneclick.bat                   # 单工程一键编译
```

> 模块化约定：应用逻辑 `app/`、用户驱动 `bsp/`、启动与链接 `sys_startup/`、
> HAL 只放 `Drivers/`、第三方库只放 `third_party/`。GCC（CMake）与 Keil（MDK-ARM）双构建后端并存。

---

## 3. 开发流程

1. 先实现 **CDC 设备**，测试虚拟串口收发正常。
2. 再叠加 **MSC**，把 SD 卡以 U 盘方式暴露，验证 PC 读写与固件侧 FatFs 内部读写共存。
3. 验收：CDC 与 MSC **同时工作、互不干扰**。

---

## 4. 构建与运行

### 一键编译
- **单工程**：双击 `build_oneclick.bat`
  - 检查工具 `cmake / ninja / openocd / arm-none-eabi-gcc`（缺失提示参考 `document/support.md`）；
  - 检查根目录 `Drivers`、`third_party`（缺失提示从 `..\support_tools\env_support_for_stm32h743.zip` 解压/拷贝）；
  - 流程 `configure → clean → build`；所有出口 `pause` 停留查看。
- **全仓库**：在仓库根目录双击 `build_all.bat`，顺序编译 `project.list` 中的全部 21 个工程；**某工程失败会暂停等你回车后继续**，末尾输出 `Passed/Failed/Skipped` 汇总。

### 手动（预设工程）
```bash
cmake --preset debug
cmake --build build/debug --target clean
cmake --build build/debug
```
产物：`build/debug/h743_tinyusb_cdc.elf`（同时生成 `.hex` / `.bin` 与 size 报告）。

### 约束与实测
- **零警告**：`-Wall -Wextra`（第三方库按 `-Wno-*` 豁免），构建应 0 warning。
- **依赖缺失**：报 `Missing dependency: third_party/tinyusb` → 解压 `env_support_for_stm32h743.zip`。
- **实测资源**（`arm-none-eabi-size`，2026-11-02 重新核对，含 32 KB USB 缓冲 + 128 KB SD 缓存）：

| 区域 | 用途 | Debug | Release |
|------|------|-------|---------|
| FLASH | code + rodata | 75,364 B / 2 MB ≈ **3.59%** | 63,900 B ≈ **3.05%** |
| DTCMRAM | .data/.bss/stack（含 CDC 2×32 KB FIFO） | 84,224 B / 128 KB ≈ **64.3%** | 同左 |
| RAM_D1 (AXI) | MSC 32 KB EP buf + SD 缓存 128 KB + 合并暂存 32 KB | 196,704 B / 512 KB ≈ **37.5%** | 同左 |

---

## 5. USB 缓冲与 SD 读缓存

### 5.1 底层收发缓存（≥ 32 KB）

| 缓冲 | 大小 | 位置 | 说明 |
|------|------|------|------|
| `CFG_TUD_CDC_RX_BUFSIZE` | 32 KB | DTCM（TinyUSB 未给 FIFO 加段属性） | 主机突发下行不再顶到 NAK |
| `CFG_TUD_CDC_TX_BUFSIZE` | 32 KB | DTCM | `flood 500` → 500 行 32 KB 一次吞完（3 ms） |
| `CFG_TUD_MSC_EP_BUFSIZE` | 32 KB | RAM_D1（`CFG_TUSB_MEM_SECTION`） | 64 扇区/次 BOT 传输 |

CDC 回显按「一次读入的整块」批量写 FIFO，不再逐字节发 1 字节包：32 KB 突发回环
实测 **314 KB/s**（逐字节旧实现约 6 KB/s）。

### 5.2 SD 块缓存（128 KB 写回，`bsp/bsp_sdcache.c`）

- **位于块层之下**：FatFs（`disk_interface.c`）与 MSC（`msc_disk.c`）都走
  `sdcard_read_blocks / sdcard_write_blocks`，同一份缓存 → 两侧永远不会看到不同的
  扇区，MSC 的 read-modify-write 也不会用旧数据覆盖新数据。
- **映射**：64 组 × 4 路，`set = lba % 64`；连续扇区摊到不同组，线性扫描不打架。
- **上电预热**（`tusb_init()` 之前）：解析 MBR → 分区 BPB，把
  **系统区 72 扇区（MBR 8 + 引导/FSInfo 64）+ FAT 184 扇区** 读进缓存。
  保留区再大（本卡 2226 扇区）也不会吃掉窗口——窗口要让给 FAT。
- **读写策略**：命中直接拷；未命中且连续 ≤ 8 扇区 → 填缓存（元数据局部性）；
  更长 → 直通卡（32 KB 顺序读写不能被打碎成 512 B）。
- **回写时机**：每次 WRITE10 结束（只回写本次范围）、`SYNCHRONIZE CACHE(10)`、
  主机弹出（START STOP UNIT eject）、FatFs `CTRL_SYNC`（`f_sync`/`f_close`）、
  USB unmount/suspend。相邻脏扇区合并成一次多块写（512 B 单写 ~147 KB/s，
  32 KB 多块 ~900 KB/s）。
- **CDC 命令**：`scache` 打印命中率/回写统计与识别到的卷布局，`sync` 立即回写。

实测（30 GB FAT32 卡，part@8192 / rsvd 2226 / fatsz 15271 / spc 32）：

```
  window   : 128 KB = 256 sectors, 256 cached / 0 dirty
  read     : 216 hit  84 fill  15088 direct  (1% hit)
  write    : 45 hit  13 buffered  2048 direct
  writeback: 16 ops, 58 sectors, 0 dirty evictions, 0 drops
  prewarm  : 256 sectors (system 72, fat 184), status 0
```

主机写 1 MB 后复位再 `ls`，文件仍在（176 B / 1,048,576 B 两种尺寸都验证过），
即回写确实落到了卡上，而不是留在缓存里。

---

### 5.3 插入后为什么要等约 10 秒（实测定位）

现象：盘符出现很快，但双击打开、列出根目录要 9~10 s（对比普通 U 盘 1~2 s）。

分段实测（`tools/watch_mount.py` + `mtrace`，2026-11-02）：

| 阶段 | 耗时 | 说明 |
|---|---|---|
| 插入 → 盘符出现 | **0.22 s** | 主机只读 MBR + BPB（约 2 KB），枚举 91 ms |
| 盘符 → 可列出根目录 | **9.5 s** | 主机按 32 KB 步长顺序读完整个 FAT 区，约 8 MB |
| 单条 READ10 设备侧处理 | 3 ms | SDIO 已经很快 |
| 单条 READ10 主机侧节奏 | 37 ms | 34 ms 花在 USB 传输上 |

`sdbench` 直测卡侧（绕过缓存与 USB）：

```
512B   1.95 MB/s      4KB    6.80 MB/s
16KB  10.07 MB/s     32KB   10.96 MB/s   <- 8 MB 只需 0.75 s
```

结论：**瓶颈是 USB Full Speed 12 Mbps（实测 0.84 MB/s），不是 SDIO。**
SDIO 25 MHz × 4 线已跑到 10.96 MB/s（理论 12.5 MB/s 的 88%），在单条命令里只占 3/37 ≈ 8%；
即使把 SDIO 提到 50 MHz，9.66 s 也只能降到约 9.5 s。

已排除的猜测：

- **不是缓存没命中**：`scache` 显示挂载期 `direct` 读占绝大多数，而卡侧本来就只要 3 ms。
- **不是 FAT 脏标志**：`FAT[1]` 为 `0x7FFFFFFF`（bit31 = 0，脏）时确实会让 Windows 校验全表，
  但用「安全弹出」把干净位写回后重插，**仍然 9.5 s** —— 主机每次挂载都会读这 8 MB，与脏标志无关。
  `fatclean` 命令仍保留：它只负责把被硬拔线留下的脏标志清掉，避免下次挂载多跑一遍校验。

可以缩短的只有两条路（本次未实施，卡上有数据不宜重新格式化）：

| 方向 | 预期 | 代价 |
|---|---|---|
| 卷改 exFAT（FAT 约 1 MB 而不是 7.46 MB） | 9.5 s → 约 1.5 s | 需重新格式化 + `FF_FS_EXFAT=1` |
| FAT32 簇改 32 KB（FAT 减半） | 9.5 s → 约 4.7 s | 需重新格式化 |
| 限制卷容量（如 8 GB） | 同比例下降 | 牺牲容量 |
| 外接 ULPI PHY 跑 USB High Speed | 9.5 s → 约 0.3 s | 需改板；`OTG_HS` 内置的仍是 FS PHY |

复现方法：

```bash
py -3 tools/watch_mount.py --unplug 120     # 拔插一轮，打印各阶段耗时
# CDC 命令：sdbench 8  -> 卡侧原始读速；mtrace -> 每条 READ10 的耗时
```

---

## 6. 调试与烧录

- **VSCode（cortex-debug）**：`.vscode/launch.json` 用裸工具名（`openocdPath:"openocd"`、`gdbPath:"arm-none-eabi-gdb"`），
  `configFiles`/`svdFile` 经 `${workspaceFolder}` 指向根级 `openocd.cfg` 与 `STM32H743.svd`，`preLaunchTask:"build"`（F5 先编译再调试）。
- **命令行烧录**：
  ```bash
  openocd -f openocd.cfg -c "program build/debug/h743_tinyusb_cdc.elf verify reset exit"
  ```
- **单步 / 排错**：F5 进入调试；或用 `gdb_step_test.gdb` / `gdb_where.gdb` 等 gdb 脚本。
- SWD 速率 1800 kHz（stlink v2 clone 安全值；V3 可更高）。

---

## 7. 验收与自测

- ✅ CDC 串口正常通讯；PC 可正常访问 U 盘（SD 卡）。
- ✅ CDC 与 MSC 并行工作，互不干扰。
- ✅ 上电预热 256 扇区（系统区 72 + FAT 184），主机挂载读命中。
- ✅ 主机写入的文件在**复位冷启动**后仍能在 `ls` 中看到（回写确实落卡）。
- PC 端脚本（需 Python + pyserial，位于 `tools/`，本机用 `py -3` 启动）：
  - `py -3 tools/test_roundtrip.py` — CDC 回环测试
  - `py -3 tools/test_cdc_msc.py` — CDC + MSC 并发测试
  - `py -3 tools/test_cache.py write|verify|cleanup [COM7] [G:]` — 缓存/回写回归：
    `write` 写入 176 B + 1 MB 两个文件并打印缓存统计，**复位**后跑 `verify`
    （文件若仍在，说明数据真的在卡上），最后 `cleanup` 删除。
  - `py -3 tools/watch_mount.py [--unplug] [秒数]` — 插入耗时分解：同时记录
    CDC 端口出现、盘符可列出、首个文件可读三个时刻，并回读设备侧事件表
    （挂在 COM19 上的 USART1 日志给出 ms-since-reset 与墙钟的锚点）。
- 设备侧诊断命令（CDC）：
  - `sdbench [MB]` — 绕过缓存与 USB，直测 SDIO 读速（512B/4KB/16KB/32KB 四档）
  - `fatclean` — 清除硬拔线留下的 FAT32 脏标志（bit31/bit30），两张 FAT 都改
  - `mtrace` — 事件表，含每条 READ10 的耗时（`RDDONE arg=ms`）

---

## 8. 常见问题

| 现象 | 根因 / 处理 |
|------|-------------|
| `Missing dependency: third_party/tinyusb` | 未解压第三方库 → 见第 4 节依赖处理 |
| 改 `.ld` 后 ninja 报 `no work to do` | 链接脚本未触发重配置 → 重跑 `cmake --preset debug` 或 `clean` |
| cortex-debug Attach 超时 | `openocd.cfg` **不要**加 `connect_assert_srst`（会把 core 一直 hold 在 reset），保持 `stm32h7x.cfg` 默认软件复位 |
| USB 枚举不稳定 | 确认 `USB_PORT` 与实际焊接的 USB 引脚（FS=PA11/PA12，HS=PB14/PB15）一致 |
| 插入后盘符很快但打开要 10 s | USB Full Speed 带宽（0.84 MB/s）× 主机必读的 8 MB FAT。**不是 SDIO**，实测卡侧 10.96 MB/s。详见 5.3 |
