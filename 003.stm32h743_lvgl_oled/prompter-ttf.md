# STM32H743 + LVGL + SD/FatFs + TTF/CTF 高性能字体系统完整重构提示词

你现在需要对当前 STM32H743 项目中的 LVGL TTF 字体系统进行完整重构。

项目存在以下约束：

* MCU：STM32H743
* Cortex-M7
* 内部 RAM 约 1 MB
* **没有外部 SDRAM**
* SD Card 存储字体
* 使用 FatFs
* TTF 字体可能达到数 MB
* 使用 LVGL
* 使用 stb_truetype 或当前工程中的 stb_truetype
* 当前存在 `stbtt_GetGlyphKernAdvance()` 导致 SD 卡随机访问严重卡顿/疑似卡死的问题

本次重构的核心目标：

> **不要把整个 TTF 加载到 RAM。**
>
> **不要把整个 CTF 加载到 RAM。**
>
> **CTF 设计为磁盘索引文件，通过多级直接寻址快速定位 Unicode 对应的 CTF_Entry。**
>
> **如果 Unicode 在 TTF 中不存在，则 CTF 查询直接返回 NOT_FOUND，后续完全不访问 TTF。**
>
> **只有确认 Unicode 存在后，才访问 TTF。**
>
> **TTF 的随机访问通过 Block Cache 进行优化。**
>
> **stb_truetype 不允许直接对 FatFs 执行大量随机 `f_lseek()+f_read()`。**

---

# 一、最终目标架构

最终架构必须设计成：

```text
                         LVGL
                           │
                           ▼
                  LVGL Font Backend
                           │
                           ▼
                   Unicode / GBK
                           │
                           ▼
                  ┌────────────────┐
                  │   CTF Reader   │
                  │                │
                  │ 多级直接寻址    │
                  └───────┬────────┘
                          │
               ┌──────────┴──────────┐
               │                     │
          NOT_FOUND                FOUND
               │                     │
               ▼                     ▼
        立即返回缺字             CTF_Entry
               │                     │
               │                     ▼
               │                 glyph_id
               │                     │
               │                     ▼
               │              TTF Reader
               │                     │
               │                     ▼
               │                TTF Cache
               │                     │
               │                     ▼
               │                   FatFs
               │                     │
               │                     ▼
               │                  SD Card
               │
               ▼
        LVGL 缺字处理
```

特别强调：

```text
Unicode
   ↓
CTF
   ↓
NOT_FOUND
```

必须在这里结束。

不得继续：

```text
NOT_FOUND
   ↓
TTF
   ↓
cmap
   ↓
stb_truetype
```

---

# 二、核心设计原则

必须遵守以下原则。

## 1. CTF 是索引，不是字体数据

CTF 不保存：

* bitmap
* glyph outline 完整数据
* TTF 原始数据
* 大型字体资源

CTF 只保存：

* Unicode 索引
* Glyph ID
* glyf offset
* glyf length
* glyph metrics
* glyph flags
* TTF table 索引
* TTF 文件大小
* TTF CRC32 等验证信息

---

# 三、CTF 必须采用磁盘多级直接寻址

不要设计成：

```text
Unicode
 ↓
遍历 CTF_Entry[]
 ↓
逐个比较 Unicode
```

也不要依赖：

```c
binary_search()
```

作为主要方案。

必须设计：

```text
Unicode
   ↓
Level-1 Index
   ↓
Page Index
   ↓
CTF_Entry
```

目标是让字符查询接近：

```text
O(1)
```

---

# 四、CTF 文件布局

建议：

```text
font.ctf
│
├── CTF_Header
│
├── TTF_Table_Index[]
│
├── Unicode_Level1_Index[]
│
├── Unicode_Page[]
│
├── CTF_Entry[]
│
└── Optional Extension Area
```

文件中的所有 offset 都是：

> **文件偏移，不是 RAM 地址。**

---

# 五、CTF Header

设计固定格式：

```c
#define CTF_MAGIC 0x31465443UL   /* "CTF1" */

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;

    uint32_t flags;

    uint32_t ttf_size;
    uint32_t ttf_crc32;

    uint32_t unicode_mode;

    uint32_t table_index_offset;
    uint32_t table_index_count;

    uint32_t l1_index_offset;
    uint32_t l1_index_count;

    uint32_t page_index_offset;
    uint32_t page_index_count;

    uint32_t entry_offset;
    uint32_t entry_count;
    uint32_t entry_size;

    uint32_t units_per_em;

    uint32_t reserved[4];

} CTF_Header;
```

实际实现时必须明确：

* little endian
* 字段长度
* 对齐
* version
* header_size
* CRC
* compatibility

磁盘格式不能简单依赖 MCU 编译器的默认 struct layout。

---

# 六、TTF Table Index

PC 端生成 CTF 时解析 TTF Table Directory。

至少记录：

```text
cmap
head
hhea
hmtx
maxp
loca
glyf
kern
GPOS
```

根据当前 stb_truetype 实际使用情况增加其他 table。

定义：

```c
typedef struct
{
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
} CTF_Table;
```

例如：

```text
"head" → offset / length
"maxp" → offset / length
"loca" → offset / length
"glyf" → offset / length
"hhea" → offset / length
"hmtx" → offset / length
"kern" → offset / length
"GPOS" → offset / length
```

这样 MCU 不需要扫描 TTF Table Directory。

---

# 七、Unicode 多级索引

Unicode 是 32 bit。

采用稀疏多级索引。

建议：

```text
Unicode
   │
   ├── Level 1：高位索引
   │
   ├── Page
   │
   └── Entry
```

Level-1 可以采用高 8 bit。

例如：

```c
typedef struct
{
    uint32_t page_offset;
    uint16_t page_count;
    uint16_t reserved;

} CTF_L1_Index;
```

最多 256 项。

整个 Level-1 Index 非常小。

---

# 八、Page Index

定义：

```c
typedef struct
{
    uint32_t entry_offset;
    uint16_t entry_count;
    uint16_t flags;

} CTF_Page;
```

其中：

```text
entry_offset
```

表示：

> CTF 文件中 CTF_Entry 的偏移。

绝对不能解释为 RAM 地址。

---

# 九、CTF Entry

采用紧凑格式：

```c
typedef struct
{
    uint32_t glyf_offset;
    uint32_t glyf_length;

    uint16_t glyph_id;

    int16_t advance_width;
    int16_t bearing_x;

    uint16_t flags;

} CTF_Entry;
```

目标：

```text
16~20 bytes / glyph
```

CTF 不保存 Unicode。

Unicode 由：

```text
Level-1 + Page + Entry position
```

隐含确定。

---

# 十、Glyph Flags

至少定义：

```c
#define CTF_GLYPH_EMPTY       (1U << 0)
#define CTF_GLYPH_SIMPLE      (1U << 1)
#define CTF_GLYPH_COMPOSITE   (1U << 2)
#define CTF_GLYPH_VALID       (1U << 3)
```

注意：

> `EMPTY` 和 `NOT_FOUND` 是两个完全不同的状态。

---

# 十一、最重要的规则：Unicode 不存在时立即返回

这是本项目的硬性要求。

例如：

```text
用户请求：
U+4E2D
```

如果字体支持：

```text
CTF
 ↓
FOUND
 ↓
CTF_Entry
 ↓
TTF
```

如果字体不支持：

```text
CTF
 ↓
NOT_FOUND
 ↓
立即返回
```

禁止：

```text
CTF NOT_FOUND
 ↓
打开 TTF
 ↓
搜索 cmap
```

禁止。

---

# 十二、CTF API

定义：

```c
typedef enum
{
    CTF_OK = 0,
    CTF_NOT_FOUND,
    CTF_ERR_IO,
    CTF_ERR_FORMAT,
    CTF_ERR_RANGE,

} CTF_Result;
```

API：

```c
CTF_Result ctf_find_unicode(
    CTF_Handle *ctf,
    uint32_t unicode,
    CTF_Entry *entry
);
```

GBK：

```c
CTF_Result ctf_find_gbk(
    CTF_Handle *ctf,
    uint16_t gbk,
    CTF_Entry *entry
);
```

注意：

```text
CTF_NOT_FOUND
```

是正常业务状态。

不要打印 error。

不要刷屏。

不要进入异常处理。

---

# 十三、LVGL 中的缺字处理

当：

```c
ctf_find_unicode()
```

返回：

```c
CTF_NOT_FOUND
```

字体 Backend 必须直接返回：

```text
LV_FONT_GLYPH_NOT_FOUND
```

或者当前 LVGL 版本对应的标准“glyph 不存在”状态。

禁止访问：

```text
TTF
stb_truetype
kern
GPOS
glyf
loca
```

后续由 LVGL 自己执行：

* fallback font
* replacement glyph
* 空白
* 默认缺字行为

具体行为必须遵循当前 LVGL 版本 API。

---

# 十四、Empty Glyph 的处理

如果：

```text
CTF FOUND
```

但是：

```text
flags & CTF_GLYPH_EMPTY
```

则不能返回 NOT_FOUND。

例如：

```text
Space
```

可能：

```text
glyph exists
bitmap empty
advance_width > 0
```

此时：

```text
glyph descriptor = valid
bitmap = empty
advance = correct
```

---

# 十五、GBK

系统需要支持：

```text
GBK → Unicode
```

然后：

```text
Unicode → CTF
```

建议 API：

```c
ctf_find_gbk()
ctf_find_unicode()
```

GBK 不应该直接进入 TTF parser。

流程：

```text
GBK
 ↓
GBK → Unicode
 ↓
CTF
 ↓
Entry
```

---

# 十六、PC 端 ttf2ctf 工具

创建：

```text
tools/
└── ttf2ctf/
    ├── src/
    ├── include/
    ├── CMakeLists.txt
    └── README.md
```

生成：

```text
ttf2ctf.exe
```

命令：

```bash
ttf2ctf input.ttf output.ctf
```

支持：

```bash
--encoding unicode
--encoding gbk
--verify
--crc
--verbose
```

---

# 十七、PC 端 TTF 解析

必须解析：

```text
Offset Table
Table Directory
cmap
head
maxp
hhea
hmtx
loca
glyf
```

根据 stb_truetype 版本增加：

```text
kern
GPOS
```

---

# 十八、CTF Entry 生成

对于每个存在的 Unicode：

```text
Unicode
 ↓
cmap
 ↓
glyph_id
 ↓
loca[glyph_id]
 ↓
loca[glyph_id + 1]
 ↓
glyf_offset
glyf_length
```

同时保存：

```text
advance_width
bearing_x
flags
```

---

# 十九、只保存存在的 Unicode

禁止生成完整：

```text
0x00000000 ~ 0x10FFFF
```

Entry。

只保存：

> **当前 TTF cmap 中实际存在的 Unicode。**

例如：

```text
A
B
C
中
文
你
好
```

CTF 只保存这些。

不存在：

```text
U+12345
```

就没有对应 Entry。

---

# 二十、CTF 容量

如果 Entry 约 16 bytes：

```text
10,000 glyph ≈ 160 KB
20,000 glyph ≈ 320 KB
30,000 glyph ≈ 480 KB
```

再增加：

```text
Header
L1 Index
Page Index
Table Index
```

通常仍可以控制在：

```text
100 KB ~ 600 KB
```

但：

> CTF 即使达到几百 KB，也不能整体加载 RAM。

---

# 二十一、TTF Reader

实现：

```text
ttf_reader.c
ttf_reader.h
```

API：

```c
bool ttf_open(
    TTF_Reader *reader,
    const char *path
);

void ttf_close(
    TTF_Reader *reader
);

bool ttf_read(
    TTF_Reader *reader,
    uint32_t offset,
    void *buffer,
    uint32_t length
);

bool ttf_read_u16(
    TTF_Reader *reader,
    uint32_t offset,
    uint16_t *value
);

bool ttf_read_u32(
    TTF_Reader *reader,
    uint32_t offset,
    uint32_t *value
);
```

---

# 二十二、TTF Block Cache

默认：

```text
16 KB × 4
= 64 KB
```

采用：

```text
LRU
```

Cache：

```c
typedef struct
{
    uint32_t file_offset;
    uint32_t valid_size;
    uint32_t age;

    bool valid;

    uint8_t data[TTF_CACHE_BLOCK_SIZE];

} TTF_CacheBlock;
```

---

# 二十三、TTF Cache 工作方式

例如：

```text
stb_truetype
 ↓
读取 TTF offset = 0x153827
```

执行：

```text
offset
 ↓
计算 block
 ↓
Cache Hit？
 ├── Yes → RAM
 │
 └── No
      ↓
   f_lseek()
      ↓
   f_read(16KB)
      ↓
   Cache
      ↓
   返回
```

必须支持：

```text
任意 offset
任意 length
跨 block
```

---

# 二十四、禁止 stb_truetype 直接访问 FatFs

严禁：

```text
stb_truetype
 ↓
f_lseek()
 ↓
f_read()
```

必须：

```text
stb_truetype
 ↓
TTF Adapter
 ↓
TTF Reader
 ↓
TTF Block Cache
 ↓
FatFs
 ↓
SD
```

---

# 二十五、stb_truetype 特别处理

首先检查当前项目实际使用的：

```text
stb_truetype.h
```

版本。

不能假设：

```c
stbtt_InitFont()
```

可以直接使用一个虚假的 RAM pointer。

原始 stb_truetype 可能大量使用：

```c
data + offset
```

进行随机访问。

因此必须实际分析当前版本。

优先方案：

```text
最小修改 stb_truetype
+
TTF Reader Adapter
+
Block Cache
```

不要重写整个 stb_truetype。

---

# 二十六、Composite Glyph

必须支持 Composite Glyph。

例如：

```text
Glyph A
 ├── Component B
 └── Component C
```

CTF：

```text
A
 ↓
glyf_offset
glyf_length
```

但 stb_truetype 继续解析：

```text
B
C
```

这些二级访问必须通过：

```text
TTF Reader
 ↓
TTF Cache
```

不能直接访问 SD。

---

# 二十七、Kerning

必须保留：

```c
stbtt_GetGlyphKernAdvance()
```

功能。

禁止为了性能：

```c
return 0;
```

禁止直接关闭 Kerning。

必须确认当前 stb_truetype 实际使用：

```text
kern
```

还是：

```text
GPOS
```

---

# 二十八、Kerning 优化

第一阶段：

```text
CTF
 ↓
glyph_id
 ↓
stb_truetype
 ↓
TTF Reader
 ↓
TTF Cache
 ↓
kern / GPOS
```

第二阶段才考虑：

```text
CTF Kerning Index
```

第一阶段不要过度设计。

优先：

```text
正确
稳定
可测量
```

---

# 二十九、TTF 小表优化

可以对高频小表建立 RAM Cache：

```text
head
maxp
hhea
hmtx
```

但必须满足：

> 只缓存小型高频 table，不允许加载整个 TTF。

---

# 三十、loca

CTF 已经预计算：

```text
glyph_id
glyf_offset
glyf_length
```

所以普通 glyph 查找不应该每次重新读取：

```text
loca
```

但是 Composite Glyph 和 stb 内部逻辑仍允许通过：

```text
TTF Reader
```

读取 loca。

---

# 三十一、内存限制

严格禁止：

```c
malloc(ttf_size);
```

严格禁止：

```c
malloc(ctf_size);
```

禁止：

```text
整个 TTF → RAM
整个 CTF → RAM
```

运行时只能使用：

```text
CTF Header Buffer
CTF Page Buffer
CTF Entry Buffer
TTF Cache
SD DMA Buffer
LVGL Buffer
```

---

# 三十二、建议 RAM 预算

初始配置：

```text
TTF Cache       64 KB
CTF Header      < 1 KB
CTF Page        < 1 KB
CTF Entry       < 1 KB
SD DMA Buffer   根据现有工程
```

后续 benchmark：

```text
16KB × 4
16KB × 8
32KB × 4
```

选择最佳方案。

---

# 三十三、SD DMA / DCache

STM32H743 Cortex-M7 存在 DCache。

必须检查：

```text
SD DMA
DMA Buffer
DCache
MPU
RAM Region
```

确保：

```text
DMA → RAM
```

之后 CPU 能看到正确数据。

不能简单地在所有读取前后：

```c
SCB_CleanInvalidateDCache();
```

作为最终方案。

必须建立正确的 Cache Coherency 策略。

---

# 三十四、FatFs 并发

如果：

```text
LVGL task
Font task
其他 task
```

可能同时访问字体文件：

必须保护：

```text
FIL
TTF Reader
TTF Cache
```

推荐：

```c
SemaphoreHandle_t ttf_mutex;
```

或者使用独立 Reader Context。

禁止多个任务同时操作同一个：

```c
FIL
```

---

# 三十五、错误边界检查

所有：

```text
offset
length
count
```

必须检查。

禁止：

```c
if (offset + length > file_size)
```

作为唯一检查。

必须：

```c
if (offset > file_size)
    return false;

if (length > file_size - offset)
    return false;
```

防止整数溢出。

---

# 三十六、Parser 防死循环

所有：

```text
cmap
kern
GPOS
glyf
composite glyph
```

解析必须：

* 检查边界
* 限制循环次数
* 检查 offset
* 检查 length
* 检查 glyph ID
* 检查 table 范围

任何异常：

```text
return error
```

不能无限循环。

---

# 三十七、CTF 与 TTF 一致性

CTF Header 保存：

```text
TTF size
TTF CRC32
```

启动时至少快速验证：

```text
TTF file size == CTF ttf_size
```

CRC32 作为可选完整验证。

不要每次开机扫描几 MB TTF 计算 CRC。

---

# 三十八、LVGL 字体接口

不要优先修改 LVGL Core。

通过：

```text
lv_font_t
```

实现：

```text
get_glyph_dsc
get_glyph_bitmap
```

---

# 三十九、Glyph Descriptor 流程

```text
LVGL
 ↓
Unicode
 ↓
CTF
 │
 ├── NOT_FOUND
 │      ↓
 │   return GLYPH_NOT_FOUND
 │
 └── FOUND
        ↓
     CTF_Entry
        ↓
     glyph_id
        ↓
     stb_truetype
        ↓
     metrics
```

---

# 四十、Glyph Bitmap 流程

```text
LVGL
 ↓
Unicode
 ↓
CTF
 │
 ├── NOT_FOUND
 │      ↓
 │   return no glyph
 │
 └── FOUND
        ↓
     glyph_id
        ↓
     stb_truetype
        ↓
     TTF Reader
        ↓
     TTF Cache
        ↓
     SD
        ↓
     bitmap
```

---

# 四十一、性能关键路径

优化目标：

```text
普通字符：

Unicode
 ↓
CTF L1
 ↓
Page
 ↓
Entry
 ↓
TTF Cache
 ↓
stb
```

缺字：

```text
Unicode
 ↓
CTF
 ↓
NOT_FOUND
 ↓
END
```

缺字情况下：

```text
TTF SD Read = 0
```

这是必须验证的。

---

# 四十二、性能测试

实现 benchmark。

测试：

```text
1000 ASCII
1000 中文
1000 随机 Unicode
1000 次缺字
1000 次重复字符
1000 次 Kerning
1000 次 Composite Glyph
```

统计：

```text
CTF lookup time
TTF lookup time
Glyph render time
Kerning time
SD read count
SD read bytes
Cache hit
Cache miss
```

---

# 四十三、重点测试“缺字快速返回”

专门测试：

```text
U+12345
```

假设字体不存在。

要求：

```text
CTF lookup
 ↓
NOT_FOUND
 ↓
return
```

日志：

```text
[FONT] U+12345 NOT_FOUND
```

不得出现：

```text
[TTF] read
```

不得出现：

```text
[KERN]
```

不得出现：

```text
[GPOS]
```

不得出现：

```text
[GLYF]
```

---

# 四十四、重点测试 stbtt_GetGlyphKernAdvance

必须追踪：

```c
stbtt_GetGlyphKernAdvance()
```

调用链。

记录：

```text
glyph1
glyph2
table
TTF offset
cache hit/miss
read size
耗时
```

例如：

```text
[KERN] glyph1=123
[KERN] glyph2=456
[KERN] table=kern
[KERN] offset=0x123456
[KERN] cache=HIT
[KERN] elapsed=xx us
```

---

# 四十五、日志等级

不要在正常字体渲染过程中大量打印。

定义：

```c
FONT_LOG_ERROR
FONT_LOG_WARN
FONT_LOG_INFO
FONT_LOG_DEBUG
```

默认：

```text
INFO
```

缺字：

```text
NOT_FOUND
```

不应该按 ERROR 打印。

DEBUG 才输出：

```text
CTF offset
Entry offset
TTF offset
Cache hit
Cache miss
```

---

# 四十六、工程目录

建议：

```text
font/
├── ctf_reader.c
├── ctf_reader.h
├── ttf_reader.c
├── ttf_reader.h
├── ttf_cache.c
├── ttf_cache.h
├── stb_adapter.c
├── stb_adapter.h
├── lvgl_font.c
├── lvgl_font.h
└── font_common.h

tools/
└── ttf2ctf/
    ├── src/
    ├── include/
    ├── CMakeLists.txt
    └── README.md
```

根据当前工程实际目录结构调整，不要机械创建重复模块。

---

# 四十七、开发流程

严格按照以下阶段执行。

## Phase 1：分析

先不要大规模修改代码。

分析：

```text
1. 当前 LVGL 字体架构
2. 当前 TTF 加载方式
3. stb_truetype 版本
4. stbtt_GetGlyphKernAdvance 调用链
5. 当前 FatFs 使用方式
6. 当前 SD 驱动
7. 当前 SD DMA
8. DCache 配置
9. 当前 RAM 使用
10. 当前 FreeRTOS 架构
```

输出完整分析报告。

---

## Phase 2：CTF 工具

实现：

```text
ttf2ctf
```

支持：

```text
TTF → CTF
```

并验证：

```text
Unicode
glyph_id
glyf offset
glyf length
metrics
flags
```

---

## Phase 3：CTF Reader

实现：

```text
ctf_open()
ctf_close()
ctf_find_unicode()
ctf_find_gbk()
```

先不接 LVGL。

测试：

```text
存在字符
不存在字符
空 glyph
随机字符
```

---

## Phase 4：TTF Reader

实现：

```text
ttf_open()
ttf_read()
ttf_read_u16()
ttf_read_u32()
```

测试：

```text
连续读取
随机读取
跨 block
边界读取
非法读取
```

---

## Phase 5：TTF Cache

实现：

```text
16KB × 4
LRU
```

测试：

```text
Cache Hit
Cache Miss
Cross Block
Eviction
```

---

## Phase 6：stb Adapter

让：

```text
stb_truetype
```

通过：

```text
TTF Reader
```

访问 TTF。

禁止直接 FatFs 随机访问。

---

## Phase 7：LVGL

接入：

```text
lv_font_t
```

支持：

```text
ASCII
中文
GBK
Unicode
Space
Empty Glyph
Composite Glyph
Kerning
```

---

## Phase 8：性能测试

对比：

```text
旧架构：
stb → FatFs → SD
```

与：

```text
新架构：
CTF → TTF Cache → FatFs → SD
```

输出：

```text
平均时间
最大时间
SD read 次数
Cache hit rate
Cache miss rate
RAM 使用
```

---

# 四十八、必须特别注意的架构问题

不要认为：

```text
CTF Entry
 ↓
glyf_offset
 ↓
只读取这个 glyph
```

就可以完全替代 TTF。

这是错误的。

因为：

```text
Composite Glyph
Kerning
GPOS
Metrics
其他 TTF table
```

仍然可能访问其他 TTF 数据。

所以：

```text
CTF
```

负责：

> **快速定位和判断字符是否存在。**

而：

```text
TTF Reader + Cache
```

负责：

> **完整兼容 TTF 的随机访问。**

---

# 四十九、最终数据访问策略

必须达到：

```text
情况 A：Unicode 不存在

Unicode
 ↓
CTF
 ↓
NOT_FOUND
 ↓
结束

TTF Access = 0
```

---

```text
情况 B：普通 Glyph

Unicode
 ↓
CTF
 ↓
Entry
 ↓
glyph_id
 ↓
TTF Cache
 ↓
stb
```

---

```text
情况 C：Composite Glyph

Unicode
 ↓
CTF
 ↓
Entry
 ↓
glyph_id
 ↓
stb
 ↓
component glyph
 ↓
TTF Cache
```

---

```text
情况 D：Kerning

glyph1 + glyph2
 ↓
stb
 ↓
kern / GPOS
 ↓
TTF Cache
```

---

# 五十、最终验收标准

完成后必须满足：

### 功能

* [ ] ASCII 正常
* [ ] 中文正常
* [ ] GBK 正常
* [ ] Unicode 正常
* [ ] 空格正常
* [ ] Empty Glyph 正常
* [ ] Composite Glyph 正常
* [ ] Kerning 正常
* [ ] LVGL 正常显示

### 内存

* [ ] 不加载整个 TTF
* [ ] 不加载整个 CTF
* [ ] RAM 使用可统计
* [ ] TTF Cache 可配置

### 性能

* [ ] CTF Unicode 查找接近 O(1)
* [ ] 缺字不访问 TTF
* [ ] TTF 随机访问经过 Cache
* [ ] SD read 次数显著下降
* [ ] `stbtt_GetGlyphKernAdvance()` 不再出现长时间阻塞

### 稳定性

* [ ] 非法 Unicode
* [ ] 非法 CTF
* [ ] 非法 TTF
* [ ] SD 错误
* [ ] offset 越界
* [ ] length 越界
* [ ] composite glyph 异常
* [ ] kern/GPOS 异常
* [ ] 不发生死循环
* [ ] 不发生内存泄漏
* [ ] 长时间运行稳定

---

# 五十一、最终交付内容

完成后必须输出：

```text
1. 完整架构说明
2. CTF 文件格式说明
3. CTF 文件大小统计
4. TTF Cache 设计
5. stb_truetype Adapter 设计
6. LVGL 集成方式
7. 修改文件列表
8. 新增文件列表
9. PC ttf2ctf 工具
10. STM32 端完整代码
11. 编译方法
12. CTF 生成方法
13. 字体部署方法
14. RAM 使用统计
15. 性能测试结果
16. 缺字测试结果
17. Kerning 测试结果
18. Composite Glyph 测试结果
19. DCache/SD DMA 处理说明
20. 已知限制
```

---

# 五十二、最重要的设计结论

整个系统必须最终遵循：

```text
             Unicode
                │
                ▼
        ┌──────────────┐
        │ CTF Index    │
        └──────┬───────┘
               │
       ┌───────┴────────┐
       │                │
   NOT_FOUND           FOUND
       │                │
       ▼                ▼
    立即返回          CTF_Entry
       │                │
       │                ▼
       │            glyph_id
       │                │
       │                ▼
       │          stb_truetype
       │                │
       │                ▼
       │           TTF Reader
       │                │
       │                ▼
       │           Block Cache
       │                │
       │                ▼
       │              FatFs
       │                │
       │                ▼
       │                SD
       │
       ▼
    LVGL 缺字处理
```

**硬性要求：**

> Unicode 不存在于 CTF 时，必须立即返回 `CTF_NOT_FOUND`，绝对不能继续访问 TTF。

> `CTF_NOT_FOUND` 是正常状态，不应该产生 ERROR 日志。

> Empty Glyph 不等于 NOT_FOUND。

> CTF 只负责索引和快速存在性判断。

> TTF Reader 负责完整字体随机访问。

> TTF Cache 负责把大量小型随机读取转换成较大的 SD Block Read。

> 整个 TTF 和整个 CTF 都不能加载到 RAM。

> 最终目标是在 STM32H743 无 SDRAM、约 1 MB 内部 RAM 的条件下，让几 MB TTF 字体稳定、高效地服务于 LVGL。

**现在先执行 Phase 1。**

不要立即修改大量代码。

先分析当前工程，确认实际使用的 stb_truetype 版本、LVGL 版本、字体调用链以及 `stbtt_GetGlyphKernAdvance()` 的真实数据访问路径，然后给出重构方案和需要修改的文件列表。

只有完成分析后，再开始 Phase 2。
