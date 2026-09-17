# LVGL 多页面 UI 拆分约定（app/app_ui.c 框架 + app/ui/ 每页独立）

当 LVGL v8 工程超过 2~3 个页面（如状态 / 硬件信息 / 控制 / 时钟闹钟），把单文件 `app_ui.c` 拆成
**框架 + 每页独立文件**，避免单文件膨胀难维护（已在多页 LVGL 工程双构零警告验证）。

### 15.1 目录与文件职责
```
app/
├── app_ui.c         # 框架核心：屏幕背景/布局/导航条/2Hz tick 刷新/页面分发(ui_build/ui_rebuild)/公共 API
├── app_ui.h         # 对外公共 API（供 ui_task 等调用，如 create/show_centered/show_fault/switch_page）
└── ui/
    ├── ui_common.h  # 共享声明枢纽：几何宏(UI_PAD/HDR_H/NAV_H/...)、UI_PAGE_COUNT、页枚举 ui_page_t
    │               #   + ui_handles_t(widget 句柄) + extern 全局(s_w/s_h/s_page/s_uptime_sec/...)
    │               #   + 辅助函数原型(mk_label/make_band/mk_nav_button/...) + 各页 build/refresh 原型
    ├── page_status.c   # 页0 状态（USB/字库/SD/运行时间）
    ├── page_hwinfo.c   # 页1 硬件信息（传感器）
    ├── page_ctrl.c     # 页2 控制（LED/蜂鸣器）
    └── page_rtc.c      # 页3 时钟/闹钟（编辑/报警/自动关）
```
- **框架只管调度**：`ui_build()` 按 `s_page` 调 `build_page_xxx()`；2Hz `lv_timer` 调 `refresh_xxx()`。
- **每页只管自己**：widget 句柄存 `ui_handles_t`（在 `ui_common.h` 声明，各页 `extern` 读写）。
- **跨文件全局**（`s_w`/`s_h`/`s_uptime_sec` 等）一律在 `ui_common.h` 用 `extern` 声明，在 `app_ui.c` 定义一次。

### 2. CMakeLists 必须 GLOB 子目录
```cmake
include_directories(app/ui)                       # ui_common.h 可被各页找到
file(GLOB APP_SOURCES app/*.c)
file(GLOB UI_SOURCES app/ui/*.c)                  # 新增页面文件自动进编译
add_executable(${PROJECT_NAME}.elf ${APP_SOURCES} ${UI_SOURCES} ${BSP_SOURCES} ...)
```
⚠️ 加了 `file(GLOB app/ui/*.c)` 后**必须重跑 cmake 重扫**（改动 CMakeLists 即触发），否则新页不编译。

### 3. static / extern 冲突坑（多文件拆分必踩）
页内函数若要在 `ui_common.h` 里声明为 `extern` 非 static 原型（被其他页/框架调用），
**页内实现绝不能加 `static`**，否则报 `static declaration of 'xxx' follows non-static declaration`。
规则：
- 真正跨文件用的（build/refresh/按键回调）→ 头里声明为普通原型，页内实现**不带 static**；
- 仅本页私有的辅助函数 → 头里不声明，页内 `static` 即可。

### 4. LVGL 切页防野指针
`ui_teardown()` 必须**先 `lv_timer_del(s_timer)` 再 `lv_obj_clean(lv_scr_act())`**：顺序反了定时器会
持有已释放的 `lv_obj_t*`，下一 tick 触发 HardFault。

### 5. 交互细节约定
- 编辑类控件（上/下 步进）实时改编辑单元格即可；**汇总标签（如「闹钟 08:30 开」）只在提交动作**
  （如 `设置`/`开启`/`关闭` 按钮）时刷新，避免编辑过程中标签乱跳。
- 报警/蜂鸣器类：关闭动作要同时停外设（`BSP_BEEP_Off()`），并持久化状态（EEPROM/备份寄存器）。
- 自动关：报警用 tick 计数窗口（如 2Hz×120=60s），超时调关闭函数（停外设+持久化+刷新标签）。
