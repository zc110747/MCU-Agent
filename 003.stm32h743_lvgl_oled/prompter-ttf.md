# STM32H743 + LVGL + SD/FatFs + TTF/CTF 高性能字体系统 —— 完整设计提示词

> 本提示词是该字体系统的**权威设计契约**。它把"目标约束 + 最终架构 + 关键决策与理由 + 实施阶段
> + 验证验收 + 已知坑"一次性讲清，既可用于从零重建，也可用于向 Zephyr/其它 STM32 LVGL 工程移植。
> 凡带 ✅ 的阶段为**已在 `003.stm32h743_lvgl_oled` 实机验证通过**的方案，新工程直接沿用，不要重蹈覆辙。

---

## 〇、角色与背景

你是对当前 STM32H743 项目的 LVGL 字体系统进行设计 / 重构 / 移植的嵌入式工程师。

硬件与约束：

* MCU：STM32H743ZIT6（Cortex-M7，480 MHz，无外部 SDRAM，AXI-SRAM 512 KB @ `0x24000000`，RAM_D2 288 KB @ `0x30000000`）
* 显示：240×240 SPI OLED（ST7789，SPI6）
* 存储：板载 microSD（SDIO）+ FatFs，盘符 `1:`，字库存 `1:/SYSTEM/FONT/`
* 调试串口：USART1 PA9/PA10（115200 8N1，ST-Link 虚拟串口，端口号运行时枚举）
* 工具链：arm-none-eabi-gcc（≥13）+ cmake + ninja + openocd + ST-Link；VSCode Cortex-Debug
* 字体：TTF 可能达数 MB（如 HarmonyOS Sans SC 8 MB），需支持中文 + 拉丁 + 缺字回退
* **禁止修改 `third_party/`（lvgl / FatFs）与 `Drivers/`**（共享库）。改动只在 `Application/`、`Bsp/` 自有代码与 `CMakeLists.txt`

---

## 一、不可违反的硬约束（front-loaded —— 任何方案都必须先满足这 8 条）

1. **不要把整个 TTF 加载到 RAM。不要把整个 CTF 加载到 RAM。** 运行时只有：CTF 头/页/entry 缓冲、TTF 块缓存、字形缓存、栅格化临时 arena。内部 RAM 仅 ~1 MB。
2. **CTF 是磁盘索引**：多级直接寻址快速定位 `Unicode → CTF_Entry`；索引**只包含 TTF 实际存在的字符**。
3. **缺字零 IO**：若 Unicode 不在 TTF（`CTF_NOT_FOUND`），立即返回，**后续完全不访问 TTF、零 SD 读**。这是硬性验收指标。
4. **只有确认 Unicode 存在后才访问 TTF**；TTF 的随机访问必须经 **Block Cache** 优化；**严禁 `stb_truetype` 直接对 FatFs 做大量随机 `f_lseek()+f_read()`**（这是原卡死根因）。
5. **缺字不显示、不报错、不刷屏**：`CTF_NOT_FOUND` 是正常业务状态，LVGL 走 fallback；英文/数字必须始终可用（内置 Montserrat 内部数据）。
6. **Latin 预取误假设（高频坑）**：CTF 索引通常**包含拉丁范围**，LVGL 用 TTF 渲染拉丁字形，Montserrat 只兜底索引确实没有的码点。`preload_enqueue()` **不得** `if (cp < 0x80) return;`。
7. **首帧/首绘慢 ≠ 缓存 miss**：LVGL 一个 screen 首次实际绘制有一次性 CPU 开销（样式/排版/draw-task 构建），可达 ~200 ms，纯 CPU、零 IO。解法=抑制 flush 的离屏预热，不是加缓存。
8. **Debug/Release 双构零警告**（`-Wall -Wextra`）；源码 UTF-8 字面量，**勿改 `-fexec-charset`**。

---

## 二、最终架构（as-built，自上而下）

```
LVGL label
  │  (lv_font_t: get_glyph_dsc / get_glyph_bitmap)
  ▼
CTF 后端 (lvgl_font.c)            ← 仅此层接 LVGL，不碰 LVGL core
  ├─ 缺字 → return false → LVGL 走 Montserrat 回退（编译进 Flash 的内部数据）
  ├─ 命中 → glyph_cache_lookup() ──hit──► 返回位图
  │                                  └─miss─► 见下
  ▼
CTF Reader (ctf_reader.c)         ← 三级直接寻址 O(1)，NOT_FOUND 零 IO
  ├─ L1 平面表（常驻 RAM 2 KB）
  ├─ Page 表（常驻 RAM ~10 KB，all-or-nothing）
  ├─ TTF Table 目录（常驻 RAM ~132 B）
  └─ Entry（24 B：glyf 偏移/长度/glyph_id/度量/flags）
        │  FOUND
        ▼
TTF Reader (ttf_reader.c)         ← 全工程唯一 SD 随机读收口：ttf_fill()
  └─ 16 KB × 4 块缓存 (blkcache.c, LRU)
        │
        ▼
stb_adapter.c                     ← STBTT_STREAM 宏接 ttf_reader，stb 源码零修改
  └─ stb_truetype（v1.26htcw fork）栅格化 → 8-bpp 位图
        │
        ▼
glyph_cache.c (200 KB, .ram_d2)   ← 已栅格化字形池：LRU + epoch 钉扎 + 异步预取
```

**关键语义链**：

```
Unicode ─► CTF ─► NOT_FOUND ─► END            (零 SD，必须验证)
Unicode ─► CTF ─► FOUND ─► glyph_id ─► TTF Reader ─► 块缓存 ─► stb ─► 位图
```

`EMPTY`（空格，合法 advance、位图 0）与 `NOT_FOUND` 严格区分。

---

## 三、关键设计决策与理由（✅ = 已实机验证）

### 3.1 CTF 是索引，不是字体数据 ✅
CTF 只存 `Unicode→glyph_id / glyf 偏移 / 长度 / 度量 / flags`，不含 bitmap/轮廓副本。
三级直接寻址（plane 8B → page 40B(256-bit 位图) → entry 24B），查找 O(1) 无遍历无二分。
**位图为 0 = 字体没这个字 → 立刻 NOT_FOUND**。

### 3.2 resident RAM index（常驻索引）✅
- L1 平面表（2 KB，已在 `ctf_open()` 搬入 `s_l1_shadow`）+ Page 表（256×40B≈10 KB）+ TTF 目录(~132B) 常驻。
- **仅当整张页表能放进页池才全常驻**（all-or-nothing），否则退化走块缓存、绝不部分常驻（避免每个 lookup 加分支）。
- 加载时一次性校验所有非空平面 `page_offset` 落在池内，使第二跳减法永不溢出。
- 效果：**缺字判定（bit 测试）完全在 RAM，lookup 第二跳零 `f_read`**；`NOT_FOUND costs 0 SD reads` 可启动打印。

### 3.3 TTF 块缓存 16KB×4 LRU ✅
- stb 经 `STBTT_STREAM` 宏接到 `ttf_reader`，**stb 源码零修改**。
- seek 变一次 store（零 IO）；read 走 16 KB 块缓存（命中即内存拷贝）。`f_lseek()+f_read()` 全工程**仅一处** `ttf_fill()`。
- 能力：跨块合并、短块有效长度、LRU 替换、统计（hits/misses/fills/fill_bytes）。
- 实测：整轮 65536 码位遍历仅 20 次 `f_read`，而原 stb 流式是几十万次。

### 3.4 200 KB 字形缓存 `glyph_cache.c`（.ram_d2）✅
- 单一 200 KB 池放 `.ram_d2`（CPU 只读、SPI6 DMA 不碰 → 无 cache 一致性问题）。
- 变长字形用 free-list + 偏移排序 + 前后合并精确回收。
- **LRU + epoch 钉扎**：当前页/正在用字形持续抬升，永不被淘汰；切页 `bump_epoch()` 让旧页落入淘汰域，压力下自动回收。
- 异步预取：`mk_label → preload_label` 扫描 UTF-8 去重入队，`preload_timer_cb` 每 30 ms 处理 4 字后台栅格化，队列清空自删 timer。GBK 引擎（`px_of==0`）no-op。
- **为何放 RAM_D2**：避免 AXI-SRAM(512KB 主力) 使用率逼近 90%；挪到 RAM_D2 后 RAM_D1 维持 ~61.7%、RAM_D2 占 69.44%，两者皆安全。

### 3.5 内置 Montserrat 12/16/24/32 回退 ✅
编译进 Flash 的"内部数据"，SD 未挂载 / CTF 缺失 / 版本不符时 ASCII 与数字照常显示，UI 不空白。
编译期断言 4 档全开，否则 `#error`。

### 3.6 首帧预热 `ui_warmup_pages()` ✅（消除首帧 ~200 ms 卡顿的真正修复）
```c
static void dummy_flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *c) {
    lv_disp_flush_ready(drv);          /* 吞掉帧缓冲推送 */
}
static void ui_warmup_pages(void) {
    lv_disp_drv_t *drv = lv_disp_get_default()->driver;
    void (*orig)(...) = drv->flush_cb;
    drv->flush_cb = dummy_flush_cb;    /* 抑制显示刷新，首绘不可见 */
    for (i = 0; i < PAGE_COUNT; i++) {
        lv_scr_load(s_pages[i]);
        (void)lv_timer_handler();       /* 触发首绘，成本在此付清 */
    }
    drv->flush_cb = orig;
}
```
调用时机：页面构建完、显示启动/加载页**之前**。预热成本（~880 ms）从首帧前移到加载页背后（用户不可见），首个真实翻页即 warm。

### 3.7 kerning 是卡死元凶，后端不调用 ✅
`stbtt_GetGlyphKernAdvance()` 走 GPOS 全表字节级扫描（字体 ~32 KB）→ 几万次 SD 随机访问 → "卡死"。
LVGL 8 label 绘制无字距语义，**后端不调用 kerning**（能力保留在 adapter 仅供 benchmark）。

---

## 四、CTF 磁盘格式（契约，PC 端 `ttf2ctf` 与 MCU 端 reader 共享真源）

> 磁盘格式不能依赖 MCU 编译器默认 struct layout（little endian / 字段长度 / 对齐需显式）。

```c
#define CTF_MAGIC 0x31465443UL   /* "CTF1" */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t ttf_size;        /* CTF 绑定 TTF：启动时比 size，可选 CRC32 */
    uint32_t ttf_crc32;
    uint32_t unicode_mode;
    uint32_t table_index_offset;  uint32_t table_index_count;
    uint32_t l1_index_offset;     uint32_t l1_index_count;
    uint32_t page_index_offset;   uint32_t page_index_count;
    uint32_t entry_offset;        uint32_t entry_count;  uint32_t entry_size;
    uint32_t units_per_em;
    uint32_t reserved[4];
} CTF_Header;                  /* 所有 offset 是文件偏移，非 RAM 地址 */

typedef struct { uint32_t tag; uint32_t offset; uint32_t length; } CTF_Table;

typedef struct { uint32_t page_offset; uint16_t page_count; uint16_t reserved; } CTF_L1_Index;  /* 最多 256 项 */

typedef struct { uint32_t entry_offset; uint16_t entry_count; uint16_t flags; } CTF_Page;

typedef struct {               /* 16~20 bytes/glyph，CTF 不存 Unicode（由层级隐含）*/
    uint32_t glyf_offset; uint32_t glyf_length;
    uint16_t glyph_id;
    int16_t  advance_width; int16_t bearing_x;
    uint16_t flags;
} CTF_Entry;

#define CTF_GLYPH_EMPTY    (1U<<0)
#define CTF_GLYPH_SIMPLE   (1U<<1)
#define CTF_GLYPH_COMPOSITE(1U<<2)
#define CTF_GLYPH_VALID    (1U<<3)
/* EMPTY 与 NOT_FOUND 是两个完全不同的状态 */

typedef enum { CTF_OK=0, CTF_NOT_FOUND, CTF_ERR_IO, CTF_ERR_FORMAT, CTF_ERR_RANGE } CTF_Result;
```

API（先不接 LVGL，单独测）：

```c
CTF_Result ctf_open(CTF_Handle*, const char* path);
CTF_Result ctf_find_unicode(CTF_Handle*, uint32_t unicode, CTF_Entry* entry);  /* NOT_FOUND 是正常业务状态，不打印 error */
CTF_Result ctf_find_gbk(CTF_Handle*, uint16_t gbk, CTF_Entry* entry);          /* GBK→Unicode→CTF */
void       ctf_close(CTF_Handle*);
```

查找路径（三级直接寻址，无遍历无二分）：

```c
plane = (u >> 16) & 0xFF;   /* L1：page_offset / page_count */
page  = (u >>  8) & 0xFF;   /* Page：entry_offset + 256-bit 位图 */
bit   =  u        & 0xFF;   /* 位图位 → popcount rank → entry 下标 */
/* 位为 0 → 立即 CTF_NOT_FOUND，一次 SD 都没有碰 */
```

页数组**必须稠密**（含空洞页也占位），否则 `page_no` 无法直接索引。

---

## 五、模块与文件布局（按当前工程实际结构调整，勿机械复制）

```
Bsp/font/
├── ctf_format.h      CTF v1 格式定义（与 PC 端唯一真源对齐）
├── ctf_reader.c/.h   三级直接寻址 + NOT_FOUND 语义 + resident RAM index
├── blkcache.c/.h     通用 LRU 块缓存（跨块合并 / 短块有效长度 / 统计）
├── ttf_reader.c/.h   TTF 随机访问（f_lseek+f_read 经块缓存），唯一 SD 读收口 ttf_fill()
├── stb_adapter.c/.h  stb_truetype 零修改接入 ttf_reader（STBTT_STREAM 宏）+ bump arena
├── glyph_cache.c/.h  200 KB 栅格化字形池（LRU + epoch 钉扎 + 异步预取），放 .ram_d2
├── lvgl_font.c/.h    lv_font_t 后端 + 内置 Montserrat 回退 + 预取队列
└── font_common.h
Bsp/lv_font_provider.c/.h   引擎切换（LV_FONT_ENGINE: 2=CTF+TTF / 1=TTF直读 / 0=GBK 点阵）
Application/
├── ui_common.{c,h}       共享原语 + ui_mk_label（内部透明预取）
├── ui_page_info.{c,h}    信息页
├── ui_page_font.{c,h}    字体状态页（含拉丁文本，验证 Latin 预取）
├── ui_page_boot.{c,h}    启动加载页（Waiting... + 进度条，预加载门控）
├── ui_page_fault.{c,h}   ASCII 故障页
├── app_ui.c              编排器 + ui_warmup_pages()（首帧预热）
└── app_main.c            application_run() 内 DWT 首帧计时 [PAGE] 日志
tools/
├── ttf2ctf/              PC 端 TTF → CTF 生成器（--encoding unicode/gbk --verify --crc）
├── host_test/            在 PC 编译运行固件真实源码的验收测试（gcc 编 blkcache/ttf_reader/ctf_reader/stb_adapter）
└── verify_font_firstload.py  首帧≈二次 回归脚本（见 §七）
```

---

## 六、实施阶段（✅ = 已验证，直接沿用；未 ✅ = 按需）

> 任何新模块动手前**必须先出实现计划并获确认**。每完成一模块立即增量汇报（✅ 收尾）。

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1 | 现状分析（LVGL/stb/FatFs/SD/DCache/RAM，kerning 是卡死根因） | ✅ |
| 2 | CTF 格式 + PC 端 `ttf2ctf`（生成/校验/覆盖率）；生成鸿蒙 CTF 全 PASS | ✅ |
| 3 | MCU 端 `ctf_reader`（三级寻址 + NOT_FOUND 零 IO） | ✅ |
| 4/5 | `ttf_reader` + 16KB×4 块缓存（LRU、跨块合并、统计） | ✅ |
| 6 | `stb_adapter`（STBTT_STREAM 接 ttf_reader，stb 零改） | ✅ |
| 7 | LVGL 字体后端 + 内置 Montserrat 回退 | ✅ |
| 8 | 主机验收 + 双构零警告 + 烧录 + 串口自检 | ✅ |
| 8.14 | resident RAM index（L1 + Page 表 + TTF 目录常驻） | ✅ |
| 8.15 | 200 KB 字形缓存（LRU + epoch 钉扎 + 异步预取，放 .ram_d2） | ✅ |
| 8.16 | 页面分离 + 启动加载页（预加载门控，GBK 不预加载直接等 2s） | ✅ |
| 8.17 | 字体页首帧 ~200 ms 卡顿消除（抑制 flush 离屏预热 + Latin 预取修复） | ✅ |
| 冷栅格化优化 | CTF v2 glyf 预取打包 / 顺序预读 / FA_FASTSEEK（读放大，对首帧无影响，后续项） | ⬜ 待做 |

**经验教训（写入每阶段）**：
- 缺字 `NOT_FOUND` 必须 `ctf_sd==0` 才能算达标，验收必测。
- resident index 必须 all-or-nothing，部分常驻得不偿失。
- 200 KB 字形缓存放 RAM_D2 而非 AXI-SRAM，规避 90% 危险区。
- **Latin 不得跳过预取**（§一.6）；**首帧慢用预热而非加缓存**（§一.7）。
- 抓串口必须先开后台抓再 `openocd reset`；ST-Link VCP 端口运行时枚举、禁止硬编码。

---

## 七、验证与验收标准（每模块必走，用数字显式列出）

1. **双构零警告**：`cmake -B build -DCMAKE_BUILD_TYPE=Debug` 与 `-B build-release -DCMAKE_BUILD_TYPE=Release`，
   均 0 warning；显式列 FLASH / RAM_D1 / RAM_D2 占比。
2. **OpenOCD 烧录**：`openocd -f openocd.cfg -c "program build-release/lvgl_oled.elf verify reset exit"` → `Verified OK`。
3. **功能**：ASCII / 中文 / GBK / Unicode / 空格(EMPTY) / Composite / 缺字回退 全部正常；UI 不空白、不刷屏。
4. **缺字零 IO**：CTF probe 对缺字码点（如 `U+1F600`）断言 SD 列 == 0。
5. **首帧计时（DWT CYCCNT）**：`application_run()` 用 `DWT->CYCCNT` 包裹 `lv_scr_load + lv_timer_handler`，
   打印 `scr_load/refr` 耗时 + glyph 缓存 `bmp_miss/hit/evict` + `ctf_sd/ttf_fill/ttf_read` 增量。
   判据：首帧若 `bmp_miss+0 / ctf_sd+0 / ttf_fill+0 / ttf_read+0` → 差距纯 CPU，用预热解决。
6. **首帧≈二次回归**（`scripts/verify_font_firstload.py`）：
   自动开串口 →（`--flash`）烧录复位 → 抓 35 s → 解析所有 `[PAGE] switch -> 1` →
   断言 **首帧 `bmp_miss==0`、零 SD 读、`|首帧 refr - warm 均值| ≤ 30 ms`**，退出码 0/1（CI 友好），
   支持 `--in <文件>` 离线。复跑：`python scripts/verify_font_firstload.py --flash`。
   实机结果（PASS）：首帧 `129085 us` ≈ 二次 `129101 us`，差距归零。
7. **主机验收**（`tools/host_test`，PC 编译固件真实源码）：0..0xFFFF 全表遍历、边界/跨块、栅格化 ASCII art、
   常驻页表路径与降级路径等价性（rc/glyph_id/advance/empty 逐字一致）。7 字体全绿。
8. **运行时计数直读**（无命令接口时）：烧录 Debug + gdb 直读 `glyph_cache.c` 静态量
   `s_hits/s_misses/s_evicts/s_free_bytes`（`x/1uw &'glyph_cache.c'::s_hits`）。

---

## 八、已知坑 / 反模式（务必规避，避免重复踩）

1. **Latin 预取误假设**：`preload_enqueue` 不得 `if (cp<0x80) return;`；CTF 含拉丁，LVGL 用 TTF 渲染。
2. **首绘慢误判为缓存 miss**：先量化 `bmp_miss/ctf_sd/ttf_fill/ttf_read` 增量，确认零 IO 再用预热。
3. **EMPTY ≠ NOT_FOUND**：空格合法 advance、位图 0，正常排版，不能当缺字。
4. **kerning 卡死**：后端不调用 `stbtt_GetGlyphKernAdvance`（LVGL 8 无字距语义）。
5. **GBK 引擎天然跳过预取**：`px_of==0` 时 no-op；启动门控 `pending0==0 → drain_pct=100%`，纯按 2s 走。
6. **端口不硬编码**：ST-Link VCP 串口号依本机分配，用 `pyserial list_ports` 枚举。
7. **边界检查防溢出**：`if (offset > size) return false; if (length > size - offset) return false;`（禁止 `offset+length>size` 写法）。
8. **Parser 防死循环**：cmap/kern/GPOS/glyf/composite 解析必须查边界、限循环次数、限 offset/length/glyphID/table 范围，异常即 `return error`。
9. **DCache 开启**：SDIO 轮询非 DMA 无一致性问题；将来上 DMA 用 MPU 非缓存区，绝不全局关 D-Cache。

---

## 九、移植提示（到 Zephyr 009 / 其它 STM32 LVGL）

- `Bsp/font/*` 与 LVGL 版本弱耦合，主要依赖 `lv_font_t` 两个回调，可整体搬。
- Zephyr 下 SD/FatFs 路径、SPI6/ST7789 显示需对齐本裸机参考实现（本工程即 009 的参考）。
- RAM 预算按目标芯片重算：200 KB 字形缓存放 RAM_D2（或等价非主力 RAM 区），避免挤占主 AXI-SRAM。
- 首帧预热依赖 `lv_disp_t::driver::flush_cb` 可临时替换；不同 LVGL 版本 API 名可能微调。
- 验证脚本的 `[PAGE]` 日志格式依赖固件侧 DWT 计时打印，移植时同步移植该日志。
