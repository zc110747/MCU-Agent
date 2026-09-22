# Ebook LVGL — Waveshare ESP32-S3-Touch-LCD-4.3B

An e-book / utility device UI built from scratch on **ESP-IDF + LVGL 9** for the
Waveshare **ESP32-S3-Touch-LCD-4.3B** (800×480 RGB panel, capacitive touch).

Ten product pages: Home, Reader, File Manager, Photos, Notes, Drawing, Clock,
Calendar, Weather, Settings — plus the two acceptance screens the phase plan
calls for, Display Test (panel bring-up) and Touch Test (input bring-up).
Nothing else: the page set is closed, and any page outside that list is a
regression.

---

## Project

| | |
|---|---|
| Name | Ebook LVGL |
| Target board | Waveshare ESP32-S3-Touch-LCD-4.3B |
| Display | 800 × 480 landscape, RGB565, DPI (RGB) interface |
| ID | `ebook_lvgl` |

The project is a **re-implementation**, not a port: the page structure, the
interaction model and the visual language of the original web project were
studied and then rebuilt for a fixed 800×480 landscape panel. Nothing is
obtained by scaling an existing layout up or down.

---

## Hardware

| Item | Detail |
|---|---|
| Module | ESP32-S3-WROOM-1 **N16R8** |
| Flash | 16 MB (80 MHz; the I/O mode is auto-detected by esptool) |
| PSRAM | 8 MB **octal** SPI @ 80 MHz |
| CPU | Xtensa LX7 dual core @ 240 MHz |
| Display | 4.3" 800×480 RGB TFT, 16-bit DPI, driven by `esp_lcd_rgb_panel` |
| Touch | GT911 capacitive, I²C |
| RTC | PCF85063, I²C |
| Expander | CH422G I²C I/O expander (LCD reset, backlight, touch reset, SD CS) |
| Storage | microSD in **SPI** mode |
| Other | RS485 transceiver, CAN transceiver, USB (native + serial/JTAG) |

---

## Software

| | |
|---|---|
| Framework | ESP-IDF **v6.1** |
| GUI | LVGL **9.3.0** (`lvgl/lvgl`) |
| LVGL port | `espressif/esp_lvgl_port` **2.9.0** |
| Build | CMake + Ninja |
| Toolchain | `xtensa-esp-elf-gcc` 15.2.0 |
| Language | C++26 for the application, C for the platform layer |

**Arduino and PlatformIO are explicitly not used.**

Component dependencies are declared in `main/idf_component.yml` and resolved
into `managed_components/`.

---

## Build

The Windows host needs the ESP-IDF environment activated. Two helpers exist so
that this is reproducible and does not depend on the interactive shell state:

```
tools\idf_env.bat     activate IDF v6.1 (toolchain, python venv, PATH)
tools\run_idf.bat     call idf_env.bat then run idf.py, mirroring all output
                      into build_log.txt
```

```bat
tools\run_idf.bat build
```

Equivalent manual sequence:

```
call tools\idf_env.bat
idf.py build
```

To regenerate the Phase-1 test pattern asset:

```
python tools\gen_test_pattern.py
```

### Build configuration

`sdkconfig.defaults` holds the whole configuration. The points that matter:

* `CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y` — **required**. The RGB frame buffers
  live in PSRAM, so they are behind the data cache; the RGB driver only performs
  the cache-to-memory sync that makes CPU writes visible to the DMA engine when
  it sees a non-zero cache line size. Without this the panel shows stale pixels.
* `CONFIG_SPIRAM_MODE_OCT=y` — the N16R8 part has octal PSRAM, not quad.
* `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` — **required**. GPIO43/GPIO44 are wired
  to the on-board RS485 transceiver, so the UART0 console must not be used.
* `CONFIG_PARTITION_TABLE_CUSTOM=y` with `partitions.csv` — a single 4 MB app
  partition (no OTA).

### Size (measured, `idf.py size`, all 12 pages linked)

| Region | Used | Total | Used |
|---|---|---|---|
| Flash code — `.text` | 511 416 B | | |
| Flash data — `.rodata` 642 728 + `.appdesc` 256 + `.tdata` 16 | 643 000 B | | |
| **Image total** | **1 240 654 B** | 4 194 304 B (app partition) | **29.6 %** |
| DIRAM — `.text` 54 943 + `.data` 14 875 + `.bss` 4 624 | 74 442 B | 341 760 B | **21.8 %** |
| IRAM (`.text` 15 356 + `.vectors` 1 028) | 16 384 B | | |
| RTC slow / fast | 36 B / 24 B | 8 192 B each | 0.44 % / 0.29 % |

`ebook_lvgl.bin` on flash: 0x12EEC0 = 1 240 768 B, **70 % of the app partition
free**.

The single largest item in that 1.24 MB is not code: it is the 16 px CJK face
(`CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK`), about 1.1 MB of it. That is why
exactly one CJK size exists — see `Theme::font_cjk()`.

Zero compiler warnings.

Runtime headroom and the leak check, both from the same boot log:

| Point | Internal heap free | PSRAM free |
|---|---|---|
| after the first Home is built | 256 787 B | 5 680 480 B |
| after all 12 pages were built and torn down | 256 771 B | 5 680 480 B |
| difference | **−16 B** | **0 B** |

`largest` internal block 212 992 B. Sixteen bytes across twelve page
build/destroy cycles, with PSRAM bit-identical, is the machine-checkable form of
"`Home → Reader → Home` releases the Reader widgets" — and it is why the object
count is asserted too (60 objects before, 60 after, `failures = 0`).

> The `IRAM` row of `idf.py size` reports "100 %" on this target. That is an
> artefact: the ESP32-S3 IRAM and DRAM share one SRAM window, so the tool has no
> independent region size to divide by and uses the sum of the sections it
> lists. Read the absolute figure (16 KiB), not the percentage.
>
> Console encoding note: the size report's tables only come out readable because
> `tools/run_idf.bat` pins the code page to UTF-8 (`chcp 65001`, `PYTHONUTF8=1`).
> Without it the `idf.py` → cmake → `esp_idf_size` chain round-trips UTF-8 bytes
> through cp936 and the tables arrive as mojibake.

---

## Flash

```bat
tools\run_idf.bat -p COM14 flash
```

Verified on hardware through the board's own **USB-Serial-JTAG** port
(`VID_303A / PID_1001`, enumerated as COM14 on this host). A full
`build → flash → monitor` cycle takes about 25 s, and the device boots into
Home on its own afterwards.

For a debug session instead, `idf.py openocd` drives the same USB port: the
ESP32-S3 has a **built-in USB-JTAG controller**, so no external probe is
involved and no ST-Link is required.

---

## Monitor

```bat
tools\run_idf.bat -p COM14 monitor
```

The console is **USB-Serial-JTAG** (the USB-C port), at whatever baud the host
chooses — it is a USB CDC device, the line rate is nominal.

`idf.py monitor` is interactive: it takes over the terminal and only exits on
Ctrl+]. When the caller is a script or an agent whose stdout is swallowed that is
useless, so `tools\serial_probe.py` does the two parts that matter and exits on
its own status code:

```bat
python tools\serial_probe.py COM14 25
```

It resets the chip *into the application* rather than into download mode — on
this peripheral `USBJTAGSerialReset` (DTR low) would raise the ROM downloader and
the app would never run, so it uses `HardReset(usb)` with DTR held HIGH. It also
strips the ANSI escapes, which is what makes the log greppable.

---

## Pin Configuration

Single source of truth: `main/platform/board/board_config.h`.
Every value below was cross-checked against two independent sources — the
Waveshare schematic `ESP32-S3-Touch-LCD-4.3B-Sch.pdf` and the official wiki
pinout table. **Do not change a pin without re-checking both.**

### RGB LCD (16-bit DPI)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| PCLK | 7 | | B3 | 14 |
| DE | 5 | | B4 | 38 |
| VSYNC | 3 | | B5 | 18 |
| HSYNC | 46 | | B6 | 17 |
| R3 | 1 | | B7 | 10 |
| R4 | 2 | | G2 | 39 |
| R5 | 42 | | G3 | 0 |
| R6 | 41 | | G4 | 45 |
| R7 | 40 | | G5 | 48 |
| | | | G6 | 47 |
| | | | G7 | 21 |

The panel is wired **5-6-5**: only R3..R7, G2..G7 and B3..B7 are driven, so the
frame buffer bit mapping is

```
bit 15..11  Red[4:0]    -> LCD R7..R3
bit 10.. 5  Green[5:0]  -> LCD G7..G2
bit  4.. 0  Blue[4:0]   -> LCD B7..B3
```

`display_driver.c::fill_data_gpio_nums()` encodes exactly this order.

> GPIO0 (G3), GPIO3 (VSYNC), GPIO45 (G4) and GPIO46 (HSYNC) are ESP32-S3
> strapping pins. They are used by the panel and must not be pulled by
> external circuitry.

### I²C (shared bus)

| Signal | GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |

400 kHz. Devices: **CH422G** `0x24`, **GT911** `0x5D` (fallback `0x14`),
**PCF85063** `0x51`.

### microSD (SPI mode)

| Signal | GPIO |
|---|---|
| MOSI | 11 |
| SCK | 12 |
| MISO | 13 |
| CS | *not a GPIO* — CH422G **EXIO4**, active low |

### Other

| Signal | GPIO |
|---|---|
| Touch IRQ | 4 |
| RTC INT | 6 |
| USB D− / D+ | 19 / 20 |
| RS485 RXD / TXD | 43 / 44 |
| CAN TX / RX | 15 / 16 |

### CH422G expander

The chip has no register file — the command travels in the **I²C address phase**:

| Address | Meaning |
|---|---|
| `0x24` | set system parameter (mode byte) |
| `0x38` | write bidirectional IO (IO7..IO0) |
| `0x46` | write OC3..OC0 outputs |
| `0x26` | read bidirectional IO |

Mode byte `0x01` = all of IO0..IO7 as push-pull outputs, open-drain disabled.

| EXIO | Function |
|---|---|
| 0 | isolated digital input DI0 |
| 1 | GT911 reset (active low) |
| 2 | LCD backlight enable (high = on) |
| 3 | LCD reset (active low) |
| 4 | microSD chip select (active low) |
| 5 | isolated digital input DI1 |

---

## LCD Configuration

| Parameter | Value |
|---|---|
| Resolution | 800 × 480 |
| Colour | RGB565, 16-bit DPI |
| PCLK | 16 MHz |
| HSYNC pulse / back / front porch | 4 / 8 / 8 |
| VSYNC pulse / back / front porch | 4 / 8 / 8 |
| Frame rate | 16 MHz / 820 / 500 = **39 Hz** (panel ceiling) |
| Frame buffers | 2, in PSRAM |
| **Bounce buffer** | **800 × 10 px = 16 000 B × 2, internal DRAM** |
| DMA burst | 64 bytes |
| PCLK sampling edge | `pclk_active_neg = 0` (deliberate deviation, see below) |

Timing comes from the official Waveshare ESP-IDF demo for this exact board
(`github.com/waveshareteam/ESP32-S3-Touch-LCD-4.3B`,
`examples/ESP-IDF/09_lvgl_v9_demo/.../waveshare_rgb_lcd_port.c`, 800×480 branch),
whose pin list matches `board_config.h` line for line.

`pclk_active_neg` is the one value where we deliberately differ from the official
demo (which uses 1). A wrong sampling edge shows up as per-pixel noise or
ghosting; the picture on the bench is clean, so the current edge latches
correctly and is left alone.

### The "permanent shift"

The first hardware build showed the whole image displaced to the right with the
rightmost columns wrapped round to the left edge. It is **not** a layout fault
(LVGL reports full-screen geometry) and **not** a blanking/phase fault
(**changing the porches has no effect** — the panel is driven through DE, so the
porches set the frame rate, not the phase).

It is the RGB DMA losing scan sync because it cannot fetch the frame buffer out
of PSRAM fast enough. Espressif names this exact failure in
`esp_lcd_panel_rgb.h` → `esp_lcd_rgb_panel_restart()`:

> the LCD controller is out of sync with the DMA because of insufficient
> bandwidth. To save the screen from a **permanent shift**

The budget explains why: the 768 000 B frame buffer cannot fit in internal SRAM,
so it lives in PSRAM, and scan-out alone wants 768 000 × 39 = **30 MB/s** of a bus
that LVGL's rendering, the cache writebacks and (with
`SPIRAM_FETCH_INSTRUCTIONS`) instruction fetch are all sharing.

#### Fix 1 — the bounce buffer (in, and required)

`bounce_buffer_size_px = 800 × 10`, as the official demo does. The DMA bursts out
of internal DRAM instead of PSRAM, so it stops competing for the bus that is
stalling it. Constraint from `esp_lcd_panel_rgb.c`: `fb_size % bb_size == 0`
→ 768 000 % 16 000 = 0. Costs 32 KB of internal DRAM.

It is not free. `esp_lcd_panel_rgb.c:1042` copies the frame into the bounce
buffer with a **plain CPU `memcpy`** that runs **inside the GDMA EOF ISR**:

```c
// copy partial frame buffer to the bounce buffer
memcpy(buffer, &panel->fbs[panel->bb_fb_index][...], panel->bb_size);
```

48 chunks per frame, every frame — 768 000 B of PSRAM→DRAM copying per frame, at
39 frames/s. That is the price of the workaround, and it is why the CPU figures
reported by `system_info_log_tasks()` understate the true load: ISR time is not
attributed to any task.

#### Fix 2 — `CONFIG_LCD_RGB_RESTART_IN_VSYNC` (**deliberately off**)

The first build enabled this as well, so the two were never separated. Only the
bounce buffer is needed, and this one works against it:

* With it set, `lcd_rgb_panel_try_restart_transmission()`
  (`esp_lcd_panel_rgb.c:1276`) restarts the GDMA channel **unconditionally, every
  VBlank**, and the `#else` branch is compiled out — that branch being the
  driver's own precise detector, which fires on `need_restart` or on
  `bb_eof_count < expect_eof_count`, i.e. exactly when the DMA really did fail to
  finish all 48 chunks of a frame.
* The driver's own comment at `:1265` on the unconditional restart says it *"can
  lead to single-frame desyncs itself, as in: if this interrupt is late enough,
  the display will shift as the LCD controller already read out the first data
  bytes, and resetting DMA will re-send those."* That is the same
  right-shift-with-wrap symptom.
* Fix 1 is what makes a VSYNC interrupt late. So an unconditional restart
  manufactures the very condition under which it shifts a frame.

Net: the bounce buffer gives the DMA its headroom, and the driver restarts the
channel on its own only when a frame genuinely underran. `sdkconfig.defaults`
carries the full reasoning next to the line.

> **Coupled with the LVGL port.** `display_driver.c`'s
> `bounce_buffer_size_px != 0` and `lvgl_port.cpp`'s `rgb_cfg.flags.bb_mode = true`
> must agree. `bb_mode` only chooses which panel event releases the LVGL draw
> buffer (bounce → `on_frame_buf_complete`, otherwise → `on_vsync`); setting one
> without the other leaves LVGL waiting for an event that never arrives.

### Backlight

The 4.3" LED string is driven by an AP3032 boost converter whose CTRL pin is
wired to CH422G **EXIO2**. There is **no PWM / current-control line on this
board**, so brightness is on/off only. `BacklightService` exposes
`set_enabled()` accordingly; a `set_brightness()` that does nothing would be a
lie, so it is not offered.

---

## Touch Configuration

GT911 over the shared I²C bus at `0x5D` (address depends on the reset
sequence; `0x14` is the alternative), IRQ on GPIO4, reset via CH422G EXIO1.

Two things on this board differ from a stock GT911 bring-up, both handled in
`main/platform/touch/touch_driver.c`:

* **Reset is not a GPIO.** The line hangs off the expander, so the driver is
  told `rst_gpio_num = GPIO_NUM_NC` and the pulse is issued through
  `io_expander_touch_reset()` instead.
* **The I²C address is latched at reset release.** The controller samples the
  interrupt line while reset is de-asserted: held LOW it answers on `0x5D`,
  HIGH on `0x14`. GPIO4 is therefore driven low as an output for the duration
  of the reset pulse and only afterwards reconfigured as the controller's
  interrupt output. That deliberate double configuration makes the driver emit
  `W (909) gpio: conflict found for GPIO[4]` at every boot — it is expected and
  documented in the source, not a fault.

Touch is registered as an LVGL pointer input device with the panel as its
display and `scale = 1.0`, because the panel runs at its native resolution and
the application never rotates, so no application-level remapping is needed.

`touch_init()` reads the controller once before reporting success, so a loose
FPC is reported as `Touch: NOT DETECTED` rather than as a device that silently
ignores every tap. Touch failure is **non-fatal**: the device boots and shows
its UI either way.

**Status: implemented and in use.** The `TouchTest` page draws the live point
plus the raw coordinate readback; `main/platform/touch/` never touches LVGL
directly.

---

## SD Card

microSD in **SPI** mode on `SPI2_HOST` at up to 20 MHz, mounted at **`/sd`**
(FatFs, `esp_vfs_fat_sdspi_mount`). Chip select is **not** a GPIO — it is
CH422G EXIO4, active low, so every card transaction has to take the expander
lock first; the slot is therefore configured as `SDSPI_SLOT_NO_CS`.

The card is the storage for books, images and (planned) subset fonts. The
wear-levelled internal partition is **not** used for user content.

The UI never calls `fopen()` or `sd_card_*`. It asks
`services::storage_*` for a listing, a file's bytes or a joined path, which
keeps the "is there a card at all" question in one place and the error/empty
state in each page down to a single boolean. Listings carry the true entry
count as well as the returned ones, so a truncated folder renders as
"40 of 210" rather than as a short folder.

**Status: driver complete, card verification pending.** With no card in the
socket the boot log shows the expected two lines and nothing else:

```
E (...) sdspi_host: sdmmc_card_init failed (0x107)
W (...) sd_card:   no card mounted at /sd (ESP_ERR_TIMEOUT)
```

Mount failure is non-fatal by construction — the device boots into Home and
the file-facing pages show their empty state. No SD card was available during
this session, so read/write on real media is the one hardware path still
unverified; everything above it has been exercised.

---

## PSRAM

8 MB octal PSRAM @ 80 MHz. Two consumers so far:

| Consumer | Size |
|---|---|
| RGB frame buffers (2 × 800 × 480 × 2 B) | 1 536 000 B |
| LVGL draw buffers | **0 B** |

The zero is the interesting number: because LVGL runs in **direct mode** with
`avoid_tearing`, the LVGL draw buffers *are* the panel's own frame buffers. No
extra buffer is allocated and no pixel copy happens on flush — the driver just
switches the scan-out to the buffer LVGL has finished writing. See
`main/platform/lvgl/lvgl_port.cpp` for the full explanation.

---

## LVGL

LVGL 9.3.0, configured entirely through Kconfig (`CONFIG_LV_CONF_SKIP=y`) so
there is no `lv_conf.h` to drift out of sync. Key settings live in
`sdkconfig.defaults`:

* 16 bit colour depth
* newlib for malloc / string / printf (smaller image, one shared heap)
* Montserrat 14 / 16 / 20 / 28 — these four are what `Theme::font_*` maps to
* no demos, no examples
* observers + system monitor enabled; the performance overlay is available for
  soak testing but not shown by default

All LVGL access from outside the LVGL task goes through
`lvgl_port_acquire()` / `lvgl_port_release()` (a recursive lock, so a callback
that runs *inside* the LVGL task may take it again).

---

## Runtime model (FreeRTOS)

The system runs on FreeRTOS with a 1 kHz tick and exactly **three**
application-level tasks. Everything else in the task table belongs to ESP-IDF.

| Task | Core | Prio | Stack | Owns |
|---|---|---|---|---|
| `main` | 0 | 1 | 8192 | boot sequence, then exits |
| `taskLVGL` | 1 | 6 | 8192 | **all** UI: input polling, animation, layout, rendering |
| `app` | 0 | 4 | 4096 | application lifetime, 30 s memory heartbeat |
| `weather` | any | 4 | 4096 | sample-data provider, off the UI path |

Reasoning, and it is the whole scheduling budget:

* **`taskLVGL` on its own core, above everything application-level.** Rendering
  competes for the same PSRAM bandwidth the RGB DMA is streaming a frame from,
  so time-slicing it against `app_main` and the services on core 0 is exactly
  what must not happen. It stays far below `ipc0/ipc1` (24) and `esp_timer` (22),
  which the system needs to stay healthy.
* **The application heartbeat gets a separate task**, so a slow 30 s memory
  report can never delay a flush.
* **`task_max_sleep_ms = 100` is a cap, not a period.** The port waits on an
  event group with a `lv_timer_handler()`-derived timeout and then yields for
  1 ms regardless, so idle-to-flush latency is bounded by LVGL's timers, not by
  this number.
* **`timer_period_ms = 5`** — a 200 Hz LVGL tick.

Measured on hardware (`system_info_log_tasks()`, run-time stats enabled; stack
column is the high-water mark, i.e. what is *left*):

```
taskLVGL    core 1  prio 6   2820 B stack left   14 % CPU
app         core 0  prio 4   3376 B stack left   <1 % CPU
weather     any     prio 4   3324 B stack left   <1 % CPU
```

Read that 14 % as a **floor on the real cost**, not the truth: the bounce-buffer
`memcpy` runs in the GDMA EOF ISR (see *LCD Configuration*), and FreeRTOS
run-time statistics do not attribute ISR time to any task. The stack figures are
the useful part — 2.8 KB left on the rendering stack is the number to watch when
adding widget depth.

---

## Interactive performance

"The touch feels laggy" is not measurable by eye, so the pipeline is measured
instead. Three numbers matter, and two of them are ceilings that no amount of
application tuning can move.

**1. The panel is the hard ceiling: 39 Hz.** `pclk 16 MHz / (820 × 500)`. The
porches are already at the official minimum, so the only way up is a faster PCLK
— and a faster PCLK means *more* DMA bandwidth pressure, which is the very thing
that caused the shift in the first place. It is left alone.

**2. LVGL's refresh period is a request, not a rate.**
`CONFIG_LV_DEF_REFR_PERIOD = 16` ms. The same value sets the input-device read
period (per the official Kconfig text: *"Default display refresh, input device
read and animation step period"*), so input is sampled at up to 62.5 Hz.

**3. In direct mode every flush blocks the LVGL task for a whole panel frame.**
`esp_lvgl_port_disp.c` does not just kick off the transfer:

```c
if (lv_disp_flush_is_last(drv)) {
    esp_lcd_panel_draw_bitmap(panel, 0, 0, hor_res, ver_res, color_map);
    /* Waiting for the last frame buffer to complete transmission */
    xSemaphoreTake(disp_ctx->trans_sem, 0);
    xSemaphoreTake(disp_ctx->trans_sem, portMAX_DELAY);   /* blocks */
}
```

and that semaphore is given from `on_frame_buf_complete` when `bb_mode` is set —
once per *completed frame*. So a two-pixel button highlight costs the same wait
as a full repaint: up to 25.6 ms. Because input polling happens inside
`lv_timer_handler()` on the same task, a tap that arrives mid-flush is seen after
it.

Net expectation, and what to compare against: **~39 updates/s while interacting,
with worst-case tap-to-feedback around one to two frames (26–50 ms)**. A tap that
also rebuilds a page pays the page build on top of that.

### How to measure it

The acceptance build (`EBOOK_ACCEPTANCE_SWEEP`) carries a render-rate probe that
counts `LV_EVENT_RENDER_READY` — the event that is only sent for a cycle that
actually painted something, unlike `LV_EVENT_REFR_READY` — and prints it every
5 s:

```
W (7085) app: RATE renders = 19 in 5000 ms -> 3/s
```

Idle reads **3/s**, which is the performance overlay's own 300 ms redraw and is
the check that the probe means what it says. **While dragging a finger across the
Home grid, expect ~39/s.** Materially below that during interaction means a real
bottleneck above the panel and is worth chasing; at ~39/s the UI is simply at the
panel's ceiling.

---

## UI Layout

**Fixed 800 × 480 landscape. The application layer performs no rotation.**

Display adaptation is done by:

* **re-layout** — pages are designed for 800×480 in real pixels, not converted;
* **Flex / Grid** — free space is distributed by flex weight, so a card grows or
  shrinks instead of being squashed;
* **equal-proportion scaling** — where an asset has to change size, its aspect
  ratio is preserved;
* **percentage and content sizing** — `LV_PCT`, `LV_SIZE_CONTENT`, min/max.

**Prohibited anywhere in this project:**

```c
scale_x = 800 / old_w;      /* forbidden */
scale_y = 480 / old_h;      /* forbidden */
```

A non-uniform stretch is a bug even when it happens to look acceptable: use
`lv_image_set_scale()` (uniform) or a source image at the right size.

### Safe area

A 16 px margin is kept on all four sides (`Theme::kSafePad`). Chrome such as
the header and footer spans the full width on purpose; content never touches
the glass edge.

### Screen skeleton

```
┌──────────────────────────────────────────────────────────┐  800 x 480
│ header   56 px                              title | info  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│   body    16 px padding, flex row, 16 px gap             │
│                                                          │
├──────────────────────────────────────────────────────────┤
│ footer   36 px                             board | status │
└──────────────────────────────────────────────────────────┘
```

---

## Architecture

Strictly one-directional. A layer may only call the layer below it.

```
        ┌──────────────────────────────────────────┐
        │  app/      pages, navigation, AppManager │
        ├──────────────────────────────────────────┤
        │  ui/       Theme, widgets, Page, assets  │
        ├──────────────────────────────────────────┤
        │  services/ FileService, BookService, ... │
        ├──────────────────────────────────────────┤
        │  platform/ display, lvgl, touch, storage │
        ├──────────────────────────────────────────┤
        │  ESP-IDF   drivers, esp_lcd, esp_lvgl_port│
        └──────────────────────────────────────────┘
```

**The UI layer never touches GPIO, I²C, SD or WiFi.** It asks a service; the
service asks the platform. That is why `Theme`, `Page` and the widgets have no
idea what an `esp_lcd_panel_handle_t` is.

```
main/
├── main.cpp                       boot sequence only
├── app/
│   ├── app_manager.{h,cpp}        page registry, navigation stack, app task
│   └── pages/                     12 pages, one class each
│       ├── home_page.*            the navigation root (3x3 grid)
│       ├── reader_page.*          paged text, GBK/UTF-8
│       ├── file_manager_page.*    /sd browsing
│       ├── photos_page.*          image listing + viewer
│       ├── notes_page.*           editable notes
│       ├── drawing_page.*         freehand canvas
│       ├── clock_page.*           RTC time, 200 ms tick
│       ├── calendar_page.*        month grid
│       ├── weather_page.*         conditions + forecast
│       ├── settings_page.*        preferences
│       ├── display_test_page.*    Phase-1 acceptance screen
│       └── touch_test_page.*      live point + raw readback
├── ui/
│   ├── page.h                     the Page lifecycle contract
│   ├── theme.{h,cpp}              the one and only visual language
│   ├── widgets.{h,cpp}            page_layout(), buttons, rows, slots
│   ├── lvgl_fs.h                  LVGL <-> VFS glue for file-backed images
│   └── assets/                    generated assets (icons, GBK table, pattern)
├── services/                      UI-facing, hardware-agnostic
│   ├── storage_service.{h,cpp}    mount state, listings, whole-file R/W
│   ├── text_service.{h,cpp}       encoding detection and decoding
│   ├── clock_service.{h,cpp}      RTC time and calendar helpers
│   ├── weather_service.{h,cpp}    conditions provider (sample data, see above)
│   └── net_service.{h,cpp}        network state (not configured yet)
└── platform/                      the only layer that touches hardware
    ├── board/board_config.h       every pin, every timing
    ├── display/display_driver.{h,c}   RGB panel + backlight
    ├── lvgl/lvgl_port.{h,cpp}     LVGL <-> esp_lvgl_port binding
    ├── touch/touch_driver.{h,c}   GT911, reset and address sequence
    ├── storage/sd_card.{h,c}      microSD over SPI, CS on the expander
    ├── rtc/pcf85063.{h,c}         RTC over the shared I²C bus
    └── system/                    i2c_bus, io_expander, system_info, str_util
```

`main/CMakeLists.txt` discovers sources and include directories automatically, so
a new file inside an existing layer needs no CMake change.

### Page lifecycle

```cpp
class Page {
    virtual const char *name() const = 0;
    virtual void create(lv_obj_t *parent) = 0;
    virtual void destroy() = 0;
    virtual void on_enter() {}
    virtual void on_leave() {}
};
```

`AppManager` keeps **exactly one page alive**. Pushing a page tears the previous
one down; `pop()` rebuilds the one beneath it. This is deliberate: the memory
requirement is that `Home → Reader → Home` provably releases the Reader
widgets, and the only way to prove that is to actually delete them. The
application task logs the live LVGL object count every 30 s so a leak is
visible in the console instead of only in a heap graph.

---

## Applications

The complete application, and every entry is built and reachable:

| Page | What it does | Status |
|---|---|---|
| Home | 3×3 tile grid, the only navigation root | ✅ |
| Reader | paged text viewer, GBK/UTF-8 decode, drag-to-turn | ✅ |
| File Manager | browse `/sd`, folder/file listing with sizes | ✅ |
| Photos | image listing + decoder-backed viewer | ✅ |
| Notes | text areas with save/load through the storage layer | ✅ |
| Drawing | freehand canvas with palette and clear | ✅ |
| Clock | RTC-backed time, 200 ms tick, 12/24 h toggle | ✅ |
| Calendar | month grid, RTC-derived "today" | ✅ |
| Weather | current conditions + short forecast | ✅ |
| Settings | the preferences the other pages read | ✅ |
| Display Test | Phase-1 acceptance screen (panel geometry, colour bars) | ✅ |
| Touch Test | live point + raw coordinate readback | ✅ |

No page contains a `TODO`, a stub or a placeholder body; the tree is 12 pages,
5 services and 6 platform modules, about 7 600 lines.

The list above is the complete application. **There is no other page, service or
menu entry, and none is planned** — a page outside this list is a regression.
There is likewise **no music, audio or player anything**: not in the sources, the
headers, the CMake files, the UI, the menus or the README.

Every page is laid out by `ui::page_layout()`, which also guarantees the
**close button in the top-right corner** that returns to Home. The button is
placed last in the header row *and* the header's right slot is widened by the
button's own width, so a page that appends chips of its own (Clock's `24H`,
Weather's `Refresh`) can never end up to the right of it. An exit button that
moves between pages is a bug the user has to hunt for.

---

## Phase Progress

| Phase | Content | Status |
|---|---|---|
| 0 | Project skeleton, ESP-IDF + LVGL bring-up, build passes | ✅ |
| 1 | LCD bring-up: RGB panel, 800×480, flush, backlight, test page | ✅ build + hardware |
| 2 | 800×480 UI layout, Theme, shared widgets | ✅ |
| 3 | Touch: driver, LVGL input, Touch Test page | ✅ |
| 4 | Home | ✅ |
| 5 | Reader | ✅ |
| 6 | File Manager | ✅ driver; card verification pending |
| 7 | Photos | ✅ |
| 8 | Notes | ✅ |
| 9 | Drawing | ✅ |
| 10 | Clock / Calendar | ✅ |
| 11 | Weather | ✅ |
| 12 | Settings, stability soak, final acceptance | ✅ settings; soak pending |

---

## Known Issues

1. **The panel's residual shift is pending a final visual check.** The bounce
   buffer is in and `CONFIG_LCD_RGB_RESTART_IN_VSYNC` has been turned **off**
   again (see *LCD Configuration* for why it was working against the bounce
   buffer). The change builds and boots; whether the glass is now clean is a
   physical observation and is the one item not yet closed.

2. **SD read/write on real media is unverified.** No card was available. The
   driver, the mount path and every page above it are complete, and a missing
   card is handled as a normal state rather than an error, but nothing has yet
   read a byte off real flash.

3. **Weather is sample data, and says so.** `weather_service` generates values
   locally, delays 1200 ms to model a round trip, and sets `is_sample = true`;
   the boot log reads `service up (stage 1: sample data)`. Nothing is fetched
   over the network.

4. **The network is not configured.** `net_service` reports
   `status: not configured (WiFi bring-up is a separate increment)`. Weather and
   any future online feature are unaffected by this because they do not use it.

5. **`Theme` styles are formatted for `LV_OBJ_FLAG_SCROLLABLE`-free containers.**
   Pages must clear the scroll flag on their own roots.

6. **The interactive update rate is panel-bound at 39 Hz** and cannot be raised
   without a faster PCLK, which would increase the DMA bandwidth pressure that
   caused the shift. See *Interactive performance* for the measurements and for
   what a number below 39/s would mean.

7. **The acceptance probes are still compiled in.** The `EBOOK_ACCEPTANCE_SWEEP`
   block in `app_manager.cpp` (12-page sweep, geometry dump, calibration frame,
   render-rate probe) plus the matching `target_compile_definitions()` line in
   `main/CMakeLists.txt` are test code and must be deleted before delivery. They
   also keep the LVGL performance overlay visible, which is why an instrumented
   build feels slightly heavier than the shipped one.

---

## Development flow

Per phase: plan → implement → Debug/Release build with zero warnings → hardware
verification → a one-shot `verify_*.py` where it applies → phase report.

Every phase is a separate commit.

### What the machine can check, and what it cannot

Build, flash, boot log, page construction and teardown, object counts and heap
deltas are all machine-checkable and are checked on every build. Two things are
not, and they are the reason this section exists:

**Colour and geometry on the glass.** The acceptance build
(`EBOOK_ACCEPTANCE_SWEEP`) draws a calibration frame on `lv_layer_top()`: a 2 px
magenta border flush with the glass edge, four 40×40 corner blocks (red / green /
blue / yellow at top-left / top-right / bottom-left / bottom-right) and a white
centre cross. It exists because a photograph cannot separate a panel-timing fault
from a layout fault on its own — but it can with this. Judge in this order:

1. Is the magenta border flush with all four edges? If it is inset or clipped,
   the fault is **panel timing**, not layout.
2. Are the corner blocks *in* the corners, the red one top-left? If a block has
   slid onto the opposite edge, the image is displaced — the DMA desync above.
3. Only then look at the page content. `GEOM` lines in the same log give the
   authoritative LVGL coordinates, so a content problem is diagnosed from the
   numbers, never from the photo.

**Touch gestures.** Four corners, centre, a button press, a scroll, and a long
press cannot be scripted from here. `TouchTest` shows the live point plus the raw
readback, so a mis-scaled or mirrored axis is obvious; the corners are what prove
the panel-to-LVGL mapping across the whole surface rather than just at the centre.

> The `GEOM`/`SWEEP`/`RATE` probes and the calibration frame are **test code** and
> are removed before delivery — the acceptance block in `app_manager.cpp` plus
> its `target_compile_definitions()` line in `main/CMakeLists.txt`. They are kept
> in place while the two items above are still open, because the calibration frame
> *is* the instrument.
