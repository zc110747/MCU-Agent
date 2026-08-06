# STM32H743 TinyUSB UVC 项目 — 项目记忆

## 硬件平台
- 芯片: STM32H743ZIT6 (鹿小班开发板)
- 编译器: arm-none-eabi-gcc, CMake 构建
- 调试: ST-Link + OpenOCD + cortex-debug
- USB: FS (PA11/PA12), dwc2 rhport 0 @ 0x40080000
- 内存: AXI SRAM 0x24000000 (512KB, non-cacheable via MPU Region0)

## 关键技术决策
1. **OV5640 驱动**: ST BSP 组件驱动 (`BSP/ov5640/`) 的 QVGA 表有 bug（水平 binning 未使能），改用用户提供的参考驱动 (`ov5640_ref.c`)
2. **DCMI 极性**: RISING / LOW / LOW (PCK/VSYNC/HSYNC)，对应传感器 0x4740=0x21
3. **传感器输出**: 400×300 YUV422/YUYV，DCMI crop 到 240×240
4. **DMA**: DMA2_Stream1 CIRCULAR, WORD alignment, FIFO FULL, INC4/SINGLE
5. **DCMI 地址**: **0x48020000** (AHB2)，不是 0x40050000（之前读错地址浪费了大量时间）

## 调试经验
- OpenOCD 路径必须用 `cygpath -m`（正斜杠 POSIX 格式）
- DCMI_CR 正确地址是 0x48020000（STM32H7 AHB2 上），DMA PAR 可交叉验证
- Edit 工具对不可见字符敏感时可用 Python 字符串替换脚本 patch
- 彩条模式 (0x503D=0x80) 是判断 DVP 故障段的最快方法
- 总线波形抓取（GPIO 采样 + 离线解码）可区分"传感器不输出"和"DCMI 不采样"

## 当前状态
- [x] DCMI 像素数据正确（2026-08-05 修复）
- [x] USB UVC 实时推送验证（可显示图像）
- [x] 剧烈变化拼接(tearing)修复（消隐期相位锁存 + 三缓冲，A/B 验证通过）
- [ ] 最终帧率测试

## 调试经验（补充）
- 撕裂(tearing)根因 = 在 DMA CIRCULAR 覆写缓冲的任意相位做 memcpy，CPU 中途越过 DMA 写指针；静止场景不可见，运动场景暴露
- 垂直消隐期量化法：采样 DMA NDTR 序列，消隐期占比 ≈ 窗口时长/帧周期（本项目 19.5% ≈ 16ms / 83ms）
- A/B 对照最有效的帧间调制：关 AEC(0x3503=0x03) + 手动曝光 0x3501=0x04↔0x60，产生 17 倍亮度差（SDE 负片 0x5580 对 YUV 输出无效，不可用）
