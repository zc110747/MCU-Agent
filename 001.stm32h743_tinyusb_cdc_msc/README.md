# STM32H743 + OV5640 UVC 摄像头（TinyUSB）

基于 **TinyUSB** 的 USB Video Class（UVC）设备示例：把 OV5640 摄像头采集的
240×240 YUY2 图像通过 **USB Full-Speed** 实时推流到主机（如 VLC / 相机应用 /
浏览器 `getUserMedia`）。

- 芯片：STM32H743ZIT6（鹿小班开发板）
- 摄像头：OV5640（DVP 接口，SCCB/I2C4 配置）
- 软件栈：TinyUSB（device, UVC class）+ STM32 HAL
- 构建：arm-none-eabi-gcc + CMake（≥ 3.20）+ OpenOCD（ST-Link）

---

## 1. 数据流总览

```
OV5640 (DVP)
   │  400×300 YUV422 (YUYV)，PCLK/HSYNC/VSYNC
   ▼
DCMI  ── 裁剪到居中 240×240（硬件 crop）
   │
   ▼
DMA2_Stream1 (CIRCULAR, WORD, FIFO FULL, INC4/SINGLE)
   │  持续循环写入 fb[0] @ 0x24000000
   ▼
bsp_camera_snapshot()  ── 仅在「垂直消隐期」锁相拷贝
   │
   ▼
发送侧缓冲 fb[1] / fb[2]（三缓冲，与 USB 传输并行）
   │
   ▼
USB FS 等时传输 (UVC)  →  主机
```

关键点：**DMA 不断覆写 `fb[0]`，CPU 只在消隐期把整帧拷到 `fb[1]/fb[2]`，
再由 USB 发出**。细节见第 6 节「撕裂修复」。

---

## 2. 硬件接口（鹿小班板）

| 功能 | 引脚 | 说明 |
|------|------|------|
| 运行灯 | PG7 | 心跳 LED |
| USB FS | PA11 (DM) / PA12 (DP) | dwc2 RHPORT 0，全速 |
| 外部晶振 | 25 MHz 无源 | `HSE_VALUE=25000000` |
| OV5640 PWDN | PF13 | 掉电控制 |
| SCCB / I2C4 | PF14 (SCL) / PF15 (SDA) | 地址 `0x78`（8-bit 形式） |
| DCMI_HSYNC | PA4 | |
| DCMI_PIXCLK | PA6 | |
| DCMI_VSYNC | PG9 | |
| DCMI_D0..D7 | PC6, PC7, PG10, PG11, PE4, PD3, PE5, PE6 | 8-bit 并行总线 |

### 内存布局

| 区域 | 地址 | 用途 |
|------|------|------|
| FLASH | 0x08000000 (2048K) | 程序 |
| AXI SRAM (RAM_D1) | 0x24000000 (512K) | **帧缓冲 `.framebuffer` 段**，MPU 标记 non-cacheable |
| DTCM / D2 / D3 | 0x20000000 / 0x30000000 / 0x38000000 | 其他 |

- 三缓冲 `s_fb[3][FRAME_SIZE]`，每个 `FRAME_SIZE = 240×240×2 = 115200` 字节，
  共 `345600` 字节（0x54600），落在 RAM_D1 且 < 512K。
- **必须**用 MPU 把帧缓冲区标为 non-cacheable，否则 CPU 可能读到缓存里的旧帧。

---

## 3. 图像格式与帧率

- 传感器输出 **400×300 YUV422 (YUYV)**（保持 4:3 几何，与参考驱动 PLL/ISP/binning
  设置匹配；直接输出 240×240 会改变预分频比、重开子采样问题）。
- DCMI 硬件裁剪到居中 **240×240**，即 UVC 推流的 `YUY2` 帧。
- `FRAME_RATE = 8`：USB FS 等时每 1 ms 微帧约 1023 字节 → 上限约 **8 fps**。
  主机可通过 UVC `SetCur(dwFrameInterval)` 请求其他帧间隔。

---

## 4. 构建与烧录

### 依赖

- `arm-none-eabi-gcc`
- `cmake` ≥ 3.20
- `openocd`（带 `stlink` 支持；本项目 `interface/stlink.cfg`）

### 步骤

```bash
# 配置（默认 Debug + 自带工具链文件 cmake/arm-none-eabi-gcc.cmake）
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# 编译：生成 build/stm32h743_uvc.elf/.bin/.hex 并打印 memory 用量
cmake --build build

# 烧录（OpenOCD，使用 debug/openocd.cfg）
cmake --build build --target flash
```

等价地，也可以直接：

```bash
openocd -f debug/openocd.cfg -c "program build/stm32h743_uvc.elf verify reset exit"
```

### 调试（cortex-debug / GDB）

```bash
openocd -f debug/openocd.cfg          # 启动 GDB server（默认 :3333）
arm-none-eabi-gdb build/stm32h743_uvc.elf
(gdb) target extended-remote :3333
```

> Windows 注意：OpenOCD 配置文件路径须用 POSIX 形式（如 `cygpath -m` 转换），
> 且**残留的 openocd.exe 进程会占用 ST-Link**，导致 `libusb_open()` 失败——
> 烧录前先结束旧进程。

---

## 5. 关键技术决策（移植者必读）

1. **OV5640 驱动不用 ST BSP 的初始化表。**
   ST BSP 组件表 `0x3814=0x31`（水平 2× 抽样）却未使能 `0x3821 bit0`（水平
   binning），是 OV5640 的非法组合——列地址不推进，DVP 整行反复输出同一像素。
   改用手动移植的 **`ov5640_ref.c`**（来自同款硬件的已验证参考驱动），仅改两项用于 UVC：
   - `0x4300`：0x6F → **0x30**（RGB565 → YUV422/YUYV）
   - `0x501F`：0x01 → **0x00**（ISP RGB → YUV422）
2. **DCMI 极性**：`PCK=RISING / VSYNC=LOW / HSYNC=LOW`，对应传感器 `0x4740=0x21`。
3. **传感器仍输出 400×300**（不是 240×240 直出），靠 DCMI crop 取中心 240×240。
4. **DMA**：`DMA2_Stream1`，CIRCULAR，WORD 对齐，FIFO FULL 阈值，INC4/SINGLE。
5. **DCMI 基地址 `0x48020000`（AHB2）**，不是 `0x40050000`。
6. **帧缓冲放 `0x24000000`（RAM_D1）+ MPU non-cacheable**。

---

## 6. 撕裂（tearing / 拼接缝）根因与修复

> 这是本项目最有价值的工程经验，移植任何「DMA 持续写入 + CPU 取帧」的流水线都应照搬。

### 根因
旧代码在 USB 帧间隔到达时，直接对 DMA 循环缓冲 `fb[0]` 做 `memcpy`。
DMA 以 CIRCULAR 模式持续覆写，而 CPU 拷贝速度约为 DMA 的 **21 倍**，在任意相位
拷贝会中途越过 DMA 写指针：缓冲前半是新帧、后半是旧帧 → **拼接缝**。
静止场景新旧帧相同、缝不可见；运动/剧烈变化场景立刻暴露。

量化：采样 DMA `NDTR` 序列，垂直消隐期占比 ≈ 19.5% ≈ **16 ms**（帧周期 83 ms），
有效读出期 67 ms → 旧随机相位 memcpy **约 80% 概率撕裂**。

### 修复：消隐期相位锁存 + 三缓冲

- **`bsp_camera_snapshot(dst)`**（`Core/Src/bsp_camera.c`）：
  - **门控**：仅当 `NDTR >= FRAME_SIZE/4`（满值 `0x7080`，即垂直消隐期、约 16 ms
    窗口）才拷贝；非消隐期返回 `false`，调用方下轮重试。
  - **撕裂自检**：拷贝后再次读 `NDTR`，若 DMA 已落后超过 1 行余量，判为撕裂帧并
    丢弃（`frame_done=false`，下次重试），绝不把半新半旧帧交出去。
  - 实测拷贝耗时 ≈ **3.12 ms**（`DWT->CYCCNT`），远小于 16 ms 窗口。
- **三缓冲**（`Core/Src/uvc_app.c`）：`fb[0]`=DMA 目标；`fb[1]/fb[2]`=发送侧交替
  缓冲，由 `s_tx_idx`（在传帧）/ `s_ready_idx`（就绪帧）管理。快照可在 USB 传输
  进行中并行执行，避免二缓冲下必须等下一消隐窗的帧率损失。

### A/B 对照验证（决定性证据）
用手动曝光 `0x3503=0x03`（关 AEC）+ `0x3501=0x04↔0x60` 让相邻帧亮度
Y≈7↔120（17 倍差），同时抓「同步快照」与「非同步拷贝」两缓冲做逐行亮度剖面：

| 缓冲 | 行亮度 spread | 最大行跳变 | 结论 |
|------|------|------|------|
| **GATED（修复后）** | 8.8 | 0.4 | 无拼接缝 |
| **UNSYNC（旧行为复现）** | 80.2 | 73.3 @ row 105→106 | 撕裂明确复现 |

---

## 7. 移植到其他板子 / 摄像头

### 必改清单

| 改什么 | 文件 | 要点 |
|--------|------|------|
| 分辨率 / 帧率 | `Core/Inc/main.h` | `FRAME_WIDTH/HEIGHT`、`FRAME_BYTES_PER_PX`、`FRAME_RATE`、传感器尺寸 |
| 外部晶振 | `Core/Inc/main.h` → `CMakeLists.txt` `HSE_VALUE` | 须与实际板载晶振一致 |
| 引脚 | `Core/Inc/main.h` | LED、PWDN、I2C、DCMI 全引脚 |
| I2C / DCMI / DMA 外设 | `Core/Src/bsp_board.c`、`bsp_camera.c` | 总线实例、AF、DMA 流/通道 |
| 帧缓冲大小 | `linker/STM32H743ZITx_FLASH.ld` | 保证 `RAM_D1` ≥ `FB_COUNT × FRAME_SIZE`（默认 3×115200=345600 B） |
| 摄像头驱动 | `Core/Src/ov5640_ref.c`、`bsp_camera.c` | 换摄像头需重写初始化表与 `bsp_camera_*` API |
| UVC 描述符 | `Core/Src/usb_descriptors.c` | `dwMaxVideoFrameBufferSize = FRAME_SIZE`；格式 GUID 用 `YUY2` |
| Cache 一致性 | MPU 配置 | 帧缓冲必须 non-cacheable（否则出现随机花屏/旧帧） |

### 性能调优
- USB FS → 约 8 fps（未压缩 YUY2）。要更高帧率：换 **USB HS**，或在固件侧做
  **MJPEG / H.264 压缩**后再经 UVC 的 MJPEG payload 推流。
- 换更大分辨率时注意 RAM_D1 容量与 DMA 吞吐。

---

## 8. 调试经验 / 坑

- **ST BSP OV5640 水平 binning bug**：见第 5 节，必须用 `ov5640_ref.c`。
- **DCMI 地址**：`0x48020000`（AHB2），曾误用 `0x40050000` 浪费大量时间。
- **OpenOCD `libusb_open()` 失败**：残留 `openocd.exe` 占用 ST-Link，先结束旧进程。
- **SDE 负片（`0x5580`）对 YUV 输出无效**：做 A/B 调制请改用**手动曝光**
  （`0x3503=0x03` + `0x3501` 调亮度），可产生 17 倍亮度差。
- **消隐期量化法**：采样 DMA `NDTR` 序列，消隐期占比 ≈ 窗口 / 帧周期。
- **调试钩子**（`bsp_camera.c` 中 `cam_snap_*` / `cam_flicker` / `cam_poke_*`）：
  用于复现与验证撕裂，正常工作流不依赖它们，可保留为可选调试特性或编译期剔除。

---

## 9. 目录结构

```
stm32_tinyusb/
├── CMakeLists.txt            # 构建（含 flash target）
├── cmake/arm-none-eabi-gcc.cmake
├── linker/STM32H743ZITx_FLASH.ld   # .framebuffer → RAM_D1
├── debug/openocd.cfg         # ST-Link SWD 烧录配置
├── startup/                  # 启动文件
├── Core/
│   ├── Inc/  main.h, bsp_camera.h, ov5640_ref.h, usb_descriptors.h ...
│   └── Src/
│       ├── main.c            # 主循环、sys tick
│       ├── bsp_board.c       # 板级初始化
│       ├── bsp_camera.c      # OV5640 + DCMI + DMA + 快照/撕裂修复
│       ├── ov5640_ref.c      # 参考驱动移植（替代 ST BSP 表）
│       ├── usb_descriptors.c # UVC 描述符
│       └── uvc_app.c         # 三缓冲调度、UVC 类回调
├── BSP/ov5640/               # ST 组件驱动（参考，实际用 ov5640_ref.c）
├── Drivers/                  # HAL + CMSIS
└── third_party/tinyusb/      # TinyUSB 子模块
```

---

## 10. 验证状态

- [x] DCMI 像素数据正确（参考驱动移植 + 极性修正）
- [x] USB UVC 实时显示图像
- [x] 剧烈变化拼接（tearing）修复 —— 消隐期相位锁存 + 三缓冲，A/B 验证通过
- [ ] 最终帧率（吞吐）测试

---

## 11. 快速上手

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cmake --build build --target flash
# 主机打开相机应用，应看到 240×240、约 8 fps、无拼接缝的画面
```
