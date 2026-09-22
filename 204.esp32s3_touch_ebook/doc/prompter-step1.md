# Ebook → Waveshare ESP32-S3-Touch-LCD-4.3B LVGL 完整开发任务

你是一名资深 ESP32-S3 / ESP-IDF / LVGL / Embedded UI 工程师。

现在需要将开源项目：

`https://github.com/CHENXiNNNX/Ebook`

迁移并重新实现到：

**Waveshare ESP32-S3-Touch-LCD-4.3B-BOX**

---

# 一、项目目标

这不是简单的源码移植，也不是对原项目进行局部修改。

目标是：

> 参考原 Ebook 项目的功能、页面结构、交互逻辑和视觉设计思想，重新使用 ESP-IDF + LVGL 实现一套适配 Waveshare 4.3 寸 800×480 RGB LCD 的嵌入式应用。

核心目标：

```text
原 Ebook
   │
   ├── 功能
   ├── 页面结构
   ├── 交互逻辑
   └── UI 设计思想
          │
          ▼
    LVGL 重新实现
          │
          ▼
Waveshare ESP32-S3-Touch-LCD-4.3B
          │
          ▼
      800 × 480
```

不要直接复制原项目的底层显示框架。

不要强行保留原项目的 e-paper 渲染架构。

---

# 二、目标硬件

目标硬件：

**Waveshare ESP32-S3-Touch-LCD-4.3B-BOX**

主要硬件：

```text
MCU:
ESP32-S3

Flash:
16MB

PSRAM:
8MB

LCD:
800 × 480
RGB LCD
IPS

Touch:
Capacitive Touch
I2C
Interrupt

Storage:
SD Card

Network:
WiFi
```

注意：

**所有 GPIO、LCD 时序、Touch I2C 地址、Touch IRQ、Touch Reset、SD Card 引脚等硬件参数，必须以 Waveshare 当前官方文档和官方示例为准。**

禁止凭经验猜 GPIO。

如果仓库或者当前 SDK 中已有官方 BSP，优先研究并复用官方 BSP。

---

# 三、软件技术栈

必须使用：

```text
ESP-IDF
C/C++
CMake
Ninja
LVGL
```

禁止：

```text
Arduino
Arduino IDE
PlatformIO
```

标准开发命令：

```bash
idf.py build
idf.py flash
idf.py monitor
```

---

# 四、最重要的 UI 适配原则

## 4.1 固定 800×480

应用最终 LVGL 工作区域：

```text
800 × 480
```

采用：

```text
横屏
Landscape
```

整个应用 UI 统一以：

```text
width  = 800
height = 480
```

作为设计基准。

---

# 五、不要做“整体拉伸”

这是本项目最重要的要求之一。

禁止将原项目整个画面直接：

```text
264 × 176
      ↓
800 × 480
```

然后分别对 X/Y 做独立缩放。

例如禁止：

```text
scale_x = 800 / old_width
scale_y = 480 / old_height
```

再把所有 UI 元素统一拉伸。

因为这样会导致：

```text
圆 → 椭圆

正方形 → 长方形

人物图片 → 变形

图标 → 变形

字体 → 横向/纵向变形
```

---

# 六、正确的 UI 适配方式

采用：

> **重新布局 + 等比例缩放 + 自适应尺寸**

而不是：

> 整体非等比例 Stretch。

例如：

```text
原 UI

┌──────────────────┐
│ Header           │
├──────────────────┤
│                  │
│     Content      │
│                  │
├──────────────────┤
│ Footer           │
└──────────────────┘
```

迁移到 800×480：

```text
┌──────────────────────────────────────────┐
│                Header                    │
├──────────────────────────────────────────┤
│                                          │
│                                          │
│                 Content                  │
│                                          │
│                                          │
├──────────────────────────────────────────┤
│                Footer                    │
└──────────────────────────────────────────┘
```

内容应该根据 800×480 的空间重新排列。

---

# 七、UI 尺寸适配原则

优先级：

```text
1. 重新布局
2. 等比例缩放
3. Flex/Grid 自适应
4. 最后才考虑固定尺寸
```

禁止：

```text
整个页面 bitmap 拉伸
```

禁止：

```text
width 和 height 独立缩放图片
```

图片必须保持：

```text
aspect ratio
```

例如：

```text
原图片：

200 × 100

等比例放大：

400 × 200

而不是：

400 × 300
```

---

# 八、800×480 UI 布局策略

使用 LVGL：

```text
Flex
Grid
Percentage
Aspect Ratio
Min Size
Max Size
Padding
Margin
Alignment
```

尽可能让 UI 根据父容器自动布局。

不要让所有页面充斥：

```cpp
lv_obj_set_pos(...)
lv_obj_set_size(...)
```

这种绝对坐标。

可以使用绝对坐标完成：

* 特殊绘图页面
* Drawing Canvas
* 特殊装饰
* 特定硬件信息

但普通页面优先使用 Flex/Grid。

---

# 九、UI 安全区域

设计：

```text
Screen
800 × 480
```

建议保留：

```text
10~24 px
```

左右边距。

示例：

```text
┌──────────────────────────────────────────┐
│  16px                                  │
│ ┌──────────────────────────────────────┐ │
│ │                                      │ │
│ │             Application              │ │
│ │                                      │ │
│ └──────────────────────────────────────┘ │
│                                          │
└──────────────────────────────────────────┘
```

不要因为追求“填满屏幕”而让 UI 紧贴边缘。

---

# 十、页面功能范围

最终页面：

```text
Home
├── Reader
├── Photos
├── Notes
├── Weather
├── Clock
├── Calendar
├── Drawing
├── File Manager
└── Settings
```

明确删除：

```text
Music
Audio
Player
Music Service
Audio Service
Music UI
Music Menu
Music scanning
Music playback
```

整个项目不得保留音乐功能。

---

# 十一、总体架构

采用：

```text
Application
      │
      ▼
   Services
      │
      ▼
   Platform
      │
      ▼
   ESP-IDF
```

建议目录：

```text
Ebook-LVGL-4.3B/
│
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   │
│   ├── app/
│   │   ├── app_manager.cpp
│   │   ├── app_manager.h
│   │   │
│   │   ├── home/
│   │   ├── reader/
│   │   ├── photos/
│   │   ├── notes/
│   │   ├── weather/
│   │   ├── clock/
│   │   ├── calendar/
│   │   ├── drawing/
│   │   ├── file_manager/
│   │   └── settings/
│   │
│   ├── ui/
│   │   ├── ui_manager.cpp
│   │   ├── ui_manager.h
│   │   ├── theme.cpp
│   │   ├── theme.h
│   │   └── widgets/
│   │
│   ├── platform/
│   │   ├── display/
│   │   ├── touch/
│   │   ├── storage/
│   │   ├── wifi/
│   │   └── system/
│   │
│   └── services/
│       ├── ebook_service/
│       ├── file_service/
│       └── wifi_service/
│
├── components/
│   └── lvgl/
│
└── assets/
```

如果官方 BSP 或 ESP-IDF 组件结构更合理，可以调整目录，但必须保持：

```text
UI
Application
Services
Platform
```

职责分离。

---

# 十二、职责划分

## Application

负责：

```text
页面
页面生命周期
页面导航
业务交互
```

例如：

```cpp
ReaderPage
PhotosPage
NotesPage
SettingsPage
```

---

## UI

负责：

```text
LVGL Widget
Theme
Font
Layout
公共组件
```

UI 不应该直接：

```text
GPIO
SD
WiFi
HTTP
文件系统
```

---

## Services

负责：

```text
EbookService
FileService
WeatherService
WiFiService
```

例如：

```text
ReaderPage
    ↓
EbookService
    ↓
Storage
```

而不是：

```text
ReaderPage
    ↓
fopen()
```

---

## Platform

负责：

```text
LCD
Touch
SD
WiFi
Backlight
System
```

例如：

```text
Display
Touch
Storage
Backlight
Network
```

---

# 十三、Display 架构

Display Platform 负责：

```text
LCD 初始化
LVGL Display
Frame Buffer
Flush
Backlight
```

最终：

```text
Application
      ↓
LVGL
      ↓
Display Platform
      ↓
ESP-IDF LCD Driver
      ↓
RGB LCD
```

应用层不要直接操作 LCD GPIO。

---

# 十四、屏幕方向要求

本项目**不是做旋转适配项目**。

默认目标：

```text
800 × 480 Landscape
```

不要设计：

```text
0°
90°
180°
270°
```

这种业务级旋转功能。

如果 Waveshare 硬件安装方向需要底层处理，则在 BSP / Display Driver 层一次性解决。

应用层始终认为：

```text
SCREEN_WIDTH  = 800
SCREEN_HEIGHT = 480
```

应用页面不能出现：

```cpp
if (rotation == ...)
```

也不能出现：

```cpp
x = width - x;
y = height - y;
```

这种分散式坐标转换。

---

# 十五、Touch 架构

Touch：

```text
Touch Controller
       ↓
Raw X/Y
       ↓
Touch Driver
       ↓
LVGL Input Device
       ↓
UI
```

Touch 必须与最终：

```text
800 × 480
```

坐标系统一致。

禁止业务页面自己转换坐标。

---

# 十六、Touch 验证

建立专门的 Touch Test 页面：

```text
Touch Test

X: xxx
Y: xxx
Pressed: YES/NO

触摸点：
      ●
```

必须测试：

```text
左上
右上
左下
右下
中心
```

同时测试：

```text
Button Click
Swipe
Long Press
```

验证：

```text
显示位置正确
触摸位置正确
按钮点击正确
```

---

# 十七、Backlight

建立：

```text
BacklightService
```

至少提供：

```cpp
set_brightness(uint8_t value);
get_brightness();
set_enabled(bool enabled);
```

UI 不允许直接操作 GPIO。

Settings 页面通过：

```text
Settings
   ↓
BacklightService
```

修改亮度。

---

# 十八、PSRAM 使用

ESP32-S3：

```text
8MB PSRAM
```

合理利用 PSRAM：

```text
LVGL buffer
图片缓存
Reader buffer
大块临时数据
```

但不能假设：

> 所有 LVGL Buffer 都可以随便放 PSRAM。

必须根据：

```text
DMA
Cache
Memory Capability
RGB LCD Driver
```

实际要求决定内存位置。

避免：

```text
DMA buffer 放在不支持 DMA 的内存
```

---

# 十九、Phase 0：项目初始化

目标：

建立干净的 ESP-IDF 项目。

完成：

```text
ESP-IDF
ESP32-S3
16MB Flash
8MB PSRAM
LVGL
CMake
```

创建基础架构：

```text
app
ui
platform
services
```

启动后打印：

```text
Ebook LVGL
ESP32-S3
Flash: OK
PSRAM: OK
LVGL: OK
```

### 验收

必须：

```bash
idf.py build
```

PASS。

烧录：

```bash
idf.py flash
```

PASS。

串口：

```bash
idf.py monitor
```

PASS。

设备正常启动。

---

# 二十、Phase 1：LCD Bring-up

完成：

```text
LCD 初始化
RGB Interface
800×480
LVGL Display
Flush
Backlight
```

显示测试：

```text
Ebook LVGL

ESP32-S3-Touch-LCD-4.3B

800 × 480

LCD OK
```

同时显示：

```text
矩形
圆形
直线
文字
图片
```

用于确认：

```text
颜色
比例
分辨率
刷新
```

### 验收

必须：

* 800×480 正常显示
* 无明显花屏
* 无明显撕裂
* 无严重闪烁
* Backlight 正常
* LVGL 正常刷新
* 圆形仍然是圆形
* 文字没有拉伸

至少运行：

```text
30 min
```

---

# 二十一、Phase 2：800×480 UI Layout

这是整个项目的关键阶段。

重新分析原 Ebook UI。

不要直接缩放原页面。

针对：

```text
800 × 480
```

重新设计：

```text
Home
Header
Navigation
Cards
Icons
Buttons
Content
Footer
```

例如 Home：

```text
┌──────────────────────────────────────────────┐
│ Ebook                              10:30     │
├──────────────────────────────────────────────┤
│                                              │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐│
│  │ Reader │ │ Photos │ │ Notes  │ │Weather ││
│  │   📖   │ │   🖼   │ │   📝   │ │   ☁    ││
│  └────────┘ └────────┘ └────────┘ └────────┘│
│                                              │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐│
│  │ Clock  │ │Calendar│ │Drawing │ │ Files  ││
│  │   🕐   │ │   📅   │ │   ✎    │ │   📁   ││
│  └────────┘ └────────┘ └────────┘ └────────┘│
│                                              │
├──────────────────────────────────────────────┤
│ Settings                              Status │
└──────────────────────────────────────────────┘
```

这里只是示例。

最终布局根据实际 UI 视觉效果调整。

### 重要

按钮：

```text
不要全部做成极小尺寸
```

同时：

```text
不要为了填满屏幕强行放大
```

应该让：

```text
图标
文字
按钮
卡片
间距
```

形成合理比例。

### 验收

Home 必须：

* 充分利用 800×480
* 无大面积无意义空白
* 无严重拥挤
* 图标不变形
* 图片不变形
* 字体不变形
* 所有按钮可以触摸
* 无 Music

---

# 二十二、Phase 3：Touch

完成：

```text
Touch Driver
LVGL Input
Coordinate Mapping
```

创建 Touch Test。

### 验收

必须完成：

```text
四角
中心
按钮
滑动
长按
```

测试。

---

# 二十三、Phase 4：Home

实现完整 Home。

页面：

```text
Reader
Photos
Notes
Weather
Clock
Calendar
Drawing
File Manager
Settings
```

不要出现：

```text
Music
```

导航：

```text
Home
 ↓
Page
 ↓
Back
 ↓
Home
```

---

# 二十四、Phase 5：Reader

第一阶段只要求：

```text
TXT
```

目录建议：

```text
/sd/Ebook/txt/
```

功能：

```text
Book List
Open
Read
Next Page
Previous Page
Font Size
Progress
Back
```

支持：

```text
English
Chinese
```

需要注意：

LVGL 中文字体不能直接假设系统字体存在。

需要规划：

```text
Font
Font Size
Chinese Glyph
Flash / PSRAM
```

### 验收

测试：

```text
英文 TXT
中文 TXT
长文本
空文件
异常文件
```

---

# 二十五、Phase 6：File Manager

实现：

```text
目录
文件
进入目录
返回
打开
删除
```

至少识别：

```text
TXT
JPG
JPEG
PNG
```

异常情况：

```text
文件不存在
SD 未插入
目录为空
读取失败
```

必须显示友好的错误信息。

---

# 二十六、Phase 7：Photos

实现：

```text
图片列表
缩略图
图片预览
上一张
下一张
返回
```

支持：

```text
JPG
JPEG
PNG
```

图片必须：

```text
保持宽高比
```

如果图片比例和屏幕比例不同：

允许：

```text
Letterbox
Contain
Crop
```

但默认不要：

```text
非等比例 Stretch
```

例如：

```text
原图 1600×900
```

显示到：

```text
800×480
```

不能直接变成：

```text
800×480
```

因为比例不同。

应该采用：

```text
保持 16:9
800 × 450
```

然后剩余：

```text
30 px
```

通过居中留白处理。

---

# 二十七、Phase 8：Notes

实现：

```text
New
Edit
Save
Delete
Open
List
```

使用：

```text
LVGL Textarea
LVGL Keyboard
```

至少验证：

```text
英文输入
数字
基本中文输入方案
```

如果完整中文 IME 过于复杂：

第一阶段允许：

```text
英文 + 数字 + 基础输入
```

但必须预留后续中文输入接口。

---

# 二十八、Phase 9：Drawing

实现：

```text
Canvas
Touch Draw
Clear
Save
Back
```

示例：

```text
┌──────────────────────────────────────────┐
│ Drawing                           Clear  │
├──────────────────────────────────────────┤
│                                          │
│            用户触摸绘图区域              │
│                                          │
│                                          │
├──────────────────────────────────────────┤
│ Save                              Back   │
└──────────────────────────────────────────┘
```

Touch 坐标必须准确。

不能出现：

```text
手指在 A
线画在 B
```

---

# 二十九、Phase 10：Clock / Calendar

Clock：

```text
HH:MM:SS
YYYY-MM-DD
```

支持：

```text
12H
24H
```

Calendar：

```text
Year
Month
Day
Previous
Next
Today
```

必须正确处理：

```text
大小月
闰年
跨年
```

---

# 三十、Phase 11：Weather

架构：

```text
Weather Page
      ↓
Weather Service
      ↓
WiFi Service
      ↓
Network
```

第一阶段：

```text
Mock Data
```

第二阶段：

```text
真实 API
```

不要一开始就把网络请求直接写进 LVGL Event Callback。

网络操作必须：

```text
非阻塞
```

必须处理：

```text
Loading
Success
Timeout
Network Error
API Error
No WiFi
```

UI 不能因为网络请求卡死。

---

# 三十一、Phase 12：Settings

Settings：

```text
Display
Network
Storage
System
About
```

Display：

```text
Brightness
```

不要加入：

```text
Rotation
```

因为本项目 UI 固定：

```text
800×480 Landscape
```

Network：

```text
WiFi Status
SSID
Connect
Disconnect
```

Storage：

```text
SD Status
Capacity
Free Space
```

System：

```text
Restart
```

About：

```text
Project
Version
ESP32-S3
LVGL
```

---

# 三十二、Music 删除要求

这是强制要求。

最终项目中：

```text
Music
Audio
Player
```

全部删除。

需要检查：

```text
源码
头文件
CMake
组件
UI
README
菜单
Service
```

搜索：

```text
music
audio
player
```

但是第三方依赖内部代码可以忽略。

项目自身不得存在：

```text
MusicPage
AudioService
MusicService
MusicPlayer
MusicMenu
MusicScan
MusicPlayback
```

README 中也不得把 Music 作为功能。

---

# 三十三、原 Ebook 项目的处理方式

首先分析：

```text
CHENXiNNNX/Ebook
```

重点分析：

```text
页面
功能
数据结构
交互
文件组织
资源
```

不要机械复制：

```text
Router
Compositor
Rendering Pipeline
e-paper Driver
e-paper Refresh
```

尤其不要迁移：

```text
Full Refresh
Partial Refresh
Fast Refresh
e-paper Ghosting
```

这些属于原 e-paper 平台特性。

当前目标是：

```text
RGB LCD
LVGL
```

---

# 三十四、原项目 UI 不允许直接放大

例如原项目：

```text
264 × 176
```

不能简单：

```text
264 → 800
176 → 480
```

因为：

```text
800 / 264 ≠ 480 / 176
```

会产生不同的 X/Y 缩放比例。

正确方式：

```text
分析原 UI
       ↓
提取功能
       ↓
提取视觉层级
       ↓
重新布局
       ↓
适配 800×480
```

---

# 三十五、统一 Theme

建立：

```text
theme.cpp
theme.h
```

统一管理：

```text
Font
Color
Button
Card
Header
Footer
Spacing
Radius
Border
```

例如：

```cpp
Theme::init();
```

页面不要各自定义一套完全不同的视觉风格。

---

# 三十六、公共 Widget

建立：

```text
AppButton
AppCard
AppHeader
AppFooter
IconButton
ListItem
PageContainer
```

减少：

```text
重复 LVGL 创建代码
```

例如：

```text
Home
Reader
Settings
File Manager
```

应该复用：

```text
AppHeader
AppButton
ListItem
```

---

# 三十七、页面生命周期

建议：

```cpp
class Page
{
public:
    virtual void create() = 0;
    virtual void destroy() = 0;
    virtual void on_enter() {}
    virtual void on_leave() {}
};
```

或者使用等价的 C/C++ 页面管理方式。

AppManager：

```text
Home
Reader
Photos
Notes
Weather
Clock
Calendar
Drawing
FileManager
Settings
```

统一管理。

---

# 三十八、内存管理

重点关注：

```text
Heap
PSRAM
LVGL Object
Image Buffer
Font
Task Stack
```

页面切换：

```text
Home
 ↓
Reader
 ↓
Home
```

必须确认：

```text
Reader 页面销毁后
LVGL Object 被正确释放
```

不能持续增长。

---

# 三十九、稳定性测试

至少：

```text
30 min
1 hour
2 hours
```

持续运行。

观察：

```text
Free Heap
Free PSRAM
LVGL Memory
Task Stack
CPU
Watchdog
```

测试导航：

```text
Home
 ↓
Reader
 ↓
Home
 ↓
Photos
 ↓
Home
 ↓
Notes
 ↓
Home
 ↓
Drawing
 ↓
Home
 ↓
Settings
 ↓
Home
```

循环执行。

验收：

```text
No Crash
No Guru Meditation
No Watchdog Reset
No Obvious Memory Leak
No UI Freeze
```

---

# 四十、UI 回归测试

每次修改 UI 后必须检查：

```text
LCD
Touch
Home
Navigation
Back
```

重点防止：

```text
显示正常 / 触摸错位

触摸正常 / 显示布局错误
```

以及：

```text
图片变形
文字变形
按钮超出屏幕
页面出现大面积空白
```

---

# 四十一、Build 验证

每个 Phase 完成后：

```bash
idf.py build
```

必须成功。

硬件测试：

```bash
idf.py flash
idf.py monitor
```

必须记录结果。

---

# 四十二、Git 提交策略

每个 Phase 单独提交：

```text
phase0: initialize project

phase1: add lcd driver

phase2: implement 800x480 ui layout

phase3: add touch

phase4: add home ui

phase5: add reader

phase6: add file manager

phase7: add photos

phase8: add notes

phase9: add drawing

phase10: add clock calendar

phase11: add weather

phase12: add settings
```

不要一次提交整个项目。

---

# 四十三、README

README 必须包含：

```text
Project
Hardware
Software
Build
Flash
Monitor
Pin Configuration
LCD Configuration
Touch Configuration
SD Card
PSRAM
LVGL
UI Layout
Architecture
Applications
Phase Progress
Known Issues
```

特别说明：

```text
Screen:
800×480 Landscape

UI:
LVGL

Display adaptation:
Responsive Layout + Aspect Ratio

Music:
Removed
```

---

# 四十四、开发流程

必须严格按照：

```text
Phase 0
   ↓
Build
   ↓
Hardware Test
   ↓
Phase 1
   ↓
Build
   ↓
Hardware Test
   ↓
Phase 2
   ↓
...
```

禁止：

> 一次生成所有功能，然后最后一起调试。

每个阶段完成后必须验证。

---

# 四十五、开发代理工作规则

你作为代码开发 Agent，必须遵循：

## 规则 1

先分析现有代码。

不要立即创建大量新文件。

---

## 规则 2

先确认官方硬件资料。

如果 GPIO、LCD、Touch、SD 配置不确定：

```text
查询 Waveshare 官方文档
```

禁止猜测。

---

## 规则 3

优先使用官方 BSP。

如果 Waveshare 提供：

```text
LCD Driver
Touch Driver
BSP
```

优先评估复用。

---

## 规则 4

不要为了兼容原项目而保留错误架构。

如果原项目架构：

```text
e-paper
Router
Compositor
Rendering Pipeline
```

不适合 LVGL：

重新实现。

---

## 规则 5

UI 不直接访问硬件。

错误：

```cpp
HomePage
    ↓
gpio_set_level()
```

正确：

```text
HomePage
    ↓
Service
    ↓
Platform
    ↓
Hardware
```

---

## 规则 6

图片必须保持比例。

禁止：

```cpp
set_width()
set_height()
```

对图片进行任意非等比例缩放。

需要根据：

```text
source width
source height
target area
```

计算等比例缩放后的尺寸。

---

## 规则 7

页面必须针对 800×480 重新布局。

不要：

```text
原项目 UI screenshot
       ↓
直接 resize
       ↓
显示
```

---

# 四十六、Phase 完成报告

每个 Phase 完成后输出：

```text
Phase:
Status:

Implemented:
- xxx
- xxx
- xxx

Files:
- xxx
- xxx

Build:
PASS / FAIL

Hardware Test:
PASS / FAIL / NOT TESTED

UI Test:
PASS / FAIL / NOT TESTED

Memory Test:
PASS / FAIL / NOT TESTED

Known Issues:
- xxx

Next Phase:
xxx
```

---

# 四十七、最终验收标准

最终设备启动后：

```text
┌──────────────────────────────────────────────┐
│                  Ebook                       │
│                                              │
│   Reader     Photos      Notes      Weather  │
│                                              │
│   Clock      Calendar    Drawing    Files    │
│                                              │
│                  Settings                    │
│                                              │
└──────────────────────────────────────────────┘
```

最终要求：

### Display

```text
800×480
Landscape
LCD 正常
无严重撕裂
无花屏
```

### UI

```text
充分利用屏幕
布局自然
无明显变形
无大面积无意义空白
```

### 图片

```text
保持宽高比
```

### 字体

```text
正常
不拉伸
```

### Touch

```text
坐标准确
按钮准确
滑动正常
```

### Application

```text
Reader       PASS
Photos       PASS
Notes        PASS
Weather      PASS
Clock        PASS
Calendar     PASS
Drawing      PASS
File Manager PASS
Settings     PASS
```

### Music

```text
完全不存在
```

### Stability

```text
2 小时运行
无 Crash
无 Watchdog
无明显内存泄漏
```

---

# 四十八、最终执行顺序

严格按照以下顺序执行：

```text
1. 分析 CHENXiNNNX/Ebook
        ↓
2. 分析 Waveshare 官方硬件/BSP
        ↓
3. 创建 ESP-IDF 项目
        ↓
4. ESP32-S3 + PSRAM
        ↓
5. LCD 800×480
        ↓
6. LVGL
        ↓
7. Touch
        ↓
8. 800×480 UI Layout
        ↓
9. Home
        ↓
10. Reader
        ↓
11. File Manager
        ↓
12. Photos
        ↓
13. Notes
        ↓
14. Drawing
        ↓
15. Clock / Calendar
        ↓
16. Weather
        ↓
17. Settings
        ↓
18. 删除 Music
        ↓
19. 稳定性测试
        ↓
20. 最终验收
```

---

# 四十九、最重要的最终原则

整个项目始终遵循以下原则：

```text
不是：
原项目 → 强行缩放 → 800×480

而是：

原项目
   ↓
功能/交互/UI思想
   ↓
重新设计
   ↓
LVGL
   ↓
800×480
```

最终目标：

> **让原 Ebook 项目的功能和 UI 思想自然地“长”在 800×480 屏幕上，而不是把原来的小屏 UI 拉伸到 800×480。**

同时：

```text
固定 800×480
固定 Landscape
不做业务层旋转
不做整体非等比例缩放
图片保持宽高比
UI 使用 Flex/Grid 自适应
充分利用屏幕
Music 完全移除
ESP-IDF + LVGL
```

优先保证：

```text
硬件正确
    >
显示正确
    >
触摸正确
    >
UI 布局正确
    >
功能完整
    >
性能优化
```

不要在底层硬件和显示还没有验证的情况下继续开发大量业务页面。
