# STM32H743 TinyUSB UVC 项目 — 项目记忆

## 硬件平台
- 芯片: STM32H743ZIT6 (鹿小班开发板)
- 编译器: arm-none-eabi-gcc (CMake) + ARMCLANG (Keil MDK-ARM)；双构建并存
- 调试: ST-Link + OpenOCD + cortex-debug
- USB: FS (PA11/PA12), dwc2 rhport 0 @ 0x40080000
- 内存: AXI SRAM 0x24000000 (512KB, non-cacheable via MPU Region0)

## 工程结构（2026-09-11 重构：Core/ → app/ + bsp/）
- `app/` 应用层：main.c/main.h、uvc_app.c/h、stm32h7xx_it.c、stm32h7xx_hal_msp.c、
  stm32h7xx_hal_conf.h、tusb_config.h、syscalls.c(GCC 堆桩)
- `bsp/` 板级/外设驱动：bsp_board.c、bsp_camera.c、bsp_ov5640_ref.c、
  usb_descriptors.c、ov5640/(ST 组件参考驱动)
- `sys_startup/` 启动文件/设备头/链接脚本（gcc|arm|iar + STM32H743ZITx_FLASH.ld）
- `MDK-ARM/` Keil 工程（stm32h743.uvprojx + stm32h743.sct）：源列表/include/宏与
  `CMakeLists.txt` 同步；用 `sys_startup/arm` 启动、**排除 app/syscalls.c**（由
  `mdk_target.c` 提供 no-semihosting 重定向）
- 旧 `Core/`+`BSP/` 已废弃；`ov5640_ref.c` → `bsp/bsp_ov5640_ref.c`（函数名
  `ov5640_ref_init()` 不变）

## 关键技术决策
1. **OV5640 驱动**: ST BSP 组件驱动 (`bsp/ov5640/`) 的 QVGA 表有 bug（水平 binning 未使能），改用用户提供的参考驱动 (`bsp/bsp_ov5640_ref.c`)
2. **DCMI 极性**: RISING / LOW / LOW (PCK/VSYNC/HSYNC)，对应传感器 0x4740=0x21
   ⚠️ 固件默认值 `s_cam_diag.ctl.pclk_pol=1` 与 `ctl.crop_en=1` **是硬件配置而非可选开关**：
   pclk_pol 决定采样沿按 RISING；crop_en 决定 DCMI 取 240 列而非整条 400px 行。
   任一个被清零 → DCMI 误判消隐 → DMA 从不驻留 → `bsp_camera_snapshot()` 全 torn → 无画面。
3. **传感器输出**: 400×300 YUV422/YUYV，DCMI crop 到 240×240（FRAME_SIZE=115200B, total=28800 words）
4. **DMA**: DMA2_Stream1 CIRCULAR, WORD alignment, FIFO FULL, INC4/SINGLE
5. **DCMI 地址**: **0x48020000** (AHB2)，不是 0x40050000（之前读错地址浪费了大量时间）
6. **快照/撕裂修复**: `bsp_camera_snapshot()` 只在消隐期(NDTR≥total)做 memcpy，双端校验
   `snap_ndtr1≥total-120`，越界判 torn 丢弃。UVC 侧三缓冲(fb0 采集 + fb1/fb2 发送)让拷贝
   与传输重叠。生产热路径遥测留在 `s_cam_diag`(stats/ctl/snap)。

## 调试探针子系统 (bsp/sys_prob.c/.h, 2026-09-30)
- 纯探针(pin/bus/pull/regdump/poke + 16KB trace)抽到独立 TU，宏 `CAM_DIAGNOSTICS` 开关
  (CMake `option(ENABLE_CAM_DIAGNOSTICS OFF)`)。关闭=零代码/零数据进链接；开启时诊断状态经
  `sys_prob_get_state()-><group>.<field>` 以结构体指针暴露(字段名沿用旧 `s_cam_diag.<group>.*`)。
- 探针走 bsp_camera 桥接接口借用采集管线；`bsp_camera_service()` 内 `#ifdef CAM_DIAGNOSTICS sys_prob_service()`。

## 调试经验
- OpenOCD 路径必须用 `cygpath -m`（正斜杠 POSIX 格式）
- DCMI_CR 正确地址是 0x48020000（STM32H7 AHB2 上），DMA PAR 可交叉验证
- Edit 工具对不可见字符敏感时可用 Python 字符串替换脚本 patch
- 彩条模式 (0x503D=0x80) 是判断 DVP 故障段的最快方法
- 总线波形抓取（GPIO 采样 + 离线解码）可区分"传感器不输出"和"DCMI 不采样"

## 帧率定论（2026-11-06）
**240×240 的 8fps 是硬件上限，已确认不再优化。** FS 等时每 1ms ≤1023B（本板 EP 952B
→ 950B 载荷），115200B/帧 → 122 包 → 8.2fps 已是饱和值；16fps 需 1.84MB/s >
12Mbit/s 总线，换 bulk（≤1.216MB/s）也不够。用户决定接受 8fps。
- MJPEG（OV5640 片内 JPEG）改造做过一版：commit 12d4bd9 + 17dc753，**已回退**，
  代码保留在本地分支 **`mjpeg-ov5640`**（未 push）。主线停在 d10dd0c 的 YUY2 路径。
- 若将来重启该方向：不要相信"改描述符/加缓冲"能提帧率；真要 >8fps 只有三条路——
  降分辨率（160×120 YUY2→26fps）、降位深（Y8→17fps）、或换 USB HS + ULPI PHY。

## 当前状态
- [x] DCMI 像素数据正确（2026-08-05 修复）
- [x] USB UVC 实时推送验证（可显示图像）
- [x] 剧烈变化拼接(tearing)修复（消隐期相位锁存 + 三缓冲，A/B 验证通过）
- [x] 最终帧率测试（固件 uvc_fps_x10 + tools/fps_test.py 就绪；真机实测待填）
- [x] 调试探针子系统抽取 sys_prob + CAM_DIAGNOSTICS 宏开关（2026-09-30，双构零警告）
- [x] 修复结构化丢失默认值(pclk_pol/crop_en)导致的"无画面"（2026-09-30）。
      **定位指纹**：`stats.frame_count>0` 但 `snap_ok==0 且 snap_torn==帧数`，
      `ctl.words_per_frame` 非 28800 → 裁剪切窗/采样沿不对。修复后 snap_ok>0、
      frames_sent>0，主机 ffmpeg dshow 抓到正确 240×240 YUY2 帧。commit abaeb9a。

## 调试经验（补充）
- **SWD 读结构体成员的解析坑**：本机 OpenOCD 的 `mdw` 输出是 `0x20000050: 00000010`
  —— **值不带 `0x` 前缀**，而 telnet 会把命令行原样回显。任何"取输出里最后一个 `0x…`"
  的解析都会命中回显里的地址，于是每个字段都读成自己的地址，看起来"有值"全是垃圾。
  必须锚定 `addr: value` 行；地址解析同理要用 `printf "%s 0x%08x\n"` 自描述输出，
  不能按 `$1/$2` 顺序对应字段（一个符号缺失就整体错位）。
- 读 `bool` 成员会连带读出后面 3 字节（如 `cam_running=true,tx_idx=-1,ready_idx=-1`
  读成 `0x00FFFF00`），不是异常。
- 撕裂(tearing)根因 = 在 DMA CIRCULAR 覆写缓冲的任意相位做 memcpy，CPU 中途越过 DMA 写指针；静止场景不可见，运动场景暴露
- 垂直消隐期量化法：采样 DMA NDTR 序列，消隐期占比 ≈ 窗口时长/帧周期（本项目 19.5% ≈ 16ms / 83ms）
- A/B 对照最有效的帧间调制：关 AEC(0x3503=0x03) + 手动曝光 0x3501=0x04↔0x60，产生 17 倍亮度差（SDE 负片 0x5580 对 YUV 输出无效，不可用）

## 代码规范（2026-09-30 对齐）
- 自研代码统一遵循 `mcu-code-style` skill；**缩进 = 4 空格 + Allman 开/闭括号独立成行**，由仓库根 `.clang-format` 锁定（此前 2 空格 + K&R 开括号的提交未真正落地，本次彻底对齐）。
- 全局变量：文件级可变全局全部 `static` + 合并结构体（uvc_app → `uvc_app_t s_uvc`；
  bsp_camera → `cam_state_t s_cam` + `cam_diag_t s_cam_diag`；main → `app_state_t s_app`），
  SWD 直接读 .elf 符号（不再走 extern）。纯探针状态独立为 `sys_prob_state_t`（见上）。
  ⚠️ **合并/重命名时必须逐项核对原文件的非零初值**——漏搬会静默清成 0（本次
  `pclk_pol`/`crop_en` 即因此导致无画面，编译零警告）。核对法：
  `git show HEAD:<file> | grep -E "^(volatile|static).*="`。
- 格式化工具：本机 clang-format 装在托管 Python venv（`~/.workbuddy/binaries/python/versions/3.13.12/Scripts/clang-format.exe`）。
