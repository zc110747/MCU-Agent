# STM32H743 NES 模拟器工程 — 长期记忆（精简版，细节见 .workbuddy/memory/ 每日日志）

## 项目定位
STM32H743ZIT6 + LVGL v8 菜单框架 + 纯 C NES 模拟器。**无实体按键**，全部 UI 输入来自串口文本命令（USART1 ST-Link VCP / USB CDC）。固件 `H743-NES v1.0.0`。

## 硬件/构建
- HSE 25MHz；ST7789 240x240（SPI6 PG13/14/8/15/12）；SD=SDMMC1 `1:`，字库 `1:/SYSTEM/FONT/`；USART1 PA9/10 115200；USB OTG_FS PA11/12 TinyUSB CDC。无 APU。
- CMake+Ninja+arm-none-eabi-gcc；烧录 `openocd -f openocd.cfg -c "program build-release/nes_h743.elf verify reset exit"`。Debug(-Og)=`build/`，Release(-O2)=`build-release/`（实机必需）。UTF-8 源码；`LV_COLOR_16_SWAP=0`（图标裸像素=LE RGB565）。
- 内存：DTCM(128K)+RAM_D2(288K)=运行时 `bsp/sram_pool.c` 边界标记分配器（每块开销 16B）；NES 开页分配 ~82KB 机器态(DTCM)+≤286KB ROM(D2)，退出归还供相机复用。RAM_D3 64K non-cacheable=USB DMA。MPU Region0/1 write-back，DCache 开。空闲基线 DTCM 131024 / D2 294864。
- `status` 命令报 sram 空闲/cache 状态；上位机 `tools/NesPadTool`。

## UI 配色与字体体系（2026-09-10 深色表盘风）
- 全工程取色唯一源：`app/app_page.h` COL_* 宏。深色底 0x07070B + 白字 + iOS 彩色点缀（ACCENT/SEL 0x0A84FF、VALUE 0x30D158、ERR 0xFF453A）；灰阶=淡蓝灰 LABEL 0xA8B4C6 / DIM 0x7E8BA0。
- 主菜单每应用彩色圆片：`app_menu.c` 的 `s_chip[]`（注册序 clock橙/camera青/txt蓝/image紫/nes红/keytest黄/sysinfo绿/about粉）；选中=白环+光晕+白标题。
- 图标位图=彩底白图形：`tools/recolor_menu_icons.py` 原地换色 `app/menu_icons.c`（墨量反相算法保抗锯齿；CRLF/行宽逐字节保留——`read_text()` 会吞 CRLF，须 `open(newline="")`）。
- 字体：UI 全部用 `app/ui_font.c` 的 ui_font_12/16/24/32=Montserrat（编入 Flash）+ `fallback=&lv_font_gbk_xx`（SD 卡字库）——英文/数字/符号 Montserrat、汉字自动回落；**唯一例外 TXT 正文用纯 `lv_font_gbk_16`**（`TXT_BODY_FONT`）。艺术字进一步定制改 lv_conf.h 启用的 Montserrat 字号即可。

## 已修复缺陷（一句话档，详见每日日志）
1. USB 缓存一致性死机(08-12)：`.usb_ram`→RAM_D3 non-cacheable + `CFG_TUD_MEM_DCACHE_ENABLE=1`。
2. 页头时钟悬空指针 BusFault(08-13)：页面拆卸点置 `s_hdr_clock_page=NULL`。
3. NES 静态内存→动态池(08-13)：sram_pool 挂接 nes_open/close，链接期 DTCM/D2 零占用。
4. 分配器 off-by-8 写穿 footer(08-13)：alloc 开销改收 16B（2*BLK_META）。
5. NES_ROM_MAX 256KiB→286KiB(08-13)：撑满 RAM_D2，魂斗罗可载入。
6. 实现 mapper23 VRC2/VRC4(08-13)：Contra(J) 实机 ~41fps，`scripts/verify_contra.py`。
7. JPEG 涂抹(08-14)：`decode_jpeg()` 补 `jd.swap=0`，`stress_img.py` 30 轮 PASS 零泄漏。

## 模块要点
- `app/app_cmd.c` 唯一输入（行≤96B，回 OK/ERR）；虚拟键 up/down/left/right/a/b/select/start/ok/back/menu。
- NES ROM `1:/NES`；Mapper 0/1/2/3/4/7/23；实机验证 mapper0/23 fps~41。全屏 NES 页暂停 LVGL 直刷 SPI6。
- `sd_browser` 共享浏览器（静态单例，末参 filter；目录/".."永不过滤）。
- TXT 阅读器：≤32KB，GBK→UTF-8（UTF-8 BOM 快路径），FF_CODE_PAGE=936 勿改回 437 ⚠️当前 ffconf.h 实为 437，待用户确认改回。
- 图片查看器：BMP+JPEG(TJpgDec，工作区 sram_pool 32KB)；截图 `cap`→`1:/catch/`（JPEG 缓冲 sram_pool 80KB）；相机页 192×192（CAM_DISP_OFFSET=24，共享 RAM_D2）。
- 验收脚本：`scripts/serial_test.py`(51 项)、`stress_txt/usb/sram/img.py`、`verify_cap/camera/img/contra/features.py`。串口号易变勿硬编码（曾 COM19=ST-Link VCP/COM4=CDC）。

## 已知限制
- NES 无声；mapper1/2/3/4/7 未实机跑；VRC4e(stride-4) 未覆盖；显示观感需真机目检。
- 排障速记：SWD 可读 PC/xpsr/s_page_count 判固件死活（09-10 曾 SWD 证明固件健康而 COM19 无日志=VCP 跳线问题）。
