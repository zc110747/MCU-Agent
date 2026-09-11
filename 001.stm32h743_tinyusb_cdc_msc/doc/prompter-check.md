# USB专家级风险验证与故障注入测试

除了正常功能开发，必须主动验证以下 USB 系统性风险，不允许只以“Windows能够枚举”为验收标准。

## 1. USB基础稳定性

验证：

* USB FS 48 MHz 时钟
* Device Descriptor
* Configuration Descriptor
* Interface Number
* Endpoint Address
* Endpoint Type
* Endpoint Max Packet Size

测试：

* USB 插拔 500 次
* Windows USB Host
* Linux USB Host
* PC直连
* USB Hub
* 不同USB线

要求：

* 无随机枚举失败
* 无异常USB断连
* 无MCU异常复位

---

## 2. STM32H7 Cache/DMA

重点验证：

* USB DMA Buffer
* SDMMC DMA Buffer
* D-Cache一致性
* Buffer Alignment
* RAM区域

测试：

* CDC 100 MB 数据完整性
* MSC 大文件读写
* CDC + MSC 同时运行
* 开启D-Cache进行压力测试

数据必须使用 Pattern / SHA256 验证。

禁止通过增加 Delay 或 Buffer 掩盖 Cache 问题。

---

## 3. MSC与FatFs所有权

必须明确：

```text
LOCAL_FS
USB_MSC
```

同一时间只能有一个系统拥有 SD 文件系统。

MSC 工作期间禁止 MCU FatFs 修改同一个文件系统。

必须通过测试证明：

* 正常 Ownership 不会损坏文件系统
* USB拔出后可以重新挂载
* 大文件不会损坏

---

## 4. USB Reset / Re-enumeration

测试：

* USB Reset 100次
* USB重新枚举100次
* 不重新上电
* MCU保持运行

要求：

* CDC恢复
* MSC恢复
* SD状态正常
* 不HardFault
* 不死锁

---

## 5. USB Suspend / Resume

测试：

* Windows锁屏
* Windows唤醒
* Windows睡眠
* Windows恢复

要求：

* USB恢复
* CDC恢复
* MSC恢复

---

## 6. MSC数据完整性

测试：

```text
1 MB
10 MB
100 MB
500 MB
```

根据SD容量调整。

流程：

```text
生成测试文件
↓
USB MSC写入
↓
安全弹出
↓
重新连接
↓
读取
↓
SHA256校验
```

要求：

```text
原始SHA256 == 读取SHA256
```

---

## 7. 随机LBA测试

测试：

* 随机LBA
* 随机Block Count
* 1~128 blocks
* 边界LBA
* 首块
* 最后一块
* 超范围LBA

写入后立即读取并比较。

至少执行10000次随机测试。

---

## 8. USB异常拔出

在以下情况下随机拔出USB：

* CDC通信过程中
* MSC读取过程中
* MSC写入过程中
* 大文件复制过程中
* CDC + MSC并发过程中

验证：

* MCU不HardFault
* MCU不死锁
* USB重新插入后可以重新枚举
* SD文件系统仍然可以使用

---

## 9. SCSI异常命令

统计并测试：

```text
INQUIRY
TEST UNIT READY
REQUEST SENSE
READ CAPACITY
READ10
WRITE10
MODE SENSE
START STOP UNIT
SYNCHRONIZE CACHE
```

对于不支持的命令：

* 必须返回合理错误
* 不允许死循环
* 不允许HardFault
* 不允许破坏MSC状态机

---

## 10. CDC压力测试

测试：

```text
64 B
256 B
1 KB
10 KB
1 MB
10 MB
100 MB
```

分别测试：

* MCU → PC
* PC → MCU
* 双向同时传输

验证：

* 数据完整
* 无丢包
* 无死锁
* 无USB掉线

---

## 11. CDC + MSC并发

同时执行：

```text
CDC持续双向通信
+
MSC持续读写
```

至少运行：

```text
30分钟
```

进一步执行：

```text
1小时
```

记录：

```text
CDC发送量
CDC接收量
MSC读取量
MSC写入量
USB错误
SD错误
SCSI错误
HardFault
USB重新枚举次数
```

---

## 12. 故障注入原则

主动测试：

```text
USB拔出
USB Reset
Suspend
Resume
异常SCSI
非法LBA
SD读写错误
大文件
随机读写
Cache压力
CDC + MSC并发
```

目标不是让所有异常操作成功。

目标是：

**所有异常都必须以可控方式失败，而不能导致 HardFault、死锁、文件系统永久损坏或USB无法重新枚举。**

---

## 13. 最终验收

USB工程必须同时满足：

```text
正常功能
+
数据完整性
+
异常恢复
+
长时间稳定性
+
Windows兼容性
+
Linux兼容性
+
USB物理环境兼容性
```

只有“USB能枚举、CDC能通信、U盘能打开”不能作为最终验收标准。
