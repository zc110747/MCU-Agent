---
name: stm32-swd-forensics
description: 串口/板子不可用时的 SWD+OpenOCD 内存取证与中断链路诊断：CH340 code-31 驱动故障、arm-none-eabi-nm 取符号地址、mdw 4 字节对齐读 + Python 切字节、"没报错≠有数据"正向验证、EXTI_SWIER 软件中断注入验证中断链路、中断风暴定位与防护（ISR 自屏蔽 + 任务侧消抖重武装 + 上拉 + 速率看门狗）、I2C 总线锁死恢复。适用于"串口打不开""COM5 驱动故障""读内存验证变量""中断不进""触摸不响应""I2C 死锁""中断风暴""EXTI 不触发""用 SWD 抓数据"。触发词：SWD 取证、mdw、arm-none-eabi-nm、OpenOCD 读内存、CH340 code-31、串口打不开、中断不进、EXTI_SWIER、软件中断注入、中断风暴、T_PEN 不响应、I2C 锁死、BSP_I2C_Recover、正向验证、没报错但有数据。
agent_created: true
---

# SWD 内存取证 + 中断链路诊断

## 一、何时用本 skill

- 串口打不开（CH340 `code-31` / `PermissionError(13)`，需重新插拔）、COM 口占错；
  **日志看不到了，但代码还在跑**，要用 SWD 读内存验证行为。
- "日志没报错"但怀疑根本没数据——需要**正向验证**：直接读目标 RAM 里的数据结构。
- 中断不进 / 触摸不响应 / EXTI 不触发——用软件中断注入验证链路，不靠人手点屏。
- 中断风暴（IRQ 计数飙到几万/秒）把低优先级任务饿死、锁死 I2C 总线。

## 二、CH340 code-31 与残留 openocd 冲突

- COM5 code-31 时**不要反复重试串口**：改用 SWD 抓内存（见下）。物理重插拔 CH340 可恢复。
- 烧录前先 `taskkill /IM openocd.exe /F`：残留 openocd 会让 ST-Link 报
  `libusb_open() failed with LIBUSB_ERROR_ACCESS`。
- 机器上若有多个 CH340（如 COM4 / COM5），**先抓字节确认哪个是本板串口**，占错口会
  一直读到空（102 工程的 COM4 不是本板）。

## 三、SWD 读内存通用模板（必背）

OpenOCD `mdw` **只能 4 字节对齐整字读**；读 `uint8`/`uint16` 混排的静态变量要先按
4 字节对齐整字读，再在 Python 里按字节位置切出来。

> **halt 时序与读数落盘（真机易踩）**：
> - `halt` 后**务必 `wait_halt`** 再 `mdw`：刚发 `halt` 时目标可能还在跑，立刻读 SRAM 会报
>   `Failed to read memory`；`wait_halt` 等真正停稳再读即正常。
> - 用 `-c "init; ...; mdw ...; shutdown"` 一次性脚本时，**成功 mdw 行在 `shutdown` 前常被缓冲丢弃**
>   （只透出错误/PC 行）。稳法：把输出 `> ocd.log 2>&1` 落盘再 `grep`，或先把 mdw 做完、最后单独 `shutdown`。

```
# 1) 取符号地址（不同构建地址不同，不要硬编码）
arm-none-eabi-nm build/xxx.elf | findstr s_data
# 2) OpenOCD 烧录并 boot 后 halt，按 4 字节对齐整字读
openocd -s <scripts> -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c init -c "reset halt" \
  -c "flash write_image erase build/xxx.elf" -c "verify_image build/xxx.elf" \
  -c "reset run" -c "sleep 15000" -c "halt" \
  -c "mdw 0x20001000" -c "mdw 0x20001004" ...
```

Python 侧切字节（参考 `verify_sensors.py` / `verify_log_switch.py`）：

```python
import re, subprocess
def read_struct(elf, addr, size):
    words = range(addr & ~3, (addr + size + 3) & ~3, 4)
    cmds = ["init", "reset halt", "flash write_image erase %s" % elf,
            "verify_image %s" % elf, "reset run", "sleep 15000", "halt"]
    for w in words: cmds.append("mdw 0x%08X" % w)
    cmds.append("shutdown")
    p = subprocess.run([OCD, "-s", SCR, "-f", "interface/stlink.cfg",
                        "-f", "target/stm32f4x.cfg"] + sum((["-c", c] for c in cmds), []),
                       capture_output=True, text=True, timeout=300)
    vals = {}
    for line in (p.stdout + p.stderr).splitlines():
        m = re.match(r"\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$", line)
        if m: vals[int(m.group(1), 16)] = int(m.group(2), 16)
    raw = bytearray()
    for a in range(addr, addr + size):
        raw.append((vals[a & ~3] >> (8 * (a & 3))) & 0xFF)
    return bytes(raw)
```

## 四、"没报错 ≠ 有数据"——正向验证模式

串口日志只在失败时打印、且失败还被限流，所以"没有 FAILED"不等于"有数据"。
直接读目标内存里的采样结构，拿到物理量（lux、g、dps、uT）确认量纲合理。

`verify_sensors.py` 判据（102 工程真机）：
- `s_data` 结构用 `arm-none-eabi-nm` 取址，`LAYOUT` 列出每个字段的偏移与 `<` 格式。
- AP3216C `ap3216_ok==1`；MPU9250 `mpu_ok==1`；`samples>0`；`errors==0`。
- 静止时加速度模长 `|a| ∈ [0.5, 1.5] g`（板平放应≈1.00 g）。
- AK8963 `WIA`（即 `g_mag_id`）读 `0x00` → 本模块**未装配可用磁力计**（硬件事实，
  非故障），页面显示「AK8963 未装配」，**绝不把 0.0 uT 当成功上报**；读到 `0x48` 才是有磁力计。

## 五、EXTI_SWIER 软件中断注入——免手指验证中断链路

`EXTI->SWIER`（F4 上 `0x40013C10`）写 1 产生**与引脚边沿完全等价**的中断请求。
验证 T_PEN (PH7, line 7) 链路：`EXTI -> NVIC -> HAL_EXTI_IRQHandler -> 回调 ->
xSemaphoreGiveFromISR -> touch_task 被唤醒`。

```bash
openocd ... -c "reset run" -c "sleep 14000" -c "halt" \
  -c "mww 0x40013C00 0x80"   /* IMR 强制使能 line 7（ISR 自屏蔽时需先开）*/ \
  -c "mww 0x40013C10 0x80"   /* SWIER 注入 line 7 */ \
  -c "resume" -c "sleep 4000" -c shutdown
```

> ⚠️ `verify_touch_irq.py` 的特殊点：touch ISR 在入口**自屏蔽** EXTI line 7（防噪声），
> 所以注入前必须先用 `mww EXTI_IMR 0x80` 把 mask 位打开，否则软件中断被丢掉。
> 这条验证的是**链路通断**，不验证引脚电平——仍需手指点一次看 `[TOUCH] raw=` 行。

回读 `SYSCFG->EXTICR[1]`（= `EXTICR2`）：`0x00007000` 表示 line 7 已复用到 GPIOH（PH7）。

## 六、中断风暴定位与防护（102 工程真机经验）

**现象**：`irq` 计数几秒内爬到 93,000+；FreeRTOS 启动后 I2C2 传感器全读失败。

**根因**：`T_PEN`(PH7) 配成 `GPIO_NOPULL` 浮空，且紧邻 `PH6`(位绑定 SCL ~165 kHz)，
串扰耦合出边沿 → 实测 **46,925 次/秒** 中断。ISR 抢占低优先级的轮询式 I2C 传输，
把 I2C2 总线锁死（BUSY 锁存）。

**防护三件套**（缺一不可）：
1. **上拉**：`T_PEN` 改 `GPIO_PULLUP`（不止地址锁存那一瞬用 NOPULL）。
2. **ISR 内立即屏蔽 + 任务侧消抖重武装**：
   ```c
   /* ISR 入口 */
   EXTI->IMR &= ~TOUCH_EXTI_LINE_MSK;     /* 立刻屏蔽 line 7 */
   xSemaphoreGiveFromISR(s_touch_sem, &hp);
   /* 任务侧（轮询到抬手 + 去抖延时后） */
   void bsp_touch_irq_rearm(void) {
     taskENTER_CRITICAL();
     EXTI->PR  =  TOUCH_EXTI_LINE_MSK;    /* 清挂起，避免立即重触发 */
     EXTI->IMR |= TOUCH_EXTI_LINE_MSK;    /* 重新武装 */
     taskEXIT_CRITICAL();
   }
   ```
   → 噪声中断从 ~47 kHz 降到 ~20 Hz；任务轮询仍能抓到真实触摸。
3. **速率看门狗**（1 s 窗口计数，超阈值 `TOUCH_IRQ_STORM_HZ` 打一条 WARNING）：
   把"中断风暴"从猜测变成 `46925/s` 这样的数字，是这类问题最划算的诊断投入。

> 注意：正解是 **ISR 内屏蔽 + 任务侧延时重武装**，不是在 ISR 里做软件滤波。
> 噪声中断的正解从来不是滤波，而是切断噪声源（上拉）+ 不让噪声进 ISR（自屏蔽）。

## 七、I2C 总线锁死恢复

HAL 超时后从机仍拉住 SDA、外设锁在 BUSY；不 `BSP_I2C_Recover()` 则一次失败变永久失败。
`BSP_I2C_Recover()`（102 工程真机验证）流程：
1. 检测 SDA 低 / BUSY / AF-ARLO-BERR 任一 → 才动手；
2. SCL 改推挽输出翻转 ≥9 次释放从机，SDA 释放即提前停；
3. 发 STOP（SCL 高时 SDA 低→高）；
4. `HAL_I2C_DeInit + Init` 清锁存标志；
5. 还原 AF_OD + 重配时序；6. 复查 BUSY/SDA。

调用方防护（轮询式 HAL 传输无 DMA 时）：加大 timeout(10→50 ms) + `vTaskSuspendAll()`
包成原子操作 + 失败先 `BSP_I2C_Recover()` 再立即重试一次。
