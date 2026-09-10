---
name: stm32-lvgl-font-engine
description: STM32 + LVGL 在 SD 卡上用 CTF 索引 + 原 TTF 实现中文/多语言显示的高性能字体引擎：不可违反的硬约束、最终架构、关键设计决策与理由、已知坑（Latin 预取误假设、首绘 CPU 非缓存）、验证与回归方法论。适用于"STM32 显示中文""LVGL 字库优化""TTF 不进 RAM""字体页首帧卡顿""移植字体引擎到 Zephyr/其它 STM32 LVGL 工程"。触发词：LVGL 字体、CTF 索引、TTF 块缓存、字形缓存、首帧慢、中文显示、字库预热、NOT_FOUND 零 SD、Montserrat 回退。
agent_created: true
---

# STM32 + LVGL 高性能字体引擎（CTF 索引 + TTF 块缓存）

在 STM32（无外部 SDRAM、内部 RAM ~1 MB）上用 **LVGL** 显示中文/多语言，字库放 **SD 卡（FatFs）**，
核心是 **CTF 磁盘索引 + 原 TTF 流式栅格化**，绝不把整个 TTF/CTF 装进 RAM。本 skill 沉淀的是
`003.stm32h743_lvgl_oled` 项目经实机验证的完整设计、不可违反约束与踩坑，可直接用于重建或移植
（如 Zephyr + LVGL 项目 009、NES+LVGL 项目）。

> 配套：环境见 `stm32-ai-dev-environment`；构建/验收见 `stm32-verification-acceptance`；
> 外设驱动见 `stm32-peripheral-drivers`；总方法论见 `stm32-vibe-coding-workflow`。

## 一、不可违反的硬约束（front-loaded）

1. **整个 TTF / 整个 CTF 都不进 RAM**。运行时只有：CTF 头缓冲 / 页缓冲 / entry 缓冲 /
   TTF 块缓存 / 字形缓存 / 栅格化临时 arena。无 SDRAM、内部 RAM 仅 ~1 MB。
2. **缺字（CTF `NOT_FOUND`）必须立即返回，零 SD/TTF 访问**。这是硬性验收指标
   （`NOT_FOUND` 的 SD 读次数 == 0）。`CTF_NOT_FOUND` 是正常业务状态，不打印 error、不刷屏。
3. **`third_party/` 与 `Drivers/` 禁止修改**（共享第三方库）。所有改动只在
   `Application/`、`Bsp/` 自有代码与 `CMakeLists.txt`。
4. **Debug/Release 双构零警告**（`-Wall -Wextra`）。
5. **Latin 预取误假设（高频坑）**：CTF 索引通常**包含拉丁范围**，LVGL 会用 TTF 渲染拉丁字形，
   Montserrat 回退只兜底索引**确实没有**的码点。`preload_enqueue()` **不得** `if (cp < 0x80) return;`
   —— 跳过拉丁会让每个含拉丁文本的页面首帧冷栅格化（见 §四.2）。
6. **首帧/首绘慢 ≠ 字库缓存 miss**：LVGL 一个 screen 第一次被实际绘制时的一次性 CPU 开销
   （样式计算 / label 排版 / draw-task 构建）可达 ~200 ms，纯 CPU、零 IO。解法不是加缓存，
   而是"抑制 flush 的离屏预热"（见 §三.6）。
7. **编码**：源码 UTF-8 字面量（`-finput-charset=UTF-8`）；**勿改** `-fexec-charset`。
8. **缓存/MPU**：D-Cache **开启**（SDIO 轮询非 DMA，无一致性问题）。将来若上 DMA，用 MPU 把
   DMA 缓冲标 non-cacheable，**绝不全局关 D-Cache**。

## 二、最终架构（as-built，自上而下）

```
LVGL label
  │  (lv_font_t: get_glyph_dsc / get_glyph_bitmap)
  ▼
CTF 后端 (lvgl_font.c)            ← 仅此层接 LVGL，不碰 LVGL core
  ├─ 缺字 → return false → LVGL 走 Montserrat 回退（内部 Flash 数据）
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

**关键语义**：`Unicode → CTF → NOT_FOUND → END`（零 SD）；只有确认存在才碰 TTF。

## 三、关键设计决策与理由

1. **CTF = 磁盘索引，不是字体数据**：只存 `Unicode→glyph_id/glyf 偏移/长度/度量/flags`，
   位图/轮廓副本一概不存。三级直接寻址（plane → page(256-bit 位图) → entry），查找 O(1)，
   无遍历、无二分。位图为 0 = 字体没这个字 → 立刻 `NOT_FOUND`。
2. **resident RAM index（§8.14）**：L1(2KB) + Page 表(256×40B≈10KB) + TTF 目录(~132B) 常驻。
   仅当整张页表能放进页池才全常驻（all-or-nothing），否则退化走块缓存、绝不部分常驻。
   加载时一次性校验所有非空平面的 `page_offset` 落在池内，使第二跳减法永不溢出。
   效果：**缺字判定（bit 测试）完全在 RAM 完成，lookup 第二跳零 `f_read`**。
3. **TTF 块缓存 16KB×4 LRU（blkcache.c）**：stb 经 `STBTT_STREAM` 宏接到 `ttf_reader`，
   **stb 源码零修改**。seek 变一次 store（零 IO），read 走块缓存（命中即内存拷贝）。
   能力：跨块合并、短块有效长度、LRU 替换、统计（hits/misses/fills）。
   `f_lseek()+f_read()` 全工程**仅一处** `ttf_fill()`。
4. **200 KB 字形缓存 `glyph_cache.c`（.ram_d2）**：变长字形用 free-list + 偏移排序 + 前后合并
   精确回收；**LRU + epoch 钉扎**——当前页/正在用的字形持续抬升，永不被淘汰；切页 `bump_epoch()`
   让旧页落入淘汰域。异步预取：建页 `mk_label → preload_label` 扫描去重入队，`preload_timer_cb`
   每 30 ms 处理 4 字后台栅格化，队列清空自删 timer。**为何放 RAM_D2**：避免 AXI-SRAM(512KB 主力)
   使用率逼近 90% 危险区，挪到 RAM_D2 后 RAM_D1 维持 ~61.7%、RAM_D2 占 69.44%，两者皆安全。
5. **内置 Montserrat 12/16/24/32 回退**：编译进 Flash 的"内部数据"，SD 未挂载 / CTF 缺失 /
   版本不符时 ASCII 与数字照常显示，UI 不空白。编译期断言 4 档全开，否则 `#error`。
6. **首帧预热 `ui_warmup_pages()`（app_ui.c，§8.17）**：构建完两页后，把 `disp->flush_cb`
   换成 `dummy_flush_cb`（吞掉帧缓冲推送、`lv_disp_flush_ready` 收尾），逐页 `lv_scr_load +
   lv_timer_handler` 各渲染一次，把"首绘一次性开销 + 字形冷栅格化"全挪到启动加载页背后
   （用户不可见）。首个真实翻页即 warm。**这是消除首帧 ~200 ms 卡顿的真正修复**（不是缓存优化）。

## 四、已知坑 / 反模式（务必规避）

1. **Latin 预取误假设**：见 §一.5。原 `if (cp < 0x80) return;` 注释称"Latin 走 Montserrat"，
   但实测 HarmonyOS CTF 含拉丁，LVGL 用 TTF 渲染 → 首帧拉丁字形冷栅格化，贡献页面卡顿。
   **正确做法**：Latin 一并入预取队列。
2. **首绘慢误判为缓存 miss**：首帧 `refr 346ms / bmp_miss+12` 看起来像缓存未命中，但扩展日志
   加 `ctf_page_sd / ttf_fills / ttf_read` 增量后证明 `bmp_miss+0`、`ctf_sd+0 ttf_fill+0 ttf_read+0`
   —— 零 IO。差距是 LVGL 首绘 CPU 开销，须用"预热"而非"加缓存"解决（§三.6）。
3. **EMPTY（空格）≠ NOT_FOUND**：空格有合法 advance、位图尺寸 0，必须正常排版，不能当缺字。
4. **kerning 是卡死元凶**：`stbtt_GetGlyphKernAdvance()` 走 GPOS 全表字节级扫描，字体 ~32 KB
   即几万次 SD 随机访问 → "卡死"。LVGL 8 label 绘制无字距语义，**后端不调用 kerning**
   （能力保留在 adapter 仅供 benchmark）。
5. **GBK 引擎天然跳过预取**：`lvgl_font_px_of()==0` 时 `preload_label` no-op；GBK 启动门控
   `pending0==0 → drain_pct=100%`，进度条纯按 2 s 走，不预加载。
6. **端口不硬编码**：ST-Link VCP 串口号依本机分配（本机曾 COM6 / COM19 变化），脚本用
   `pyserial list_ports` 运行时枚举，禁止写死。

## 五、验证与回归方法论

1. **双构零警告**：`cmake -B build -DCMAKE_BUILD_TYPE=Debug` 与 `-B build-release -DCMAKE_BUILD_TYPE=Release`，
   均 0 warning，显式列出 FLASH/RAM_D1/RAM_D2 占比。
2. **OpenOCD 烧录**：`openocd -f openocd.cfg -c "program build-release/lvgl_oled.elf verify reset exit"`
   → `Verified OK`。
3. **首帧计时（DWT CYCCNT）**：在 `application_run()` 用 `DWT->CYCCNT` 包裹 `lv_scr_load + lv_timer_handler`，
   打印 `scr_load / refr` 耗时，并报告 glyph 缓存 `bmp_miss/hit/evict` 与 `ctf_sd/ttf_fill/ttf_read` 增量。
4. **串口抓取顺序**：**先开串口后台抓**（ST-Link VCP），再 `openocd ... reset`，否则错过启动 banner。
5. **回归脚本 `scripts/verify_font_firstload.py`**：自动开串口 →（`--flash`）烧录复位 → 抓 35 s →
   解析所有 `[PAGE] switch -> 1` → 断言 **首帧 `bmp_miss==0`、零 SD 读、`|首帧refr - warm均值| ≤ 30ms`**，
   退出码 0/1（CI 友好），支持 `--in <文件>` 离线。复跑：`python scripts/verify_font_firstload.py --flash`。
6. **NOT_FOUND 零 SD 验收**：CTF probe 对缺字码点（如 `U+1F600`）断言 SD 列 == 0。
7. **运行时计数直读（无命令接口时）**：烧录 Debug 构建 + gdb 直读 `glyph_cache.c` 静态量
   `s_hits/s_misses/s_evicts/s_free_bytes`（`x/1uw &'glyph_cache.c'::s_hits`），比串口更准。

## 六、移植提示（到 Zephyr 009 / 其它 STM32 LVGL）

- 字体引擎逻辑（`Bsp/font/*`）与 LVGL 版本弱耦合，主要依赖 `lv_font_t` 两个回调，可整体搬。
- Zephyr 下 SD 卡/FatFs 路径、SPI6/ST7789 显示对接需对齐裸机参考实现（本工程即 009 的参考）。
- RAM 预算需按目标芯片重算：200 KB 字形缓存放 RAM_D2（或等价非主力 RAM 区），避免挤占主 AXI-SRAM。
- 首帧预热依赖 `lv_disp_t::driver::flush_cb` 可被临时替换；不同 LVGL 版本 API 名可能微调。
- 验证脚本的 `[PAGE]` 日志格式依赖固件侧 DWT 计时打印，移植时同步移植该日志。
