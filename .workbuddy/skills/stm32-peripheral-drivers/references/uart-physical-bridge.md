# UART 物理层与桥接踩坑（CDC↔UART 透明桥）

做一个 **USB CDC ↔ UART 透明转发桥**（无任何 in-band 配置通道）时，串口侧最容易出的三个问题，
全部来自"物理层/寄存器语义"而非逻辑设计。

### 1. 环形缓冲满/空二义性 → 必须"故意少用一个字节"
`head == tail` 既表示空也表示满。若 `rb_free()` 用 `cap - used` 计算，会把「满」误判为「空」，
生产者覆盖未消费数据——实测首轮压测丢字节达 3.6e10。
**可用容量必须是 `cap - 1`**（保留 1 字节不用），这是环形缓冲的标准约定，实现前务必确认。

### 13.2 7 数据位 + 校验时，校验位会污染数据字节
STM32 的字长是**含校验位**的总长：7 数据位 + 校验 = **8 位字长**（`M=00`），
此时 RDR 的 **bit7 就是校验位**，DMA 按字节搬运会把它一起读进来。
- 修法：按数据位算掩码 `rx_data_mask = (1 << data_bits) - 1`，在排空时对已消费的 DMA 缓冲**就地掩蔽**
  （8 数据位时掩码为 `0xFF`，可跳过，不影响主路径性能）。
- 现象特征：**7N1 通过，但 7E1 / 7O1 全部失败**；而 8E1 / 8O1 不受影响（它们是 9 位字长，
  校验位落在 bit8，超出字节范围）。
- 另注意：7 数据位模式无法承载任意二进制（每字节 MSB 在线路上不存在），只能用 7 位安全 ASCII 验证。

### 3. RTS/CTS 流控：引脚模式必须与 `HwFlowCtl` 成对，否则悬空
- **`RTS` 若配成 `GPIO_MODE_AF_PP` 而 `HwFlowCtl=UART_HWCONTROL_NONE`，USART 并不驱动它**，
  且复用模式下 `HAL_GPIO_WritePin`（写 BSRR）对该引脚无效 → 引脚高阻悬空。
  典型症状：对端 CTS 被悬空的 RTS 牵连读成"未就绪"，而软件状态变量却显示已断言，**一帧都不通**。
- 推荐配置：**RTS 用 `GPIO_MODE_OUTPUT_PP` 由软件按接收环余量驱动**（硬件 RTS 只跟踪 1 字节 RDR 标志，
  对 KB 级 ring 毫无意义）；**CTS 用 `GPIO_MODE_AF_PP` + PULLDOWN** —— PULLDOWN 让"对端不驱动 CTS（悬空）"
  读作**就绪**，TX 永不被门控，对端主动拉高才暂停 TX，这才是标准透明行为。
- 流控宜做成**连接门控**（USB 端口打开时使能 CTSE + 软件驱动 RTS），断开自动还原 115200/8N1/无校验。

### 4. Windows `usbser.sys` 不转发 RTS（只转发 DTR）
实测 `EscapeCommFunction(CLRRTS/SETRTS)` **不会**触发 `tud_cdc_line_state_cb`，而 `CLRDTR/SETDTR` 会。
⇒ "由主机 RTS 切换流控开关"在 Windows 上位机**不可行**；因此流控必须**固件自管理**。
（属主机驱动限制，非固件缺陷；Linux/macOS 行为不同。）

### 5. D-Cache 与 DMA（若不用 MPU）
桥接类 DMA 缓冲在 D-Cache 开启时务必处理一致性：要么按 `soc-cache-mpu` 配置 MPU 区域，
要么在 DMA 读写前后做 Cache clean/invalidate；**不要既开 Cache 又什么都不做**。
