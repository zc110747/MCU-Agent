# STM32H743 NES 模拟器工程 — 长期记忆

## 项目定位
STM32H743ZIT6 + LVGL v8 菜单框架 + 纯 C NES 模拟器核心。板载**无实体按键**，所有 UI 输入来自 USART1(ST-Link VCP)/USB CDC 文本命令。固件 `H743-NES v1.0.0`。

## 硬件
HSE 25MHz 无源晶振；ST7789 240x240 OLED(SPI6 PG13/14/8/15/12)；SD 卡 SDMMC1 盘符 `1:`，中文字库 `1:/SYSTEM/FONT/`；USART1 PA9/10(115200)、USB OTG_FS PA11/12(TinyUSB CDC)。无 APU（NES 不发声）。

## 构建链
CMake+Ninja+arm-none-eabi-gcc 15.3；OpenOCD+ST-Link。Debug(-Og) 默认，Release(-O2) 实机必需。链接 `-specs=nano.specs`，printf 走强 `_write` 双串口。UTF-8 + `-fexec-charset=UTF-8`。

## 内存/缓存（DCache 开，MPU）
- RAM_D1 AXI-SRAM 0x24000000(512K)：`.data/.bss/heap/stack`+SD 扇区缓冲（栈堆在此）。
- DTCM 0x20000000(128K) **与** RAM_D2 0x30000000(288K)：**运行时动态内存池**（`bsp/sram_pool.c`，边界标记空闲链表分配器，8B 头+8B 尾+coalescing），链接期零占用。平时为空；NES 打开页面时 `nes_open()` 从 DTCM 分配 ~82KB 机器态(`nes_t`)、从 RAM_D2 分配 ≤286KB ROM 镜像（缓冲区已撑满 RAM_D2；原 256KiB 上限会拒掉 256KiB 级卡带如魂斗罗，其 `.nes` 文件=256KiB 数据+16B iNES 头=262160B），`nes_close()` 退出时释放归还，**供相机等后续应用复用**。NES 数据仅 CPU 访问、无 DMA，故 RAM_D2 缓存一致性无虞（MPU 现状不变）。`status` 命令有 `sram dtcm:`/`sram d2:` 实时空闲量。
- RAM_D3 0x38000000(64K)：USB DMA 缓冲(non-cacheable)。
- MPU：Region0/1(RAM_D1/RAM_D2) write-back 可缓存；Region2(RAM_D3,64K) non-cacheable(USB DMA)；DTCM 不配。
- `main()` 先 `SCB_EnableICache()` 后 `SCB_EnableDCache()`；`status` 有 `cache : I+D` 行。
- 安全依据：SDMMC=poll+FIFO、SPI6=CPU阻塞——两者无 DMA 写 AXI-SRAM；NES 态在 DTCM(非缓存)；只有 USB OTG DMA 需 non-cacheable/维护（见下）。

## 已修复缺陷（按时间）
**1. USB 缓存一致性死机（2026-08-12）** — DCache 开后，OTG DMA 直接读写的 USB 缓冲落在 write-back 区→CPU/DMA 不一致→BusFault(CFSR=0x8200, BFAR=0x00100028)。修复两路互补：① `.usb_ram` 段放 RAM_D3 + `CFG_TUSB_MEM_SECTION`(注意是 TUSB 非 TUD) + MPU Region2 non-cacheable；② `bsp/tusb_config.h` 加 `#define CFG_TUD_MEM_DCACHE_ENABLE 1` 让 TinyUSB 自动 clean/invalidate CDC 数据缓冲(rx_ff_buf/tx_ff_buf)。单搬描述符不够，CDC 数据缓冲靠②才彻底。压测 `scripts/stress_usb.py` 7+分钟无死机。

**2. 页头时钟悬空指针死机（2026-08-13，本次）** — 症状：打开任意页再退回主菜单，运行“切换一会”后整板死机。根因：`app_menu.c` 的 `app_menu_tick()` 每整分钟更新页头“HH:MM”时钟标签 `s_hdr_clock_page`；该标签在 `ui_header()` 中创建、页面退出(`lv_obj_clean(s_page_root)`)时被释放，但 `s_hdr_clock_page` 指针**未置 NULL**→分钟翻越时 `lv_label_set_text` 解引用已释放的 LVGL 对象→**精确 BusFault**(CFSR=0x00008200 PRECISERR+BFARVALID, BFAR=0x98915ac5 野指针, HFSR=0x40000000)。与 TXT 无关，但用户正是在切换 TXT 时暴露。修复：在 `app_menu_back()` 与 `app_menu_open_index()` 两处页面拆卸点（clean 之后）置 `s_hdr_clock_page = NULL`；无页头的页(全屏 NES)因此保持 NULL 而安全。重构 Release 0错0警，烧录 Verified OK。`scripts/stress_txt.py` 加“开页→退回菜单→空闲 75s 跨分钟边界”回归段，COM6/COM9 均 PASS。

**3. NES 静态内存→动态内存池（2026-08-13）** — 需求：DTCM(~82KB 机器态)与 RAM_D2(≤256KB ROM 镜像)原本作为 `nes_main.c` 链接期静态变量永久占用，要为后续相机应用让出内存。改为 `bsp/sram_pool.c` 边界标记空闲链表分配器（DTCM/RAM_D2 两区，prologue+epilogue+coalescing），`nes_open()`/`nes_close()` 挂接 NES 页面开/关（`page_nes.c` 的 `nes_enter()`/`nes_exit()`），分配失败有 `load_rom_named()` 守卫兜底。`g_nes` 去掉 const 以便运行时赋值。链接脚本删除 `.dtcm`/`.ram_d2` NOLOAD 段→链接期两区 0 占用。Release 构建 0 错 0 警、烧录 Verified OK。

**4. 边界标记分配器 off-by-8 开销越界损坏（2026-08-13，压测发现）** — 为验证动态池做随机碎片压测（`sram stress` 命令 + `scripts/stress_sram.py`，首版即暴露）。**根因**：`sram_alloc` 只收 `size + BLK_META(8)` 开销，但块布局是「payload=头+8、footer 在 size−8」，可用实际只有 `size−16`；当 `size+8` 恰为 16 对齐（size≡8 mod16）时，用户末 8 字节**写穿 footer**→头/尾不一致、邻居合并错乱→`sram_check` 报 `integrity BAD` 且泄漏逐轮累积。固定大小 NES 分配（82K/256K）永不触发，随机大小必现。**修复**：`sram_alloc` 改收 `size + 2*BLK_META(16)`；`sram_free_bytes`/`sram_check` 的可用量改 `blk_size − 2*BLK_META`；`sram_stress_test` 起始采 `empty_free` 作回收目标、且**无论中途是否损坏都先 sweep 释放自身块**再返回（避免早退泄漏），返回码 2=中途损坏/3=扫完后仍损坏/4=未完全回收。修复后 Release 0错0警、烧录 Verified OK；压测 25 轮 NES 开/关 + 10 轮双区随机碎片(各 800 次) 全 PASS，全程 `integrity ok`、退出精确回基线。注意：修复后空闲基线变为 DTCM 131024/131072、D2 294864/294912（比旧记 131040/294880 少 16B/区，因每空闲块多计 8B 头/尾开销——属正确结果）。

**5. NES ROM 缓冲撑满 RAM_D2（2026-08-13，本次）** — 需求：魂斗罗报「内存过大」打不开。根因：`NES_ROM_MAX=256KiB` 钉死 ROM 缓冲，而 256KiB 级卡带（如标准 UNROM 魂斗罗）的 `.nes` 文件=256KiB 数据+16B iNES 头=262160B，超出 256KiB 上限→`too large`。**修复**：`third_party/nes/nes.h` 的 `NES_ROM_MAX` 由 `256*1024` 改为 `286*1024`(292864B)，即把 ROM 缓冲从「钉死 256KiB」改为「撑满整个 RAM_D2（288KiB 中可用约 286KiB）」，机器态仍留 DTCM。链接期 DTCM/RAM_D2 仍 0 占用（`nes_open()` 运行时 `sram_alloc(SRAM_REGION_D2, NES_ROM_MAX, 4U)`）。Release 0错0警、烧录 Verified OK。**真机验证**：魂斗罗不再报 too large，D2 成功分配 286KiB（空闲 ~294864→1992B）。但暴露第二问题——该「魂斗罗.NES」实际是 **mapper 23(VRC2/VRC4)** 镜像（prg=8/chr=16，非标准 UNROM），核心未实现→`unsupported mapper`，待用户决定：实现 mapper23 或换标准 mapper2 ROM。

**6. 实现 mapper 23（VRC2/VRC4，2026-08-13，本次）** — 需求：魂斗罗(J)（KON-RC826, KONAMI-VRC-2=VRC2b, submapper 3）`.nes`=262160B、mapper 23，核心未实现→`unsupported mapper`。修复：在 `third_party/nes/nes_mapper.c` 按 VRC4 仿真 mapper 23——`vrc_apply_prg()`(4×8KB PRG 窗口：$E000 钉最后 bank、$A000=vrc_prg1、$8000/$C000 由 $9002 M 位交换、受控于 vrc_prg0)、8×1KB CHR 窗口(`vrc_chr[8]` 9bit)、mirroring($9000 MM)、`$F00x` IRQ(latch/control/ack + $F003 拷 A→E)；寄存器 lane 由 A0(stride1)/A2(stride4) 选低/高 4bit 半字节。`nes_internal.h` 的 `mapper_t` 加 vrc_prg0/prg1/chr[8]/swap/stride/mode/prescaler/irq_latch/irq_counter；`nes_mapper_clock_irq(cycles)` 每 CPU 周期推进 prescaler(341/−3) 或 cycle-mode 直推计数器（仅 E 位有效）。`nes_main.c` 的 `run_cpu()` 每指令按 CPU 周期调用之；`nes_mapper_name()` 加 "VRC2/VRC4"。stride 默认 1（VRC2b/VRC4f），因 iNES1.0 不标子变体，VRC4e 未覆盖。Release 0错0警、烧录 Verified OK。**真机验证**：魂斗罗(J) `rom load 3`→`mapper 23 (VRC2/VRC4)`、`frames 153`、`fps 41`、`sram d2 1992/294912`；`rom stop` 后响应正常；Super Mario(mapper0) 回归 `fps 42` 无碍。`scripts/verify_contra.py` 改写支持 mapper23 + fps 检查。

**7. 图片查看器 JPEG「涂抹」（2026-08-14，本次）** — 症状：图片查看器 JPEG 显示涂抹（其它功能正常）。根因：TJpgDec（`third_party/tjpgd`，`JD_FORMAT=1` 直出 RGB565）的 `outfunc` 按 `jd->swap` 决定 RGB565 字节序；`jd_prepare()` 会 **保存并恢复** `jd->swap`（不清零），而 `app/img_decode.c` 的 `decode_jpeg()` 里 `JDEC jd;` 是**未初始化栈变量**，`jd.swap` 保留栈垃圾，Release(-O2) 栈布局下恰非零即字节交换 → ST7789(SPI6 16 位模式 MSB 先发) R/B 错乱=涂抹。BMP 用 `rgb565()`、NES 用标准 RGB565 → 不受影响，印证「其它功能正常」。修复：`decode_jpeg()` 在 `jd_prepare()` 前显式 `jd.swap = 0U;`。主机侧回归 `tests/test_jpg_rgb565.c` 证明 swap=0 输出与 NES/BMP 逐位一致、swap=1=字节交换（涂抹）。Release 0错0警构。压测脚本 `scripts/stress_img.py`（30 轮 cap→img decode→show→close，校验 SRAM 完整回收）真机 **PASS**：30/30 轮、SRAM_D2 delta=0 零泄漏、控制台存活无死机（COM6，uptime>9min、cache I+D）。

## 模块关键事实
- `app/app_cmd.c`：唯一输入，行缓冲≤96B，回 `OK `/`ERR `；命令见 README §5。
- 虚拟键：up/down/left/right/a/b/select/start/ok/back/menu。
- NES ROM：`1:/NES` 主、`1:` 回退；Mapper 0/1/2/3/4/7/23(≤286KB, RAM_D2 撑满)；实机验证 Mapper0(NROM) 与 Mapper23(Contra J, VRC2b) fps~41。
- **动态内存池**(`bsp/sram_pool.c`)：边界标记空闲链表分配器，DTCM/RAM_D2 两区，`sram_alloc(region,size,align)`(开销收 `2*BLK_META=16B`)/`sram_free`/`sram_free_bytes`/`sram_total_bytes`/`sram_check(region,&ok)`(头尾一致+邻接+覆盖校验)/`sram_stress_test(region,iters,seed)`(xorshift 随机分配释放+扫完校验完全回收)。`nes_open()`/`nes_close()` 在 NES 页面开/关时分配/释放机器态(DTCM)与 ROM 镜像(RAM_D2)，退出即归还供相机等复用。`app_main.c` 启动 `sram_pool_init()`；`status` 上报 `sram dtcm:`/`sram d2:`；控制台 `sram info/check/alloc/free/stress` 供调试压测。
- 显示：256x240 裁左右各 8 列→240x240；全屏 NES 页暂停 LVGL 直接刷 SPI6。
- `sd_browser`(共享 SD 浏览器，静态单例 `s_b`)：`sd_browser_create()` 末参 `filter(name,is_dir)`(NULL=不过滤)；仅文件被过滤，目录/“..”始终显示。NES/图片传 NULL；TXT 传 `txt_filter` 只列 `*.txt`。
- **TXT 阅读器**(`app/page_txt.c`, cmd=`txt`, icon=`icon_txt`)：整文件读 SD 入 RAM(raw 32KB 上限)→GBK→UTF-8(UTF-8 BOM 快路径不转码，超 32KB 截断)→用 LVGL `_lv_txt_get_next_line`(`misc/lv_txt.h`，返回 uint32_t 字节偏移，非指针)按面板宽 224px 分页、每页 8 行，UP/DOWN 翻页。控制台 `txt list/open/close/info/seed`(`seed` 写 80 行多页样例便于压测)。`page_txt_seed()` 调试用。
- **屏幕截图 cap**（`bsp/jpeg_enc.c` 软件 baseline JPEG 编码器 + `bsp/screen_cap.c/.h`）：`screen_cap_capture()` 把 `LCD_GetFrameBuffer()`(240×240 RGB565 阴影帧缓冲) 编码为 JPEG 存 `1:/catch/HH-MM-SS-NNN.jpg`（RTC 时分秒 + 3 位随机数，`f_mkdir` 自动建目录，同名 `_k` 后缀去重）；JPEG 缓冲 80KB 从 `sram_pool`(DTCM 优先/D2 兜底) 运行时分配释放。`app/app_cmd.c` 的 `cmd_cap` 支持 `cap`/`cap shot`/`cap now`/`cap list`/`cap head [name]`（`CAP_DIR "1:/catch"` 在 `screen_cap.h` 导出）。上位机 `tools/NesPadTool` 的「截图保存 (cap)」按钮发 `cap`。实机 `cap head` 首字节 `FF D8 FF E0 00 10 JFIF…` 确认 JPEG 有效；NES 全屏页经 `LCD_CopyBuffer` 镜像故也能截实时帧。
- **图片查看器**(`app/img_decode.c` + `app/page_image.c`, cmd=`img`, icon=`icon_image`)：BMP(24/32bit) + JPEG(baseline) 解码到 240×240 RGB565 帧(`s_fb`, RAM_D1 .bss 112.5KB)，经 `img_blit()`(= `LCD_CopyBuffer` 分带推屏)显示；与 NES 页互斥。JPEG 走 ChaN TJpgDec(`third_party/tjpgd`)，`tjpgdcnf.h` 设 `JD_FASTDECODE=2`(最坏需求 9644B)。`decode_jpeg()` 的 TJpgDec 工作区**不再用静态 8KB**(曾因 <9644B 致所有 JPEG 报 JDR_MEM1/2「内存不足」)，改为运行时从 `sram_pool` 取 **32KB**(先 RAM_D2、失败退 DTCM)，所有出口统一释放。`app/app_cmd.c` 有 `img list`/`img show <idx>`/`img decode <path>`(无 UI 头端解码，调试用)/`img close`/`img info`。实机 `img decode 1:/catch/HH-MM-SS-NNN.jpg` 对本机 4:4:4 截图返回 `OK img decode JPEG 240x240`(此前必「内存不足」)，`sram d2` 分配后回满基线。
- 调试/自测脚本：`scripts/stress_txt.py`(开/翻/关循环+burst+fuzz+跨分钟回归+死锁时 OpenOCD/gdb 抓现场)、`scripts/serial_test.py`(51 项，含 `test_txt`)、`scripts/stress_usb.py`、`scripts/verify_features.py`、`scripts/verify_cap.py`(截图一键验收)、`scripts/verify_camera.py`(相机一键验收)、`scripts/verify_img.py`(图片查看器「内存不足」修复验收：cap 4:4:4 → img decode 成功 + sram d2 回满)。

## 已知限制
- NES 无 APU（不发声）。
- FatFs `FF_CODE_PAGE` 必须 **936**（曾误用 437 致中文 LFN 丢失/乱码）；`fno.fname` 返回 GBK，OLED 字体做 UTF-8→Unicode→GBK 映射，故喂 `lv_label_set_text` 前须 `gbk_to_utf8()`。**勿改回 437。**
  - ⚠️ **现状不一致**：2026-08-14 实查 `third_party/FatFs/ffconf.h` 当前确为 `#define FF_CODE_PAGE 437`（与「必须 936」要求矛盾），中文 SD 文件名可能乱码。截图(cap)功能用 ASCII 文件名不受影响，故未改动该文件；**待用户确认是否改回 936**。
- 实机验证 NES Mapper 0(NROM) 与 Mapper 23(VRC2/VRC4, Contra J)；Mapper1/2/3/4/7 代码支持未实跑。
- 部分名为「魂斗罗/Contra」的 `.nes` 实为 **mapper 23(VRC2/VRC4)** 镜像（128KB PRG+128KB CHR）；**已实现 mapper 23**（VRC4 仿真，stride-1 覆盖 VRC2b/VRC4f）。Contra(J)=VRC2b 实机验证通过(~41fps)。iNES1.0 不记录子变体，VRC4e(stride-4) 卡带暂未覆盖。
- 显示/SPI 观感需真机肉眼确认。

## 实机验证（烧录器 ST-Link V2，OpenOCD 直连）
`openocd -f openocd.cfg -c "init" -c "program build-release/nes_h743.elf verify reset exit"` → Verified OK。双通道 COM6(ST-Link VCP)/COM9(USB CDC) 共用解析器（本机实测为 COM19/COM4，同一块带 OV5640 板）。`serial_test.py` 51/51 PASS（2026-08-13）。NES 实战 mapper0 fps~41(Release@480MHz)。`scripts/verify_cap.py` 真机 ALL CHECKS PASSED（2026-08-14：cap 存 `1:/catch/HH-MM-SS-NNN.jpg`、`cap head` 见 `FF D8 FF E0 00 10 JFIF…`、连续多次无泄漏/无死机）。图片查看器涂抹修复后 `scripts/stress_img.py` 真机 **PASS**（30/30 轮、SRAM_D2 零泄漏、cache I+D 下稳定运行>9min）。
