# 四项需求 · 实现计划

> 状态：**F1 / F2 已完成并真机验收**，F3 已完成并真机验收，F4 未开始。
> 计划本身保留原样作为记录；与实际实现不符的地方见 §8「实施结果与计划偏离」。

---

## 0. 侦察结论（已确证，每条都有出处）

| # | 事实 | 证据 |
|---|---|---|
| 1 | SD 上是**经典 HZK 风格 GBK 点阵字库**，4 档 | `GBK12=574560`、`GBK16=766080`、`GBK24=1723680`、`GBK32=3064320`，四者 ÷23940 分别为 24/32/72/128 整数 |
| 2 | 点阵规格 | 12×12（24 B）、16×16（32 B）、24×24（72 B）、32×32（128 B），每行 2/2/3/4 字节 |
| 3 | **HZK 索引 ≠ 现有 `gbk_table` 索引** | HZK：190 码位/区（126×190=23940）；`gbk_table.h:20`：191 码位/区（126×191=24066） |
| 4 | Reader 默认就是 CJK 档字体 | `reader_page.cpp:94` `kDefaultFontIndex = 4` |
| 5 | 所以**乱码 = flash 字库缺字**，不是选错字体 | 同上；`lv_font_source_han_sans_sc_16_cjk` 是子集字库 |
| 6 | **Settings 的 Network 卡片 UI 已就绪** | `settings_page.cpp:113-136`：WiFi/SSID/IP 三行 + Connect/Disconnect/Scan 已连 `net_action_cb` |
| 7 | `net_service.cpp` 是**纯桩** | `net_service.cpp:70-75` 直接返回 `ESP_ERR_NOT_SUPPORTED` |
| 8 | LVGL **9.3.0**，字体回调签名已核对 | `lv_font.h:95-118`；`lv_font_glyph_dsc_t` 在 `lv_font.h:55-78` |
| 9 | App 分区 **4 MB**，当前镜像 1.24 MB | `partitions.csv`（注释本身就预留了 "embedded fallback font"） |
| 10 | LVGL 用 clib `malloc`，缓存**关闭** | `CONFIG_LV_USE_CLIB_MALLOC=y`；`CONFIG_LV_CACHE_DEF_SIZE=0` |
| 11 | **Photos 崩溃根因未确证** | 上一轮探针自身栈溢出，抓到的是假现场。需要新取证（见 §3） |

---

## 1. F1 · 移除右下 Back 按钮

**12 处**，全部是 `ui::app_button(..., "Back", back_cb, nullptr)`：

```
calendar_page.cpp:143      clock_page.cpp:108
drawing_page.cpp:174,209   file_manager_page.cpp:115
notes_page.cpp:195         photos_page.cpp:93
reader_page.cpp:184,388    settings_page.cpp:64
touch_test_page.cpp:167    weather_page.cpp:83
```

**保留**（不是 Back，语义不同）：
- `file_manager_page.cpp:294` `"Close"` —— 预览 overlay 关自己
- `notes_page.cpp:365` `"Close"` —— 同上
- `home_page.cpp:148,150` `"Display"/"Touch"` —— Home 是应用列表，不是退出按钮

**同时清理**：删掉各页因此变成死代码的 `static void back_cb()`，否则 `-Wunused-function` 会打破零警告。

**风险**：极低。右上角 X 由 `ui::page_layout(..., with_close=true)` 统一提供，功能不变。

---

## 2. F2a · Reader 部分文字乱码

**不单独改 Reader。** 根因是 flash 字库缺字（§0 第 5 条），F3 落地后 Reader 自然覆盖全 GBK。

**必须保留的验收**：乱码字必须都在 GBK 范围内。若真机发现仍有缺字，说明还有第二个原因（转码丢字），届时单独查 `text_to_utf8()`。

---

## 3. F2b · Photos 打不开

**根因未确证，先取证再改。** 当前只有间接线索：
- `CONFIG_LV_USE_ASSERT_MALLOC=y` → 任何 LVGL 分配失败都是 **abort**，不是优雅返回
- `lv_draw_sw_img.c:36` `MAX_BUF_SIZE = 4×800×2 = 6400` 字节；`:499` 的 `transformed_buf` 最多 ~6.4 KB
- 6.4 KB 都分配不出来，说明**堆已经很紧或已损坏**，而不是"图太大"这么简单
- LVGL 的 TJPGD 是**按 MCU 流式解码**（`lv_tjpgd.c:237` `decoded->data = jd->workbuf`），不是整图缓冲

**取证方式（安全探针，不再踩上次的坑）**：
1. 所有缓冲放**静态区**，一个字节都不放栈上（上次就是 `DirEntry f[32]` = 4.3 KB 打穿 8 KB 栈）
2. 只调 `lv_image_decoder_get_info()` **读文件头拿尺寸**，绝不解码
3. 外加手工解析 JPEG SOF0，两条路径交叉验证
4. 打印解码前/后的 `heap_caps_get_free_size()`，看清堆的真实余量

**修复方向（取证后二选一或组合）**：
- **预检 + 拒绝**：解码前算 `w×h×2`，超出可用 PSRAM 安全线就显示"图片过大"而不是崩。这是**唯一保证不崩**的做法
- 若确认是堆紧张：开启 `CONFIG_LV_CACHE_DEF_SIZE`、检查 TTF/图片缓存策略

---

## 4. F3 · SD 卡 GBK 字库优先（本轮技术核心）

### 4.1 分层落位

```
platform/storage/hzk_font.c    SD 读写 + HZK 点阵定位（唯一碰文件系统的地方）
      ↓
ui/fonts/sd_font.c             实现 lv_font_t 的 get_glyph_dsc / get_glyph_bitmap
      ↓
ui/theme.cpp                   Theme::font_cjk() 改为：SD 可用→SD 字体，否则→flash 字库
```

### 4.2 关键设计

**① Unicode → GBK 反查表**（新增 `tools/gen_hzk_index.py` → `ui/assets/hzk_index.c`）

`gbk_table` 只有 GBK→Unicode 单向。字形回调拿到的是 **Unicode**，必须反查。

用 **两级页表**：Unicode 高 8 位做页号（256 页），每页 256 项 `uint16`（存 GBK 码位，0 = 无映射）。
- 非空页约 98 个 → **约 49 KB flash**（4 MB 分区里零压力）
- 查找 O(1)，无二分、无排序

**② 字库整块进 PSRAM**

`GBK16.FON` 766 KB 一次性读入 PSRAM，之后每个字形都是内存访问。
- **不这么做**：`CONFIG_LV_CACHE_DEF_SIZE=0`（缓存关闭），每个字形每次渲染都要读 SD → 翻页会卡到不可用
- PSRAM 预算：双 FB 1.5 MB + 字库 0.77 MB ≈ 2.3 MB / 8 MB，充裕

**③ 索引换算（两套公式必须分清）**

```
HZK 索引 : idx = (b1-0x81)*190 + (b2-0x40) - (b2 >= 0x7F ? 1 : 0)     // 0..23939
gbk_table: tidx = (b1-0x81)*191 + (b2-0x40)                            // 0..24065
```

**④ 字形回调**

- `get_glyph_dsc`：`box_w/box_h` = 点阵边长，`adv_w` = 边长（全角），`format = LV_FONT_GLYPH_FORMAT_A8`，`gid.index` = Unicode（供 LVGL 缓存键）
- `get_glyph_bitmap`：从 PSRAM 读 1bpp 点阵，**逐位展开成 A8（0x00/0xFF）写入 `draw_buf->data`**，返回 `draw_buf->data`

### 4.3 需要你定的：用哪一档？

| 档位 | 文件 | PSRAM | 本机 UI 契合度 |
|---|---|---|---|
| 16 px | 766 KB | 最低 | **与现有 16px CJK 完全一致**，纯替换、零布局回归 |
| 24 px | 1.7 MB | 中 | 更清晰，但行高变化会改动 Reader 每页行数 |
| 32 px | 3.0 MB | 高 | 适合标题，正文太大 |

---

## 5. F4 · WiFi 配置与连接

### 5.1 现状：UI 已就绪，只缺 service

`settings_page.cpp` 的 Network 卡片三行数据 + 三个按钮**已经接好** `net_action_cb`，`net_service.h` 的接口（`net_scan_start` / `net_wifi_state` / `net_ssid` / `net_ip` / `net_rssi_bars`）也已定型。
**所以这一项主要是把 `net_service.cpp` 的桩填成真实现，页面改动很小。**

### 5.2 实现

```
net_service.cpp    esp_netif + esp_wifi + esp_event，STA 模式
                   ├ esp_event_handler: WIFI_EVENT / IP_EVENT 更新状态
                   ├ net_scan_start()  → esp_wifi_scan_start(blocking=false)
                   └ 凭据存 NVS（nvs_flash 已初始化，main.cpp:80）
settings_page.cpp  新增"WiFi 配置"入口 → SSID 列表 + 密码输入（lv_keyboard）
CMakeLists.txt     REQUIRES 增加 esp_wifi / esp_netif / esp_event / lwip
sdkconfig.defaults 增加 WiFi 相关项（改完必须 del sdkconfig 再构建）
```

### 5.3 ⚠️ 必须提前说明的风险

`net_service.h` 自己写着：

> enabling the WiFi stack costs internal SRAM that the RGB panel's DMA descriptors compete for

之前那条"面板整体位移"的坑，就是 RGB DMA 带宽/缓冲失步引起的。**WiFi 栈吃内部 SRAM，有可能让位移复发。**

**缓解措施**：
- `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` 已是 `y`，WiFi 缓冲优先走 PSRAM
- 分两步验证：**先只起栈不连接** → 看显示；**再连接** → 再看显示。任何一步出现位移就回退并单变量分析

---

## 6. 执行节奏（两轮构建/烧录）

| 轮次 | 内容 | 验证点 |
|---|---|---|
| **轮 1** | F1（删按钮）+ F3（SD 字库）+ 安全探针（只读取证） | Back 消失；Reader 中文完整无乱码；取回 JPEG 尺寸与堆数据 |
| **轮 2** | F2b（Photos，基于轮 1 证据）+ F4（WiFi） | Photos 不崩且能看图；WiFi 能扫能连；**显示无位移** |

每轮：构建（零警告）→ 烧录 COM14 → 抓串口 → 逐条对账。
探针在第 1 轮验证完后**立即移出交付固件**（上次的教训：诊断代码自己变成了故障）。

---

## 7. 需要你确认的三件事

1. **SD 字库用哪一档？**（建议 16 —— 与现有 UI 完全一致，零布局回归）
2. **Photos 策略**：确认走"预检 + 拒绝过大图片"这条**永不崩溃**的路？（需要先取证再定阈值）
3. **WiFi 风险**：接受"WiFi 栈可能影响 RGB 面板显示"这个已知风险，按 §5.3 的两步验证推进？

---

## 8. 实施结果与计划偏离

计划里被证据推翻的部分，就地记录，避免下次照着旧结论动手。

### 8.1 F2b Photos：不是堆问题，是 **LVGL 任务栈溢出**

| 计划推测 | 实测结论 |
|---|---|
| `lv_draw_sw_img.c` 的 6.4 KB 缓冲分配不出 → 堆已紧张或损坏 | **错误**。死机时 PSRAM 还有 5.6 MB 空闲，内部堆 247 KB —— 堆从来不是瓶颈 |
| 需要"预检 + 拒绝过大图片"保证不崩 | **不适用**。没有一条路径是因为图片太大而失败的 |

真实根因：`***ERROR*** A stack overflow in task main has been detected.` 落在 taskLVGL 上。
`addr2line` 把回溯解到 `vApplicationStackOverflowHook`（检测器自己，**不是**肇事者），
`objdump` 才量出真凶：

```
lv_tjpgd.c.obj:decoder_info   entry a1, 0x10b0   ← 4272 字节一帧
lv_tjpgd.c:110  uint8_t workb[TJPGD_WORKBUFF_SIZE];   /* 4096 */
                JDEC jd;                              /*  176 */
```

`decoder_info` 在**渲染**路径上（`lv_image_decoder_open` 读文件头就会进），叠在约 5.3 KB
的控件树递归之上，实测峰值 **10156 B**，而栈只有 8192 B。

**修复**：`lvgl_port.cpp` 的 `task_stack` 8192 → **16384**。
验收：Photos 渲染 `001.jpg` 后剩 6228 B（峰值 10156）；空闲启动峰值仍是 5340，
即多出的 8 KB 是纯余量。上游那个不一致（`decoder_info` 用栈、`decoder_open` 改用堆）
已写进代码注释，防止后人把栈改回去。

### 8.2 F3 字库：反查表落在 **PSRAM 平表**，不是 flash 两级页表

| 计划的 §4.2① | 实际 |
|---|---|
| 两级页表，98 个非空页，约 49 KB **flash** | 一张 **64 KiB 的 `uint16` 平表**，一份，放 **PSRAM** |
| `tools/gen_hzk_index.py` 生成 `ui/assets/hzk_index.c` | 不生成文件，安装时用已有的 `gbk_table` 反向建一次 |

理由：`lv_font_t` 的回调是热路径，平表把查找压成**一次访存**，不需要页号运算；
而生成文件要多维护一份「必须与 `gbk_table` 保持一致」的真值。现在只有一份真值
（`gbk_table`），反查表是它的**运行期导出**。代价是启动多 533 ms、多 128 KB PSRAM
（实测安装后 psram 4776136 B / int 247067 B，内部堆一个字节没动）。

索引换算的 ⚠️ **两套公式**（计划 §4.2③ 那条）完全成立，是实现里最容易错的一处：
`hzk_index` 用 190 码位/区，`gbk_table` 用 191，差的那 1 就是 0x7F 不是尾字节。

### 8.3 档位选了 16 px，但「零布局回归」要打个折

计划 §4.3 说 16 px 是「纯替换、零布局回归」。**方向对，但不是零**。
`tools/verify_sd_font.py` 用两个独立来源（固件链接的 `gbk_table.c` + 主机 cp936）
把 1277 个 CJK 字形逐个量过：

- 1275 个前进宽度 = 256/16 = **16 px** → 断行、分页确实不动
- 例外 2 个，都是那个 demo 子集自己的栅格化瑕疵：
  `、` U+3001 是 **8.81 px**（偏窄）、`（` U+FF08 是 **16.125 px**
- 本字体给每个字统一 16 px，所以 `、` 会**加宽 7.2 px**，含它的行可能提前折行
  —— 这是纠正（表意逗号本来就该全角），但是可见差异，已写进代码注释并由脚本钉住

### 8.4 覆盖率（拿本项目自己的契约文档当正文样本，530 个不同汉字）

| 字体 | 覆盖 | 缺 |
|---|---|---|
| flash 内嵌子集 | 283 / 530 = **53.4%** | 247 |
| SD 卡 GBK16.FON | 524 / 530 = **98.9%** | 6（全是 emoji，两套字体都没有） |

「改用卡上字库」因此多让 **20514 个码点**从豆腐块变成真字。
复合字体（卡字库 + 回退到内嵌）确实是内嵌的**超集** —— 前提是 `fallback` 链挂着，
它同时负责 ASCII、LVGL 的 `LV_SYMBOL_*`（U+F00D/U+F013… 全在卡字库够不到的
私用区）以及 cp936 未定义的槽位。**这条链不是可选项。**
