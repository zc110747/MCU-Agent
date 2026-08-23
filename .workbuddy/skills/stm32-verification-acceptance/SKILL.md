---
name: stm32-verification-acceptance
description: STM32 嵌入式项目的端到端验收方法论：Debug/Release 双构零警告、OpenOCD 烧录、串口/网络真机验证、Python/C# verify 脚本 pass/fail 计数模式、增量交付清单。适用于"验收 STM32 固件""写嵌入式自测脚本""定义项目验收标准""真机烧录后如何确认功能正常""整理交付清单"。触发词：验收流程、零警告构建、OpenOCD 烧录验证、串口自测、verify 脚本、pass/fail 计数、交付清单、真机验证、嵌入式测试、snmp_verify、serial_test。
agent_created: true
---

# STM32 端到端验收方法论

嵌入式代码"生成"只是第一步，**跑通验证闭环**才是迭代基础。本 skill 把 9 个项目的验收
实践固化成可复制流程。配套：`stm32-project-scaffold`（构建）、`stm32-ai-dev-environment`（环境）。

## 一、标准验收链（每个模块必走）

```
实现计划（先确认再动手）
   ↓
编码实现（app/ + bsp/ + tools/ 同步）
   ↓
Debug + Release 双构零警告        ← 用数字显式列出 RAM/FLASH 占比
   ↓
仿真 / 真机验证（openocd 烧录 + 串口/网络）
   ↓
扩展 verify 脚本（python / C#，pass/fail 计数）
   ↓
交付清单（增量汇报，✅ 状态收尾）
```

**铁律**：任何新模块动手前必须先出实现计划并获确认；每完成一模块立即增量汇报。

## 二、双构零警告（构建验收）

```bash
# Debug
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# Release
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```
- 强约束 **零错误零警告**（`-Wall -Wextra`，见 `stm32-ai-dev-environment`）。
- 验收时**显式列出资源占用数字**：
  - FLASH：字节 / 总容量 / 百分比
  - RAM（内部）：字节 / 总容量 / 百分比
  - SDRAM 池：占用大小
  - 例：`FLASH 310808B / 1024KB (29.79%)`、`RAM 85992B / 192KB (43.74%)`
- 仅 RWX 段良性提示可豁免（裸机/链接脚本特性），其余警告须清零。

## 三、OpenOCD 烧录验证

```bash
openocd -f openocd/stm32h743_stlink.cfg \
  -c "init" -c "program build-release/xxx.elf verify reset exit"
```
成功标志：
```
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
```
- 记录目标信息：ST-Link 固件版本、Target voltage、SWD DPIDR（H743=0x6ba02477，F429=0x2ba01477）。
- 烧录后通过串口确认系统起来（心跳 LED / 启动日志）。

## 四、真机功能验证（串口 / 网络）

### 4.1 串口控制台（最常用）
- H7 调试串口 USART1 PA9/PA10（COM19 ST-Link 虚拟串口，或 TinyUSB CDC COM4）。
- 真机打印启动日志：时钟、版本、字库挂载状态、系统存活（LED 心跳）。
- 命令式测试：用 python `pyserial` 发命令、收响应、断言。

### 4.2 网络验证（F4 网络项目）
- `ping` 全尺寸扫一遍区分故障类型（1472 通/1473 断 = 分片路径失效，详见
  `stm32-bare-metal-bringup` 的"悬崖 vs 渐变"）。
- Web：`curl http://IP/ | grep -q "<特征串>"`（避开 Git Bash 吞 `%{}`，见 `stm32-ai-dev-environment`）。
- `arp -a <IP>` 核对 MAC OUI（ST=00:80:E1）防假通。
- SNMP：用 PC 端工具或 `snmpget` 验证 Agent 响应（UDP 161）。

## 五、verify 脚本模式（可复制模板）

**核心原则：脚本给出 pass/fail 计数**，而非人肉看日志。本项目实测有效的几种形态：

### 5.1 Python 串口自测（例：008 NES 菜单，28/28 PASS）
```python
import serial, sys
port = sys.argv[sys.argv.index("--port")+1]
ser = serial.Serial(port, 115200, timeout=2)
checks = 0; passed = 0
def chk(name, cond):
    global checks, passed
    checks += 1; passed += 1 if cond else 0
    print(f"[{'PASS' if cond else 'FAIL'}] {name}")
# basics / navigation / keys / error handling ...
print(f"\n** {passed}/{checks} checks passed **")
sys.exit(0 if passed==checks else 1)
```

### 5.2 C# 批量验证（例：101 SNMP，31/31 PASS）
- 构造 Get/GetNext/Set 请求，解析 VarBind，对每项错误计数。
- `dotnet run` 后打印 `31/31 PASS` 或失败项明细。

### 5.3 离线逻辑单测（PC 侧 gcc 编译 + ctypes）
- BER/MIB/报文编解码等纯逻辑，在 PC 用 gcc 编译成 .so/.dll，python ctypes 调用断言。
- 不依赖硬件，CI 友好（例：36/36 PASS）。

### 5.4 视觉渲染测试（桌面仪表盘）
- PrintWindow 截图 + Pillow 亮像素检测，验证每页非空白（例：6/6 PASS）。

## 六、交付清单（增量汇报模板）

每个模块完成即汇报，用 ✅ 状态收尾：

```
## 交付清单 — <模块名>
- [x] 实现计划已确认
- [x] 编码完成（app/ + bsp/）
- [x] Debug/Release 双构零警告（FLASH xx% / RAM xx%）
- [x] 真机烧录验证（Verified OK）
- [x] verify 脚本 <n>/<n> PASS
- [x] README.md 已更新
```

## 七、稳定性验收（网络/长运行项目必做）

- 压测前后各查一次 CFSR/HFSR=0（无总线故障）。
- 连续数十次业务请求无失败（验 pbuf/pcb 无泄漏）。
- 长运行浸泡（如 SNMP 30s 轮询 `sensor_valid` 恒为 1），确认无数据冻结。
- 详见 `stm32-bare-metal-bringup` 的"运行时状态验证"与 `stm32-peripheral-drivers` 的 I2C 恢复。
