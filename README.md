<div align="right">

**English** · [简体中文](./README.zh-CN.md)

</div>

# MCU-Agent

> **AI Agent for real-world MCU firmware development** — build, flash, debug and self-iterate on
> bare-metal targets with `arm-none-eabi-gcc + CMake + Ninja + OpenOCD`, then verify on hardware.

<p>
  <img alt="projects" src="https://img.shields.io/badge/projects-21-2b6cb0">
  <img alt="skills" src="https://img.shields.io/badge/curated%20skills-46-2f855a">
  <img alt="platform" src="https://img.shields.io/badge/platform-STM32%20%7C%20ESP32%20%7C%20RA8D1%20%7C%20Zephyr-c05621">
  <img alt="toolchain" src="https://img.shields.io/badge/toolchain-arm--none--eabi--gcc%20%7C%20CMake%20%7C%20Ninja%20%7C%20OpenOCD-4a5568">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-718096">
</p>

Front-end and desktop applications were the first to be taken over by AI coding agents.
Embedded used to look protected by its barrier — cross-compilation, hardware bring-up, real-time
constraints. In practice that knowledge is **fixed**, and once it is written down as an agent's
operating procedure, the barrier stops being a barrier.

This repository is the working proof: **21 complete MCU projects**, all driven by an AI agent
through the full loop — scaffold → implement → dual-configuration zero-warning build → flash →
on-target verification → regression script.

---

## What problem this solves

Code generation is not the hard part. The hard part is that an MCU target is **not** a container
you can just restart:

| Question | Where it is answered |
|---|---|
| ❓ How does the agent compile, flash, simulate and debug **by itself**, then close the self-iteration loop? | [Development environment](./document/support.md) · [`embedded-verification-acceptance`](https://github.com/zc110747/EmbedSkills) |
| ❓ How should prompts be written so the agent plans MCU work instead of guessing? | [Prompt four-element rule](#the-prompt-four-element-rule) · per-project `prompter.md` |
| ❓ How do you **accept** an AI-built project, and what do you do when it stalls? | [Verification-first workflow](#verification-first-workflow) · `tools/verify_*.py` |

---

## Table of contents

- [Hardware](#hardware)
- [Project matrix](#project-matrix)
- [Quick start](#quick-start)
- [Project gallery](#project-gallery)
  - [STM32H743ZIT6](#stm32h743zit6)
  - [STM32F429IGT6](#stm32f429igt6)
  - [ESP32-S3](#esp32-s3)
  - [Renesas RA8D1](#renesas-ra8d1)
- [Curated skills](#curated-skills)
- [Verification-first workflow](#verification-first-workflow)
- [The prompt four-element rule](#the-prompt-four-element-rule)
- [Lessons learned the hard way](#lessons-learned-the-hard-way)
- [Repository layout](#repository-layout)
- [Roadmap](#roadmap)
- [License](#license)

---

## Hardware

| Board | MCU | Debug probe | Notes |
|---|---|---|---|
| **Luxiaoban H743 core board** | STM32H743ZIT6 (Cortex-M7 @480 MHz) | ST-Link / self-made CMSIS-DAP | Main development platform — 13 projects |
| **ALIENTEK Apollo** | STM32F429IGT6 (Cortex-M4 @180 MHz) | ST-Link | LAN8720A PHY, SDRAM, USB Host |
| **ESP32-S3-DevKitC-1** | ESP32-S3 N16R8 | built-in USB-Serial/JTAG | Arduino CLI + ESP-IDF |
| **ESP32-S3-Touch-LCD-4.3B** | ESP32-S3 N16R8 | built-in USB-Serial/JTAG | Waveshare 800×480 RGB + capacitive touch |
| **RA8D1 Vision Board** | R7FA8D1BH (Cortex-M85 @480 MHz) | ART-Link (CMSIS-DAP) | Renesas, pyOCD + OpenOCD |

---

## Project matrix

Legend: ✅ complete and hardware-verified · 🚧 in progress

### STM32H743ZIT6

| # | Project | Stack | Highlights |
|---|---|---|---|
| 001 | [USB composite device (CDC + MSC)](./001.stm32h743_tinyusb_cdc_msc/README.md) | TinyUSB | One device, two classes, shared endpoints |
| 002 | [UVC camera (OV5640)](./002.stm32h743_tinyusb_uvc_ov5640/README.md) | TinyUSB + DCMI | Multi-buffer capture submitted over USB ISO |
| 003 | [LVGL OLED (SD-card CJK fonts)](./003.stm32h743_lvgl_oled/README.md) | LVGL + FatFs | SD card font loading, Chinese rendering |
| 004 | [SD-card image carousel](./004.stm32h743_sd_oled_img/README.md) | FatFs + JPEG | decode-on-the-fly, direct framebuffer write |
| 005 | [Person detection](./005.stm32h743_person_detect/README.md) | CMSIS-NN | OV5640 + int8 model on Cortex-M7 |
| 006 | [Face detection + display](./006.stm32h743_face_detect/README.md) | CMSIS-NN + PyTorch | Model trained and quantized locally, then deployed |
| 007 | [CMSIS-DAP debug probe](./007.stm32h743_cmsis_dap/README.md) | TinyUSB + SWD/JTAG | Turns the board into a real debugger |
| 008 | [LVGL smart-watch suite](./008.stm32h743_lvgl_mos/README.md) | LVGL + NES | Clock, camera, image viewer, text reader, NES emulator |
| 009 | [Zephyr + LVGL](./009.stm32h743_zephyr/README.md) | Zephyr 3.7 + LVGL 8.4 | Device-tree overlay, FatFs, Zephyr shell |
| 010 | [QSPI virtual USB drive + secure bootloader](./010.stm32h743_boot/README.md) | TinyUSB MSC + HMAC-SHA256 | QSPI FatFs, signed upgrade, app jump |
| 011 | [emWin + FreeRTOS information panel](./011.stm32h743_freertos_emwin/README.md) | STemWin + FreeRTOS | ST7789 SPI6, GBK dot-matrix fonts |
| 012 | [USB CDC ↔ UART4 high-reliability bridge](./012.stm32h743_usb_serial/README.md) | TinyUSB + DMA | Ring buffer, hardware flow control, no RTOS |
| 013 | [Software UART (GPIO + Timer + DMA)](./013.stm32h743_virtual_uart/README.md) | TIM + DMA + EXTI | No USART peripheral used at all; 3-hour soak test |

### STM32F429IGT6

| # | Project | Stack | Highlights |
|---|---|---|---|
| 101 | [LAN management system](./101.stm32f429_net/README.md) | lwIP + FreeRTOS | HTTP/HTTPS, UART shell, telnet shell, SNMP agent |
| 102 | [USB Host + exFAT + LVGL + touch panel](./102.stm32f429_tinyusb_ui/README.md) | TinyUSB Host + LVGL | Real exFAT (not FAT32 in disguise), GT911 over software I²C |

### ESP32-S3

| # | Project | Stack | Highlights |
|---|---|---|---|
| 201 | [FreeRTOS multi-task device monitor](./201.esp32s3_rtos/README.md) | Arduino + FreeRTOS | 4 tasks + software timer, UART command console |
| 202 | [Remote hardware debugger](./202.esp32s3_hw_detect/README.md) | ESP-IDF | UART / ADC / GPIO over WebSocket + MQTT, OTA |
| 203 | [CMSIS-DAP wireless debug probe](./203.esp32s3_wireless_debug/README.md) | TinyUSB HID | SWD + JTAG, 8 MHz clock, Wi-Fi fallback transport |
| 204 | [Touch e-book / utility device](./204.esp32s3_touch_ebook/README.md) | ESP-IDF + LVGL 9 | 800×480 RGB panel, 10 product pages + 2 acceptance pages, GBK reader |

### Renesas RA8D1

| # | Project | Stack | Highlights |
|---|---|---|---|
| 301 | [RA8D1 Vision Board + LVGL](./301.ra8d1_lcd_lvgl/README.md) | RT-Thread Nano + LVGL 9.1 | GLCDC/MIPI-DSI bring-up, RTC, boot splash |
| 302 | [RA8D1 NES emulator + DRW2D](./302.ra8d1_lcd_nes/README.md) | RT-Thread Nano + DRW2D | GPU-accelerated blit, W25Q64 OSPI, console gamepad |

---

## Quick start

### 1. Prepare the environment

The toolchain an AI agent is most comfortable with is the **CLI cross-compile + hardware debug**
path — no IDE required:

```
arm-none-eabi-gcc + cmake + ninja + openocd + pyocd  →  added to the system PATH
python                                                →  for generation / test / verification scripts
vscode + Cortex-Debug                                 →  interactive online simulation
```

Full setup guide and download links: **[document/support.md](./document/support.md)**

> ⚠️ Every tool **must be on the system PATH** and invocable from a bare shell —
> the agent will call it that way.
> ⚠️ ST-Link / J-Link / USB-serial drivers must be installed manually.
> ⚠️ Using a probe other than ST-Link (J-Link, DAP-Link) requires editing the `.cfg` file.

Paste this prompt into your agent to bootstrap everything:

```text
Add an embedded online simulation and debug environment to this project:

1. Tools: python, vscode (Cortex-Debug), arm-none-eabi-gcc, cmake, ninja, openocd —
   all must be added to the system environment.
2. Probe the current environment via CLI first. If a tool is missing, install it under
   D:/software/embed-tools; if present, leave it alone.
3. When finished, verify everything from the command line and report all results.
```

### 2. Unpack the shared dependencies

`Drivers/` and `third_party/` are large and duplicated across projects, so they are **not**
committed. Extract the matching archive into the project root before building:

```text
support_tools/env_support_for_stm32h743.zip   →  each H743 project
support_tools/env_support_for_stm32f429.zip   →  each F429 project
support_tools/env_support_for_esp32s3.zip     →  each ESP32-S3 project
support_tools/env_support_for_zephyr.zip      →  009.stm32h743_zephyr
support_tools/sd_card.zip                     →  the root of the microSD card (fonts, images, ROMs)
```

Extract **directly into the project directory** so that `Drivers/` and `third_party/` end up at
the project root.

### 3. Build, flash and debug

Open the project in VS Code, then `Terminal → Run Task`:

| Task | Action |
|---|---|
| `configure` | CMake configure (respects `CMakePresets.json` when present) |
| `build` | build (default build task) |
| `clean` | clean |
| `flash` | `dependsOn: build` — always builds before flashing |

Press **F5** for `cortex-debug`: it runs `build` as `preLaunchTask`, loads the ELF via GDB,
resets to `entry()` and stops with the SVD peripheral view already loaded.

> Zephyr (009) uses a different path: `python -m west build -b <board>/<soc> -d build -s .`
> A `build_oneclick.bat` is provided in most projects for a one-shot configure + build + flash.

---

## Project gallery

### STM32H743ZIT6

#### 001 · USB composite device (CDC + MSC)

🚀 [001.stm32h743_tinyusb_cdc_msc](./001.stm32h743_tinyusb_cdc_msc/README.md)

![001](./document/image/001-01.jpg)

**Agent-built:** project scaffold, TinyUSB port, composite device descriptor

**Human input:** simulation environment, prompt

**Work:** USB low-level driver → TinyUSB port with CDC and MSC classes → composite device and interface completion

Prompt: [prompter.md](./001.stm32h743_tinyusb_cdc_msc/docs/prompter.md)

#### 002 · UVC camera (OV5640)

🚀 [002.stm32h743_tinyusb_uvc_ov5640](./002.stm32h743_tinyusb_uvc_ov5640/README.md)

![002](./document/image/002-01.jpg)

**Agent-built:** project scaffold, TinyUSB port, UVC implementation

**Human input:** simulation environment, prompt, OV5640 driver

**Work:** integrate the OV5640 driver → port TinyUSB with the UVC class → multi-buffer capture submitted over USB

Prompt: [prompter.md](./002.stm32h743_tinyusb_uvc_ov5640/prompter.md)

#### 003 · LVGL OLED with SD-card CJK fonts

🚀 [003.stm32h743_lvgl_oled](./003.stm32h743_lvgl_oled/README.md)

![003](./document/image/003-01.jpg)

**Agent-built:** project scaffold, LVGL port, SD card / FatFs port

**Human input:** simulation environment, prompt, OLED driver

**Work:** SD card driver + FatFs + Chinese font support → LVGL wired to the SD font → UI with real Chinese text

Prompt: [prompter.md](./003.stm32h743_lvgl_oled/docs/prompter.md)

#### 004 · SD-card image carousel

🚀 [004.stm32h743_sd_oled_img](./004.stm32h743_sd_oled_img/README.md)

![004](./document/image/004-01.jpg)

**Agent-built:** project scaffold, SD/FatFs port, JPEG decoding library

**Human input:** simulation environment, prompt, OLED driver

**Work:** SD card + FatFs + image reading → JPEG decode → blit to the panel

Prompt: [prompter.md](./004.stm32h743_sd_oled_img/docs/prompter.md)

#### 005 · Person detection (OV5640 + CMSIS-NN)

🚀 [005.stm32h743_person_detect](./005.stm32h743_person_detect/README.md)

![005](./document/image/005-01.jpg)

**Agent-built:** project scaffold, CMSIS-NN port, detection model, output rendering

**Human input:** simulation environment, prompt, OV5640 + OLED drivers

**Work:** camera + display drivers → CMSIS-NN framework and an int8 model → multi-buffer capture through the inference pipeline onto the screen

Prompt: [prompter.md](./005.stm32h743_person_detect/docs/prompter.md)

#### 006 · Face detection and display

🚀 [006.stm32h743_face_detect](./006.stm32h743_face_detect/README.md)

![006](./document/image/006-01.jpg)

**Agent-built:** project scaffold, CMSIS-NN port, **local model training and conversion**, output rendering

**Human input:** simulation environment, prompt, camera + display drivers

**Work:** drivers → CMSIS-NN + a face model trained locally with Python → multi-buffer capture through inference onto the screen

> This one took about three hours end to end. The agent downloaded image datasets, wrote its own  
> Python training script, produced the model, converted it to int8 and deployed it. That single
> loop is what changed my mind about Vibe Coding.

Prompt: [prompter.md](./006.stm32h743_face_detect/docs/prompter.md)

#### 007 · CMSIS-DAP debug probe

🚀 [007.stm32h743_cmsis_dap](./007.stm32h743_cmsis_dap/README.md)

![007](./document/image/007-01.jpg)

**Agent-built:** project scaffold, TinyUSB port, CMSIS-DAP protocol

**Human input:** simulation environment, prompt, JTAG/SWD timing (located the official Arm reference)

**Work:** TinyUSB port → CMSIS-DAP protocol → JTAG/SWD bit-banging timing integrated and validated

Prompt: [prompter.md](./007.stm32h743_cmsis_dap/docs/prompter.md)

#### 008 · LVGL smart-watch suite

🚀 [008.stm32h743_lvgl_mos](./008.stm32h743_lvgl_mos/README.md)

| Clock | Camera |
|---|---|
| ![008-1](./document/image/008-01.jpg) | ![008-2](./document/image/008-02.jpg) |

| Text reader | NES emulator |
|---|---|
| ![008-3](./document/image/008-03.jpg) ![008-4](./document/image/008-04.jpg) | ![008-5](./document/image/008-05.jpg) |

**Agent-built:** scaffold, TinyUSB, LVGL, image library, NES emulator, menu shell, virtual serial input

**Human input:** simulation environment, prompt, OLED + OV5640 drivers

**Work:** camera / display / USB / SD drivers → LVGL UI → NES emulator with a host-side virtual keyboard → camera, image viewer, text reader

Prompt: [prompter.md](./008.stm32h743_lvgl_mos/docs/prompter.md)

#### 009 · Zephyr + LVGL

🚀 [009.stm32h743_zephyr](./009.stm32h743_zephyr/README.md)

| LVGL UI | Zephyr shell |
|---|---|
| ![009-1](./document/image/009-01.jpg) | ![009-02](./document/image/009-02.jpg) |

**Agent-built:** scaffold, Zephyr port, LVGL display binding, FatFs binding, UI, shell commands

**Human input:** simulation environment, prompt, OLED driver, feature guidance

**Work:** `nucleo_h743zi` board target, device-tree overlay for ST7789/SPI6, SD-card font path

Prompt: [prompter.md](./009.stm32h743_zephyr/doc/prompter.md)

#### 010 · QSPI virtual USB drive + secure bootloader

🚀 [010.stm32h743_boot](./010.stm32h743_boot/README.md)

| Boot → app jump | Post-upgrade jump | QSPI exposed as a USB drive |
|---|---|---|
| ![010-1](./document/image/010-01.jpg) | ![010-2](./document/image/010-02.jpg) | ![010-3](./document/image/010-03.jpg) |

**Agent-built:** scaffold, QSPI FatFs, TinyUSB MSC, HMAC-SHA256 secure upgrade, firmware jump, OpenOCD + GDB function-level verification

**Human input:** simulation environment, prompt, on-target acceptance

**Work:** bootloader that verifies an HMAC-SHA256 signature over the image, writes QSPI via FatFs and jumps to the app

Prompt: [prompter.md](./010.stm32h743_boot/docs/prompter.md)

#### 011 · emWin + FreeRTOS information panel

🚀 [011.stm32h743_freertos_emwin](./011.stm32h743_freertos_emwin/README.md)

**Agent-built:** scaffold, STemWin GUI stack port, ST7789 SPI6 driver, SD-card GBK fonts, FreeRTOS

**Human input:** simulation environment, prompt, on-target acceptance

**Work:**

1. Rebuilt the whole OLED information panel from 003 with emWin (UI and features aligned 1:1)
2. Resolved STemWin prebuilt-library incompatibility with binutils 2.44 (pinned GNU Arm Embedded 14.2.rel1)
3. Fixed a `GUI_Init` hang (CRC clock not enabled), ARGB channel-order mismatch, `LCD_*` symbol collisions

#### 012 · USB CDC ↔ UART4 high-reliability bridge

🚀 [012.stm32h743_usb_serial](./012.stm32h743_usb_serial/README.md)

**Agent-built:** bridge architecture, ring buffers, USB CDC descriptors and scheduling, flow control, PC-side stress/latency tools
**Human input:** simulation environment, prompt, on-target stress acceptance
**Work:**
1. Transparent bidirectional CDC ACM ↔ UART4 forwarding with **no RTOS**, D-Cache **left enabled**, no MPU carve-out
2. Fixed ring-buffer full/empty ambiguity that silently overwrote data (one byte kept unused)
3. Fixed the parity bit polluting the data byte in 7-bit + parity mode (masked in place)
4. Fixed RTS configured as alternate function but `HwFlowCtl` never enabled → floating pin, dead flow control
5. Traced a 34 ms "phantom latency" to the host-side read pattern (`read(in_waiting or 4096)`)
6. PA0/PA1 loopback plus a one-shot Python regression covering stress, latency, flow control and 7-bit parity

#### 013 · Software UART (GPIO + Timer + DMA)

🚀 [013.stm32h743_virtual_uart](./013.stm32h743_virtual_uart/README.md)

**Agent-built:** software UART TX/RX stack, DMA chaining, RX start-bit detection and frame validation, echo loop, stress harness
**Human input:** simulation environment, prompt, pin-level measurement
**Work:**
1. Generates and parses real UART waveforms using **only** GPIO + Timer + DMA + EXTI — **no USART peripheral at all**
2. TX via timer-triggered DMA into `GPIOx->BSRR`, verified bit period on the pin
3. RX start-bit detection, mid-bit sampling and frame validation (start/stop/parity error counters)
4. Two build back-ends kept in sync: **GCC** (arm-none-eabi 15.3.1 + CMake + Ninja) and **Keil MDK** (ARMCLANG 6.14)
5. Acceptance: 100 kB round-trip, all buffer-boundary sizes, a **1800 s continuous full-duplex soak**
   (1,728,000 / 1,728,000 bytes, 0 mismatches, exactly 960 B/s = 9600/10), plus baud-rate fault injection
   → **24/24 cases passed**

### STM32F429IGT6

#### 101 · LAN management system

🚀 [101.stm32f429_net](./101.stm32f429_net/README.md)

| HTTP | HTTPS |
|---|---|
| ![101-1](./document/image/101-01.jpg) | ![101-2](./document/image/101-02.jpg) |

| UART shell | Telnet shell | SNMP agent + client |
|---|---|---|
| ![101-3](./document/image/101-03.jpg) | ![101-4](./document/image/101-04.jpg) | ![101-5](./document/image/101-05.jpg) |

**Agent-built:** scaffold, Ethernet driver, FreeRTOS port, lwIP port, HTTP/HTTPS services, web UI, UART shell, telnet service, SNMP agent and client

**Human input:** simulation environment, prompt, base drivers, feature guidance

**Work:** LAN8720A (RMII, PHY addr 0) + `ETH_RESET` gated through a PCF8574T expander (P7), SDRAM-backed
FreeRTOS + lwIP, dual-stack HTTP/HTTPS with a self-signed certificate, and four independent management
interfaces. HAL timebase moved from SysTick to TIM7 to avoid the FreeRTOS conflict.

Prompt: [prompter.md](./101.stm32f429_net/prompter.md)

#### 102 · USB Host + exFAT + LVGL + touch panel

🚀 [102.stm32f429_tinyusb_ui](./102.stm32f429_tinyusb_ui/README.md)

**Agent-built:** scaffold, TinyUSB Host (MSC), FatFs exFAT, LVGL, GT911/GT9147 software-I²C touch, FreeRTOS + SDRAM, `PRINT_LOG` logging

**Human input:** simulation environment, prompt, on-target acceptance

**Work:**
1. TinyUSB host stack reading a USB flash drive on F429 — **real exFAT support**, not FAT32 in disguise
2. LVGL on an 800×480 FMC 8080 LCD with GBK fonts from both SD card and USB drive, with a startup loader fallback
3. GT911 touch interrupt path plus three layers of interrupt-storm protection and I²C bus recovery

### ESP32-S3

#### 201 · FreeRTOS multi-task device monitor

🚀 [201.esp32s3_rtos](./201.esp32s3_rtos/README.md)

**Agent-built:** FreeRTOS multi-task scaffold (tasks / queues / semaphores / mutexes / software timers), ESP32-S3 Arduino core

**Human input:** simulation environment, prompt

**Work:** a reusable FreeRTOS base project with all GPIO/pins centralized in `config.h` (never guessed),
4 tasks + 1 software timer forming the monitor, and a UART command console. Zero-warning build,
CH343 download, dual-core info and task-table acceptance.

#### 202 · Remote hardware debugger

🚀 [202.esp32s3_hw_detect](./202.esp32s3_hw_detect/README.md)

**Agent-built:** modular event bus, UART/ADC/GPIO monitors, Wi-Fi STA+AP, MQTT, Web + WebSocket UI, OTA, config persistence

**Human input:** simulation environment, prompt, hardware constraints

**Work:** turns an ESP32-S3 into a **networked hardware debug gateway** exposing the target's UART log,
external ADC voltage (ADS1115, with fallback to the internal ADC) and GPIO state to a browser,
a host application, or a future AI agent. GPIO48 (on-board WS2812B) is reserved for a dedicated LED
controller and GPIO19/20 (USB D-/D+) are permanently excluded from general GPIO use.

See also: [`user_document.md`](./202.esp32s3_hw_detect/user_document.md) · [`DELIVERY.md`](./202.esp32s3_hw_detect/DELIVERY.md) · [prompter.md](./202.esp32s3_hw_detect/prompter.md)

#### 203 · CMSIS-DAP wireless debug probe

🚀 [203.esp32s3_wireless_debug](./203.esp32s3_wireless_debug/README.md)

**Agent-built:** CMSIS-DAP v1 command handling, SWD/JTAG bit-bang engine, debug engine, Wi-Fi transport, OpenOCD probe configuration

**Human input:** simulation environment, prompt, on-target bring-up against a real target board

**Work:**
1. CMSIS-DAP v1 over TinyUSB HID (single HID channel, fixed VID/PID), supporting both SWD and JTAG
2. Performance: CPU at 240 MHz, hot paths in `IRAM_ATTR`, maximum GPIO drive strength, hot-path logging
   downgraded, clock ceiling raised to 8 MHz
3. USB / Wi-Fi transport arbitration with `tud_mounted()` as the host-present test; begins a SoftAP fallback
   when no USB host is attached
4. Fixed a JTAG `Invalid ACK (4)` FAULT (a `bool` parameter promotion kept TDI high) and corrected the
   last-bit timing of Shift-IR
5. Statically reserved PSRAM pool plus tightened Wi-Fi buffers, leaving room for DAP-over-WiFi

#### 204 · Touch e-book / utility device

🚀 [204.esp32s3_touch_ebook](./204.esp32s3_touch_ebook/README.md)

**Agent-built:** full LVGL 9 application, ten product pages, GBK dot-matrix reader, panel and touch bring-up

**Human input:** simulation environment, prompt, panel schematic analysis, on-target acceptance

**Work:** an e-book / utility UI on the Waveshare ESP32-S3-Touch-LCD-4.3B (800×480 RGB, capacitive touch).
Pages: Home, Reader, File Manager, Photos, Notes, Drawing, Clock, Calendar, Weather, Settings, plus the Display Test and Touch Test acceptance screens. 

The page set is **closed** — anything outside it is a regression. 

The reader uses the classic HZK-style GBK dot-matrix fonts from the SD card (12/16/24/32),
whose 23940-code-point index differs from the older `gbk_table`.

Prompts: [prompter-step1.md](./204.esp32s3_touch_ebook/doc/prompter-step1.md) · Plan: [plan-4reqs.md](./204.esp32s3_touch_ebook/doc/plan-4reqs.md)

### Renesas RA8D1

#### 301 · RA8D1 Vision Board + LVGL

🚀 [301.ra8d1_lcd_lvgl](./301.ra8d1_lcd_lvgl/README.md)

**Stack:** RT-Thread Nano 5.0.2 + LVGL v9.1.0 + Arm GNU GCC 15.3.1 + CMake/Ninja + OpenOCD/pyOCD

**Agent-built:** board bring-up, FSP integration, GLCDC/MIPI-DSI path, LVGL port, RTC driver, boot splash, phase verification scripts

**Human input:** simulation environment, prompt, probe configuration

**Work (phases 0 → 8, all ✅):**

- Phase 0.5/0.6 — bare-metal GCC minimum project and a working OpenOCD + pyOCD debug chain
  (DFP pack target fails on ART-Link; `target_override: cortex_m` + FLM injection works)
- Phase 1 — RT-Thread Nano + LED + SCI9 console shell (`led on|off|blink`), 15/15 verify
- Phase 2–4 — GLCDC LCD, CEU camera, LVGL v9.1.0 on the framebuffer, 7/7 verify
- Phase 5–6 — RGB666 parallel path replaced with GLCDC → DSI bridge for the real 480×360 MIPI panel;
  CST812T touch proven correct at the register level via SWD but **the controller NACKs every I²C
  transaction** → judged a panel/hardware fault
- Phase 7 — touch removed, converged to a single-page telemetry view with a hardware RTC.
  Trap found: `RCR4.RCKSEL` is **inverted** relative to its name (`0 = subclock`, `1 = LOCO`);
  the wrong value ran the clock **10.63× fast**
- Phase 8 — replaced the frozen reset colour-bar with a real boot splash; captured the transient
  startup frame by resetting and halting after 0.55 s over SWD

#### 302 · RA8D1 NES emulator

🚀 [302.ra8d1_lcd_nes](./302.ra8d1_lcd_nes/README.md)

**Stack:** RT-Thread Nano + DRW2D (D/AVE 2D) + W25Q64 over OSPI

**Work:** NES emulator on the RA8D1 Vision Board rendering to a 480×360 MIPI-DSI panel through the
DRW2D blitter. The 6502/PPU interpreter costs ~27 M cycles per frame (~13 fps at 480 MHz); the
present path via `d2_blitcopy` costs 0.61 M (7% of a 60 Hz budget). No physical gamepad is wired to
the emulator — the console is the controller, decoded into a lock-free joypad bitmask.

> ⚠️ Two documented deviations from the original plan: **USB MSC and the W25Q64 FatFs are not
> implemented** — ROMs reach the emulator through an SWD staging window (`nes stage` → `nes runmem`).
> And `-O1`/`-O2` break the RT-Thread console (lines truncate mid-format), so both presets build
> at `-Og`. Both are recorded in the project README rather than left for the next person to discover.

---

## Curated skills

Everything learned across these 21 projects is distilled into a separate, reusable repository:

### 👉 [zc110747/EmbedSkills](https://github.com/zc110747/EmbedSkills)

> **46 skills · 123 reference documents** — written as `agent_created: true` Agent Skills, so the agent
> **auto-loads** the relevant one when it hits the corresponding problem.

This is the most valuable output of the whole effort. Instead of writing lessons into a README nobody
reads, each one becomes a rule the agent can act on. Three admission criteria apply:

| Gate | Meaning |
|---|---|
| **Decidable** | Expresses as "if X then Y", not a war story |
| **Reproducible** | Still true on another board, or explicitly scoped to a chip |
| **Sourced** | Pins, registers, clocks and part numbers are verified — anything unchecked is labelled |

The three-tier loading model is what keeps it cheap: only `name` + `description` are resident every
session (~3 KB total); the `SKILL.md` body loads on trigger-word match; `references/*.md` load only
when the body points at them.

| Domain | Skills |
|---|---|
| **MCU / Embedded (12)** | `mcu-ai-agent-workflow` · `embedded-windows-toolchain` · `stm32-build-and-scaffold` · `stm32-peripherals-and-memory` · `mcu-debug-forensics` · `embedded-verification-acceptance` · `mcu-logging` · `mcu-code-style` · `mcu-code-style-apply` · `mcu-cpp-development` · `embedded-config-portal` · `avr-flash-ram` |
| **Display / GUI (2)** | `lvgl-font-and-render` · `lvgl9-mcu-porting` |
| **Platform porting (4)** | `zephyr-stm32-porting` · `esp32-platform-notes` · `ra8d1-fsp-platform-notes` · `ra8d1-vscode-pyocd-debug` |
| **SOC / Video / Edge AI (1)** | `soc-video-and-edge-ai` |
| **Host app / C++20 coroutines (3)** | `app-cpp20-coroutine-runtime` · `app-select-relay-server` · `app-host-load-acceptance` |
| **Desktop (.NET / C#) (2)** | `winforms-desktop-app` · `piper-offline-tts` |
| **Robotics / multi-physics (5)** | `truth-source-and-model-vendoring` · `robotics-mechanism-and-sim` · `robotics-link-diagnostics` · `arm-photo-texturing` · `motor-telemetry-alias-proof` |
| **Front-end / Windows GUI forensics (3)** | `web-headless-verification` · `win-gui-screenshot-verify` · `wpf-realtime-sim` |
| **Static site / WebView shell (4)** | `web-static-site-pipeline` · `web-local-progress-store` · `app-webview-shell` · `android-apk-no-gradle` |
| **Reverse-engineering (4)** | `replica-recon` · `replica-memory-engine` · `replica-ui-fidelity` · `replica-mock-harness` |
| **Tooling (6)** | `schematic-pdf-netlist` · `pyocd-flm-inject-debug` · `mail-weekly-digest` · `deterministic-clock-testing` · `git-silent-source-loss` · `win-batch-sandbox-probe` |

### Install

```bash
git clone https://github.com/zc110747/EmbedSkills.git
cd EmbedSkills

# single skill
cp -r stm32-build-and-scaffold ~/.workbuddy/skills/

# all of them
for d in */; do
  [ -f "$d/SKILL.md" ] && cp -r "$d" ~/.workbuddy/skills/
done
```

Skills take effect in a **new session**. Copy the whole directory (including `references/` and
`scripts/`) — copying only `SKILL.md` leaves every "see references/…" pointer dangling.

> The global `~/.workbuddy/skills/` directory is the single authoritative copy; every project uses it
> directly. The mirror is kept in sync with `EmbedSkills/sync_skills.sh` (`--check` / `--prune` / `--restore` / `--list`).

---

## Verification-first workflow

An AI-built project is only done when it has been **proven on hardware**. The rhythm used throughout:

```text
plan  →  confirm  →  implement  →  Debug + Release zero-warning build
      →  flash  →  serial / on-target verification
      →  extend a one-shot tools/verify_*.py  (prints PASS / FAIL counts)
      →  delivery checklist
```

Every project ships a `tools/verify_*.py` harness that runs the whole suite in one shot and prints a
pass/fail tally. It is *not* a formality — it catches the cases where a build succeeds but the hardware
quietly does nothing:

| Project | Harness | Result |
|---|---|---|
| 013 | `tools/verify_stress.py` | 24/24 passed — 1800 s full-duplex soak at exactly 960 B/s |
| 301 | `tools/verify/verify_phase8.py` | 10 passed / 0 failed / 2 manual — SWD framebuffer pixel assertions |
| 302 | `tools/verify/dump_fb.py` | SWD readback of the SDRAM framebuffer, then pixel comparison |
| 012 | PC-side stress + latency + flow control + 7-bit parity | one-shot Python regression |

Three rules that make it work:

1. **Assert on the physical result, not the API's return value.** "The LED driver returned `HAL_OK`"
   proves nothing; reading back `PODR` over SWD proves something.
2. **Distinguish environment failures from regression failures.** A missing `third_party/` (gitignored)
   is reported separately so a genuine break never hides behind it.
3. **Never let a probe write into the shipped firmware.** Instrumentation that feeds synthetic data must
   be removed before delivery.

---

## The prompt four-element rule

Every project carries the exact prompt that produced it (`prompter.md`). The working structure:

| Element | Content |
|---|---|
| **Functional requirements** | What the thing does, in observable behaviour |
| **Hardware information** | MCU, pins, peripherals, clocks, part numbers — stated, never left to be guessed |
| **Development steps** | Phased and ordered, so the agent always has a next concrete action |
| **Verification means** | How each phase proves itself (serial output, SWD readback, measurement) |

> Do not ask the agent to "build a result". Give it a **suggestion, a sequence and a target** —
> then it can walk the path itself.

---

## Lessons learned the hard way

🏷️ **Have a precise picture of the project before writing the prompt.**
Functional requirements, hardware details, development steps and verification methods. Asking for an
outcome produces a plausible-looking outcome; giving steps and a target produces working firmware.

🏷️ **Hand over every driver you already have.**
Internal peripherals (RTC, GPIO, I²C) are no trouble for the agent. External parts (LCD, CMOS sensor)
are a different story — when they fail, the agent can loop for hours. A known-good driver source beats
generation every time.

🏷️ **Provide a complete simulation environment.**
Writing code is only step one. Unverifiable code still has to be understood later before it can be
debugged — and that cost is not small. A closed loop is the precondition for iteration.

🏷️ **Intervene early and by hand.**
Embedded work needs cross-compilation and remote debugging. If the probe disconnects or the device
misbehaves, the whole flow blocks. During the USB projects it stalled for half an hour simply because
the device was never re-enumerated.

Counter-examples worth remembering: an incorrect CMOS/OLED register configuration cost an hour with no
progress, and a missing link in a CMake file left the entry point unlinked so startup hung in a loop —
another half hour. Both resolved instantly once a known-good driver was supplied and a human stepped in.

---

## Repository layout

```text
MCU-Agent/
├── 001..013.*/              STM32H743ZIT6 projects
├── 101..102.*/              STM32F429IGT6 projects
├── 201..204.*/              ESP32-S3 projects
├── 301..302.*/              Renesas RA8D1 projects
│   ├── app|applications/    application layer
│   ├── bsp/                 board support layer
│   ├── Drivers/             vendor HAL (gitignored, from support_tools/)
│   ├── third_party/         upstream libraries (gitignored, from support_tools/)
│   ├── tools/verify/        one-shot acceptance scripts
│   ├── .vscode/             configure / build / clean / flash + cortex-debug
│   └── README.md            per-project details
├── document/
│   ├── support.md           environment setup guide
│   ├── image/               screenshots used in this README
│   ├── stm32h743/            schematic + hardware notes
│   ├── stm32f429/            schematic + hardware notes
│   └── esp32s3/              schematic + hardware notes
├── support_tools/           shared drivers/third_party archives + SD card content
├── project.list             project index
├── vscode_unify_convention.md   unified .vscode rules across all projects
└── LICENSE                  MIT
```

Per-project dependency direction is strict: `applications → bsp → {Drivers, third_party}`.
Reversed dependencies are not allowed.

**Unified `.vscode` convention** (three hard rules, see [vscode_unify_convention.md](./vscode_unify_convention.md)):

1. Tools are invoked by **bare name** — they must be on the system PATH; never hardcode an install path.
2. `tasks.json` keeps **exactly four tasks**: `configure` / `build` / `clean` / `flash`.
3. `openocd.cfg` and `*.svd` live at the **project root**, not in an `openocd/` subdirectory.

---

## Roadmap

- [x] 001–013, 101–102, 201–204, 301–302 — 21 projects complete and hardware-verified
- [x] 46 curated skills published in [EmbedSkills](https://github.com/zc110747/EmbedSkills)
- [x] Bilingual README — English (default) + Simplified Chinese
- [ ] CI consistency check across the skill repository

---

## License

[MIT](./LICENSE) © 2026 听心跳的声音

<sub>Projects in this repository were built in collaboration with an AI agent; each project directory documents which parts were agent-generated and which required human input.</sub>
