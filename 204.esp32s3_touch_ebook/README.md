# Ebook LVGL — Waveshare ESP32-S3-Touch-LCD-4.3B

An e-book / utility device UI built from scratch on **ESP-IDF + LVGL 9** for the
Waveshare **ESP32-S3-Touch-LCD-4.3B** (800×480 RGB panel, capacitive touch).

Ten pages: Home, Reader, File Manager, Photos, Notes, Drawing, Clock, Calendar,
Weather, Settings. Nothing else — the project scope is closed and any page
outside that list is a regression.

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

### Size (measured, Phase 1 build)

| Region | Used | Total | Used |
|---|---|---|---|
| Flash — `.text` | 368 888 B | | |
| Flash — `.rodata` + `.appdesc` + `.tdata` | 127 464 B | | |
| **Image total** | **571 134 B** | 4 194 304 B (app partition) | **13.6 %** |
| Internal SRAM (`dram0_0_seg`) | 62 210 B | 341 760 B | **18.2 %** |
| IRAM (`iram0_0_seg`, 358 144 B) — `.text` + `.vectors` | 16 384 B | | |
| RTC slow | 36 B | 8 192 B | 0.44 % |

Zero compiler warnings.

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
tools\run_idf.bat -p <PORT> flash
```

or, using OpenOCD + ST-Link, the usual `idf.py openocd` / `flash` pair.

> **Note:** PC-side flashing has not been verified on hardware in this session;
> no ESP32-S3 USB CDC port was enumerated on the build host.

---

## Monitor

```bat
tools\run_idf.bat -p <PORT> monitor
```

The console is **USB-Serial-JTAG** (the USB-C port), at whatever baud the host
chooses — it is a USB CDC device, the line rate is nominal.

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
| HSYNC pulse / back / front porch | 8 / 16 / 16 |
| VSYNC pulse / back / front porch | 8 / 16 / 16 |
| Frame buffers | 2, in PSRAM |
| Bounce buffer | none |
| DMA burst | 64 bytes |

> **Not hardware-verified.** These timing values come from the configuration
> that is documented as working on this board model. They have not yet been
> confirmed on a physical panel from this build. If the image is wrong, the
> symptom tells you which knob to turn — horizontal shift or diagonal bands:
> `HSYNC_*` / PCLK polarity; vertical shift or a partial frame: `VSYNC_*`; no
> image at all: lower PCLK. All of them are in `board_config.h`.

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

Touch is wired into LVGL as an input device with the panel as its display, so
coordinates arrive in the panel's landscape orientation and need no application
level remapping.

**Status: not yet implemented (Phase 3).**

---

## SD Card

microSD in SPI mode on SPI2_HOST. Chip select is **not** a GPIO — it is CH422G
EXIO4, active low, so every card transaction has to take the expander lock first.

The card is the storage for books, images and (planned) subset fonts. FatFs is
enabled in the build. The wear-levelled internal partition is **not** used for
user content.

**Status: not yet implemented (Phase 6).**

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
`lvgl_port_acquire()` / `lvgl_port_release()`.

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
├── main.cpp                     boot sequence only
├── app/
│   ├── app_manager.{h,cpp}      page registry, navigation, app task
│   └── pages/
│       └── display_test_page.*  Phase 1 acceptance screen
├── ui/
│   ├── page.h                   the Page lifecycle contract
│   ├── theme.{h,cpp}            the one and only visual language
│   └── assets/                  generated image assets
├── services/                    (Phase 5+)
└── platform/
    ├── board/board_config.h     every pin, every timing
    ├── display/display_driver.* RGB panel + backlight
    ├── lvgl/lvgl_port.*         LVGL <-> esp_lvgl_port binding
    └── system/                  i2c bus, CH422G, system info
```

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

| Page | Status |
|---|---|
| Display Test | ✅ implemented (Phase 1) |
| Home | ⬜ Phase 4 |
| Reader | ⬜ Phase 5 |
| File Manager | ⬜ Phase 6 |
| Photos | ⬜ Phase 7 |
| Notes | ⬜ Phase 8 |
| Drawing | ⬜ Phase 9 |
| Clock | ⬜ Phase 10 |
| Calendar | ⬜ Phase 10 |
| Weather | ⬜ Phase 11 |
| Settings | ⬜ Phase 12 |

The list above is the complete application. There is no other page, service or
menu entry, and none is planned.

---

## Phase Progress

| Phase | Content | Status |
|---|---|---|
| 0 | Project skeleton, ESP-IDF + LVGL bring-up, build passes | ✅ |
| 1 | LCD bring-up: RGB panel, 800×480, flush, backlight, test page | ✅ (build) / ⬜ (hardware) |
| 2 | 800×480 UI layout, Theme, shared widgets | ⬜ |
| 3 | Touch: driver, LVGL input, Touch Test page | ⬜ |
| 4 | Home | ⬜ |
| 5 | Reader | ⬜ |
| 6 | File Manager | ⬜ |
| 7 | Photos | ⬜ |
| 8 | Notes | ⬜ |
| 9 | Drawing | ⬜ |
| 10 | Clock / Calendar | ⬜ |
| 11 | Weather | ⬜ |
| 12 | Settings, stability soak, final acceptance | ⬜ |

---

## Known Issues

1. **RGB timing not verified on hardware.** See *LCD Configuration* above for
   the symptom → knob mapping.
2. **No hardware verification of Phase 0/1 at all.** No ESP32-S3 USB CDC port
   was available to the build host, so `flash`, `monitor` and the panel itself
   are untested. The build is green; the glass is unknown.
3. **Touch not implemented** (Phase 3). The panel currently has no input device.
4. **CJK text is not rendered.** Only Montserrat (Latin) is compiled in. The
   Reader page will mount a subset CJK font from storage; anything Chinese shown
   before that will be blank boxes.
5. **`Theme` styles are formatted for `LV_OBJ_FLAG_SCROLLABLE`-free containers.**
   Pages must clear the scroll flag on their own roots.
6. **No SD, no WiFi, no RTC code yet** — the corresponding phases have not run.

---

## Development flow

Per phase: plan → implement → Debug/Release build with zero warnings → hardware
verification → a one-shot `verify_*.py` where it applies → phase report.

Every phase is a separate commit.
