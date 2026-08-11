# NES 虚拟手柄工具（NesPadTool）

基于 C# + WinForms（.NET 9）的 NES 模拟器上位机控制台。配合 STM32H743 上的
LVGL 菜单 + NES 模拟器固件（`app_cmd.c` 串口协议）使用，把 PC 键盘 / 鼠标
变成 NES 手柄，通过串口（ST-Link VCP 或 USB CDC）发送控制指令。

> 设计参考了同目录下的两个 C# 工具：
> - `串口工具`（SerialTool）—— 串口连接 / 端口扫描 / UTF-8 收发模式
> - `记账工具`（AccountingTool）—— `AppConfig` JSON 配置持久化、`Skin` 配色主题、`StyleButton` 统一按钮风格

## 功能

- **配置文件保存串口信息**：端口、波特率、皮肤、按键映射全部存于程序目录
  `nespad.config.json`，下次启动自动加载。
- **按键映射**：物理键 → NES 虚拟键，支持「学习」重绑，默认 WASD=方向、
  J/K=A/B、左Shift=Select、Enter=Start。
- **图标显示一致**：手柄所有按钮统一风格（同一 `StyleButton` 思路 + `Skin`
  配色），且**鼠标点按与物理键盘按下共用同一高亮态**——视觉完全统一。
- **NES 虚拟手柄**：方向键 + A/B + Select/Start，鼠标按住=`down`，松开=`up`。
- **快捷指令**：打开 NES 页 / ROM 列表 / 加载 ROM0 / 停止 / 状态 / 释放全部。
- **设备回显**：实时显示固件返回的 `OK` / `ERR`。

## 构建与运行

前置：.NET 9 SDK（已验证 `9.0.308`）。

```bat
rem 方式一：直接运行（首次会编译）
run.bat

rem 方式二：命令行
dotnet run -c Release

rem 仅编译 / 发布独立 exe
dotnet build  -c Release
dotnet publish -c Release
```

产物：`bin/Release/net9.0-windows/publish/NesPadTool.exe`（双击即可，需 .NET 9 运行时）。
> 若需完全脱离运行时的单文件 exe，用：
> `dotnet publish -c Release -r win-x64 -p:PublishSingleFile=true`

## 使用步骤

1. 板子通过 ST-Link 连上电脑，固件已烧录并运行（控制台走 USART1 VCP 或 USB CDC）。
2. 运行 `NesPadTool.exe`，在「串口配置」选择 `COM19`（ST-Link VCP）或 `COM4`（USB CDC），
   波特率 `115200`，点「打开串口」。
3. 点「打开 NES 页」→「ROM 列表」→「加载 ROM0」即可开始游戏。
4. 在手柄区**鼠标点按**按钮，或直接**敲击键盘映射键**控制；「释放全部」可解除卡住的按键。
5. 「按键映射」面板里点某键的「学习」，再按一个物理键即可重新绑定；「恢复默认映射」复位。

## 串口协议（与固件 `app_cmd.c` 对齐）

每行以 `\r\n` 结束，回复 `OK <...>` / `ERR <...>`：

| 指令 | 说明 |
| --- | --- |
| `down <name>` | 按下并保持（name: up/down/left/right/a/b/select/start） |
| `up <name>` | 抬起 |
| `key <name>` | 点按一下（自动释放） |
| `release` | 释放全部按键 |
| `open nes` | 打开 NES 页 |
| `rom list` / `rom load <n>` / `rom stop` / `rom info` | ROM 管理 |
| `status` / `menu` | 状态 / 返回菜单 |

## 文件结构

```
NesPadTool/
├─ NesPadTool.csproj   # net9.0-windows + WinForms + System.IO.Ports
├─ Program.cs          # 入口（STAThread / 高DPI / 异常捕获）
├─ Skin.cs             # 配色主题（record + 预设，统一图标颜色）
├─ AppConfig.cs        # 配置 JSON 持久化（串口 + 皮肤 + 映射）
├─ KeyMap.cs           # 默认物理键→NES 虚拟键映射
├─ NesKeys.cs          # 虚拟键定义与串口命令构造
├─ MainForm.cs         # 主窗口：串口 / 手柄 / 映射 / 快捷指令 / 日志
├─ run.bat             # 一键运行
└─ nespad.config.json  # 运行时生成的配置（自动）
```
