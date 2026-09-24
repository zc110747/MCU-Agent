# 204.esp32s3_touch_ebook — 项目长期约定

目标：把开源 Ebook 的功能/交互/视觉**重新实现**到 Waveshare ESP32-S3-Touch-LCD-4.3B
（800×480 RGB，GT911 触摸）上。需求全文见 `doc/prompter-step1.md`（那是契约，不是参考）。

## 不可违反的硬约束

- 技术栈 **ESP-IDF + C/C++ + CMake + Ninja + LVGL**；**禁止 Arduino / PlatformIO**。
- 固定 **800×480 横屏**，应用层不做业务级旋转。
- **禁止整体非等比例拉伸**（`scale_x = 800/old_w` 这类写法一律拒绝）；
  必须"重新布局 + 等比例缩放 + Flex/Grid 自适应"。
- 硬件参数以 **Waveshare 官方文档/示例为准，禁止猜 GPIO**。
- 页面集合封闭：Home / Reader / FileManager / Photos / Notes / Drawing / Clock /
  Calendar / Weather / Settings（+ Phase1 的 DisplayTest）。列表外的页面即回归缺陷。
- 项目中不得出现音视频播放类功能或相关词（源码 / 头文件 / CMake / UI / 菜单 /
  Service / README 全部检查）。

## 工程约定

- 分层单向 `app → ui → services → platform → ESP-IDF`；UI 层不得直接碰 GPIO/I²C/SD。
- 引脚与时序唯一真源 `main/platform/board/board_config.h`；改引脚必须重新双证据链核对
  （原理图 PDF + 官方 wiki）。
- 配置唯一真源 `sdkconfig.defaults`；`sdkconfig` 已 gitignore，不要提交。
  **改完 `sdkconfig.defaults` 必须 `del sdkconfig` 再构建**——Kconfig 只在 `sdkconfig`
  *不存在*时用 defaults 播种，之后改 defaults 完全无效且**静默**（源码写 16ms、
  固件跑 33ms、中间构建全绿）。`tools/idf_launcher.py` 已加陈旧告警，看到
  `idf_launcher: WARNING: sdkconfig.defaults is NEWER than sdkconfig` 就照做。
- **分层是编译期强制的**：app 层引 `board_config.h` 会直接编译失败。要在日志里对照
  平台层的值，就让平台层自己打印（`system_info_log_panel()`），不要往上层拖。
- `lv_obj_get_coords()` 惰性求值：`create()` 之后同周期读会得到 `x=0..-1 / 0x0`
  （"未布局"，看起来像整页尺寸为 0 的假 bug）。读之前先 `lv_obj_update_layout()`。
- LVGL 的 CPU/FPS 浮层**只要 `LV_USE_PERF_MONITOR=y` 就自动显示**（`lv_display.c`
  在创建 display 时调 `lv_sysmon_show_performance()`，位置 `LV_ALIGN_BOTTOM_RIGHT`），
  不需要应用代码。本项目默认 `lv_sysmon_hide_performance()` 关掉。
- **显示通路成对约束**：`display_driver.c` 的 `bounce_buffer_size_px != 0` 与
  `lvgl_port.cpp` 的 `rgb_cfg.flags.bb_mode = true` **必须同时成立**。bb_mode 只决定
  释放 LVGL 绘制缓冲的事件（bb → `on_frame_buf_complete`，非 bb → `on_vsync`），
  弄错就是等一个永不到来的事件（卡死/黑屏）。改一个必须改另一个。
- **显示通路三条铁律**（细节+行号见 skill `esp32-platform-notes/references/rgb-lcd-panel.md`）：
  1. 面板"整体位移/右缘绕回左边"= RGB DMA 带宽失步（permanent desync），**别调 porch**。
     正解**只用 bounce buffer**；`CONFIG_LCD_RGB_RESTART_IN_VSYNC` 必须**保持关闭**
     （它无条件重启 GDMA、并编译掉驱动自己的欠载检测，反而制造位移条件）。
     **两个修复同时上 = 无法归因**，事后必须做单变量回退。
  2. `system_info_log_tasks()` 的 CPU 占比是**地板不是真相** —— FreeRTOS 不把 ISR 时间
     归给任何任务，而 bounce 填充是每帧 768 KB 的 ISR 内 memcpy。
  3. direct 模式每次 flush 无条件等**一整帧**；面板 39 Hz（pclk 16/820/500）是硬上限。
     **低于面板帧率才算真瓶颈，先测再改。** `LV_DEF_REFR_PERIOD` 是请求不是上限，
     且一个值管三件事（刷新/**输入读取**/动画）。
- **GT911 触摸：INT 是开漏输出，主机侧必须给上拉。** 无上拉时空闲电平悬空 → 触摸点读不到
  （而 I²C 仍能读到 TouchPad_ID，开机日志一切正常，所以极易误判为"驱动没问题"）。
  复位时序按官方 `waveshare_rgb_lcd_port.c`：RST 低 **100 ms** → INT 拉低 → **100 ms** →
  RST 高 → **200 ms**，地址在 RST 上升沿锁存；10 ms 是数据手册的**脉冲**最小值，
  不是上电窗口。轮询模式（`int_gpio_num = GPIO_NUM_NC`）本身就是官方路径。
  取证方法：被动观测三层（INT 电平 / `touch->data.points` / indev 事件计数），
  **直接读 0x814E 的探针会抢走 buffer-ready 位**，不能用来诊断。
- **CJK 不走存储**：`CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK` 编译进镜像（约 1.1 MB，
  镜像里最大的一项），只此一档，经 `Theme::font_cjk()` 提供；别的页面不要引。
- `main/CMakeLists.txt` 自动发现源文件与 include 目录，新增层内目录无需改 CMake。
- 视觉唯一来源 `ui/theme.*`；页面不得自定义配色/圆角/字体。
- `AppManager` 同一时刻只存活一个页面；`Page` 生命周期 create/on_enter/on_leave/destroy。
- **每个页面右上角必须有退出按钮**（回 Home），只有 Home 例外（它本身就是应用列表）。
  实现：`ui::page_layout(..., with_close=true)` 自动加，动作由 app 层经
  `ui::set_page_close_handler(app::go_home)` 注入一次——ui 层不能反向依赖 app 层。
  页内 overlay（如 FileManager 预览）传 `with_close=false`，用自己的 Close 关掉自己。
- FreeRTOS 任务结构是明写的且可核对：`taskLVGL`(core1/prio6/8K) + `app`(core0/prio4/4K)
  + `main_task` 只跑启动序列。`system_info_log_tasks()` 打印任务表与 CPU 占比。
- `.bat` 纯 ASCII + 每条退出路径 visible；构建一律走 `tools\run_idf.bat`（输出镜像到
  `build_log.txt`）。
- **构建只能这么调：Bash 工具里 `./tools/run_idf.bat <args>`**。`cmd //c` 在 Bash 里是
  **静默空转**（退出码 0、`build_log.txt` 不更新，极易误判成功）；`cmd.exe` 字面量在
  Bash 与 PowerShell 工具里都被安全策略拦截。判定构建结果必须核对首行
  `=== idf.py ... ===`、末行 `EXIT CODE`，并看 `build_log.txt` 的 mtime 是不是本次的。
- **`sizeof(services::DirEntry)=136`**（`name[128]` + bool + uint32），所以
  `entries_[kMaxEntriesPerPage]` = **8704 B**。`PhotosPage`/`ReaderPage` 各有一个。
  页面由 `AppManager::instantiate()` 用 `new` 从堆建，目前安全 ——
  **绝不要把 Page 对象或 DirEntry 数组放到栈上**（main 任务栈只有 8192 B）。
- SD 卡字库是**经典 GBK 点阵字库**（不是 LVGL 二进制字库）：GBK16.FON = 766080 =
  23940 × 32 B。码位索引 `idx = (b1-0x81)*190 + (b2-0x40) - (b2>=0x7F?1:0)`，
  `lv_binfont_create()` 读不了，需自写读取器。
- **SPI-SD（CS 挂在 I²C 扩展器上）mount 前必须"CS 拉高 + ≥74 个空闲时钟"。**
  本板 CS 由 CH422G EXIO4 驱动、驱动侧 `SDSPI_SLOT_NO_CS`（与 Waveshare 官方同路径），
  CS 从 mount 起常低、全程不复位；**缺唤醒序列时卡片会粘在半条命令上**，而
  **芯片复位不切断卡片供电** → 普通复位/重烧都清不掉，症状伪装成"卡坏了/接触不良"。
  `sd_card.c` 现为：CS 高 → 80 个 400 kHz 空闲时钟（临时无 CS 的 SPI device 发）→
  CS 低 → mount，整套失败重试 3 次（`esp_vfs_fat_sdspi_mount` 失败会自清理，重试安全）。
  **真机已验证：第 1 次尝试即挂载成功。**
  排查口径：`cmd=52`/`cmd=5` 是 SDIO 探测命令，`CONFIG_SD_ENABLE_SDIO_SUPPORT=y` 时
  **SPI 模式也会发**、对普通 SD 卡必然失败且无害 —— 不能据此判定"驱动走错分支"。
- **诊断/探针代码不得进入交付固件，且必须能在最坏情况下全身自保。**
  探针在启动序列里崩溃 = 用户看到的"设备自己反复重启"（本项目 `rst:0xc` 循环就是这么来的），
  还会把它正要测量的硬件拖进坏状态（7.1 的粘卡）。临时改动一律
  `git checkout -- <files>` 整体还原 + 删掉新增文件，不要逐个手改。
- **Reader/Photos 等页面的"查找目录列表"与"打开文件路径"必须同源**：只有一份
  `const char *dirs[]`，列表和打开都按同一顺序走。分开写就会出现"列得出来、打不开"。
- **抓串口用 `python tools/serial_probe.py COM14 [秒]`**（`idf.py monitor` 会独占终端且只能
  Ctrl+] 退出，被 agent 调用时 stdout 会被吞）。该脚本用 `HardReset(usb)` 复位进**应用**，
  不是 `USBJTAGSerialReset`（后者会进 ROM 下载模式，应用根本不跑）；并自带去 ANSI 转义。
  串口原始输出含 NUL，`grep` 会判成二进制：`tr -d '\000' | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g'` 先净化。
- 图片资源用脚本生成（`tools/gen_*.py`），不手写大数组。
- **测试生成文件一律放 `logs/`**（时间戳归档；`logs` 已入 .gitignore）。
- **用户一键入口**：根目录 `build_oneclick.bat` / `flash.bat [COMx]`（纯 ASCII、每条退出
  路径 PAUSE，自动化用 `BAT_NOPAUSE=1` 跳过；内部走 `tools\run_idf.bat`，日志按时间戳
  归档 `logs\`）。在 cmd 中验证：Bash 直接 `BAT_NOPAUSE=1 ./xxx.bat`，
  **`cmd //c "x.bat"` 会静默空跑**，判据 = 必须看到脚本自己的 banner。
- **PCF85063A RTC（I²C 0x51）两个实测硬件怪异，务必记住：**
  1. **不支持自增块读/写**——单条多字节 `transmit`/`transmit_receive` 返回/写入乱字节。
     必须逐寄存器单字节事务（`transmit` 寄存器地址 1 B → 再 `receive`/`transmit` 1 B）。
     `pcf85063_get`/`pcf85063_set` 已改成这种写法，不要"优化"回块读。
  2. **秒计数器硬件损坏**：SECONDS 寄存器永远冻结在 0x00，而 HOURS 每真实秒 +1（MINUTES 正常）。
     任何 I²C 模式都救不了坏硅。**时间改由 ESP-IDF 系统时钟（`time()`/`settimeofday`）作权威走时源**：
     `clock_init()` 从 RTC 播种一次、`clock_set()` 同步写回 RTC 持久层；`clock_now()` 读系统时钟
     （不再读冻坏的 RTC）。这样秒必定以 1 Hz 递增（串口采样 sec=01→06 已验证）。
     RTC 仍作断电持久层，但**年份寄存器写入也不持久**（读回 0x06=2006 而非 0x26=2026），
     即该芯片日期也不可靠；SetTime 能运行时纠正屏幕时间，复位可能恢复 RTC 存的旧日期。

## CJK 字库策略（`ui/sd_font.*` + `services/storage_service.*`）

- 卡上 GBK 点阵字库（`/sd/fonts/GBK{16,24,32}.FON`）**绝不整包装进 PSRAM**。
  16px(UI) 与 24px(阅读) 体量小（748KB / 1683KB）才整包进 PSRAM；**32px(3064KB)
  走 SD 按需读 + 缓存**，否则白占 ~3MB PSRAM（Photos 整屏解码 ~768KB 会撞墙）。
- **32px 的 SD 缓存实现**：`.FON` 保持打开（`storage_open` 长生命周期 FILE*），
  字形按固定偏移 `gid*N*N/8` 用 `storage_read_at` 现取，已解码位图存 384 槽 LRU
  缓存（约 48KB）。一屏去重汉字 <384，冷读后稳态滚动零 SD I/O。这正是 STM32H7
  上"按字形从卡读"的同一思路。
- 随机读接口：`storage_open` / `storage_read_at` / `storage_close`（ESP-IDF FatFs VFS
  用一把全局锁串行化每次 fseek/fread，所以绘制任务与 app 任务不会互相破坏）。
- 索引表 `s_index`（64K×2B=128KB，PSRAM）仍是全尺寸共享；`ensure_index()` 只建一次。
- Reader 字号梯：`CN16 / CN24 / CN32`，默认取最大（XL>large>16）；16px 汉字约 1.9mm、
  1px 细笔画，作正文不可读，所以从不是默认。
- 改字库后若怀疑"整屏虚化/发糊"，先核对：设备实际装的是哪一档（`serial_log.txt` 里
  `sd_font: installed ...` 行会写 PSRAM/SD-cached 与 px），再决定加档还是调缓存容量。

## WiFi 站（`services/net_service.*`，Settings 页是其唯一前端）

- **凭据单一主人**：`net_service` 写 NVS 命名空间 `wifi`；驱动侧
  `CONFIG_ESP_WIFI_NVS_ENABLED=n` + `esp_wifi_set_storage(WIFI_STORAGE_RAM)`，不留第二份
  （第二份永远是 Forget 够不到的那份）。**代价：配置不跨复位** ⇒ 每次关联（含开机）都必须
  先 `esp_wifi_set_config()`，两处入口统一走 `apply_and_connect()`。漏掉就是
  `ESP_ERR_WIFI_SSID` + 不产生任何事件 + 面板永远 connecting（实测开机 18 s 无 auth，
  正确路径 1.24 s 进 auth）。**该缺陷在有探针时被掩盖**——探针自己会写配置。
- **`WIFI_REASON_ASSOC_LEAVE`(8) / `STA_LEAVING`(36) 永远不算失败**：只可能由本机
  `esp_wifi_disconnect()` 产生，且有三处调用点把它当"通往链路的一步"（重配前、扫描前、按钮），
  只判"想不想要链路"的意图标志会漏掉前两处 → 重试账目错位（日志缺 `attempt 1/3`）。
  **意图与"这份报告是不是故障"是两个问题，后者只能从 reason 答。**
- **`esp_timer_start_once()` 对已武装的定时器返回 `ESP_ERR_INVALID_STATE` 且不重启**，
  静默吃掉一格预算却不产生尝试。
- 代价：内部 SRAM 75 172 B（开机端 121 KB / largest 79.8 KB），PSRAM 接走约 660 KB 流量缓冲。
  **`net_init()` 必须在显示通路之后**（`display_init()` L94 早于 `net_init()` L126）——
  改顺序等于改面板的内存保证。
- `tools/verify_wifi.py`：读日志重放状态机，`kMaxRetries`/`kRetryDelayMs` 从源码读；
  **一个日志都读不到时必须返回失败**（`0/0` 会被印成通过，假绿比没脚本更糟）。

## 节奏

先出实现计划 → 确认 → 实现 → 零警告构建 → 真机验证 → `verify_*.py` 一次性脚本
→ 交付清单；每个 Phase 单独 commit（本地），push 由用户自己做。
报告格式见 `doc/prompter-step1.md` §四十六。
