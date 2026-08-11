# STM32H743 NES 模拟器工程 — 长期记忆

## 项目定位
STM32H743ZIT6 + LVGL v8 菜单框架 + 纯 C NES 模拟器核心。板载**无实体按键**，所有 UI 输入来自 USART1(ST-Link VCP)/USB CDC 文本命令。固件标识 `H743-NES v1.0.0`。

## 硬件（核对要点）
- HSE 25MHz 无源晶振（`HSE_VALUE=25000000`）
- ST7789 240x240 OLED，SPI6（PG13 SCK/PG14 MOSI/PG8 CS/PG15 DC/PG12 BL）
- SD 卡 SDMMC1，盘符 `1:`，中文字库 `1:/SYSTEM/FONT/`
- USART1 PA9/PA10（115200）；USB OTG_FS PA11/PA12（TinyUSB CDC）
- 已剔除：GPS/温湿度/气压/FM/VS1053/外扩SRAM；NES 无 APU（不发声）

## 构建链
CMake+Ninja+arm-none-eabi-gcc；OpenOCD+ST-Link+Cortex-Debug。Debug(-Og) 默认，Release(-O2) 实机游玩必需（NES 解释型 6502 在 -Og 跑不满 60fps）。链接 `-specs=nano.specs`，printf 走强 `_write` 双串口。源码统一 UTF-8 + `-fexec-charset=UTF-8`。

## 内存布局
机器态(~82KB)→.dtcm 0x20000000；ROM(≤256KB)→.ram_d2 0x30000000。Release 实测：DTCM 62.82%、RAM_D2 88.89%、FLASH 22.85%（启用 CP936 中文字符表后 ff.c/ffunicode.c 增大）。

## 模块关键事实
- `app/app_cmd.c`：唯一输入设备，行缓冲≤96B，回 `OK `/`ERR ` 一行；命令见 README §5。
- 虚拟按键名：up/down/left/right/a/b/select/start/ok/back/menu。
- NES ROM：`1:/NES` 主、`1:` 回退；支持 iNES Mapper 0/1/2/3/4/7（NROM/MMC1/UNROM/CNROM/MMC3/AxROM），≤256KB。
- 显示：256x240 裁左右各 8 列→240x240；带状缓冲每 30 行刷一次。
- 全屏 NES 页暂停 LVGL（`app_menu_is_full_screen()`），直接刷 SPI6。

## 验收/测试
`scripts/serial_test.py`（pyserial）：--ports/--list/--play N/--interactive/--port。覆盖 basics/navigation/keys/nes/errors。默认 COM19。

## PC 上位机工具（NesPadTool，C#/WinForms）
- 用户偏好：**PC 上位机用 C#/WinForms 实现**（非 Python/tkinter）。参考既有 C# 工具 `串口工具`(D:/data/workspace/串口工具) 与 `记账工具`(D:/data/workspace/记账工具) 的风格。
- 工程：`D:/data/workspace/NesPadTool/`（net9.0-windows + WinForms + System.IO.Ports 9.0.0）。用途：串口控制菜单/NES + NES 虚拟手柄 + 按键映射，替代手敲命令。
- 复用范式：串口工具的 `SerialPort`(Dtr/Rts + 扫描定时器 + UTF-8 收发 + `\r\n`)、记账工具的 `AppConfig`(JSON 存程序目录) + `Skin`(record+预设) + `StyleButton(role)`/`ApplySkin`(统一配色=图标一致)。
- 关键设计：鼠标点按与物理键**共用 `SetKeyPressed()` 单一高亮入口**（图标显示一致）；物理键 `Form_KeyDown` 用 `_heldPhysical` 去重防 auto-repeat；「学习」模式重绑物理键（保持 1:1）；配置（串口 port/baud+皮肤+映射）落盘 `nespad.config.json`。
- 虚拟键指令：`down/up <up/down/left/right/a/b/select/start>`、`release`、`open nes`、`rom list/load/stop/info`、`status`(与固件 `app_cmd.c` 对齐)；默认映射 WASD=方向、JK=A/B、左Shift=Select、Enter=Start。
- 验证：`dotnet build/publish -c Release` **0 错误 0 警告**；产物 `bin/Release/net9.0-windows/publish/NesPadTool.exe`。运行 `run.bat` 或 `dotnet run -c Release`；选 COM19(ST-Link VCP)/COM4(USB CDC)，115200。
- 注：早期曾实现 Python/tkinter 版（`tools/pc_gui/`）但已被放弃，改走 C#。Python 版通信已真机验证但 GUI 未实拉，保留仅供参考。

## 实机验证（2026-08-11，ST-Link V2 已连本机）
- 烧录器：ST-Link V2（V2J38M27，VID:PID 0483:374B），OpenOCD 0.12.0 直连识别，无需额外驱动。
- 烧录命令：`openocd -f openocd.cfg -c "init" -c "program build-release/nes_h743.elf verify reset exit"` → Programming/Verified OK。
- 控制台双通道均验证：COM19=STLink VCP（USART1 PA9/PA10）；COM4=USB CDC（PA11/PA12，TinyUSB，"uart+usb"）。两者共用 app_cmd 解析器。
- `status` 实测：sysclk 480MHz、clock HSE 25MHz（外部晶振锁定）、view=menu。
- Python 自测 **34/34 PASS**（`serial_test.py`，basics/navigation/keys/nes/errors）；新增 `scripts/verify_features.py` 专门验证中文名显示 + SELECT 退出（3/3 PASS）。
- **NES 实战已跑通**：ROM 放 SD 卡根目录 `1:`（非 `1:/NES`，回退扫描生效），`rom list` 找到 4 个 `.NES`（中文名现已正确显示：超级玛丽/导弹坦克/快打旋风中文无敌版/魂斗罗）。`rom load 0` 加载 40976B / **mapper 0 (NROM)**，`rom info` 实测 **fps 31**，`status` 显示 `view: nes (fullscreen)`，`rom stop` 优雅退出（442 frames）。Release@480MHz 实测 31fps，高于预估 ≥20。

## 已知限制
- 无 APU：NES 不发声。
- 中文 UTF-8 / GBK 文件名显示（2026-08-11 修复）：
  - FatFs `FF_CODE_PAGE` 必须为 **936**（原为 437，会把中文 LFN 转 CP437 丢字符 → f_open 命中失败、串口/屏全乱码）。`fno.fname` 现返回 GBK（保留中文、保 `f_open` 命中）。**切勿改回 437。**
  - 串口 `rom list` 经 `bsp/gbk_conv.c` 转 UTF-8（`bsp/gbk_unicode_tbl.c` 由 `tools/gen_gbk_table.py` 离线生成，23940 项/21791 映射，~48KB Flash）。
  - OLED：`lv_font_gbk_16` 字体驱动做 **UTF-8→Unicode→GBK 字形** 映射，故 `page_nes.c` 的 `build_browser` 必须先把 GBK 名 `gbk_to_utf8()` 转 UTF-8 再喂 `lv_label_set_text`（直接喂 GBK 字节会乱码——即原 bug #1）。
- NES 退出键（2026-08-11 新增）：运行中按 **SELECT / BACK / MENU** 任一均退出游戏回 ROM 浏览器；浏览器中按 **SELECT** 退出 NES 页回主菜单。SELECT 已从 NES 手柄映射移除（设计取舍：SELECT 专用作“退出”，游戏中 SELECT 按钮不再可用）。
- 仅验证 Mapper 0 (NROM)，其余 Mapper 1/2/3/4/7 尚未实机跑（代码已支持）。
- 显示/SPI 刷新观感需用户在真机肉眼确认（已用 `rom info` fps=31 量化证明模拟器在跑）。
