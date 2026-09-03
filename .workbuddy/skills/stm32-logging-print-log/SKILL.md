---
name: stm32-logging-print-log
description: 把裸 printf 替换为 PRINT_LOG 全局可控日志系统的完整方法：单宏编译期零成本关闭、栈缓冲格式化避免重入 UART 发送锁、RTOS 下调度器前后双 TX 路径+懒互斥量、裸机（无 RTOS）下 TX 中断驱动环形缓冲（写缓冲时关 TX 中断避免竞争）、CMake 选项一键开关、串口不可用时用 SWD 读 TX 环验证开关是否真生效。含固件上板验收踩坑（CRLF 行尾安全编辑、openocd -c 多词命令引号、PowerShell 环境块重复键、先开串口再复位抓 banner）。适用于"去掉裸 printf""统一加日志开关""编译关日志省 FLASH""UART 日志时有时无""ISR 里不能打日志""裸机 TX 中断日志""构建层控制日志"打印。触发词：PRINT_LOG、printf 替换、日志开关、关日志省 FLASH、uart_write、TX 环形缓冲、调度器前后、懒互斥量、UART 互斥、SWD 验证日志、编译期关闭日志、TX 中断、裸机日志。
agent_created: true
---

# 把裸 printf 换成 PRINT_LOG 全局可控日志

## 一、何时用本 skill

- 项目里散落着 `printf("...")`，想统一成一个可一键关闭的日志原语。
- 需要"发布版本关掉所有日志"以省 FLASH/RAM/UART 带宽，但调试版本全开。
- 出现"日志时有时无 / UART 卡死"——根因是 `printf` 走 newlib `_write` 重入 UART 锁，或裸 `printf` 在堆坏/调度器未起时崩溃。
- 需要在 ISR 之外任何上下文安全打印（boot 阶段、任务内、堆损坏时）。
- **裸机（无 RTOS）项目**想要非阻塞、不依赖 RTOS 互斥量的 UART 日志。

## 二、架构（三层，绝不重入 UART 锁）

```
PRINT_LOG(fmt, ...)
   └─> printf_log()         // 应用唯一日志 sink
         └─> vsnprintf(buf, 192, fmt, ap)   // 栈缓冲，不分配堆
               └─> uart_write(buf, len)      // 显式长度入 TX 环
```

关键点：
1. **编译期零成本关闭**：`PRINT_LOG_ENABLE=0` 时宏展开成 `((void)0)`，函数体也早返回——
   不占 FLASH、不占栈、不产生任何 UART 流量。
2. **绝不走 newlib `printf`/`_write`**：自己 `vsnprintf` 进栈缓冲，再 `uart_write()` 显式长度，
   因此不会重入 HAL UART 的重入锁（裸 `printf` 在并发/中断里会互相踩）。
3. **安全于调度器前 / 堆坏时**：栈缓冲不分配堆，所以 boot 阶段和堆损坏时仍能打日志。

## 三、log.h（宏 + 声明）

```c
#ifndef PRINT_LOG_ENABLE
#define PRINT_LOG_ENABLE 1
#endif

void printf_log(const char *fmt, ...);
void vprintf_log(const char *fmt, va_list ap);

#if PRINT_LOG_ENABLE
  #define PRINT_LOG(fmt, ...)   printf_log(fmt, ##__VA_ARGS__)
#else
  #define PRINT_LOG(fmt, ...)   ((void)0)
#endif
```

**约束（务必写进代码评审）**：
- `PRINT_LOG` **不能在 ISR 里调**——内部可能拿 UART 互斥量（`uxSemaphoreGetMutexHolder`
  在 ISR 里非法）。中断上下文用 `uart_write()` / `uart_puts()` 直连。
- 二进制/无 NUL 的内容 dump（如 U 盘目录遍历）**不要走 `PRINT_LOG`**（当 `%s` 遇到
  `0x00` 字节会崩），保持 `uart_write()` 直连，并自己用 `#if PRINT_LOG_ENABLE` 包住。

## 四、log.c（栈缓冲 + 早返回）

```c
#define LOG_BUF_SIZE 192   /* 最长日志行 ~110 字符，留足余量；vsnprintf 超出会截断 */

void vprintf_log(const char *fmt, va_list ap)
{
#if PRINT_LOG_ENABLE == 0
  (void)fmt; (void)ap; return;
#else
  char buf[LOG_BUF_SIZE];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  if (n < 0) return;
  if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;
  uart_write((const uint8_t *)buf, n);
#endif
}

void printf_log(const char *fmt, ...)
{
#if PRINT_LOG_ENABLE == 0
  (void)fmt; return;
#else
  va_list ap; va_start(ap, fmt);
  vprintf_log(fmt, ap);
  va_end(ap);
#endif
}
```

## 五、uart_write 双 TX 路径 + 懒互斥量（RTOS 场景，最易踩坑）

`BSP_UART_Init()` 在 102 工程里**早于 SDRAM / `vPortDefineHeapRegions()` 之前**调用，
此时堆尚未定义。所以 **TX 互斥量不能在 init 里创建**——创建会落在未定义的堆上。

正解：调度器运行后、第一次 `uart_write()` 时才惰性创建互斥量，且用临界区包住创建，
保证线程安全。

```c
static SemaphoreHandle_t g_tx_mutex = NULL;

static SemaphoreHandle_t tx_mutex_get(void)
{
  taskENTER_CRITICAL();
  if (g_tx_mutex == NULL)
    g_tx_mutex = xSemaphoreCreateMutex();
  taskEXIT_CRITICAL();
  return g_tx_mutex;
}

int uart_write(const uint8_t *data, int len)
{
  /* boot 阶段（调度器未起）或 UART 未就绪：阻塞轮询发送，绝不丢字节 */
  if ((g_uart_ready == 0) ||
      (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
  {
    if (g_uart_ready == 0) return 0;
    HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
    return len;
  }

  SemaphoreHandle_t mtx = tx_mutex_get();
  if (mtx != NULL && xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE)
    return 0;

  taskENTER_CRITICAL();                     /* 保护环计数器 + busy 标志 */
  for (int i = 0; i < len; i++) {
    uint16_t next = (g_tx_head + 1) & (UART_TX_BUF_SIZE - 1);
    if (next == g_tx_tail) break;          /* 满 -> 静默丢弃剩余字节 */
    g_tx_buf[g_tx_head] = data[i];
    g_tx_head = next;
    queued++;
  }
  if (g_tx_busy == 0 && g_tx_head != g_tx_tail) {
    g_tx_busy = 1;
    SET_BIT(huart3.Instance->CR1, USART_CR1_TXEIE);
  }
  taskEXIT_CRITICAL();

  if (mtx != NULL) xSemaphoreGive(mtx);
  return queued;
}
```

- `UART_TX_BUF_SIZE` 至少 **2048**（512 在 115200 下任意连续打印即爆，见
  `stm32-peripheral-drivers` §十一）。
- 批量打印（如 dump U 盘）必须限流：>阈值文件只列目录项、整轮遍历设总字节预算，
  否则静默丢其它任务日志。

## 六、CMake 一键开关

```cmake
# PRINT_LOG_ENABLE=1 -> 路由到 printf_log(); =0 -> 编译成空语句，零成本
option(ENABLE_PRINT_LOG "Compile in PRINT_LOG() application logging" ON)
if(ENABLE_PRINT_LOG)
  add_compile_definitions(PRINT_LOG_ENABLE=1)
  message(STATUS "PRINT_LOG: enabled (PRINT_LOG_ENABLE=1)")
else()
  add_compile_definitions(PRINT_LOG_ENABLE=0)
  message(STATUS "PRINT_LOG: compiled out (PRINT_LOG_ENABLE=0)")
endif()
```

关日志构建：`cmake -S . -B build_nolog -DENABLE_PRINT_LOG=OFF && cmake --build build_nolog`。

## 七、验收

1. **双构建零警告**：`ENABLE_PRINT_LOG=ON` 与 `OFF` 都要 `-Wall -Wextra` 干净。
   102 工程实测 Release 开 298,120 B (28.43%) / 关 291,960 B (27.84%)——关日志省约 6 KB FLASH。
2. **开关真的生效（串口不可用时用 SWD）**：见 `stm32-swd-forensics` 与
   `verify_log_switch.py`——关掉后 `g_tx_head` 必须恒为 0（一个字节都没进 TX 环）。
3. 替换范围：全工程 `printf(` 改 `PRINT_LOG(`，保留 `snprintf` 这类字符串格式化；
   ISR 内 `printf` 改成 `uart_write`/`uart_puts`。

---

## 八、裸机（无 RTOS）TX 中断驱动变体（003 工程实测）

无 RTOS 时不能用 FreeRTOS 互斥量保护 TX 环。改用一个**发送（TXE）中断驱动的环形缓冲**，
临界区用 **关闭串口 TX 中断** 而非互斥量——这正是用户要求的"写入要关闭串口中断避免出错"。

### 8.1 组件
- `log.h`：`PRINT_LOG_ENABLE` 开关 + `printf_log()` / `vprintf_log()` + `log_uart_tx_irq()`（ISR 侧）+ `log_uart_init()`（使能 NVIC）。
- `log.c`：
  - 栈缓冲 `LOG_BUF_SIZE=256` 格式化（保留）；
  - TX 环形缓冲 `UART_TX_BUF_SIZE=1024`，索引 `uart_tx_w/r/n` 用 `volatile`；
  - `uart_write()`：写前 `__HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE)` 关 TX 中断保护临界区；
    若发送器空闲（`uart_tx_active==0`）则把首字节 prime 进 `Instance->TDR`，再 `__HAL_UART_ENABLE_IT(..., UART_IT_TXE)` 重开；
  - `log_uart_tx_irq()`：TXE 事件里逐字节取缓冲发送，发完（`uart_tx_n==0`）自动关 TXE 并清 `uart_tx_active`；
  - `log_uart_init()`：幂等 `HAL_NVIC_SetPriority(USART1_IRQn,5,0)` + `HAL_NVIC_EnableIRQ(USART1_IRQn)`。

### 8.2 接线（裸机关键三步）
1. `Core/Src/stm32h7xx_it.c`：原 `USART1_IRQHandler` 走 `Default_Handler`，新增
   `void USART1_IRQHandler(void){ log_uart_tx_irq(); }` 并 `#include "log.h"`。
2. `Core/Src/main.c`：`MX_USART1_UART_Init()` 之后调用 `log_uart_init();`（也可在
   `uart_write()` 里懒使能 NVIC 作兜底）。
3. `CMakeLists.txt`：注册 `Bsp/log.c`。

### 8.3 为什么"关 TX 中断"是对的
- 裸机没有互斥量；TX 环索引被**线程上下文（uart_write）**与**中断上下文（ISR）**共享，
  唯一正确的临界区保护是：在修改索引期间**禁止 TXE 中断**，使 ISR 无法同时改 `uart_tx_r/n`。
- prime 首字节到 `TDR` 必须在重开中断**之前**完成——否则 ISR 可能在 TDR 空、索引未推进时
  误读旧数据。首字节写 TDR 是安全的（TDR 空时写即触发移位输出）。
- **不要**在 ISR 里调 `PRINT_LOG`/`printf_log`（会重入同一环）；ISR 只允许 `log_uart_tx_irq()`。

### 8.4 验收（003 实测）
- Debug FLASH 338528B / RAM_D1 262440B；Release FLASH 342500B；**均 0 warning**。
- OpenOCD 烧录 `build/lvgl_oled.elf` **Verified OK**。
- COM6（ST-Link VCP）抓到完整启动 banner，证明 TX 中断驱动 `PRINT_LOG` 在硬件上真实输出。

---

## 九、验证踩坑（固件日志上板验收）

### 9.1 CRLF 行尾安全编辑（极易静默破坏）
仓库 `.c/.h` 多为 **CRLF**。naive `text.split("\n")` 再 `"\r\n".join(...)` 会把行尾变成
`\r\r\n`，**翻倍**的 CR 会破坏 `#if` 行的 `\` 续行（报 "operator '&&' has no right operand"）。
修法（二选一）：
- Python 二进制：逐行 `line.rstrip(b"\r")` 去掉尾部 CR，再按 `b"\r\n"` 回接；
- 或按**锚点局部二进制替换**（只在已知串前后插入），完全不碰行尾。
**严禁**用 `sed -i` 或整体 `"\n".join` 重写 CRLF 文件。

### 9.2 openocd `-c` 多词命令必须再套一层引号
PowerShell / 某些 shell 下 `openocd -f openocd.cfg -c "init; reset run; exit"` 会被按空格
拆成多个 token，报 `Unexpected command line argument: reset`。
正确写法：`openocd -f openocd.cfg -c '"init; reset run; exit"'`（外层单引号包住整个多词命令）。
命令行直接跑时也可：`openocd -f openocd.cfg -c "program build/xxx.elf verify reset exit"`（单命令无需内引号）。

### 9.3 PowerShell 5.1 `Start-Process` 环境块大小写重复键崩溃
WorkBuddy 会话会注入 `http_proxy`/`HTTP_PROXY` 等大小写变体；PS 5.1 用大小写不敏感字典组装
子进程环境块时会撞重复键，抛 "已添加项"。`Get-ChildItem Env:` 会把变体折叠成一项导致去重无效。
修法（脚本顶部，干净终端下是 no-op）：
```powershell
$all = [System.Environment]::GetEnvironmentVariables('Process')   # 真实大小写变体名
foreach ($k in $all.Keys) { [System.Environment]::SetEnvironmentVariable($k, $null, 'Process') }  # 大小写不敏感，清一个即清全部
# 然后按需重建需要的变量（如 Path）
```
PS 7 无此问题；5.1 必须显式清理。

### 9.4 抓取启动 banner：先开串口再复位
LOG 只在启动时打印一次。顺序必须：**先打开串口（如 COM6, 115200 8N1）再复位目标**，
否则 banner 已经刷过、抓不到。pyserial 不可用时用 .NET `System.IO.Ports.SerialPort`
（PowerShell）读；若端口被拒，多半是上一次捕获句柄未释放或 ST-Link VCP 复位后重枚举，
重试 + 短暂 `Start-Sleep` 即可。
