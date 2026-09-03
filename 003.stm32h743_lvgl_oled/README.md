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
| DCache | **关闭**（只开了 ICache）；MPU region0 = AXI-SRAM 512 KB，cacheable/write-back |
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
