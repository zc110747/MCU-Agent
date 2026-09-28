# 204.esp32s3_touch_ebook — 项目长期约定

目标：把开源 Ebook 重实现到 Waveshare ESP32-S3-Touch-LCD-4.3B（800×480 RGB, GT911 触摸）。

## 硬约束
- 栈：ESP-IDF + C/C++ + CMake + Ninja + LVGL 9.3.0；禁 Arduino/PlatformIO。
- 固定 800×480 横屏，应用层不做业务级旋转。
- 禁整体非等比例拉伸（必须重布局+等比缩放+Flex/Grid）。
- 硬件参数以 Waveshare 官方文档/示例为准，禁猜 GPIO。
- 页面封闭：Home/Reader/FileManager/Photos/Notes/Drawing/Clock/Calendar/Weather/Settings(+DisplayTest)。
- 禁音视频播放类功能/词（源码/头/CMake/UI/菜单/Service/README 全查）。

## 工程约定
- 分层单向 app→ui→services→platform→ESP-IDF；UI 层禁直接碰 GPIO/I²C/SD。
- 引脚真源 board_config.h；配置真源 sdkconfig.defaults（改后必须 `del sdkconfig` 再构建，否则静默失效）。
- `lv_obj_get_coords()` 惰性：create() 后同周期读得 0x0，读前先 `lv_obj_update_layout()`。
- 视觉唯一真源 ui/theme.*；页面禁自定义配色/圆角/字体。
- 每页右上角必须有退出按钮回 Home（Home 例外）；ui::page_layout(with_close=true) 自动加，动作经 ui::set_page_close_handler(app::go_home) 注入一次。
- 构建只走 `./tools/run_idf.bat <args>`（Bash 里，勿用 `cmd //c` 静默空转）；判据=看到脚本 banner + 末行 EXIT CODE + build_log.txt mtime。
- 新源文件入主仓须 `touch main/CMakeLists.txt`（GLOB 只在 CMakeLists 变更时重扫，否则新文件静默不编译→链接期 undefined reference）。
- 抓串口 `python tools/serial_probe.py COM14 [秒]`（HardReset(usb) 进应用；输出含 NUL 先 `tr -d '\000'`）。
- 测试产物放 logs/（已 gitignore，时间戳归档）。一键入口 build_oneclick.bat / flash.bat [COMx]（纯 ASCII，每条退出路径 PAUSE，BAT_NOPAUSE=1 跳过）。

## 防坑速记（实测坑）
- 显示通路成对约束：`display_driver.c` 的 `bounce_buffer_size_px!=0` 与 `lvgl_port.cpp` 的 `rgb_cfg.flags.bb_mode=true` 必须同时成立；弄错=等永不到来的事件(卡死/黑屏)。三条铁律细节见 skill esp32-platform-notes/references/rgb-lcd-panel.md。
- GT911 INT 开漏，主机侧必须上拉；无上拉时触摸点读不到但 I²C 仍能读 ID（极易误判驱动正常）。复位时序 100ms/100ms/200ms（10ms 是脉冲最小值非上电窗口）。轮询模式(int_gpio_num=GPIO_NUM_NC)即官方路径。
- SPI-SD（CS 挂 I²C 扩展器 CH422G EXIO4，驱动侧 SDSPI_SLOT_NO_CS）：mount 前必须 CS 高 + ≥74 空闲时钟，否则卡粘在半条命令（芯片复位不切卡供电，普通复位/重烧清不掉）。
- 诊断/探针代码禁入交付固件；临时改动一律 `git checkout -- <files>` + 删新增文件，勿逐个手改（探针在启动序列崩溃=设备反复重启 rst:0xc）。
- Reader/Photos 等"查找目录列表"与"打开文件路径"必须同源（同一份 const char *dirs[]）。
- `sizeof(services::DirEntry)=136`；Pages 由 AppManager::instantiate() 用 new 从堆建，绝不放栈（main 栈 8192B）。

## CJK 字库策略（ui/sd_font.* + services/storage_service.*）
- 卡上 GBK 点阵字库 GBK{16,24,32}.FON：16px(UI)/24px(阅读) 整包进 PSRAM(748KB/1683KB)；32px(3064KB) 走 SD 按需读+384 槽 LRU 缓存(~48KB PSRAM)，不整包占 PSRAM（Photos 整屏解码~768KB 会撞墙）。
- **GBK 文件只含双字节区 23940 字形，不含 ASCII**——混合文档里的英文必须走独立回退字（不是"GBK 兼容 ASCII"，文件大小 766080=23940×32 已证明无 ASCII 余量）。
- **英文与中文同高修复（2026-09-28，基线）**：原 GBK 字 fallback 硬编码成内嵌 16px CJK 字，CN32 文档里英文只有 16px 且按 lv_draw_label 用行字度量定位会偏下。改法：自生成**满格位图 ASCII 字**(tools/gen_ascii_font.py → main/ui/ascii_fonts.cpp，16/24/32 三档，每字母塞 N×N 整格、与 GBK 同源列优先 1bpp 布局)，编译进固件(共~22KB，不依赖 SD 卡)；把 16/24/32 GBK 字的 fallback 分别指向同尺寸 ASCII 字(满格/同基线/同格宽)。sd_font.cpp 的 face_glyph_dsc 增 ascii 分支(gid=cp-0x20)。已真机验证：boot 日志 `installed ... ascii fallback: N px, 95 glyphs` ×3，干净启动。
- 索引表 s_index(64K×2B=128KB PSRAM，ensure_index 只建一次)；Reader 字号梯 CN16/CN24/CN32，默认取最大（正文 16px 汉字~1.9mm/1px 笔画不可读，从不是默认）。
- 随机读 storage_open/storage_read_at/storage_close（FatFs VFS 全局锁串行化）。

## RTC（PCF85063A, I²C 0x51）
- 不支持自增块读写：必须逐寄存器单字节事务（pcf85063_get/set 已改）。
- 秒计数器硬件损坏：SECONDS 冻结 0x00，HOURS 每真实秒+1。时间改由 ESP-IDF 系统时钟(time()/settimeofday)权威走时；RTC 仅持久层（年份写入也不持久）。软件时钟兜底现状（用户：电池没电先不处理）。

## WiFi 站（services/net_service.*）
- 凭据单一主人：net_service 写 NVS `wifi`，驱动 CONFIG_ESP_WIFI_NVS_ENABLED=n + WIFI_STORAGE_RAM；代价=配置不跨复位，每次关联必须先 esp_wifi_set_config()(apply_and_connect())。
- `WIFI_REASON_ASSOC_LEAVE`(8)/`STA_LEAVING`(36) 永远不算失败（只来自己方 disconnect）。

## 节奏
先计划→确认→实现→零警告构建→真机验证→交付清单；每 Phase 单独本地 commit，push 用户自理。
