# 003 · STM32H743 LVGL OLED 中文显示

基于 STM32H743ZIT6 + **LVGL**，在 240×240 SPI OLED（ST7789，SPI6 接口）上显示中文标签与界面。
中文字库存放于 SD 卡 `1:/SYSTEM/FONT/`（GBK 点阵字库），经 FatFs 读取并由 GBK→Unicode 映射驱动 LVGL 字体渲染。
调试串口 USART1（PA9/PA10）。

> 需求与验收详见本目录 `prompter.md`。本工程亦作为 Zephyr + LVGL 项目（009）的裸机**参考实现**，用于对齐 SPI6 / ST7789 / 字库加载路径。

---

## 1. 硬件与接口

| 项 | 说明 |
|----|------|
| MCU | STM32H743ZIT6 |
| 时钟 | HSE 外部无源晶振 25 MHz（`HSE_VALUE=25000000`） |
| 显示 | 240×240 SPI OLED（ST7789，SPI 接口） |
| 存储 | 板载 microSD（SDIO）：中文字库 + FatFs |
| 调试串口 | USART1 PA9/PA10，115200 8N1 |
| 调试 | SWD + ST-Link；SVD = `STM32H743.svd` |

---

## 2. 工程结构

```
003.stm32h743_lvgl_oled/
├── Core/          startup_stm32h743xx.s / system_stm32h7xx.c / main.c
│                 stm32h7xx_hal_msp.c / stm32h7xx_it.c / syscalls.c
├── Bsp/          drv_spi_oled.c / drv_oled_fonts.c / drv_oled_text.c
│                 drv_sdio.c / drv_rtc.c / disk_interface.c
│                 lv_port_disp.c（LVGL 显示对接）/ lv_font_gbk.c / lv_gbk_map.c（GBK 字体）
│                 lv_font_cfg.h / lv_font_provider.c（引擎切换）
│                 font/           ← CTF + TTF 字体引擎（见第 8 节）
│                   ├── ctf_format.h      CTF v1 格式定义（与 PC 端唯一真源对齐）
│                   ├── ctf_reader.c/.h   三级直接寻址，NOT_FOUND 语义
│                   ├── blkcache.c/.h     通用 LRU 块缓存
│                   ├── ttf_reader.c/.h   TTF 随机访问（f_lseek+f_read，经块缓存）
│                   ├── stb_adapter.c/.h  stb_truetype 零修改接入 ttf_reader
│                   └── lvgl_font.c/.h    lv_font_t 后端 + 内置字体 fallback
├── Application/  app_main.c / app_ui.c（LVGL 界面）
├── Drivers/      CMSIS + STM32H7xx HAL
├── third_party/  lvgl / FatFs
├── tools/
│   ├── ttf2ctf/        PC 端 TTF → CTF 索引生成器（见 tools/ttf2ctf/README.md）
│   └── host_test/      在 PC 上编译运行固件真实源码的验收测试
├── cmake/        gcc-arm-none-eabi.cmake
├── CMakeLists.txt          # 普通 CMake（无 CMakePresets）
├── STM32H743ZITX_FLASH.ld  # 根级链接脚本
├── openocd.cfg             # 根级，stlink + swd
├── STM32H743.svd
└── build_oneclick.bat      # 单工程一键编译
```

> 模块化约定：应用逻辑 `Application/`、用户驱动 `Bsp/`、HAL `Drivers/`、第三方 `third_party/`。

---

## 3. 开发流程

1. 先实现 **SD 卡读取**（FatFs）。
2. 再实现 **OLED 显示输出**（SPI 驱动）。
3. 集成 **LVGL**，实现基于 LVGL 的显示应用与 SD 卡 GBK 字库渲染。

---

## 4. 构建与运行

### 一键编译
- **单工程**：双击 `build_oneclick.bat`
  - 检查工具 `cmake / ninja / openocd / arm-none-eabi-gcc`；检查根目录 `Drivers`、`third_party`
    （缺失提示从 `..\support_tools\env_support_for_stm32h743.zip` 解压/拷贝）；
  - 流程 `configure → clean → build`；所有出口 `pause` 停留查看。
- **全仓库**：仓库根目录双击 `build_all.bat`，顺序编译全部 14 个工程；失败暂停等你回车后继续，末尾输出汇总。

### 手动（普通 CMake，无预设）
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target clean
cmake --build build
```
产物：`build/lvgl_oled.elf`（同时生成 `.hex` / `.bin` 与 `.map`）。

### 约束
- **零警告**：`-Wall -Wextra`，构建应 0 warning。
- **编码**：源码为 UTF-8 字面量（`-finput-charset=UTF-8`），字库内部再转 GBK 索引；**切勿**改写 `-fexec-charset`。
- **HAL 模板排除**：`*_template.c` 已在 CMake 中排除，避免与自定义 `HAL_MspInit` / `HAL_InitTick` 冲突。

---

## 5. 调试与烧录

- **VSCode（cortex-debug）**：`.vscode/launch.json` 用裸工具名，`configFiles`/`svdFile` 指向根级 `openocd.cfg` 与 `STM32H743.svd`，`preLaunchTask:"build"`（F5 先编译再调试）。
- **命令行烧录**：
  ```bash
  openocd -f openocd.cfg -c "program build/lvgl_oled.elf verify reset exit"
  ```
- **运行日志 / 交互**：调试串口 PA9/PA10（115200 8N1）。

---

## 6. 验收

- ✅ 仿真确定 OLED 上显示的界面符合预期（中文标签正确渲染、无乱码）。
- ✅ **CTF 字体引擎上板验收**（2026-09-03，Release 固件，COM19）：
  Debug / Release 双构 0 warning；CTF 索引 + TTF 引擎启动正常；
  缺字 `NOT_FOUND` 的 SD 读次数为 **0**，UI 不显示该字符也不报错；
  英文 / 数字经内置 Montserrat 正常显示。详见 **[8.12 板级自检实测](#812-phase-8b板级自检实测已完成)**。
- ✅ **字体页首帧 200 ms 卡顿消除**（2026-09-04，Release 固件，COM19）：
  根因定位为 **LVGL 首绘一次性 CPU 开销**（样式/排版/draw-task 构建，非字库缓存 miss、非 SD 读），
  并以"抑制 flush 的离屏预热"把首绘成本挪到启动加载页背后。回归脚本 `scripts/verify_font_firstload.py`
  实机复跑 **PASS**：首帧 `129085 us` ≈ 二次 `129101 us`，差距归零；首帧 `bmp_miss==0`、零 SD 读。
  详见 **[8.17 字体页首帧 200 ms 卡顿消除](#817-字体页首帧-200-ms-卡顿消除本轮新增)**。

---

## 7. 常见问题

| 现象 | 根因 / 处理 |
|------|-------------|
| 中文乱码 / 方框 | 确认源码 UTF-8、`lv_font_gbk.c` / `lv_gbk_map.c` 映射正确、SD 字库路径 `1:/SYSTEM/FONT/` 存在且可读 |
| 编译报 `HAL_InitTick` 重复定义 | 已排除 `*_template.c`；确认仅 `stm32h7xx_hal_msp.c` 提供 Msp/时基 |
| 改 `.ld` 后 ninja 报 `no work to do` | 链接脚本未触发重配置 → 重跑 `cmake -S . -B build ...` 或 `clean` |
| OLED 不亮 | 核对 SPI 引脚/时钟、`drv_spi_oled.c` 初始化时序与 ST7789 复位/背光线 |
| 显示偏移 / DMA 异常 | 参考 009 工程的 SPI6/ST7789/字库对齐结论，保持刷新缓冲与 DCache 一致性 |

---

## 8. 字体系统：CTF 索引 + TTF 块缓存（重构进行中）

### 8.1 问题

原来的做法把 SD 卡上的 TTF 直接交给 `stb_truetype` 流式读取：

```
LVGL ─► stb_truetype ─► f_lseek() ─► f_read() ─► SD 卡
```

后果是**每读一个字节就是一次 seek + 一次 1 字节读**。一旦进入
`stbtt_GetGlyphKernAdvance()` → `stbtt__GetGlyphGPOSInfoAdvance()`，stb 会逐字节扫
`GPOS` 表（鸿蒙字体 ~32 KB），于是出现几万次 SD 随机访问 —— 表面上就是"卡死"。

### 8.2 Phase 1 分析结论（已完成）

| 项 | 现状 |
|----|------|
| MCU | STM32H743ZIT6，Cortex-M7，无外部 SDRAM，AXI-SRAM 512 KB @ `0x24000000` |
| LVGL | **8.3.11** |
| stb_truetype | **v1.26htcw**（LVGL fork，带 `STBTT_STREAM` 流式支持） |
| FatFs | `third_party/FatFs`，盘符 `1:` |
| RTOS | 裸机（无 FreeRTOS 参与字体路径） |
| DCache | **开启**（I/D Cache 均开，`main.c:48` `SCB_EnableDCache`）；MPU Region0 = AXI-SRAM 512 KB cacheable/write-back；SDIO 走轮询（非 DMA）无一致性问题。缓存/MPU 架构原则见 [§9.6](#96-缓存mpu-架构原则与踩坑) |
| 卡死根因 | `stbtt_GetGlyphKernAdvance()` 走 GPOS 全表字节级扫描 × 流式每次 1 字节 → SD 事务爆炸 |
| LVGL 是否需要 kerning | **不需要**。LVGL 8 的 label 绘制按单字形 advance 排版，没有字距语义；kerning 只会白白付出 GPOS 扫描代价 |

结论：自研字体后端**不调用 kerning**；kerning 能力保留在 adapter 里，仅用于显式调用与
基准对比。

### 8.3 Phase 2：CTF 索引格式与 ttf2ctf 工具（已完成）

CTF = **磁盘索引**：只记录 `Unicode → glyph_id / glyf 偏移 / 长度 / 度量 / 类型`，
不含 bitmap、不含 glyf 副本。采用三级直接寻址：

```
Unicode ─► 一级索引(plane, 8 B) ─► 页索引(page, 40 B, 256-bit 位图) ─► Entry(24 B)
```

- 位图位为 0 = 字体没有这个字 → **立即 NOT_FOUND，一次 SD/TTF 访问都没有**
- 位图位的 popcount rank = entry 在页内下标 → 查找 O(1)，无遍历、无二分

工具与完整格式说明见 **[`tools/ttf2ctf/README.md`](tools/ttf2ctf/README.md)**。

生成结果（`support_tools/sd_card/SYSTEM/`，全部 `--verify` PASS）：

| 字体 | CTF 大小 | 索引字符 | GB2312 一级覆盖 | 校验 |
|------|---------:|---------:|----------------:|:----:|
| **HarmonyOS_Sans_SC_Regular** | 693 KB | 29 063 | **100%** | PASS |
| HarmonyOS_Sans_TC × 6 字重 | 354 KB ×6 | 14 596 | 68.0% | PASS |

> **默认字体用 SC，不用 TC**：`HarmonyOS_Sans_TC` 是繁体子集，UI 里 48 个常用简体字有 23 个缺字；
> `HarmonyOS_Sans_SC` 覆盖 GB2312 一级全部 3755 字。TC 作为繁体显示的可选字族保留。

### 8.4 缺字与回退策略（用户明确要求）

```
Unicode
   │
   ├─ CTF 查不到（位未置位 / MISSING / CTF 不可用）
   │        └─► 立即返回 NOT_FOUND，不碰 TTF
   │                 └─► LVGL 走 fallback
   │                          └─► 内置字体（编译在 Flash 的 Montserrat）
   │                                   └─► 英文 / 数字正常显示
   │
   └─ CTF 命中 ─► glyph_id ─► TTF Reader ─► 16 KB×4 块缓存 ─► stb ─► 位图
```

三条硬性规则：

1. **缺字不显示、也不报错**：`CTF_NOT_FOUND` 是正常业务状态，LVGL 交给 fallback 处理，
   不打印 error、不刷屏。
2. **英文/数字始终可用**：fallback 指向 LVGL 内置字体（内部数据），即使 SD 未挂载、
   CTF 缺失或版本不符，ASCII 与数字照常显示，UI 不会变成一片空白。
3. **整个 TTF / 整个 CTF 都不进 RAM**：无 SDRAM、内部 RAM 仅 1 MB，运行时只有
   CTF 头缓冲 / 页缓冲 / entry 缓冲 / TTF 块缓存 / 字形临时缓冲。

`EMPTY`（如空格）与 `NOT_FOUND` 严格区分：空格有合法 advance、位图尺寸为 0，必须正常排版。

### 8.5 后续 Phase

| Phase | 内容 | 状态 |
|-------|------|------|
| 1 | 现状分析（LVGL / stb / FatFs / SD / DCache / RAM） | ✅ 已完成（见 8.2） |
| 2 | CTF 格式 + PC 端 `ttf2ctf`（生成 / 校验 / 覆盖率） | ✅ 已完成（见 8.3） |
| 2.5 | 生成鸿蒙 CTF 并校验 | ✅ 已完成（7 个字体全部 PASS） |
| 3 | MCU 端 `ctf_reader`（多级直接寻址 + NOT_FOUND 语义） | ✅ 已完成（见 8.7） |
| 4/5 | `ttf_reader` + 16 KB×4 块缓存（LRU、跨块合并、命中率统计） | ✅ 已完成（见 8.8） |
| 6 | `stb_adapter`（`STBTT_STREAM` 接到 ttf_reader，不改算法） | ✅ 已完成（见 8.9） |
| 7 | LVGL 字体后端 + 内置字体 fallback | ✅ 已完成（见 8.10） |
| 8 | 主机验收 + 双构零警告 + 烧录 + COM19 自检 | ✅ 已完成（见 8.11 / 8.12） |

### 8.6 构建基线（Debug + Release 双构，均 0 warning）

| 配置 | FLASH | RAM_D1 (AXI-SRAM) |
|------|--------------:|------------------:|
| Debug (`build/`) | **340 864 B / 2 MB（16.25%）** | **318 736 B / 512 KB（60.79%）** |
| Release `-O2` (`build-release/`) | **344 824 B / 2 MB（16.44%）** | **318 744 B / 512 KB（60.80%）** |

```bash
cmake -S . -B build         -G Ninja -DCMAKE_BUILD_TYPE=Debug   -DLV_FONT_ENGINE=2
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLV_FONT_ENGINE=2
```

RAM 预算构成（全部静态 BSS，不进 LVGL 堆）：

| 缓冲 | 大小 | 用途 |
|------|-----:|------|
| `s_ttf_cache` | 64 KB | TTF 块缓存 16 KB × 4 |
| `s_bmp_pool` | 32 KB | 已栅格化字形位图池（176 slot，LRU 整体回收） |
| `s_ctf_cache` | 4 KB | CTF 块缓存 512 B × 8 |
| `s_l1_shadow` | 2 KB | CTF 一级索引常驻（256 × 8 B，查找第一跳免 SD） |
| `stb_adapter` arena | 36 KB | 栅格化临时内存（bump + 批量释放） |

AXI-SRAM 仍余 ~200 KB。已启用 `LV_FONT_MONTSERRAT_12/16/24/32` 作为缺字 fallback 的"内部数据"
（原本 4 档全为 0，UI 的 ASCII/数字在 CTF 不可用时无字可退）。

---

### 8.7 Phase 3：MCU 端 `ctf_reader`（已完成）

`Bsp/font/ctf_reader.c/.h`。查找路径严格三级直接寻址，**无遍历、无二分**：

```c
plane = (u >> 16) & 0xFF;     /* 一级索引 8 B：page_offset / page_count   */
page  = (u >>  8) & 0xFF;     /* 页索引 40 B：entry_offset + 256-bit 位图 */
bit   =  u        & 0xFF;     /* 位图位 → popcount rank → entry 下标      */
```

- **位为 0 → 立刻返回 `CTF_NOT_FOUND`，一次 SD 都没有碰**。这是"缺字零开销"的核心。
- 一级索引（2 KB）在 `ctf_open()` 时整体读入 `s_l1_shadow`，第一跳免 I/O。
- 页数组**必须是稠密的**（含空洞页也要占位），否则 `page_no` 无法直接索引。
- `CTF_NOT_FOUND` 是正常返回值，**不打印 error、不刷屏**；只有 `CTF_ERR_*` 才是故障。

边界检查统一两段式，禁止 `offset + length > size` 的溢出写法：

```c
if (entry_at > c->size)                     { return CTF_ERR_RANGE; }
if (CTF_ENTRY_SIZE > (c->size - entry_at))  { return CTF_ERR_RANGE; }
```

`ctf_entry_is_empty()` 区分空格类字形：有合法 advance、位图尺寸为 0，必须正常排版，
**不能**当成 `NOT_FOUND`。

`ctf_verify_ttf()` 把 CTF 与其 TTF 绑定：必比 `ttf_size`，可选 CRC32（表首次使用时构建，1 KB scratch）。

### 8.8 Phase 4/5：`ttf_reader` + 块缓存（已完成）

`Bsp/font/blkcache.c/.h` + `Bsp/font/ttf_reader.c/.h`。

`blkcache` 是通用 LRU 块缓存（`BLKCACHE_MAX_BLOCKS 8`），能力：

| 能力 | 说明 |
|------|------|
| 跨越合并 | 一次请求跨 N 个连续块时，一次性把 N 块填满再拷出，不重复 seek |
| 短块处理 | 文件尾不满一块时，用 `f_read()` 的 `*br` 作为该块**有效长度**，尾块仍可命中 |
| LRU 替换 | `age[]` 时间戳 + `clock`，淘汰最久未用 |
| 统计 | `hits / misses / fills / fill_bytes`，`fills` 即真实 `f_read()` 次数 |

`ttf_reader` 把 FatFs 调用收口到**唯一一处**（`ttf_fill()`）：

```c
f_lseek(&r->f, offset); f_read(&r->f, dst, len, &br);   /* 全工程仅此一处 SD 随机读 */
```

统计字段：`hits / misses / fills / bytes`，另加 `seek_cycles / read_cycles`（DWT CYCCNT）
用于定位 SD 耗时。

### 8.9 Phase 6：`stb_adapter`（已完成）

`Bsp/font/stb_adapter.c` 单独编译 `stb_truetype_htcw.h`（LVGL fork，v1.26htcw），
**stb 源码零修改**，只用宏把 `STBTT_STREAM` 接到 `ttf_reader`：

```c
#define STBTT_STREAM_TYPE          stb_stream_t *
#define STBTT_STREAM_SEEK(s, x)    stb_stream_seek((s), (uint32_t)(x))
#define STBTT_STREAM_READ(s, x, y) stb_stream_read((s), (x), (uint32_t)(y))
```

于是 stb 那个"每字节一次 seek + 一次 1 字节读"的流式访问模式被彻底改造：

- **seek 变成一次 store**（只更新 `s->pos`），零 I/O；
- **read 走 16 KB 块缓存** —— 一个 8 MB TTF 里任意位置的字节，命中缓存后是纯内存拷贝。

读失败时把目标缓冲清零，避免 stb 拿到栈上垃圾去解偏移。

**Bump arena**（36 KB，独立于 LVGL 堆）：stb 栅格化一个字形会分配 vertices / edges /
active-edges 三块临时内存，返回前全部释放。用 bump 分配器 + `s_live` 计数，
全部 free 后整体 `s_used = 0`；下次进门前若 `s_live != 0` 说明上次溢出，直接重置恢复。
实测 **peak 5 920 B**（最坏字形 U+9F9F 鼟 30 画），溢出 0 次。

**box 一致性自检**：`ctf_box_from_entry()`（CTF entry 算出的框）与
`stbtt_GetGlyphBitmapBox()`（stb 自己算的框）逐字比对，不一致则 `s_box_mismatch++`。
主机与实机**均为 0**，证明 CTF 度量与 stb 栅格化完全对齐。

`stb_adapter_kerning()` 保留供 benchmark，**LVGL 后端不调用** —— LVGL 8.3.11 的 label
绘制没有字距语义，且 kerning 会去逐字节扫 GPOS（32 KB），正是原卡死的根因。

### 8.10 Phase 7：LVGL 字体后端（已完成）

`Bsp/font/lvgl_font.c/.h`，实现 `lv_font_t` 的两个回调。

**`ctf_get_glyph_dsc()` —— 纯索引运算，零 TTF 访问：**

```c
rc = ctf_find_unicode(&s_ctf, letter, &e);
if (rc != CTF_OK) {
    s_missing++;
    return false;          /* → LVGL 走 fallback，不碰 TTF、不打日志、不出占位图 */
}
if (ctf_entry_is_empty(&e)) {            /* 空格：合法 advance，box 为 0 */
    dsc->adv_w = adv; dsc->box_w = 0; dsc->box_h = 0; dsc->bpp = 0;
    return true;
}
ctf_box_from_entry(&e, fd->scale, &ix0, &iy0, &ix1, &iy1);
dsc->adv_w = adv; dsc->box_w = ix1-ix0+1; dsc->box_h = iy1-iy0+1;
dsc->ofs_x = ix0; dsc->ofs_y = -iy1; dsc->bpp = 8;
```

**`ctf_get_glyph_bitmap()`**：查位图池 → miss 才查 CTF entry → EMPTY 返回 NULL →
算 box → 池分配 → `stb_adapter_render()` → 提交。位图池 32 KB / 176 slot，
用完按 LRU 整体 flush 后复用。

**缺字三条硬性规则的落点：**

| 规则 | 落点 |
|------|------|
| CTF 查不到 → 立刻 `return false`，不碰 TTF | `ctf_get_glyph_dsc()` 首屏判断 |
| UI 不显示该字符、不画占位框 | `lv_conf.h`：`LV_USE_FONT_PLACEHOLDER 0`（文本合拢） |
| 英文/数字用内部数据正常显示 | `s_font[i].fallback = &lv_font_montserrat_{12,16,24,32}` |

编译期断言保证 4 档 Montserrat 真的开着，否则 `#error`。

**引擎切换**（`Bsp/lv_font_cfg.h`，CMake 变量 `LV_FONT_ENGINE`）：

| 值 | 引擎 |
|:--:|------|
| **2** | **CTF 索引 + 原 TTF（默认）** |
| 1 | 原 TTF 直读（旧方案，慢） |
| 0 | GBK 点阵（原始方案，保留） |

`lv_font_provider_locate()` 先试配置名，找不到就扫目录取第一个 `.ctf`，
再把 `.ctf` 换后缀成 `.ttf` 并 `f_open()` 确认存在。

### 8.11 Phase 8a：主机端验收（已完成）

`tools/host_test/` 在 PC 上用 stdio shim（`host_shim/ff.h`、`main.h`）替换 FatFs，
**编译固件真实源码**（`blkcache.c` / `ttf_reader.c` / `ctf_reader.c` / `stb_adapter.c`）
去跑真实 `.ctf`/`.ttf`，在上板前把逻辑错误挡住。

5 组测试：开文件+头校验+`verify_ttf`+stb 初始化 / 定点查表（8 命中 + 6 缺字）/
**全 Unicode 0..0xFFFF 遍历** / `ttf_reader` 边界与跨块 / 栅格化（ASCII art 目视）+ 计数器。

```bash
gcc -std=c11 -O2 -I Bsp/font -I tools/host_test/host_shim \
    -I third_party/lvgl/src/extra/libs/tiny_ttf \
    -o tools/host_test/build/ctf_host_test.exe \
    tools/host_test/ctf_host_test.c Bsp/font/{blkcache,ttf_reader,ctf_reader,stb_adapter}.c -lm
# 用法：<font.ctf> <font.ttf>
tools/host_test/build/ctf_host_test.exe \
    ../support_tools/sd_card/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ctf \
    ../support_tools/sd_card/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ttf
```

Windows 一键入口：`tools/host_test/run_host_test.bat`（需 gcc 在 PATH），
不带参数时自动扫描 `../support_tools/sd_card/SYSTEM/` 下所有 `.ctf + .ttf` 对。

结果（7 个字体全绿）：

| 字体 | 结果 |
|------|------|
| HarmonyOS_Sans_SC_Regular | **81 passed, 0 failed** |
| HarmonyOS_Sans_TC × 6 字重（Regular/Black/Bold/Light/Medium/Thin） | **35 passed, 0 failed ×6** |

代表性输出：

```
U+4E2D @24px box 21x24 ofs(2,-2) adv 24 px ink 207/504 px   → 清晰"中"
U+6587 @24px box 24x24 ofs(0,-2) adv 24 px ink 214/576 px   → 清晰"文"
U+0020 empty glyph, advance 4 px at size 16                 → 空格正常排版
ttf hit=1001 miss=20 f_read=20 bytes=314888
arena peak=3976 B fails=0     ctf lookups=22 not_found=3 io_errors=0
box mismatches = 0
```

> **块缓存的量化证据**：整轮测试（含 65 536 个码位全表遍历）只发了 **20 次 `f_read()`**；
> 同等工作量下未加缓存的 stb 流式访问是**几十万次** SD 事务 —— 这就是原卡死的根因与解法验证。

这一轮主机测试抓到并修掉 4 个真问题：
1. `ctf_reader.c` 的 `entry_at` 未初始化（我改两段式边界时误删赋值，编译器先报了警告）；
2. `host_shim/ff.h` 把 EOF 短读当错误（真实 FatFs 是 `FR_OK` + `*br < btr`），导致文件最后一块读不出来；
3. 尾块断言期望值写错（误用 `size % BLOCK_SIZE`）；
4. `ttf2ctf.py --info` 会无条件重建索引（大字体要跑几分钟），改为 `.ctf` 输入直接 `load()`。

### 8.12 Phase 8b：板级自检实测（已完成）

烧录 Release 固件，COM19 @ 115200 抓取（`scripts/serial_capture.py`）：

```
[LVGL] v8.3.11, 96 KB heap
[FONT] engine: CTF index + TTF
[FONT]   ctf  1:/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ctf
[FONT]   ttf  1:/SYSTEM/HarmonyOS_Sans_SC/HarmonyOS_Sans_SC_Regular.ttf
[UI  ] built in 66 ms
[UI  ] first frame in 613 ms

[CTF ] on-target probe, 12 code points
[CTF ]  U+xxxxx  px  src adv  box     ofs      cold us  warm us   ink   SD
[CTF ]  U+04E2D   24  CTF   24  21x24    +2,-2      2574        0    207    1
[CTF ]  U+06587   24  CTF   24  24x24    +0,-2      6099        0    214    2
[CTF ]  U+091D1   32  CTF   32  31x30    +1,-2      6547        0    506    2
[CTF ]  U+09F9F   32  CTF   32  31x31    +1,-2      6054        0    480    2
[CTF ]  U+00041   16  CTF   11  12x13    +0,0       4533        0     67    2
[CTF ]  U+00030   16  CTF    9  10x14    +0,-1      6350        0     75    2
[CTF ]  U+00020   16  EMP    4   0x0     +0,+0          2        2      0    0
[CTF ]  U+000E9   32  CTF   18  17x26    +1,-1      4971        0    196    2
[CTF ]  U+0FF0C   24  CTF   24   5x8     +3,-4      6538        0     21    2
[CTF ]  U+1F600   24   --     0   0x0     +0,+0          0        0      0    0
[CTF ]  U+0F8FF   24   --     0   0x0     +0,+0          1        1      0    0
[CTF ]  U+00378   24   --     0   0x0     +0,+0          1        1      0    0
[CTF ] found 9, NOT_FOUND 3 (of which Latin fallback 0)
[CTF ] NOT_FOUND cost 0 SD reads (must be 0): PASS
[CTF ] probe: lookup 3592, not-found 6, io-err 0
[CTF ] probe: ttf hit 8602, miss 104, f_read 104, 1703936 B
[CTF ] probe: bitmap hit 67, miss 78, flush 0, 3756 B held
[CTF ] probe: stb arena peak 5920 B, overflow 0
```

**关键结论：**

| 项 | 实测 | 判定 |
|----|------|:----:|
| `NOT_FOUND` 的 SD 读次数 | **0** | ✅ 硬性要求达成 |
| 缺字时的 UI 表现 | 文本合拢，无占位方框，无 error 刷屏 | ✅ |
| EMPTY（空格）独立语义 | `adv=4 px`、`box 0x0`、正常排版 | ✅ |
| 命中字形的度量 | 与主机端逐字节一致 | ✅ |
| stb arena | peak 5 920 B / 36 KB，overflow 0 | ✅ |
| LVGL 堆 | 91 176 B free of 98 304 B，frag 1% | ✅ |
| 首屏 | UI 构建 66 ms、首帧 613 ms（含 SD 挂载 + 字库加载） | ✅ |

### 8.13 已知问题与后续（性能，本轮暂不处理）

冷栅格化 **avg 4 852 us / worst 6 547 us**，对首屏 12 个字就是 ~58 ms。
耗时拆分探针已经在 `ttf_reader` 里埋好（`seek_cycles` / `read_cycles`），
下一步按此定位：

1. `ttf_fill()` 每次 miss 都是 `f_lseek()` + `f_read()`，SDIO 命令开销远大于数据本身；
   16 KB 块读 104 次 = 1.7 MB，而实际字形数据只有几十 KB —— **读放大严重**。
2. 优先方向：把字形 `glyf` 数据在生成 CTF 时**按字预取打包**（CTF v2：entry 直接给出
   连续的 glyf 字节流），或提高块缓存关联度、加"顺序预读"。
3. 其次：`f_read()` 走的是 FatFs 单次读，可评估改 `f_read` 大块 + `FA_FASTSEEK`。

> 本轮目标（CTF 生成 + NOT_FOUND 零开销 + 英文数字正常显示）已全部达成并通过实机验证。

### 8.14 索引前部常驻 RAM（resident index，本轮新增）

**需求（用户原话）**：使用 ctf-ttf 字库时，初始化 sd 卡完成后，先把 ctf 中多级映射中第一部分
关于 ttf 信息存储地址的偏移映射搬运到 ram 里缓存，确定占用空间多少，如果满足第一层级就一直
保存在 ram 中，增加效率。

**落点**：`ctf_load_resident()` 在 `ctf_open()` 之后、`ctf_verify_ttf()` 之前调用（固件侧
`lvgl_font_engine_init()` 的调用顺序一致）。

**常驻三层（不加载 entry 表、不加载 TTF）**：
- ① TTF 表目录（table index）：`N×12 B`，鸿蒙 SC/TC 实测 `N=11`（132 B），上限
  `CTF_TABLE_RESIDENT_MAX=64`（768 B）；`ctf_find_table(tag)` 后续可按名查 glyf/loca/cmap/hmtx 偏移。
- ② L1 平面表（已在 `ctf_open()` 搬，2 KB）。
- ③ 页表（page table）：`page_index_count × 40 B`。**仅当整张页表能放进页池才全常驻**
  （all-or-nothing），否则退化走块缓存、绝不部分常驻（部分常驻要在每个 lookup 加分支判断，得不偿失）。

**占用核算（实测）**：鸿蒙 SC/TC 均为 `256 页 × 40 B = 10240 B` 页表 + L1 2048 B + 表目录 ~132 B
≈ **12.4 KB**。固件预留页池 `CTF_PAGE_POOL_PAGES=288 × 40 = 11520 B`（11.25 KB），留余量给含少量
增补平面页的字体；相对 AXI-SRAM 剩余 ~200 KB 是划算交易。

**安全**：`page_pointers_ok()` 在加载时一次性校验所有非空平面的 `page_offset` 落在池内且整段 run
不越界，使查找第二跳的 `page_offset - page_index_offset` 减法**永不溢出**，无需每查一次做边界判断。

**效率证据（可观测）**：
- `ctf_find_unicode()` Step 2 三源优先级：单条页缓存 → **常驻页表 RAM 命中**（`page_ram_hits++`）
  → 卡路径（`page_sd_reads++`）。
- 页表常驻后，**缺字判定（bit 测试）完全在 RAM 完成，lookup 第二跳零 `f_read`**。boot banner 打印
  `pages resident: 10240 B of a 11520 B pool -> NOT_FOUND costs 0 SD reads`；运行时 `page from RAM`
  非零、`page from card 0`。

**新增接口 / 统计**：`ctf_load_resident()`、`ctf_resident_info()`、`ctf_resident_bytes()`、
`ctf_find_table()`、`ctf_page_stats()`；`ctf_resident_t` 报告 table/L1/page 三段字节数与
`page_resident` 标志；`lv_font_provider.c::log_resident()` 启动打印占用；`app_main.c::log_ctf_stats()`
运行时打印 `page from RAM / from card`。

**主机端验收（`tools/host_test`，PC 编译固件真实源码）**：新增 `[2c]` 等价性测试——常驻页表路径与
降级走卡路径对 `0..0xFFFF` 全表查找结果（rc/glyph_id/advance/empty）逐字一致；并断言常驻路径
`page_ram_hits>0` 且 `page_sd_reads==0`。**7 字体全部通过（SC 88 项 + TC×6 各 42 项 = 340 项全绿）**。

**构建基线（本轮双构，0 warning）**：
| 配置 | FLASH | RAM_D1 | 说明 |
|------|------:|-------:|------|
| Debug   | 341896 B (16.30%) | 332152 B (63.35%) | 较 CTF v1 基线 RAM +~13 KB |
| Release | 345996 B (16.50%) | 332160 B (63.35%) | 页池 11.5 KB + `ctf_resident_t`/统计字段 |

> 实机验证已通过：烧录 `build-debug/lvgl_oled.elf`（**Verified OK**），COM19（ST-Link VCP）抓取启动 banner 确认
> `index in RAM 12420 B = tables 132 + L1 2048 + pages 10240`、`pages resident: 10240 B of a 11520 B pool -> NOT_FOUND costs 0 SD reads`；
> CTF probe 中 `U+1F600` / `U+0F8FF` 等缺字 SD 列 = 0，证明缺字判定零 SD 访问。主机端 `[2c]` 等价性 7 字体 340 项全绿，
> 双构均 0 warning，端到端闭环完成。

---

### 8.15 TTF Glyph Cache：独立 200 KB 栅格化字形缓存（LRU + 页钉扎 + 异步预取，本轮新增；容量 160 KB → 200 KB）

**需求（用户原话）**：TTF Glyph Cache 为 TTF 字库提供独立 Glyph Cache，Bitmap Cache 容量为 200 KB（用户本轮要求从 160 KB 进一步增至 200 KB，仍放 RAM_D2）。
Glyph Cache 按 Unicode/Glyph ID 管理已栅格化的字符；再次使用时优先从 Cache 获取，避免重复 TTF 解析与栅格化。
空间不足时采用 LRU 淘汰最久未用 Glyph 并回收空间；淘汰**不得影响正在使用或当前页面固定字符**。
创建页面时若用 TTF 字库则扫描文本去重 Unicode、建立预栅格化队列（GBK 字库跳过）；预取尽量后台异步，避免阻塞页面创建。
页面切换优先预取当前页，旧页由 LRU 自动淘汰；实际绘制优先访问 Glyph Cache，Cache Miss 再走正常 TTF 冷栅格化。

**落点**：全新模块 `Bsp/font/glyph_cache.c` + `glyph_cache.h`，替换原 `lvgl_font.c` 的 32 KB 定长"满即整体回绕"
位图池（`bmp_*` 整段删除）。逻辑侧收口于 CTF 后端（`LV_FONT_ENGINE=2`），GBK 引擎天然跳过预取。

**存储与放置**：单一 **200 KB 池 `s_pool` 放 `.ram_d2` 段（0x30000000，288 KB 空闲）**，栅格化后 8-bpp 字形位图按
`(unicode, px)` 索引。变长字形（12px≈数百 B vs 32px≈数千 B）用**边界标记空闲堆（free-list + 偏移排序 + 前后合并）**
管理，精确回收。`s_pool` 仅 CPU 读取（LVGL 绘制时），SPI6 DMA 不触碰 → **无 cache 一致性问题**。
> 为何不放 RAM_D1：若 200 KB 进 AXI-SRAM（512 KB 主力），使用率将逼近 ~90% 危险区；挪到 RAM_D2 后
> RAM_D1 维持 ~61.7%，200 KB 独占 RAM_D2 69.44%，两者皆安全（RAM_D2 共 288 KB，200 KB 占比未超 70%，余量充足）。

**LRU + 页代次（epoch）钉扎**：每个 entry 带 `lru` 访问戳与 `epoch` 页面代次。
- 查命中：`e->epoch = s_epoch; e->lru = ++s_lru` → **当前页/正在用的字形被持续抬升，永不被淘汰**（满足"不影响正在使用/当前页固定字符"）。
- 淘汰：仅在 `glyph_cache_insert` 分配失败（堆满）时触发 `while` 循环，只选 **`epoch != s_epoch` 且 `lru` 最小** 的
  victim `heap_free()` + `used=0` + `s_evicts++`，直到腾出空间；无 victim 可淘汰则 insert 返回 NULL（绘制跳过该字而非死锁）。
- 切页：`lv_font_provider_on_page_shown()` → `glyph_cache_bump_epoch()` 抬升代次，旧页字形落到当前代次之后，压力下由 LRU 自动回收，新页保持钉扎。

**异步预取（后台 LVGL timer）**：
- 建页 `mk_label()` → `lv_font_provider_preload_label()`：对 label 文本 UTF-8 扫描去重入队（`preload_enqueue`，
  Latin<0x80 跳过、已缓存/已入队跳过、队列满则放弃）。
- `preload_timer_cb` 每 **30 ms 处理 4 字**，调 `ctf_find_unicode → ctf_box_from_entry → glyph_cache_insert →
  stb_adapter_render` 预栅格化；队列清空即自删 timer。**GBK 引擎**因 `lvgl_font_px_of()==0` 直接 no-op，不预取。

**绘制路径（lookup 优先）**：`ctf_get_glyph_bitmap()` 先 `glyph_cache_lookup()`，命中即返回；Miss 走
`ctf_find_unicode → glyph_cache_insert → stb_adapter_render` 冷栅格化后返回池指针（返回的正是刚插入的 entry，不会被 LRU 误回收）。

**文件清单**：`Bsp/font/glyph_cache.{c,h}`（新增）、`Bsp/font/lvgl_font.c`（改写：删 `bmp_*` 池、加 lookup/insert/prefetch/epoch）、
`Bsp/font/lvgl_font.h`（加 `lvgl_font_px_of/preload_*/on_page_shown` 声明）、`Bsp/lv_font_provider.{c,h}`（引擎感知透传）、
`Application/app_ui.c`（`mk_label` 预取 + `take_switch` 抬升 epoch）、`CMakeLists.txt`（加 `glyph_cache.c`）。
统计复用 `lvgl_font_get_stats()` 的 `bmp_hits/bmp_misses/bmp_flushes(=evicts)/bmp_bytes`。

**构建基线（本轮双构，0 warning）**：
| 配置 | FLASH | RAM_D1 | RAM_D2 |
|------|------:|------:|------:|
| Debug   | 344536 B (16.43%) | 323624 B (61.73%) | **200 KB (69.44%)** |
| Release | 348660 B (16.63%) | 323632 B (61.73%) | **200 KB (69.44%)** |

**实机验收（OpenOCD 烧录 Verified OK + openocd/gdb 直读运行时计数）**：
- 自动双页切换 demo（5 s/次）运行 20 s 后，直读 `glyph_cache.c` 静态计数：
  `s_hits=1319` / `s_misses=210` / `s_evicts=0` / 缓存占用 **~31 KB** / `s_epoch=3` / `s_lru=1386`。
- **命中率 86%**（1319/1529）：每个独立字形仅冷栅格化一次（210 miss = 两页全部唯一 CJK 首访），其后全部命中 → TTF 解析与栅格化被彻底摊薄。
- `s_epoch=3` 证明页钉扎随切换推进；`s_evicts=0` 符合预期——两页 CJK 总量 ≈ 31 KB ≪ 200 KB，LRU 淘汰路径仅在缓存压力下触发（淘汰逻辑由 `insert` 的 victim 选择循环保证，已代码核验；触发需 >200 KB 不同字形同屏/跨页常驻）。

---

### 8.16 UI 页面分离 + 启动加载页（本轮新增）

**需求（用户原话，4 条指令中的 #1/#2）**：
1. 对 LVGL 页面代码进行分离，每个页面独立一个文件。
2. 增加启动加载页面，内容是 `Waiting...` 和动态进度条；当字库预加载完成（不足 2 s 则至少等 2 s）后进入下一页；**GBK 不需要预加载，直接等待 2 s**，中间执行完整进度条即可。
3. 缓存增至 200 KB 仍放 RAM_D2（见 [§8.15](#815-ttf-glyph-cache独立-200-kb-栅格化字形缓存lru--页钉扎--异步预取本轮新增容量-160-kb--200-kb)）。
4. `third_party/` 与 `Drivers/` 目录禁止修改（共享第三方库，本次所有改动均在 `Application/`、`Bsp/` 自有代码与 `CMakeLists.txt`）。

**页面分离（指令 #1）**：原单文件 `app_ui.c`（533 行，含信息页 + 字体页 + 故障页 + 启动逻辑）拆分为：
| 文件 | 职责 |
|------|------|
| `Application/ui_common.{c,h}` | 共享原语：屏幕尺寸 `UI_W/UI_H/UI_PAD/HDR_H`、调色板常量、`#define UI_FONT(px) lv_font_provider_get((px))`、`ui_common_screen_create()`、`ui_mk_label()`（内部调 `lv_font_provider_preload_label` 透明预取）、`ui_mk_label_center()`、`ui_mk_separator()`、`ui_align_right()` |
| `Application/ui_page_info.{c,h}` | 信息面板页（时钟 / SD / 主板信息 / 缓存行），自建 1 Hz `ui_page_info_tick` 刷新；`ui_page_info_build()` / `ui_page_info_request_sd_refresh()` |
| `Application/ui_page_font.{c,h}` | 字体引擎状态页（7 行 CJK）；`ui_page_font_build()` |
| `Application/ui_page_boot.{c,h}` | 启动加载页；`ui_page_boot_build()` / `ui_page_boot_set(uint8_t pct)` / `ui_page_boot_set_status(const char*)` |
| `Application/ui_page_fault.{c,h}` | ASCII 故障页；`ui_page_fault_show(line1,line2,line3)`（原 `app_ui_show_fault` 改名迁移） |
| `Application/app_ui.c`（重写） | 纯编排器：页数组 `s_pages[2]` + 5 s 轮播 `page_switch_cb` + **启动门控 `boot_timer_cb`**；`app_ui_create()` / `app_ui_request_sd_refresh()` |
| `Application/app_main.c` | `app_ui_show_fault(...)` → `ui_page_fault_show(...)`（SD 挂载失败分支） |
| `CMakeLists.txt` | `PROJECT_SRCS` 在 `app_ui.c` 后追加 5 个新文件 |

**启动加载页 + 预加载门控（指令 #2）**：
- `app_ui_create()` 先**离屏**构建 info + font 两页（触发 `lv_font_provider_preload_label` 把 CJK 字形入队），
  记录 `s_boot_pending0 = lv_font_provider_preload_pending()`，再显示 Boot 页。
- 50 ms 定时器 `boot_timer_cb` 计算：
  - `time_pct = elapsed / 2000 * 100`（最小停留 2 s）；
  - `drain_pct`：**GBK 引擎恒 100%**（`s_boot_pending0==0`）；CTF = `(pending0 - pending) / pending0 * 100`（预取队列排干比例）。
  - 进度条取二者较小值 `bar = min(time_pct, drain_pct)`。
- 仅当 **`time_pct >= 100%` 且 `drain_pct >= 100%`**（即至少 2 s 且预加载已排干）才 `lv_scr_load(s_pages[0])` +
  `lv_font_provider_on_page_shown()`（epoch bump，旧页字形进入淘汰域）+ 启动 5 s 轮播 + `lv_timer_del` 自删。
- **GBK 透传**：GBK 引擎 `lv_font_provider_preload_pending()` 返回 0 → `s_boot_pending0==0` → `drain_pct=100` →
  进度条纯按 2 s 走完，满足"GBK 不预加载、直接等 2 s、跑完整进度条"。

**预加载计数 API（支撑门控）**：新增
- `Bsp/font/lvgl_font.c`：`uint32_t lvgl_font_preload_pending(void)`（返回 `s_pl_count`，预取队列剩余长度）；
- `Bsp/lv_font_provider.c`：`uint32_t lv_font_provider_preload_pending(void)`（非 CTF 引擎返回 0，GBK 透传关键）。

**约束遵守**：全程未触碰 `third_party/`、`Drivers/`；未改 TTF/CTF 文件、LVGL 核心、FatFs/SD 驱动、stb_truetype。

**构建与实机验收（OpenOCD 烧录 Verified OK + ST-Link VCP 抓启动 banner，串口号运行时枚举、本次 COM19）**：
- Debug / Release 双构 **0 warning**；RAM_D2 = 200 KB / 288 KB (69.44%)，RAM_D1 不变 61.73%（见 [§8.15](#815-ttf-glyph-cache独立-200-kb-栅格化字形缓存lru--页钉扎--异步预取本轮新增容量-160-kb--200-kb) 基线表）。
- 启动门控正确性的决定性证据（CTF 引擎，串口捕获）：
  ```
  [BOOT] done: pending0=68 pending=0 elapsed=2006 engine=2
  ```
  即 68 个 CJK 字形全部预取入队并**排干**（`pending=0`）、足等 **2 s**（`elapsed=2006`）、`engine=2`(CTF) —— 完全符合"字库预加载完成（不足 2 s 至少 2 s）后进入下一页"。
- openocd/gdb 直读 `glyph_cache.c` 静态量：`s_free_bytes=175172`（容量 204800 → 已用 ≈ 29 KB，两页字形确已落池）、`s_epoch=1`（boot 已 bump）、`s_lru=336`，印证预取字形真实装填进 200 KB 池。
- 首切字体页冷渲染 ~338 ms 为 LVGL 首建全屏 + SPI6 刷新固有开销；后续 font 页 129 ms（warm），符合预期，不影响门控正确性。

### 8.17 字体页首帧 200 ms 卡顿消除（本轮新增）

**问题（用户原话）**：「第一次加载 `ui_page_font.c` 仍然比第二次多了 200 ms 左右，表示优化未生效，优先解决这个问题」。
即字体页首帧比二次慢约 200 ms（实测首帧 ~346 ms / warm ~130 ms）。

**根因定位（实机串口 + DWT 计时 + `[MISS]` 门控日志逐码点抓取）**：
1. **首帧冷 miss 非主因**：扩展 `[PAGE]` 日志记录 `ctf_page_sd / ttf_fills / ttf_read` 增量，证明首帧
   `bmp_miss+0`、`ctf_sd+0 ttf_fill+0 ttf_read+0 us` —— 字形位图池全命中、零 SD 读。200 ms 差距**不在字库**。
2. **真正根因 = LVGL 首绘一次性开销**：一个 screen 第一次被实际绘制时，LVGL 要做样式值计算、label 排版、
   draw-task 构建等一次性 CPU 工作（首帧 324 ms / warm 130 ms，差 ~194 ms，纯 CPU、零 IO）。
3. **附带发现并修复的 Latin 预取漏洞**：`preload_enqueue()` 原本 `if (cp < 0x80) return;`（注释称"Latin 走 Montserrat 回退"）。
   但 `[MISS]` 抓到首帧拉丁字形 `U+00053('S')…` 冷 miss —— HarmonyOS CTF 索引**确实含拉丁**，LVGL 用 TTF 渲染而非 Montserrat。
   该 early-return 导致每页拉丁文本首帧冷栅格化（虽只贡献 ~22 ms，但属错误假设）。

**修复**：
- `Bsp/font/lvgl_font.c` `preload_enqueue()`：删除 `cp < 0x80` 跳过，Latin 一并预取入池（注释说明索引已含拉丁）。
- `Application/app_ui.c` 新增 `ui_warmup_pages()`：在 `app_ui_create()` 构建完两页后，**抑制显示 flush**（`dummy_flush_cb`
  吞掉帧缓冲推送、`lv_disp_flush_ready` 收尾）逐页 `lv_scr_load + lv_timer_handler` 各渲染一次，
  把"首绘一次性开销 + 字形冷栅格化"全部移到启动加载页背后（用户不可见），首个真实翻页即 warm。

**约束遵守**：仅改 `Bsp/font/lvgl_font.c`、`Application/app_ui.c`；未碰 `third_party/`、`Drivers/`、TTF/CTF/LVGL 核心/FatFs/stb。

**构建与实机验收（Release 生产态，OpenOCD Verified OK + ST-Link VCP 抓串口）**：
- Debug / Release 双构 **0 warning**；FLASH / RAM 占用不变（RAM_D2 = 200 KB / 288 KB = 69.44%，RAM_D1 = 61.73%）。
- `[UI] built in ~880 ms`（warm-up 成本从首帧前移到启动，藏在加载页后）。
- 决定性证据（Release）：
  ```
  [PAGE] switch -> 1, scr_load 8 us, refr 129069 us, bmp_miss+0 hit+118 evict+0 | ctf_sd+0 ttf_fill+0 ttf_read+0 us   <- 首帧
  [PAGE] switch -> 1, scr_load 8 us, refr 129109 us, bmp_miss+0 hit+118 evict+0 | ctf_sd+0 ttf_fill+0 ttf_read+0 us   <- 二次
  ```
  **首帧 129 ms ≈ 二次 129 ms，差距归零**，用户报告的问题消除。诊断用 `[MISS]` 门控日志已清理移除。

- **回归验收脚本**：`scripts/verify_font_firstload.py` 固化本验收。自动开串口 →（可选 `--flash` 烧录并重位）
  → 抓 35 s → 解析所有 `[PAGE] switch -> 1` → 断言 **首帧 `bmp_miss==0`、零 SD 读、且 `|首帧refr - warm均值| ≤ 30ms`**，
  退出码 0/1（CI 友好）。亦支持 `--in <文件>` 离线解析。复跑：`python scripts/verify_font_firstload.py --flash`。

> **状态：字体系统全部功能完成并通过实机验证。** 自 CTF 索引重构（Phase 1~8）、resident RAM 索引（§8.14）、
> 200 KB 字形缓存 + 异步预取（§8.15）、页面分离 + 启动加载页（§8.16），到本次字体页首帧卡顿消除（§8.17），
> 整条链路 Debug/Release 双构 0 warning、OpenOCD 烧录 Verified OK、ST-Link VCP 串口验收全绿。
> 唯一遗留性能项为 §8.13 的**冷栅格化读放大**（CTF v2 glyf 预取打包 / 顺序预读 / `FA_FASTSEEK`），因对首帧体验无影响，列为后续优化。

---

## 9. 日志系统重构：PRINT_LOG + TX 中断驱动（2026-09-03）

### 9.1 目标与范围
- 全工程裸 `printf(` 调用替换为 `PRINT_LOG(...)`，参数 / 格式与原 `printf` 完全一致。
- 参考 101 工程的 logger 风格，实现**裸机版**（无 RTOS）日志接管：`Bsp/bsp_log.h` + `Bsp/bsp_log.c`。
- **FreeRTOS 移除结论**：经核查本工程**自有源码完全裸机**，无任何 `FreeRTOS`/`vTask`/`semphr`
  调用（命中均在 `third_party/`），`CMakeLists.txt` 也未编译内核 —— 无对应代码可删。

### 9.2 架构
```
PRINT_LOG(fmt, ...) ─► printf_log() ─► vsnprintf(栈缓冲 256B) ─► uart_write()
uart_write() ─► 写入 TX 环形缓冲(1024B) ─► USART1 TXE 中断逐字节发送（非阻塞）
```
- **编译期零成本关闭**：`PRINT_LOG_ENABLE=0` 时宏展开为 `((void)0)`，函数体早返回，零 FLASH / UART 开销。
- **不重入 newlib `printf`/`_write`**：自带 `vsnprintf` + `uart_write`，无并发踩锁风险。

### 9.3 临界区保护（写缓冲时关串口中断）
`uart_write()` 在修改环形缓冲索引前调用 `__HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE)`
关闭 TX 中断；若发送器空闲则把首字节 prime 进 `TDR`，再 `__HAL_UART_ENABLE_IT` 重开中断。
ISR（`log_uart_tx_irq`）在 TXE 事件里逐字节取缓冲发送、发完自动关 TXE。
→ 落实"写入要关闭串口中断避免出错"的明确要求。

### 9.4 接线点
- `Core/Src/stm32h7xx_it.c`：原 `USART1_IRQHandler` 走 `Default_Handler`，新增
  `USART1_IRQHandler() ─► log_uart_tx_irq()`，并 `#include "bsp_log.h"`。
- `Core/Src/main.c`：`MX_USART1_UART_Init()` 之后调用 `log_uart_init()`（幂等使能 USART1 NVIC）。
- `CMakeLists.txt`：注册 `Bsp/bsp_log.c`。

### 9.5 替换位置清单（58 处 `printf`→`PRINT_LOG`）
| 文件 | 替换数 | 说明 |
|------|------:|------|
| `Application/app_main.c` | 41 | 首行加 `#include "bsp_log.h"` |
| `Bsp/lv_font_harmony.c` | 10 | 首行加 `#include "bsp_log.h"`；`snprintf` 保留 |
| `Bsp/lv_font_provider.c` | 7 | 首行加 `#include "bsp_log.h"`；`snprintf` 保留 |

残余 `printf(` 全为注释 / `snprintf` 子串 / 新 `printf_log` 函数名，无真实调用。

### 9.6 缓存 / MPU 架构原则与踩坑（2026-09-03 用户纠正）
- **本工程 D-Cache 实际开启**（`main.c:48` `SCB_EnableDCache` + MPU Region0 AXI-SRAM cacheable）。
  之前记忆"D-Cache 关闭"是**过时错误结论**，已订正。
- **用户原则**：SDIO 没用 DMA 时，可以开 DCache；**即便上 DMA 也不该全局关 DCache，
  而是用 MPU 把 DMA 缓冲标记为非缓存区**。规划由用户配合设计（未落地代码）。
- 已订正 `main.c` / `drv_sdio.c` 两处"D-Cache 关 / SD 用内部 DMA"的**过时注释**
  （`drv_sdio.c` 实际用 `HAL_SD_ReadBlocks()` 轮询版，非 IDMA）。
- 未来 DMA 场景 MPU 草案（待定稿）：Region0 AXI-SRAM 512KB 保持 Cacheable；Region1
  SDMMC IDMA 扇区缓冲（SRAM_D2，non-cacheable）；Region2 SPI6(BDMA,D3 域) LCD 缓冲
  （SRAM4 0x38000000，non-cacheable）。non-cacheable 核心收益：CPU 与 DMA 看同一份内存，
  **无需手动 `SCB_Clean/InvalidateDCache`**。详见 `soc-cache-mpu` skill。

### 9.7 验收（下载验证 ✅）
- Debug FLASH 338528B(16.14%) / RAM_D1 262440B(50.06%)；Release FLASH 342500B(16.33%)；均 **0 warning**。
- OpenOCD 烧录 `build/lvgl_oled.elf`：**Verified OK**。
- ST-Link VCP（串口号运行时枚举、本次 COM19）抓到完整启动 banner，证明 TX 中断驱动 `PRINT_LOG` 在硬件上真实输出。

---

## 10. 多页面切换与字库栅格化时延评估（2026-09-03）

### 10.1 需求
新增一个含中文的页面，与现有页面 **~5s 自动切换**，借以直观评估当前 CTF 字库的
栅格化时延是否可用。

### 10.2 实现
- `Application/app_ui.c`：新增第二页（中文"鸿蒙字体引擎"状态页）；5s 定时器只置
  "待切换"标志（不在定时器回调内做重绘，避免 LVGL 重入）。
- `Application/app_main.c`：主循环在每轮 `lv_timer_handler()` 前检测待切换，执行
  `lv_scr_load(新页)` 并用 **DWT CYCCNT**（`SystemCoreClock`=HCLK 240MHz 换算）测量
  这次整页重绘（含中文栅格化）的耗时，打印 `[PAGE] switch -> N, render X us`。

### 10.3 实测时延（这才是对"字库处理时延"的可信数字）
启动自检 `CTF probe`（12 个码位）给出**纯字库**耗时：
- **CPU 纯栅格化（raster）**：67–373 us，平均 ~200 us —— 真正的"字库计算"开销，极快。
- **冷栅格化总耗时（cold，含 SD 读 TTF 块）**：1991–5170 us，平均 ~4500 us；其中
  **SD 卡等待（sd）占 ~90%**（每字约 2 次 `f_read`）。
- 整页切换渲染（`lv_refr_now` 全屏重绘）：首屏冷渲染 865 ms；后续切换（glyph 命中
  bitmap 池）176–237 ms —— 主要是 **SPI6 整屏刷屏**，字库几乎零成本。

### 10.4 结论
纯字库栅格化是**亚毫秒级**，瓶颈在 **SD 块读放大**与 **SPI 刷屏**，不在字体引擎本身。
**当前方案可用**。冷栅格化优化方向见 [§8.13](#813-已知问题与后续性能本轮暂不处理)
（CTF v2 glyf 预取打包 / 顺序预读 / `FA_FASTSEEK`）。
