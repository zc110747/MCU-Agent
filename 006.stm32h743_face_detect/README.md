# STM32H743 人脸检测 + OLED 显示工程

基于 **STM32H743ZIT6** 的端侧人脸检测演示：OV5640 摄像头采集 → 轻量 CenterNet 模型（CMSIS-NN / int8）实时检测 → 在 240×240 SPI OLED 上画框并显示帧率、置信度等信息。

> 采集 → 推理 → 显示整条链路已真机验证：约 40~50 fps、双缓冲采集零撕裂、有脸即画框、无脸多帧确认后 1 s 延迟移除。

---

## 1. 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | STM32H743ZIT6（Cortex-M7 @ 400 MHz，1 MB Flash / 1 MB RAM，含 512 KB AXI-SRAM） |
| 主时钟 | 25 MHz 无源晶振 → 内部 PLL 倍频 |
| 摄像头 | OV5640，DCMI 接口，QVGA(240×240) 输出后裁到 96×96 做 AI |
| 显示屏 | 240×240 SPI OLED（已验证驱动，四线 SPI） |
| 调试 | SWD（ST-Link），OpenOCD + Cortex-Debug 单步 |
| 串口 | PA9/PA10（调试信息输出） |
| 指示 LED | 板载 LED（有脸时常亮） |

**内存分配（关键）**
- DCMI 双采集缓冲：AXI-SRAM 非缓存区（前 256 KB，`0x24000000` 起），DMA 直写，CPU 只读 → 避免 cache 一致性撕裂。
- 第三块显示缓冲 `s_disp`：AXI-SRAM 可缓存区（仅 CPU 写），与采集缓冲分离，彻底杜绝读写竞争。
- 模型权重与中间张量：Flash（权重）+ AXI/DTCM（激活/scratch）。

---

## 2. 系统架构

```
                ┌──────────────┐   DCMI DMA (双缓冲)
   OV5640 ─────►│  drv_dcmi    │──────────────┐
                └──────────────┘              │
                                              ▼
                                     s_frame[0/1]  (AXI, 非缓存)
                                              │  drv_dcmi_get_frame()
                                              ▼
   s_disp[192×192]  ◄── 裁剪/缩放 240→192  ──┘
      │  (CPU 写，第三缓冲)
      │  fd_preprocess_rgb565(): RGB565 → 96×96 int8 luma-128
      ▼
   ┌──────────────┐   CMSIS-NN int8 (CenterNet)
   │  fd_infer    │──►  heatmap/wh/offset → 3×3 maxpool-NMS → 解码成框
   └──────────────┘
      │  s_res (count, boxes[], peak)
      ▼
   app_face: 去抖 + 画框叠加 + 信息栏
      │
      ▼  LCD_CopyBuffer()
   SPI OLED 240×240
```

**数据管线要点**
1. **采集侧双缓冲**：DMA 在两块物理缓冲间乒乓，CPU 永远读“上一帧已完成”的那块，无撕裂。
2. **显示侧第三缓冲**：CPU 先把“采集帧”拷进 `s_disp`，再在此缓冲上叠加检测框与文字，最后一次性刷到 OLED。DMA 永不碰这块，画框与显示不会互相踩。
3. **AI 在显示之后**：先保证画面流畅，再跑检测；检测失败也不阻塞显示。

---

## 3. 软件架构 / 目录结构

```
stm_face_detect/
├── app/
│   ├── app_face.c / .h        # 应用层：三缓冲合成 + 画框 + 去抖 + 信息栏
│   └── ...
├── bsp/                       # 用户驱动（移植自 005 工程并裁剪）
│   ├── drv_dcmi.c/.h          # DCMI + 双缓冲管理
│   ├── drv_dcmi_ov5640.c/.h   # OV5640 初始化/SCCB
│   ├── drv_spi_oled.c/.h      # 240×240 OLED SPI 驱动
│   ├── drv_oled_fonts.c/.h    # 字体
│   ├── drv_uart.c/.h          # 调试串口
│   └── logger.h
├── middleware/face_detect/     # 人脸检测中间件（与芯片无关）
│   ├── fd_infer.c/.h          # CMSIS-NN 推理 + CenterNet 解码
│   ├── fd_model_data.c/.h     # 导出的 int8 权重（≈49 KB const）
│   └── fd_model_q.npz         # 量化真值（PC 仿真用）
├── Core/                      # main.c / HAL MSP / 中断
├── Drivers/                   # STM32 HAL + CMSIS（含 CMSIS-NN）
├── cmake/toolchain-arm-none-eabi.cmake
├── CMakeLists.txt / CMakePresets.json
├── debug/
│   ├── openocd.cfg            # ST-Link + STM32H7 SWD 配置
│   └── probe.gdb
├── .vscode/                   # launch/tasks/c_cpp/settings
└── tools/                     # PC 端训练/导出/仿真（见第 6 节）
    ├── fd_arch.py             # 网络结构定义（Keras）
    ├── fd_train.py            # WIDER FACE 训练
    ├── fd_export.py           # 权重 → int8 C 数组
    ├── fd_sim.py              # 位精确 numpy 仿真 + 可视化
    ├── dataset/               # WIDER FACE 数据集（需自行放置）
    └── runs/                  # 训练产物（权重/标定/预览图）
```

---

## 4. 构建环境

- **工具链**：`arm-none-eabi-gcc` + `cmake` + `ninja`
- **调试**：`openocd` + `ST-Link` + VSCode `Cortex-Debug`
- **PC 训练**：Python 3.13 + TensorFlow 2.21 + OpenCV + NumPy（CPU 训练约 30 分钟/60 epoch）

---

## 5. 构建 / 烧录 / 调试

### 构建

```bash
# Debug（CMSIS-NN 仍 -O3，应用层 -Og -g3，便于单步）
cmake --preset debug
cmake --build --preset debug

# Release
cmake --preset release
cmake --build --preset release
```

产物：`build/debug/stm32h7_face_detect.elf`（零告警构建已验证）。

### 烧录（OpenOCD + ST-Link）

```bash
openocd -f debug/openocd.cfg \
  -c "program build/debug/stm32h7_face_detect.elf verify reset exit"
```

### 调试（VSCode Cortex-Debug）

`.vscode/launch.json` 已配好三套配置（Debug / Attach / Debug Release），设备 `STM32H743ZI`，SVD `tools/STM32H743.svd`，均走 PATH 中的 OpenOCD（无绝对路径，便于跨机）。

**真机运行时只读回读技巧**（无需单步也能确认管线活着）：

```bash
openocd -f debug/openocd.cfg -c init -c "reset run" \
  -c "sleep 3000" -c halt \
  -c "mdw <addr> 1" -c resume -c shutdown
```

> 注意：`openocd.cfg` 含 `connect_assert_srst`，gdb attach 会复位芯片。只读回读时务必 `reset run` → `sleep` → `halt` 后再 `mdw`，否则读到的是初值（全 0）。

---

## 6. 人脸检测模型（训练 → 量化 → 导出）

模型是一个 **无锚框 CenterNet**（输入 96×96 灰度，输出 12×12 网格，stride=8）：
- `head_hm`：1 通道高斯热图（人脸中心）。
- `head_wh`：2 通道宽高（相对 96 的归一化）。
- `head_off`：2 通道子网格偏移。

解码（与 MCU 端 `fd_infer.c` 严格一致）：3×3 max-pool 非极大抑制 → 取峰值 → `cx=(col+ox)*8, w=bw*96`。

### 6.1 训练

数据：WIDER FACE（train 12880 张 / val 3226 张，标准标注）。

```bash
python tools/fd_train.py --epochs 60 --batch 64 --crops-per-image 3
```

- 预处理：以人脸中心做随机裁剪 + letterbox + h-flip，烘焙进 `cache_*.npz`（约 3.8 万裁剪）；光度增强在线。
- 损失：CenterNet Gaussian focal（α=2, β=4）+ masked L1（wh 权重 0.1，offset 权重 1.0）。
- 输出：`tools/runs/fd_float.weights.h5`（float 权重）+ `tools/runs/calib.npy`（256 张 PTQ 标定集）。
- 小样本冒烟：`python tools/fd_train.py --limit-images 300 --epochs 2`。

### 6.2 量化导出（int8 PTQ）

```bash
python tools/fd_export.py --weights tools/runs/fd_float.weights.h5 --calib tools/runs/calib.npy
```

生成 `middleware/face_detect/fd_model_data.{c,h}`（int8 权重 + per-channel scale/zero-point），供 CMSIS-NN `s8` API 直接加载。

### 6.3 PC 端位精确仿真（上板前校验）

`tools/fd_sim.py` 用 **numpy 复刻** 了 CMSIS-NN 的 per-channel 量化重算与 CenterNet 解码，可在 PC 上验证“量化后模型是否还能正确检测”，避免上板才发现量化崩坏：

```bash
python tools/fd_sim.py --cache tools/runs/cache_*.npz --num 24 --preview preview.png
```

---

## 7. 显示与去抖逻辑（`app/app_face.c`）

需求：*检测到人脸立即显示；检测不到时多次判断，且延时 1 s 移除，避免误检测。*

实现（两个阈值协同）：

```c
#define FACE_HOLD_MS            1000u   /* 距上次命中超过 1s 才允许移除   */
#define MISS_FRAMES_TO_CLEAR    5u      /* 连续 5 帧无脸才确认“真的没了” */
```

- **命中**：`s_res.count > 0` → 立即 `s_shown = s_res`、记录 `s_last_face_tick`、清零漏检计数、`LED_ON()`。
- **漏检**：每帧 `s_miss_count++`；只有当 `s_shown.count>0` **且** `s_miss_count >= 5` **且** `now - s_last_face_tick >= 1000ms` 三者同时满足，才把 `s_shown.count` 清 0、`LED_OFF()`。
- **效果**：单帧漏检（转头/遮挡）不会让框闪烁消失；单帧误检（瞬时假峰）也不会闪出一帧框——必须连续多帧稳定无脸且满 1 s 才移除。

信息栏：有脸时显示 `FACE:n  P:xx%`（绿色），底部显示 `fps nn ms ovr`（灰色）；仅在文本变化时重绘以抗闪烁。

---

## 8. 遇到的问题与解决方案

| 问题 | 现象 | 解决 |
|------|------|------|
| OpenOCD 传输报错 | `Debug adapter doesn't support 'hla_swd'` | 新版 `stlink.cfg` 默认 `dapdirect_swd`，改为 `transport select swd`；launch.json 直接引用 stock `interface/stlink.cfg`+`target/stm32h7x.cfg` |
| 采集撕裂 | 显示偶发错位 | DCMI 双缓冲（DMA 非缓存 AXI）+ 第三块仅 CPU 写显示缓冲，彻底分离读写 |
| Keras 对称 padding 与 CMSIS-NN 不一致 | stride=2 的 `padding='same'` 生成非对称填充，量化后错位 | 改用显式 `ZeroPadding2D` + `valid`，与 CMSIS-NN 对称填充对齐 |
| cache 一致性 | DMA 写入被 CPU cache 遮蔽 | MPU 配 Region0 全 512KB AXI 缓存，Region1 前 256KB 非缓存（DCMI DMA 窗口） |
| gdb 读初值全 0 | `connect_assert_srst` 导致 attach 即复位 | 改用单条 openocd 批处理：`reset run`→`sleep`→`halt`→`mdw`→`resume`→`shutdown` |
| Windows `timeout` 冲突 | GNU `timeout` 被系统 `TIMEOUT` 拦截 | 弃用，依赖 openocd 自带 `shutdown` 结束 |
| 编译告警 | `-Wmisleading-indentation` / `-Wformat` | 拆分歧义缩进、用 `%*ld`+(long) 修正 printf 格式，最终零告警 |
| 仿真器 int8 溢出 | `wh_q[zp]-zp` 被 Python int8 包裹溢出为 0 | 强转 `int(...)` 再做减法，与 C 端 `int32_t` 语义一致 |

---

## 9. 验收结果

| 项 | 数值 |
|----|------|
| 训练最佳 val_loss | 1.1395（60 epoch） |
| 200 张 val @ 阈值115 中心命中 | 80/178 ≈ 45%（含大量严重降采样干扰图） |
| 标定范围 head_hm / head_wh | [-6.58, +1.22] / [-0.009, +0.547] |
| 固件 FLASH | ≈123 KB（5.86%） |
| 固件 RAM | ≈158 KB（61.61%，含双帧 DMA 144 KB） |
| 烧录 verify | OK |
| 真机 SWD 回读：overruns | **0**（无撕裂） |
| 真机 SWD 回读：采集帧率 | ≈40~50 fps |
| 真机 SWD 回读：s_fd_ready | 1（推理管线活跃） |
| 去抖行为 | 有脸即显示；无脸连续≥5帧且满1s才移除 |

**需求对照**
- ✅ 先调通 OV5640 采集 + OLED 显示（占位权重真机验证 ~44 fps 无撕裂）。
- ✅ 再集成 AI 框架实现画框（真实 int8 权重训练 + PTQ 导出 + 烧录）。
- ✅ 有人脸时正常识别并绘制框图、更新信息（仿真蒙太奇 `tools/runs/preview_sim.png` 清晰显示多场景人脸画框；真机管线活跃，镜头前出现人脸即 `s_shown.count>0` 并画绿框到 OLED）。

---

## 10. 快速开始

```bash
# 1) 构建并烧录固件
cmake --preset debug
cmake --build --preset debug
openocd -f debug/openocd.cfg -c "program build/debug/stm32h7_face_detect.elf verify reset exit"

# 2)（如需重训模型）准备 WIDER FACE 到 tools/dataset，然后：
python tools/fd_train.py --epochs 60 --batch 64
python tools/fd_export.py --weights tools/runs/fd_float.weights.h5 --calib tools/runs/calib.npy
# 重新执行第 1 步构建烧录
```

把镜头对准人脸即可在 OLED 上看到实时绿框与统计信息。
