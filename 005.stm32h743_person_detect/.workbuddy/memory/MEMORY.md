# 项目长期记忆 (stm32h7_person_detect_completed)

## OpenOCD/GDB 调试（STM32H7 + ST-LINK）
- 工具链：OpenOCD `/d/software/ST/OpenOCD/bin/openocd`；GDB/readelf `/e/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin/`。
- **双 openocd 实例端口冲突坑**：若 GDB 报 "Target not examined yet / refuse gdb connection"，先查是否有两个 openocd.exe 都绑了 3333/4444。Git Bash `ps` 看不到 Windows 原生进程，**必须用 `tasklist | grep openocd` + `netstat -ano | grep :3333` 拿 PID**，再 `taskkill /F /PID ...`，最后只起一个 OpenOCD。
- OpenOCD 0.12 弃用下划线选项，用 `gdb memory_map enable` / `gdb flash_program enable` / `telnet port` / `gdb port` / `tcl port`。
- 切勿用 GDB `call`(inferior function call) 在 Cortex-M 上跑函数 → HardFault（调试器限制，非固件问题）。验证改依赖真实场景变量或 `set var <全局>`。
- 启动：`openocd -f debug/openocd.cfg -l debug/openocd.log`（后台）；GDB 批处理 `arm-none-eabi-gdb -batch -x debug/verify_v2.gdb build/stm32h7_tinyyolo.elf`。

## 架构要点
- OV5640 320x240 → DCMI 居中裁 192x192 → DMA2_Stream7 双缓冲(非缓存 AXI 0x24000000) → CMSIS-NN MobileNetV1-0.25 96x96 int8 → ST7789 240x240。
- **三缓冲/防撕裂是结构保证**：`s_frame[0]/[1]`(drv_dcmi.c, DCMI DMA 硬件双缓冲)=采集侧(CMOS 写)；`s_disp`(app_vision.c, cacheable AXI)=第三块=显示缓冲(仅 CPU 写, CMOS/DMA 永不写)。app: get_frame→memcpy 到 s_disp(此刻 DMA 已切另一块)→推理→`LCD_CopyBuffer(... s_disp)`。显示区永不来自 CMOS 目标缓冲 → 刷新期间 CMOS 无法更新它 → 不可能撕裂/拼接。验证交替看 `g_dcmi_last_idx`(cap_idx 在 0/1 跳变即正常)。
- 阈值 `APP_PERSON_THRESHOLD`(app_vision.c, 当前 0.50f；空场景基线~0.30，真人样本可达0.71)。改宏注释会令后续代码行后移，GDB 断点行号要同步更新。
- **检测 1Hz + 去抖**：NN(`pd_run`)仅每 `APP_DETECT_INTERVAL_MS=1000ms` 跑一次（省 CPU）；相机预览 `LCD_CopyBuffer` 仍每帧刷新（保持三缓冲/不撕裂）。`s_res` 为 static 跨循环保留(非检测帧显示上一次分数)。人在判定用 `s_present`(去抖后状态)：detected 立即置1(检测到人立即显示)；!detected 且 `(now-s_last_person_tick)>=APP_PERSON_HOLD_MS(2000ms)` 才清0(移除延时2s防闪)。验证脚本 `debug/verify_v4_live.gdb`(断:241 每帧显示, 看 cap_idx 每帧交替+score 在检测后冻结=1Hz) 与 `debug/verify_v4_debounce.gdb`(断:213 1Hz判定, 注入 s_res.score 验证立即显/2s移除)。
- 显示：分数常显 `S:%.2f` + 顶部右侧图标(人在=绿实心人形/不在=灰空心+红斜杠)，复用 `LCD_CopyBuffer` 贴 RGB565 位图；图标状态随 `s_present` 每帧判定、仅变化时重绘防闪。
- ST-LINK 偶发 "Fail reading CTRL/STAT / DP initialisation failed" 是链路瞬断（非固件错）：杀掉 OpenOCD 进程、确认端口释放、重启单个 OpenOCD 实例即可恢复，重试验证。
