# ttf2ctf — CTF 索引生成器

PC 端工具：把 SD 卡上的 TTF 转成一个**只读索引文件** `.ctf`，让 STM32H743 不必为了找一个字符
去扫 `cmap` / `loca`，也不必为了判断"这个字有没有"去碰 TTF。

```
input.ttf ──► ttf2ctf.py ──► input.ctf   （与 input.ttf 一起放 SD 卡，文件名必须成对）
```

纯 Python 3、零第三方依赖（不需要 fontTools / Pillow）。

---

## 1. 用法

```bash
python tools/ttf2ctf/ttf2ctf.py input.ttf [-o out.ctf] [选项]
```

| 选项 | 说明 |
|------|------|
| `-o, --output PATH` | 输出文件，默认与输入同目录、后缀改 `.ctf` |
| `--verify` | 把所有 entry 从 TTF 重新推导一遍逐字段比对，并抽查缺字 |
| `--no-crc` | 跳过对 TTF 的 CRC32 扫描（生成更快，header 里 crc 记为 0） |
| `--info` | 打印 header / 各段布局 / TTF 表索引 |
| `--dump CHARS` | 打印这些字符的 entry（glyph id、glyf 偏移/长度、bbox、类型） |
| `--coverage` | 统计 ASCII / CJK / GB2312 一级汉字覆盖率 |
| `--font-index N` | `.ttc` 字体集合里的第 N 个字体 |
| `--verbose` | 输出 cmap 规模等中间信息 |

典型流程：

```bash
PY=python
$PY tools/ttf2ctf/ttf2ctf.py \
    ../support_tools/sd_card/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ttf \
    --verify --info --coverage --dump "中AV 0"
```

`--verify` 通过时会打印：

```
[ver] PASS  checked=29063  bad=0  unreachable=0  absent-probes=1943
```

四项含义：

| 字段 | 含义 |
|------|------|
| `checked` | 逐字段重新推导并比对的 entry 数（等于全部 entry） |
| `bad` | 与 TTF 重新解析结果不一致的 entry 数 |
| `unreachable` | cmap 里存在、但 CTF 查不到的码位（必须为 0） |
| `absent-probes` | 随机/越界缺字探测次数，全部必须返回 NOT_FOUND |

---

## 2. CTF v1 文件格式

全部 **小端**、**定宽**、**显式偏移序列化**（不依赖任何语言的 struct 对齐）。
固件侧镜像在 `Bsp/font/ctf_format.h`。

### 2.1 整体布局

```
┌──────────────────────────────┬──────────────┐
│ CTF Header                   │   80 B       │
├──────────────────────────────┼──────────────┤
│ TTF 表索引  Table[count]     │  N × 12 B    │
├──────────────────────────────┼──────────────┤
│ 一级索引    L1[256]          │  256 × 8 B   │  plane = (u >> 16) & 0xFF
├──────────────────────────────┼──────────────┤
│ 页索引      Page[M]          │  M × 40 B    │  page  = (u >>  8) & 0xFF
├──────────────────────────────┼──────────────┤
│ Entry 表    Entry[K]         │  K × 24 B    │  low   =  u        & 0xFF
└──────────────────────────────┴──────────────┘
```

文件中所有 offset 都是**文件偏移**，不是 RAM 地址。

### 2.2 Header（80 B）

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| 0 | u32 | `magic` | `0x31465443`（`'CTF1'`） |
| 4 | u16 | `version` | `1` — 固件只接受这一个版本 |
| 6 | u16 | `header_size` | `80` |
| 8 | u32 | `flags` | 见 2.6 |
| 12 | u32 | `ttf_size` | 绑定 TTF 的字节数 |
| 16 | u32 | `ttf_crc32` | 绑定 TTF 的 CRC32（`--no-crc` 时为 0） |
| 20 | u32 | `unicode_mode` | `0` = 稀疏 Unicode 多级索引 |
| 24 | u32 | `table_index_offset` | 表索引段文件偏移 |
| 28 | u32 | `table_index_count` | 表记录条数 |
| 32 | u32 | `l1_index_offset` | 一级索引段文件偏移 |
| 36 | u32 | `l1_index_count` | 固定 `256` |
| 40 | u32 | `page_index_offset` | 页索引段文件偏移 |
| 44 | u32 | `page_index_count` | 页记录条数（含空洞页） |
| 48 | u32 | `entry_offset` | Entry 段文件偏移 |
| 52 | u32 | `entry_count` | Entry 条数 |
| 56 | u32 | `entry_size` | 固定 `24` |
| 60 | u32 | `units_per_em` | 字体设计单位/全角字身 |
| 64 | i16 | `ascent` | hhea.ascender（font units） |
| 66 | i16 | `descent` | hhea.descender |
| 68 | i16 | `line_gap` | hhea.lineGap |
| 70 | u16 | `num_glyphs` | maxp.numGlyphs |
| 72 | u32 | `char_count` | 已索引字符数 |
| 76 | u32 | `reserved0` | 0 |

### 2.3 TTF 表索引记录（12 B）

| 偏移 | 类型 | 字段 |
|------|------|------|
| 0 | u32 | `tag`（`'glyf'` / `'loca'` / `'GPOS'` …，小端存） |
| 4 | u32 | `offset` |
| 8 | u32 | `length` |

收录 `cmap head hhea hmtx maxp loca glyf kern GPOS name OS/2 post cvt fpgm prep`
中实际存在的表。固件不必再解析 TTF 的表目录。

### 2.4 一级索引记录（8 B）

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| 0 | u32 | `page_offset` | 该 plane 页数组的文件偏移；`0` = 整个 plane 为空 |
| 4 | u16 | `page_count` | 页数组长度（= 最高用到的页号 + 1，含空洞） |
| 6 | u16 | `reserved` | 0 |

### 2.5 页记录（40 B）

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| 0 | u32 | `entry_offset` | 本页第一条 entry 的**文件偏移** |
| 4 | u16 | `entry_count` | 本页实际字符数（≤256） |
| 6 | u16 | `flags` | 保留，0 |
| 8 | u8[32] | `bitmap` | 256 位存在位图，bit *i* 置位 = 本页内 low=*i* 的码位存在 |

位图是"稀疏 Unicode → 稠密 entry"的关键：entry 表里**只放字体真正有的码位**，
而置位的 rank（popcount）就是它在页内的下标。

### 2.6 Entry（24 B）

| 偏移 | 类型 | 字段 | 说明 |
|------|------|------|------|
| 0 | u32 | `glyf_offset` | **TTF 文件内的绝对字节偏移** |
| 4 | u32 | `glyf_length` | 字节长度（0 = 空字形） |
| 8 | u16 | `glyph_id` | |
| 10 | u16 | `advance_width` | hmtx，font units |
| 12 | i16 | `bearing_x` | hmtx.lsb |
| 14 | i16 | `x_min` | glyf bbox |
| 16 | i16 | `y_min` | |
| 18 | i16 | `x_max` | |
| 20 | i16 | `y_max` | |
| 22 | u16 | `flags` | 见下 |

Entry **不存 Unicode** —— 码位由「一级索引 + 页 + 页内位置」隐含确定，这是 entry 能压到
24 B 的原因。

### 2.7 flags

Header flags：

| 常量 | 值 | 含义 |
|------|----|------|
| `CTF_FLAG_HAS_COMPOSITE` | `1<<0` | 索引里存在合成字形 |
| `CTF_FLAG_HAS_KERN` | `1<<1` | TTF 带 legacy `kern` 表 |
| `CTF_FLAG_HAS_GPOS` | `1<<2` | TTF 带 `GPOS` 表 |

Entry flags：

| 常量 | 值 | 含义 |
|------|----|------|
| `CTF_GLYPH_EMPTY` | `1<<0` | 真字形但无轮廓（空格） |
| `CTF_GLYPH_SIMPLE` | `1<<1` | numberOfContours > 0 |
| `CTF_GLYPH_COMPOSITE` | `1<<2` | numberOfContours < 0 |
| `CTF_GLYPH_VALID` | `1<<3` | entry 来自真实字形 |
| `CTF_GLYPH_MISSING` | `1<<4` | 落到 .notdef，按缺字处理 |

---

## 3. 寻址算法（固件侧，O(1)）

```
u = Unicode 码位
 1. plane = (u >> 16) & 0xFF        plane >= l1_index_count       → NOT_FOUND
 2. 读 L1[plane]  (8 B)             page_count == 0              → NOT_FOUND
 3. page  = (u >>  8) & 0xFF        page >= page_count            → NOT_FOUND
 4. 读 Page[page] (40 B)
 5. low   = u & 0xFF                bitmap 对应位为 0             → NOT_FOUND
 6. rank  = popcount(bitmap[0..low)) → idx = entry_offset + rank*24
 7. 读 Entry (24 B)                 flags & MISSING               → NOT_FOUND
```

最坏 3 次小读（8 / 40 / 24 B），全部落在块缓存里。
**每一步的 NOT_FOUND 都在 CTF 内部结束，一次 SD/TTF 访问都不会发生。**

`EMPTY`（空格）与 `NOT_FOUND` 是两种不同状态：空格有合法 `advance_width`、位图尺寸为 0，
必须正常排版，只有 `MISSING` / 未置位才由 LVGL 走缺字/回退。

---

## 4. 生成结果（HarmonyOS Sans）

生成目录：`support_tools/sd_card/SYSTEM/`

| 字体 | TTF 大小 | CTF 大小 | 索引字符 | CJK | GB2312 一级 | 校验 |
|------|---------:|---------:|---------:|----:|------------:|:----:|
| HarmonyOS_Sans_SC_Regular | 8 261 128 B | 710 012 B (693 KB) | 29 063 | 20 902 | **3755/3755 (100%)** | PASS |
| HarmonyOS_Sans_TC_Regular | 4 119 636 B | 362 804 B (354 KB) | 14 596 | 13 075 | 2553/3755 (68.0%) | PASS |
| HarmonyOS_Sans_TC_Black   | 4 052 064 B | 362 804 B | 14 596 | 13 075 | 68.0% | PASS |
| HarmonyOS_Sans_TC_Bold    | 4 064 528 B | 362 804 B | 14 596 | 13 075 | 68.0% | PASS |
| HarmonyOS_Sans_TC_Light   | 4 157 996 B | 362 804 B | 14 596 | 13 075 | 68.0% | PASS |
| HarmonyOS_Sans_TC_Medium  | 4 112 712 B | 362 804 B | 14 596 | 13 075 | 68.0% | PASS |
| HarmonyOS_Sans_TC_Thin    | 4 164 648 B | 362 804 B | 14 596 | 13 075 | 68.0% | PASS |

单文件布局（SC Regular）：

```
header        0      80 B
table index   80     132 B      (11 条)
L1 index      212    2048 B     (256 条)
page index    2260   10 240 B   (256 条，含空洞)
entry table   12500  697 512 B  (29 063 × 24)
─────────────────────────────────
合计                 710 012 B
```

### TC 还是 SC？

`HarmonyOS_Sans_TC` 是**繁体**子集，GB2312 一级简体只覆盖 68%，UI 里"测试库统显时钟运缓温压络设关单图乐视频调试状态"等 23 个常用简体字会缺字。
`HarmonyOS_Sans_SC` 覆盖 100%。**默认字体选 SC**，TC 作为繁体显示的可选字族保留。

### 合成字形

HarmonyOS Sans 的 29 221 个字形**全部是 simple glyph**（0 个 composite），所以鸿蒙字体这条路径
没有被真实数据覆盖到。生成器与分类逻辑已用 `C:/Windows/Fonts/arial.ttf`（2016 个 composite）
单独验证：1402 个合成字形被正确标记，`é Ạ Ǟ` 全部识别为 `composite`，`--verify` PASS。
Composite 依赖的其他字形仍由 TTF Reader + 块缓存提供，不走 CTF。

---

## 5. 设计约束（写入格式前请先读）

1. **CTF 只是索引**：不含 bitmap、不含 glyf 副本、不含 TTF 原始数据。原始 `.ttf` 必须与 `.ctf`
   同名同目录，header 里的 `ttf_size` / `ttf_crc32` 用来在启动时发现"索引和字体不是一对"。
2. **只索引字体真正有的码位**：不为 0x000000–0x10FFFF 铺满 entry。`cmap` 映射到 glyph 0 的码位
   直接丢弃（位图位为 0），这样固件连一次 SD 读都不用就得到 NOT_FOUND。
3. **整个 TTF 不进 RAM，整个 CTF 也不进 RAM**。CTF 是磁盘索引，固件按需读 8/40/24 字节的小记录。
4. 改任何字段偏移/宽度 = 格式变更，必须同步 `CTF_VERSION`、本文件、
   `Bsp/font/ctf_format.h` 与固件 reader。

---

## 6. 文件

| 文件 | 职责 |
|------|------|
| `ctf_format.py` | 格式常量 + 各记录的显式序列化/反序列化 + PC 端查找实现 |
| `ttf_parser.py` | 最小 TTF 解析：sfnt 目录 / head / maxp / hhea / hmtx / loca / glyf / cmap(0,4,6,12) |
| `ttf2ctf.py` | CLI：生成 / 校验 / 信息 / 覆盖率 / dump |
