# LAN8720A (RMII) 网络 + I2C 锁死恢复 + F429 显示/触摸

> 本文件是 **I2C 总线锁死恢复** 与 **GT911 触摸中断风暴防护** 的**唯一主副本**；
> `stm32-swd-forensics` 只保留用法，不重复配方。

## 1. ETH 配置（LAN8720A / RMII）

- RMII，PHY addr 0；REF_CLK 来自 PA1（需 PHY 提供 50MHz 时钟或 MCU 输出）。
- 复位常经 I/O 扩展器（如 PCF8574T 某位，高有效，经三极管反相 → 写 1 时 `ETH_RESET=0` 释放，
  写 0 时复位）。**复位时序要在 I2C 恢复之后**再执行（见第 2 节）。
- **ETH RX 零拷贝缓冲必须留内部 SRAM**：ETH DMA 写 SDRAM 分片突发丢包
  （`ping -l 1473` 起不稳定）；memp 池也在 SRAM。发送侧放 SDRAM 无碍。

## 2. I2C 总线锁死恢复（运行期必装）

从设备把 SDA 拉低锁死（噪声 / 复位中途打断事务）→ HAL 读永久超时 → 数据冻结为 0。
不恢复则**一次失败变永久失败**。

`BSP_I2C_Recover()` 流程（缺一不可）：
1. 检测 SDA 低 / BUSY / AF-ARLO-BERR **任一** → 才动手；
2. SCL 改 GPIO 推挽翻转 **≥9 次**释放从机，SDA 释放即提前停；
3. 发 STOP（SCL 高时 SDA 低→高）；
4. `HAL_I2C_DeInit + Init` 清锁存标志；
5. 还原 AF_OD + 重配时序；
6. 复查 BUSY / SDA。

- 全程纯 `__NOP()` 延时，**调度器启动前也安全**。
- 调用方防护（轮询式 HAL 传输无 DMA 时）：加大 timeout（10→50 ms）+
  `vTaskSuspendAll()` 包成原子操作 + 失败先 `BSP_I2C_Recover()` 再立即重试一次。

## 3. SDRAM (FMC) 初始化必须最先

FreeRTOS heap / LwIP 池 / mbedTLS 池都在 SDRAM，任何 `xTaskCreate` 在 SDRAM 初始化之前
会写未初始化内存 → `heap_4.c` 下溢断言。顺序：FMC 配置 → SDRAM 初始化序列 → 刷新率 →
内存自测，**全部早于一切 RTOS 对象**。

## 4. F429 LCD 8080 总线（NT35510 / ILI9806E 类 800×480 屏）

- 控制器 NT35510（`0x8000`）/ ILI9806E 回退；FMC Bank1 NE1 8080 16-bit，`RS` 接某地址线
  （`LCD_BASE = 0x60000000 | (addr_offset)`）。
- **`lcd_scan_dir` 的宽高交换逻辑是模块厂原版、正确，切勿改反/删除**：`DFT_SCAN_DIR=L2R_U2D`(MV=0)
  下因 `lcd_width(800) > lcd_height(480)` 触发交换 → 有效 GRAM 窗口 **480×800**
  （即该控制器模块铺满物理 800×480 屏所需的窗口）。屏幕尺寸只由 `LCD_WIDTH/LCD_HEIGHT` 决定，
  不要动交换逻辑。
- **LVGL 画布 = GRAM 窗口 = 480×800**（不是 800×480）；UI 布局从
  `lv_disp_get_hor_res/ver_res()` 自适应取，不要硬编码 800×480，否则渲染错位。
- **LCD 地址窗口必须用 MIPI-DCS 时序**：命令写一次 + 跟 4 数据字节；不可把 `0x2A/0x2B/0x2C`
  当连续寄存器拆写，否则渲染带写错乱 GRAM → 文字重叠。

## 5. 电容触摸 GT911/GT9147（软件位绑定 I2C）

- 芯片**只有 I2C 模式（无 SPI）**，可走软件位绑定 I2C：排针 `T_SCK=CT_SCL`、`T_MOSI=CT_SDA`、
  `T_CS=CT_RST`、`T_PEN=CT_INT`。GT911 自报 `product ID="911"`、addr `0x14`，分辨率应与画布一致。
- **184B 配置块是 GT9147 专用，绝不能给 GT911 上传**；GT911 用恒等映射即可。
- **GT9xx 中断后必须轮询到抬手**：INT 行为因模组而异（单次 / 持续脉冲），中断只当唤醒，
  任务随后 15ms 轮询直到连续 3 次无触点。
- 无需手指即可验证 EXTI 链路：OpenOCD `halt` → `mww <EXTI_SWIER> 0x80` → `resume`，
  产生与引脚边沿等价的中断（详见 `stm32-swd-forensics`）。

### 5.1 GT911/GT9147 中断风暴三层防护（真机教训）

**症状**：`T_PEN` 配成浮空输入 + 紧邻位绑定 I2C 的 SCL 线（~165 kHz）→ 串扰耦合出边沿，
实测 **~46925 次/秒** 中断；ISR 抢占低优先级的轮询式 I2C 传输 → HAL 超时 → 从机拉住 SDA
→ I2C 总线 BUSY 锁死（之后每次都失败）。**三层防护缺一不可**：

| 层 | 措施 |
|---|---|
| 源头 | `T_PEN` 改 `GPIO_PULLUP`（仅地址锁存一瞬用 NOPULL）；位绑定 I2C 事务期间屏蔽 EXTI line |
| 隔离 | 传感器读取用 `vTaskSuspendAll()/xTaskResumeAll()` 包成原子操作（中断仍开），传输不被抢占；ISR 内**立即屏蔽该 line**、任务侧消抖后**重新武装**（噪声中断率 47kHz → ~20Hz） |
| 容错 | I2C 超时 10→50 ms；失败先 `BSP_I2C_Recover()` 再立即重试一次；触摸轮询上限兜底；**IRQ 速率看门狗**（1s 窗口计数，超阈值打 WARNING） |

> **正解是「ISR 内屏蔽 + 任务侧延时重武装」，不是在 ISR 里做软件滤波。**
> 噪声中断的正解从来不是滤波，而是切断噪声源（上拉）+ 不让噪声进 ISR（自屏蔽）。
> 给所有外部中断加「1s 速率看门狗」把风暴变成数字，是这类问题最划算的诊断投入。

隔离层与容错层的代码形状：

```c
/* ISR 入口：立刻屏蔽本 line，避免噪声继续进中断 */
EXTI->IMR &= ~TOUCH_EXTI_LINE_MSK;
xSemaphoreGiveFromISR(s_touch_sem, &hp);

/* 任务侧：轮询到抬手 + 去抖延时之后，重新武装 */
void bsp_touch_irq_rearm(void)
{
    taskENTER_CRITICAL();
    EXTI->PR  =  TOUCH_EXTI_LINE_MSK;    /* 先清挂起，避免立即重触发 */
    EXTI->IMR |= TOUCH_EXTI_LINE_MSK;    /* 再重新武装 */
    taskEXIT_CRITICAL();
}
```

**要把"中断风暴"从猜测变成数字**：在 1s 窗口内计数，超过阈值（如 1000/s）打一条
WARNING，日志里就会出现 `46925/s` 这样的实测值——这是这类问题最划算的诊断投入。
