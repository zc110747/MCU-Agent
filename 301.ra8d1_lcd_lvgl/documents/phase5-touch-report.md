# Phase 5 触摸（CST812T）排查报告

> 目标：让 MIPI 2.0"（480×360）面板的 **CST812T** 电容触摸在真机可用。
> 结果：**固件链路全部验证正确；触摸控制器在本机上不响应任何 I2C 事务，判定为硬件/面板层故障**（非本工程软件问题）。
> 方法：全程 SWD 读硬件寄存器取证 + 位翻转 I2C + 串口实时状态，未做任何基于猜测的修改。

---

## 1. 结论速览

| 项 | 状态 | 证据 |
|---|---|---|
| 引脚复用 P408/P409 → SCI3 | ✅ 正确 | SWD 读 `PmnPFS`：`PMR=1 PSEL=5`，与工作的 P208（控制台 UART9）逐位一致 |
| IELSR[8]/[9] 中断映射 | ✅ 正确 | SWD 读 `0x4000C300`：`[8]=0x13A [9]=0x13B`（SCI3_TXI/TEI） |
| NVIC 使能 | ✅ 正确 | `ISER0=0x000003FF`，IRQ8/9 已使能 |
| 时钟 / 波特率 | ✅ 正确 | `CCR2`：BRR=22、MDDR=157（≈99.99 kHz）；与官方 `hal_data.c` 逐值一致 |
| 中断回调确实触发 | ✅ 正确 | `cb_count=10`（5 次重试×2 事件），ISR 正常进入 |
| 从机地址 0x15 | ✅ 正确 | 与官方 `cst812t.h` `TOUCH_SLAVE_ADDRESS 0x15` 一致 |
| 总线空闲电平 | ✅ 正常 | 固件读回：SCL(P408)=**high**、SDA(P409)=**high**（板上 R66/R67 10K 上拉到 +5V_SYS） |
| **从机响应** | ❌ **无** | SCI3 与手工位翻转 I2C 均 **NACK**；全地址扫描 0x08–0x77 找到 **0** 个设备 |

**判定**：固件与总线配置正确，但**触摸控制器不上总线**。属硬件层面（面板 FPC 未插好/面板变体不符/触摸供电或缺省上拉未贴/器件损坏），不是本工程软件能修复的问题。

---

## 2. 硬件事实（原理图 + SWD 取证）

### 2.1 引脚（Vision_Board_schematic.pdf）

- p3「RA8D1 Microcontroller」：`B14 = P408`、`E10 = P409`；DISPLAY CTRL 网络 `SCL0/SDA0` → P408/P409。
- p9「MIPI LCD DISPLAY INTERFACE」：30-pin FPC（CN2）含 `CTP_SCL / CTP_SDA / CTP_RST / CTP_IRQ_N`。
- p9 触摸上拉电阻（网络名逐行核对）：

  | 电阻 | 值 | 网络 | 说明 |
  |---|---|---|---|
  | R64 | 10K | CTP_RST | 已贴 |
  | R65 | 10K **[DNP]** | CTP_IRQ_N | **未贴**（INT 由面板推挽驱动，无需上拉） |
  | R66 | 10K | CTP_SCL | 已贴，上拉到 **+5V_SYS** |
  | R67 | 10K | CTP_SDA | 已贴，上拉到 **+5V_SYS** |

- 触摸总线 **无电平转换器**（U3 实为 USB / MIPI 用），`SCL0/SDA0` 直接连 P408/P409。

### 2.2 关键寄存器地址（本轮修正）

| 名称 | 地址 | 备注 |
|---|---|---|
| `R_ICU_BASE` | `0x40006000` | `IELSR[]` 在 **+0x6300** → **`0x4000C300`**（此前误用 0x40006200 导致测量无效） |
| `R_PFS_BASE` | `0x40400800` | `R_PFS.PORT[15]`，每端口 16 pin × 4B；`PmnPFS = 0x40400800 + m*0x40 + n*4` |
| `PmnPFS` 位域 | — | bit2 `PDR`、bit4 `PCR`、bit6 `NCODR`、bit16 `PMR`、**bits[28:24] `PSEL`** |
| SCI3 | `0x40358300` | `CCR0=0x08`、`ICR=0x20`、`CSR=0x48`、`ISR=0x4C` |

### 2.3 SWD 实测值

```
IELSR[0..9] @0x4000C300:
  [0]=0x163 [1]=0x1CD [2..7]=0x1D3..0x1D8 [8]=0x13A [9]=0x13B    ← 正确

PmnPFS:
  P408 = 0x05010002  PMR=1 PSEL=5   (SCI3 SCL)   ┐ 与
  P409 = 0x05010002  PMR=1 PSEL=5   (SCI3 SDA)   │ P208 (控制台 UART9,
  P208 = 0x05010002  PMR=1 PSEL=5   (SCI9 TXD)   ┘ 实测工作) 逐位一致

SCI3: CCR2 = 0x9D011604  →  BRR[15:8]=22  MDDR[31:24]=157   ← 与官方一致
      ICR  = 0x00F0231F  →  IICINTM=1 IICCSC=1 IICACKT=1 IICDL=31
      ISR  = 0x00000031  →  IICACKR(bit0)=1 (NACK)
```

---

## 3. 诊断过程（按时间线，均为取证）

1. **对照官方**：`Drivers/.../r_sci_b_i2c.c` 与官方工程**逐字节相同**；`hal_data.c` 的时钟/IRQ/rate/channel 与本地配置**逐值一致**；`bsp_clock_cfg.h` 仅 LCDCLK 源不同（与触摸无关）。→ 排除驱动/时钟差异。
2. **修正 IELSR 地址**：此前用 `0x40006200` 读写全部无效；改 `0x4000C300` 后确认 IELSR **本就正确**（此前"只编了 [0..7]"的结论作废）。
3. **回调计数**：`cb_count=2`（后经 5 次重试为 10）证明 ISR 真实进入，失败在**从机 NACK**而非超时。
4. **PmnPFS 实测**（修正地址后）：P408/P409 与工作的 P208 完全一致 → 引脚复用正确。
5. **总线电平**：固件把 P408/P409 切成普通输入后读回 **两线均高**（且握手期间 SCL 被 SCI3 拉低是正常传输态）→ 总线电气正常、无对地短路。
6. **手工位翻转 I2C**（`tools/dbg/i2c_bitbang_probe.py`，完全绕开 SCI3）：`START + addr 0x2A` → **NACK**；扫描 0x14/0x15/0x1A/0x38/0x5D/0x70 → **全 NACK**。
7. **固件全地址扫描**（`touch scan`，0x08–0x77）→ **0 个设备**。
8. **重试与总线恢复**：加入 5 次重试 + 9 时钟恢复，仍全 NACK（恢复函数因无收益已移除）。

> 排除了：向量映射、NVIC、时钟/波特率、引脚复用、从机地址、传输模型、总线电气、驱动版本。

---

## 4. 交付物（本阶段新增/修改）

| 文件 | 说明 |
|---|---|
| `bsp/ra8d1-vision-board/bsp_touch.c/.h` | CST812T 驱动（FSP `r_sci_b_i2c`，SCI3，P408/P409，RST=P000） |
| `applications/main.c` | `touch info/init/read/scan/id` 控制台命令 |
| `applications/lv_port.c` | LVGL pointer indev 接入（探测成功才注册） |
| `tools/dbg/inspect_regs.py` | SWD 读 IELSR / PmnPFS / SCI3（地址已修正） |
| `tools/dbg/i2c_bitbang_probe.py` | SWD 位翻转 I2C 总线探测（独立于 SCI3） |
| `tools/dbg/sci3_bus_forensics.py` | SWD 总线电平/恢复取证 |
| `tools/dbg/console_diag.py` | 串口命令捕获 |
| `bsp/ra8d1-vision-board/bsp_touch.c` | 新增总线健康检查：失败时打印 `SCL/SDA` 实际电平 |

---

## 5. 后续建议（属硬件 / 下个项目）

1. **复查面板 FPC**：确认 2.0" 面板插到位、方向正确（30-pin，CTP 在 FPC 上）。
2. **核对面板变体**：本板需 **CST812T** 面板；若换用 RGB 4.3" 变体（GT9147）或其它面板，触摸总线不同。
3. **量测 +5V_SYS**：R66/R67 上拉依赖该轨；若缺失，总线无上拉（但本次实测空闲为高，此项可能性低）。
4. **示波器**：抓 P408(SCL)/P409(SDA) 波形，确认 START/地址位是否有实际跳变、面板侧是否有 ACK 位下拉。
5. 若确认面板正常而 MCU 侧仍无响应，可换用**其它 SCI 通道**（SCI1/5/7/9 同 PSEL 组）交叉验证；本工程已把 PSEL 与通道解耦，改动仅在 `bsp_touch.c` 的 `channel`。

---

## 6. 状态

- 固件：Debug/Release 双构**零警告**，已烧录真机。
- 触摸：**未连通**（硬件层）。菜单仍可用 `menu up/down/enter/back/select N/list` 串口驱动（无触摸可用的降级路径已实现）。
- 文档：`hardware.md` / `pinmap.md` 的触摸条目已从错误的 GT9147/P512 更正为 **CST812T / P408(P409) / SCI3**。
