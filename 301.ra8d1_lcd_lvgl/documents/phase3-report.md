# Phase 3 报告 — Camera（OV5640 + SCCB + GPT7 XCLK + CEU 采集）

> 目标：打通 OV5640 摄像头 → CEU → SDRAM framebuffer 的完整采集链，并用串口命令 +
> SWD 读回做端到端验收。验收脚本：`tools/verify/verify_phase3.py` → **14 passed, 0 failed**。

## 1. 交付内容

| 模块 | 文件 | 说明 |
|---|---|---|
| SCCB 位bang 主机 | `bsp/ra8d1-vision-board/bsp_sccb.c/.h` | GPIO 位bang（P1013 SCL / P1014 SDA，开漏读回），100kHz 时序（半位 5µs），probe/read8/read16/write8/write16 |
| OV5640 驱动 | `bsp/ra8d1-vision-board/bsp_ov5640.c/.h` + `bsp_ov5640_default_regs.h` | 电源时序（PWDN/NRST）、ID 读取（0x300A/B = 0x5640）、default_regs + QVGA RGB565 寄存器表、colorbar 开关（0x503D bit7） |
| 摄像头子系统 | `bsp/ra8d1-vision-board/bsp_cam.c/.h` | GPT7 XCLK（P1006，24MHz，period=5/duty=2 @120MHz PCLKA）、CEU 实例（官方 g_ceu_qvga 配置逐值复刻）、扫描/init/snap（FRAME_END 信号量）/colorbar |
| 上游驱动（原样拷贝） | `Drivers/renesas/fsp/src/r_ceu/`、`r_gpt/`、`inc/instances/r_ceu*`、`r_gpt*`、`inc/api/r_capture_api.h`、`r_timer_api.h` | FSP CEU + GPT 模块 |
| 中断接线 | `bsp/ra8d1-vision-board/gen/vector_data.c` | IRQ slot 1 → `EVENT_CEU_CEUI (0x1DA)`，`g_ceu_ceui` 入口 |
| 应用 | `applications/main.c` | `cam` 命令：`init / scan / snap / stat / rd A R / bar on\|off`；`cmd_parse_u32` 裸十六进制兼容 |
| 验收脚本 | `tools/verify/verify_phase3.py` | 串口 + SWD 双通道，14 项 PASS/FAIL |
| 取证脚本（保留备查） | `tools/verify/probe_*.py`、`dump_gpt7*.py`、`test_*.py` 等 20 个 | 第 4 节取证链所用一次性探针 + `gpt7_dump_ours/official.json` |

## 2. 数据通路

```text
OV5640 (QVGA 320x240 RGB565, 8-bit DVP)
   ▲ XCLK 24MHz = GPT7 GTIOC7B @ P1006（PCLKA 120MHz / 5, duty 40%）
   │ SCCB (P1013/P1014 位bang 100kHz, 7-bit addr 0x3C)
   ▼
CEU（DATA_SYNCHRONOUS, 8-bit bus, HSYNC/VSYNC 高有效, byte swap 8/16/32 全开）
   │  VSYNC/HSYNC 引脚 P302/P301（复用自官方 pin 配置）
   ▼
g_cam_frame[153600B] @ 0x680BB800 (.nocache_sdram, LCD fb 之后)
   │  FRAME_END 中断 (slot 1 = EVENT_CEU_CEUI) → rt_sem_release
   ▼
cam snap 命令：rt_sem_take(200ms) → 校验和输出
```

关键配置（与官方 `vision_board_camera` 工程 `hal_data.c` 逐值一致）：

| 项 | 值 |
|---|---|
| GPT7 | TIMER_MODE_PWM，period=5，duty=2，channel 7 → 24MHz XCLK |
| CEU | 320x240 bpp2，`image_area_size=BSP_CAM_FRAME_BYTES`，中断掩码 CPEIE/VDIE/CDTOFIE/VBPIE/NHDIE/NVDIE，ipl=10 |
| OV5640 | 7-bit 0x3C（SID 高接法），ID 0x5640，QVGA RGB565 寄存器表 |

## 3. 实现要点

- **XCLK 引脚 = P1006（GTIOC7B），不是 P1011**——本 Phase 最大坑，取证见第 4 节。
  P1011 是 GTIOC6B（官方 g_timer6 的 546µs PERIODIC 输出，在官方固件里作背光 PWM），
  本工程保持其为普通 GPIO 全亮背光。
- **官方上电序列**：PWDN 拉高→拉低、NRST 拉低→拉高（各 10ms），XCLK 先行，20ms 稳定后扫描；
  另保留 4 组极性组合回退（兼容不同接线模组）。
- **地址检查放宽**：OV5640 双地址 0x36（SID 低）/ 0x3C（SID 高）均合法，以 ID==0x5640 为准。
- **帧缓冲布局**：`g_cam_frame` 紧随 LCD framebuffer（0x68000000+768000）之后，
  `.nocache_sdram` NOLOAD；D-Cache 关闭（`BSP_CFG_DCACHE_ENABLED=0`）故天然一致。

## 4. 根因取证链（cam scan 全地址 NACK → XCLK 引脚配错）

症状：固件 `cam scan` 对 0x15..0x77 全地址 NACK；同板官方预编译固件 `camera.hex`
却打印 `i2c probe camera id:0x78`（7-bit 0x3C）。取证按排除法推进：

| # | 实验 | 结论 |
|---|---|---|
| 1 | 调试器 halt 直写 PCNTR3@**0x40400168**（Port11 块 0x40400160 + 0x08），PODR/PIDR 完全跟随 | 位bang 写路径（PCNTR3）有效，洗清固件嫌疑。此前手写实验用错地址（0x40040000 笔误 + 块内偏移误解）作废 |
| 2 | PFS 位域头文件再实证：每引脚一字、单比特字段（PODR@bit0、PIDR@bit1、PDR@bit2、PMR@bit16） | 探针 `& 0x2` 采 PIDR 正确；前轮"探针测错位"的自我怀疑撤回 |
| 3 | halt 采样 PC 两次（0x02004e56/0x02004e54，addr2line → `rt_defunct_execute`） | CPU 活着（idle 线程），"卡死"假象排除 |
| 4 | **pyOCD connect 后不 reset → shell RX 致聋**（TX/心跳正常，P208/P209 PFS PMR=1 PSEL=5 正常）；connect 后紧跟 `t.reset()` → 一切正常；判别实验证明 `cam init` 不杀 shell | v2/v3 探针"零应答"的真凶是调试器会话，不是固件。所有后续脚本固化 connect+reset |
| 5 | shell 健康会话中 26940 次采样 SCL/SDA 均现 {0x0,0x2} 双电平 | 位bang 物理翻转无罪 |
| 6 | 烧官方 `camera.hex`：`i2c probe camera id:0x78` → 硬件/模组良好，传感器在 7-bit 0x3C | 排除硬件损坏；本模组 SID 高接法 |
| 7 | SWD dump GPT7 全块 66 项（含 POEG0/MSTPCRC）逐位 diff：两固件完全一致（仅 GTSTR/GTSTP/GTCNT 快照差），但 P1011 上 300 采样 PIDR 恒 0 | "寄存器一致却没输出"矛盾 → 怀疑引脚复用，而非 GPT 配置 |
| 8 | 读官方 `hal_data.c`/`pin_data.c`：**两个 GPT 实例**（ch6@line86、ch7@line240）+ **两个 GPT1 组引脚**（P1006@line435、P1011@line451）；官方 GTSTR=0xC0 = ch6+7 都在跑 | P1011 上的 PWM 是 channel 6 的输出；此前把两个引脚角色对调 |
| 9 | **P1006 PSEL 扫描（固件侧改 PFS→读 PIDR）**：GPT7 运行时 PSEL=3（GTIOC7B）出现 PWM，其余 PSEL 无 | **铁证：XCLK = P1006** |

修复：`CAM_XCLK_PIN = BSP_IO_PORT_10_PIN_06`，P1011 保持 GPIO 背光。修复后
`[cam] scan: ACK addr7=0x3c id=0x5640 (OV5640)`、`cam init: OK (0x0)`、`cam stat: cam ready`。

## 5. 其他修复（本轮）

| 坑 | 现象 | 修法 |
|---|---|---|
| SCCB 读 NACK 语义反了 | `bus_read_byte(more=false)` 实发 ACK，单字节读后 SDA 不释放 | 语义重定义：`nack=true` 释放 SDA（结束读），两处单字节读调用点改 `bus_read_byte(true)` |
| SCCB 时序偏快 | 半位 2µs（250kHz 等效），距官方 100kHz 远 | 半位 2µs → 5µs |
| 上电序列缺失 | 扫描前无 PWDN/NRST 电源循环，冷启动不确定 | 对齐官方 `sensor_probe_init()` 前置序列 + 4 组极性回退 |
| `cam rd 3c 300a` 解析成十进制 | `cmd_parse_u32` 无 0x 前缀按十进制（`3c`→3） | 无前缀但含 a-f 字母时按十六进制；usage 字符串同步更新 |
| PWPR 无法从 SWD 解锁 | `R_PMISC->PWPR (0x40400D0C)` SWD 访问直接 TransferFault | PMISC 区安全归属性阻挡，调试器不可解锁；引脚复用扫描改在**固件侧**做 PFS 写回读（PFS 写未被挡） |

## 6. Colorbar 分析（采集正确性判据）

`cam bar on`（0x503D bit7）后抓帧，SWD 读回 153600B 分析：

- **8 条竖直条带**，边界 ≈ px 36/78/119/159/200/241/282，周期 ≈ 320/8 = **40px** ✓
- 行间完全一致（row0 == row1/50/100/200）→ 条纹只沿 X 变化 ✓
- 重抓帧逐字节一致 → 采集确定性 ✓
- top-8 像素值覆盖 95%，8 个条带中心色：`0xffff 0xffa8 0x57fd 0x2fe1 0xe81f 0xc005 0x015a 0x0020`
  （白/黄/青/绿/品红/红/蓝/黑系，顺序符合标准色条）

条带内颜色为 OV5640 测试图案的实际输出值（与理想 RGB565 原色存在系统性位偏差），
属于传感器端特性而非采集损坏（条带内逐像素位级稳定、行均匀、可复现）。
**像素格式保真度在 Phase 4 上屏时目视终判**（LCD 直接显示抓帧画面）。

## 7. 实测数据

- **双构零警告**：Debug FLASH 60448B(2.88%) / 内部 RAM 67616B(6.45%)；
  Release FLASH 50944B(2.43%) / RAM 67472B(6.43%)
- SDRAM：`.sdram` 768000B（LCD fb）+ `.nocache_sdram` 153600B（camera fb）= 921600B（32MB 的 2.75%）
- 验收：`python tools/verify/verify_phase3.py` → **14 passed, 0 failed**

## 8. 复用 Pitfall（跨工程有效）

1. **pyOCD connect（无 reset）致聋 RT-Thread shell RX**：TX/心跳正常、引脚配置正常，
   `t.reset()` 即恢复。所有"串口+SWD"混合脚本：先开串口 → connect → 立即 reset。
2. **RA8 PMISC（PWPR）从 SWD 不可访问**（安全归属性，TransferFault 不可解锁）：
   需要 PFS 写的实验放固件侧做，不要在调试器侧死磕。
3. **RA8 端口寄存器布局**：PCNTR 块 stride=0x20，PCNTR3 在块内 +0x08
   （POSR 低16/PORR 高16）；Port11 PCNTR3 = 0x40400168。
4. **官方 BSP 双外设实例陷阱**：同一外设两个实例（GPT ch6/ch7）时，寄存器逐位一致
   不代表引脚角色一致——以 PSEL 扫描实测输出为准。
