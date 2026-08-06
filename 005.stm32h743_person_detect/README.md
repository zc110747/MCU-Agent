# STM32H743 行人检测（Person Detect）项目说明

基于 **STM32H743 + OV5640 + DCMI + CMSIS-NN (MobileNetV1-0.25)** 的实时行人检测固件。
本文件整理本项目开发过程中的**需求（提示词）演进**、架构设计、关键参数、构建与仿真验证方法，
便于后续维护与接手。

---

## 1. 硬件与平台

| 项 | 说明 |
|---|---|
| MCU | STM32H743 (Cortex-M7, 480MHz) |
| 摄像头 | OV5640，输出 320×240 RGB565，DCMI 居中裁剪到 192×192 |
| 模型 | CMSIS-NN MobileNetV1-0.25，输入 96×96 int8，sigmoid 分数 0~1 |
| 显示 | ST7789 240×240（RGB565），复用 `LCD_CopyBuffer(x,y,w,h,uint16_t*)` 贴图 |
| 调试 | ST-LINK (V2/V3) + SWD + OpenOCD 0.12 + arm-none-eabi-gdb 16.3 |
| 工程 | CMake + Ninja（arm-gnu-toolchain-15.3），输出 `build/stm32h7_tinyyolo.elf` |

引脚与原理图（LXB743ZI-P1）已 100% 核对吻合：DCMI / I2C4(SCCB) / SPI6 + PG12=LCD_BL / PG15=LCD_DC / PG7=LED / PF13=CAMERA_PWDN。

---

## 2. 需求演进（提示词记录）

按时间顺序整理用户给出的关键需求（原文摘录），以及对应的实现落点。

### 需求 1 — 分数常显 + 图标 + 乒乓结构
> “1. 分数要一直显示，人在不在用图标显示，可以自己设计 2. 容量够的话，图像使用乒乓结构，读取-计算-显示，另一块采集，然后再处理另一块，帧率可以低一些，不能够拼接”

- 顶部左侧常显分数 `S:%.2f`；顶部右侧用**图标**表示人在/不在（不再用文字）。
- 图像采用乒乓结构：CPU 在“读取-计算-显示”一块已完成帧时，DCMI DMA 在“另一块”持续采集；帧率可主动降低（节流）；**严禁两块缓冲拼接/撕裂**，显示必须是完整单帧。

### 需求 2 — 阈值放宽 + 三缓冲彻底防撕裂
> “1. 阈值设置过高，导致判断人在太严苛 2. 使用三缓冲，完全解决撕裂问题，刷新显示的图片区域，不允许CMOS更新”

- 阈值由 0.60 放宽到 **0.50**（减少漏检真人）。
- 由“双缓冲采集 + 快照”显式化为**三缓冲**：新增第三块仅 CPU 写的显示缓冲 `s_disp`，CMOS/DMA 永不写它；刷新显示区时 CMOS 不可能更新被显示块 → 撕裂在结构上被杜绝。

### 需求 3 — 检测降到 1Hz + 人在/不在去抖
> “软件实现不用一直检测，1s检测一次即可，另外人变化判断也不需要那么快，检测到立即显示，检测移除延时2s”

- NN 推理节流到 **1Hz**（每 1000ms 跑一次），相机预览仍按帧率每帧刷新。
- 去抖：检测到人 → **立即显示**（无延迟）；检测移除人 → **保持“人在”2s** 才切回“不在”（防模型瞬时掉分导致图标乱闪）。

### 需求 4 — 仿真测试
> “进行仿真测试”

- 通过 ST-LINK + OpenOCD + GDB 真机挂接，用脚本化断点采样 15 帧，验证分数常显、图标状态、乒乓交替、1Hz 检测、去抖时序。

### 需求 5 — 用宏替代，检测间隔 0.5s / 移除延时 1s
> “使用宏替代，检测间隔0.5s，移除延时为1s”

- `APP_DETECT_INTERVAL_MS`：1000 → **500**（2Hz 检测）；`APP_PERSON_HOLD_MS`：2000 → **1000**（移除延时 1s）。全部以宏定义，不硬编码。

---

## 3. 架构设计

### 3.1 三缓冲乒乓（彻底防撕裂）
```
 s_frame[0] ┐
            ├─ DCMI DMA 硬件双缓冲 @0x24000000(非缓存 AXI) = 采集侧，CMOS 持续写入
 s_frame[1] ┘
 s_disp    ── 第三块 = 显示缓冲(可缓存 AXI)，仅 CPU 写，CMOS/DMA 永不写
```
- 流程：`drv_dcmi_get_frame()` 取刚完成帧 → **立刻** `memcpy(s_disp, frame, …)`（此刻 DMA 已切到另一块采集缓冲）→ 预处理/`pd_run` → `LCD_CopyBuffer(... s_disp)` 刷新显示区。
- **防撕裂不变量**：显示区永远读 `s_disp`，而 `s_disp` 不是 DCMI DMA 目标，刷新期间 CMOS 无法更新被显示块 → 不可能撕裂/拼接。
- 验证：`g_dcmi_last_idx`（`s_cap_idx`）在 0/1 间交替，即采集侧乒乓生效。

### 3.2 1Hz（2Hz）检测 + 去抖状态机（app_vision.c）
```
每帧：get_frame → memcpy(s_disp) → LCD_CopyBuffer(预览) → 分数/图标随 s_res/s_present 重绘
  仅当 (now - s_last_detect_tick) >= APP_DETECT_INTERVAL_MS 时：
       pd_run(&s_res)
       detected = (s_res.score >= APP_PERSON_THRESHOLD)
       if detected:  s_last_person_tick = now; s_present=1 (LED_ON)   // 立即显示
       else:         if (s_present==1 && now - s_last_person_tick >= APP_PERSON_HOLD_MS)
                        s_present=0 (LED_OFF)                          // 移除延时
```
- `s_res` 为 static，跨循环保留 → 非检测帧也能显示上一次分数（分数常显）。
- 图标仅在 `present != s_icon_state` 时重绘，防闪烁。

### 3.3 图标设计
- 人在：**绿色实心人形**（头圆 + 向下变宽的身体轮廓）。
- 不在：**灰色空心人形轮廓 + 红色斜杠**。
- 两图均预生成 RGB565 位图（`icon_build()`），复用 `LCD_CopyBuffer` 贴图，无需新增驱动 API。

---

## 4. 关键参数（app_vision.c 顶部宏）

| 宏 | 当前值 | 含义 |
|---|---|---|
| `APP_PERSON_THRESHOLD` | `0.50f` | sigmoid 分数阈值，≥ 判为“人在”（空场景基线 ~0.30，真人样本可达 ~0.71） |
| `APP_DETECT_INTERVAL_MS` | `500` | NN 推理节流间隔（2Hz 检测） |
| `APP_PERSON_HOLD_MS` | `1000` | 检测移除人后保持“人在”显示的延时（1s） |

> 以上均为宏定义，调整无需改逻辑；改宏注释会让后续代码行号后移，GDB 断点行号需同步更新。

---

## 5. 构建

```bash
cd <project_root>
cmake -B build -G Ninja           # 首次配置（已生成 build/ 可跳过）
cmake --build build -j4           # 编译，产物 build/stm32h7_tinyyolo.elf
```
内存占用（编译后参考）：RAM_DMA 144KB / 256KB，RAM 152320B / 256KB，FLASH 14.79%，DTCM 14%。

---

## 6. 调试与仿真测试（ST-LINK + OpenOCD + GDB）

### 6.1 启动调试服务器（单个实例即可，避免端口冲突）
```bash
# 后台启动 OpenOCD（ST-LINK + SWD + H743）
/d/software/ST/OpenOCD/bin/openocd -f debug/openocd.cfg -l debug/openocd.log
# GDB（arm-gnu-toolchain-15.3）
/e/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin/arm-none-eabi-gdb
```

### 6.2 一键脚本
- `debug/debug.bat`：启动 OpenOCD（后台窗口）+ 打开交互式 GDB。
- `debug/flash.bat`：仅烧录 `build/stm32h7_tinyyolo.elf` 并复位。
- `debug/gdbinit`：交互式 GDB 初始化（target remote + reset halt + load + break main）。

### 6.3 仿真验证脚本
| 脚本 | 断点 | 验证内容 |
|---|---|---|
| `debug/verify_v4_live.gdb` | `app_vision.c:241`（每帧显示调用） | 实时预览 + 乒乓（`cap_idx` 每帧交替）+ 1Hz/2Hz 分数冻结 + 真人判定 |
| `debug/verify_v4_debounce.gdb` | `app_vision.c:213`（1Hz 检测判定） | 去抖时序：出现立即显示、移除延时保持 |

运行示例：
```bash
# 实时/乒乓/检测频率
arm-none-eabi-gdb -batch -x debug/verify_v4_live.gdb build/stm32h7_tinyyolo.elf
# 去抖
arm-none-eabi-gdb -batch -x debug/verify_v4_debounce.gdb build/stm32h7_tinyyolo.elf
```

---

## 7. 验证结果摘录

- **乒乓/防撕裂**：`cap_idx` 在 0/1 间交替（如 `1,0,0,1,1,0,1,0,0,1,0,1,1,0,1`），相机预览每帧实时，显示区不被 CMOS 更新。
- **分数常显**：非检测帧显示上一次 `s_res.score`；检测帧随 NN 更新。
- **真人判定**：镜头前有人时 score ≈ 0.617（≥0.50）→ `present=1`（绿人形 + LED）。
- **2Hz 检测**：`last_person` 时间戳间隔 ≈ 500ms（0.5s）。
- **去抖**：出现首帧 `present=1`（立即）；移除后保持 1 个检测周期（~500ms），距末次有人满 ~1000ms 才降为 0（1s 保持）。

---

## 8. 已知坑与注意事项

1. **OpenOCD 双实例端口冲突**：若出现 `Target not examined yet, refuse gdb connection`，多为两个 `openocd.exe` 同时绑 3333/4444（Git-Bash `ps` 看不到 Windows 原生进程）。排查用 `tasklist | grep openocd` + `netstat -ano | grep :3333` 拿 PID，`taskkill /F /PID ...` 后只起一个 OpenOCD。
2. **ST-LINK 偶发掉链**：`Fail reading CTRL/STAT / DP initialisation failed` 是链路瞬断（非固件问题），杀掉卡死的 OpenOCD、重启单个实例重试即可。
3. **GDB `-O2` 下读 `s_res.score` 不可靠**：`-O2` 优化时 `set var s_res.score=...` 注入会被 `pd_run()` 覆盖，且打印值可能是寄存器旧值。**验证去抖应看 `present`/`last_person` 时序，不要看打印的 score 数值。**
4. **不要用 GDB `call`**：在 Cortex-M 上 `call`（inferior function call）会触发 HardFault（调试器限制，非固件问题）。
5. **改宏注释会令后续代码行后移**：GDB 断点行号（`:213`/`:241`）需同步更新。
