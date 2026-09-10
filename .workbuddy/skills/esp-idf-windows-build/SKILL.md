---
name: esp-idf-windows-build
description: 在 Windows（WorkBuddy 沙箱）上用 ESP-IDF / idf.py 构建、烧录、验证 ESP32 系列工程的完整配方与踩坑清单。涵盖"PowerShell 工具吞掉原生进程输出必须用 Git Bash"、MSYSTEM 兼容、sdkconfig/CMake 时序坑、串口与 USB 验证手法。当需要编译/烧录/调试任何 ESP-IDF 工程（esp32/s2/s3/c3 等）时使用。
agent_created: true
---

# ESP-IDF Windows 沙箱构建与验证

## 0. 首要铁律：不要用 PowerShell 工具跑构建

本环境下 PowerShell 工具会**吞掉所有原生子进程的输出**：

- `& python.exe --version` → 空
- `Start-Process -RedirectStandardOutput ... -Wait` → 落文件 **0 字节**，`ExitCode` 为空
- `dangerouslyDisableSandbox: true` 也无效

但 **cmdlet 正常**（`Test-Path` / `Get-PnpDevice` / `Out-File` / `$PSVersionTable` 都有结果），
所以"命令执行成功、exit 0"**不等于**子进程真的跑了。

**判定方法**（开工前 10 秒自查）：

```powershell
& "C:\path\to\python.exe" --version 2>&1 | Out-File diag.txt -Encoding utf8
```

文件里没有版本号 → 子进程被吞 → 改用 Git Bash。

**结论**：idf.py / cmake / ninja / 编译器这类长任务一律走 **Bash 工具（Git Bash）**。
PowerShell 工具只用于查询类 cmdlet（结果要 `Out-File` 后再读）。

## 1. 定位 ESP-IDF 环境

IDF 可能不在默认位置。按以下顺序找：

```bash
cat "C:/Espressif/tools/eim_idf.json"        # eim 安装器记录：path / python / activationScript
ls "C:/Espressif/tools/"                      # cmake / ninja / xtensa-esp-elf / python 都在这
```

关键路径（版本号会变，务必 `ls` 确认）：

- IDF：`eim_idf.json` 里的 `path` 字段（曾见 `D:\data\agent-tools\esp32\v6.1\esp-idf`）
- 工具链根：`C:\Espressif\tools`
- venv python：`C:\Espressif\tools\python\<ver>\venv\Scripts\python.exe`
- cmake：`C:/Espressif/tools/cmake/<ver>/bin`　ninja：`C:/Espressif/tools/ninja/<ver>`
- xtensa：`C:/Espressif/tools/xtensa-esp-elf/<ver>/xtensa-esp-elf/bin`

## 2. Git Bash 下跑 idf.py（两个办法）

idf.py 在 MSYS 下会因 `MSYSTEM` 环境变量早退（"MSys/Mingw no longer supported"）。二选一：

**A. 用工程已有的 `env.sh`（推荐）**：先确认里面有 `unset MSYSTEM`，然后

```bash
source ./env.sh
"$PY" "$IDF_PATH/tools/idf.py" build
```

**B. 用 `idf_runner.py` 包装器**（工程没有时自建）：

```python
import os, sys, runpy
os.environ.pop('MSYSTEM', None)
tools_dir = os.path.join(os.environ['IDF_PATH'], 'tools')
sys.path.insert(0, tools_dir)
sys.argv = ['idf.py'] + sys.argv[1:]
runpy.run_path(os.path.join(tools_dir, 'idf.py'), run_name='__main__')
```

## 3. 长构建的正确执行方式

```bash
cd <proj> && ./build.sh > logs/buildN.log 2>&1; echo "EXIT=$?" >> logs/buildN.log
```

配 **Bash 工具的 `run_in_background: true`**，再用 `TaskOutput(block=true, timeout=600000)` 等真实结束；
最后 grep 日志：

```bash
grep -n "EXIT=\|Project build complete\|FAILED\|error:\|warning:" logs/buildN.log
```

不要在前台硬等（会撞 120 s 超时被自动 background，日志可能被截断）。

## 4. 常见构建错误

| 报错 | 根因 | 修法 |
|---|---|---|
| `implicit declaration of ESP_RETURN_ON_ERROR` | 缺 `esp_check.h` | `#include "esp_check.h"`；组件 REQUIRES 加 `esp_common log` |
| `'/*' within comment [-Werror=comment]` | 注释里出现 `*/` 或 `/*`（如 `*payload/*payload_len`） | 改写注释文字 |
| `xxx.h: No such file` + `component_requirements.py BUG` | 用 `if(CONFIG_XXX)` 条件化 `REQUIRES` | 见第 5 条 |
| 改了 sdkconfig 但行为没变 / 19 秒就构建完 | cmake 用旧缓存 | `idf.py reconfigure` 后再 `build` |

## 5. Kconfig 宏只能用于 C 的 `#if`，不能用于 CMake 的 `if()`

ESP-IDF v6.x 在**组件注册阶段取不到** `CONFIG_XXX` 的 CMake 变量。条件化 `REQUIRES` 会静默丢掉
include 路径，报 `xxx.h: No such file` + `BUG: component_requirements.py`。

**正确做法**：`REQUIRES` 无条件列出，门控放在 C 代码里：

```c
esp_err_t wifi_xxx_init(void)
{
#if !CONFIG_MY_FEATURE
    ESP_LOGW(TAG, "disabled by CONFIG_MY_FEATURE=n");
    return ESP_ERR_NOT_SUPPORTED;
#else
    ...
#endif
}
```

## 6. 烧录与串口

```bash
source ./env.sh && "$PY" idf_runner.py flash -p COM21 -b 460800
```

- **识别 COM 口必须用 pyserial**（`Get-WmiObject` / `GetPortNames()` 会漏掉 USB 串口），口号会变别硬编码。
- 抓日志用带时间戳的脚本，能区分"卡住"和"真的没输出"：

```python
ser = serial.Serial(port, 115200, timeout=0.5)
ser.setDTR(False); ser.setRTS(True); time.sleep(0.1); ser.setRTS(False)   # 复位
t0 = time.time()
while time.time() - t0 < SECONDS:
    d = ser.read(4096)
    if d: print("[%6.2fs] +%d" % (time.time()-t0, len(d)), d.decode(errors="replace"))
```

## 7. 验证手法（当串口日志不可信时）

**日志级别坑**：先确认 `CONFIG_LOG_DEFAULT_LEVEL`。若为 `2`（WARN），所有 `ESP_LOGI/D/V` **不可见**，
看起来就像"板子卡住了"。此时关键状态日志要临时用 `ESP_LOGW` 输出。

**外部行为验证**比日志更可靠：

- Wi-Fi 热点是否广播：`netsh wlan show networks mode=bssid | grep -i <SSID>`
  （Windows 扫描有缓存，变化后等 20–30 s；可写个"按住 EN N 秒"的脚本，看热点是否随之消失来确认归属）
- USB HID / CMSIS-DAP 链路体检（不需接目标板）：

```tcl
adapter driver cmsis-dap
transport select swd
adapter speed 1000
swd newdap chip cpu -irlen 4
dap create chip.dap -chain-position chip.cpu
target create chip.cpu cortex_m -dap chip.dap
init
```

  见到 `CMSIS-DAP: FW Version` / `Interface ready` 即链路正常；
  末尾 `Error connecting DP: cannot read IDR` 只是**没挂目标芯片**，属预期。

## 8. ESP32-S3 专项坑

- **`tud_connected()` ≠ 有主机**，它只表示 **VBUS 存在**（插充电头也为真）。
  判断"USB 主机已枚举"必须用 **`tud_mounted()`**。
- **Wi-Fi 与 USB OTG 冲突**：`esp_wifi` 一起，USB HID 传输时序会被 RF 破坏，
  主机侧表现为 CMSIS-DAP 通讯错误/超时。需要共存时做成**互斥仲裁**（检测到主机就绝不启动 Wi-Fi）。
- 上电常见 `gpio: conflict found for GPIO[x]` 多为引脚被重复配置，未必是致命错误。
