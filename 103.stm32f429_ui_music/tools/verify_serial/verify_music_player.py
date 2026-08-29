"""
verify_music_player.py -- STM32F429 音乐播放器固件真机验收
============================================================

流程：
  1. 自动探测 USART3 调试串口落在 COM4 / COM7 中的哪一个
     （板子有两路 CH340，只有 USART3 会打印启动横幅）
  2. OpenOCD + STLink 烧录最新固件并 reset run
  3. 抓取 ~30 秒串口输出
  4. 按分组判定 pass/fail：启动 / 加载器(字库) / 播放器(音频设备+扫描)

路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, SERIAL_PORT, FIRMWARE

用法：
  python verify_music_player.py
  SERIAL_PORT=COM7 python verify_music_player.py
  FIRMWARE=build/stm32f429_ui_music.elf python verify_music_player.py
"""
import os
import sys
import time
import serial
import subprocess

OCD_BIN = os.environ.get("OPENOCD_BIN", r"D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS",
                             r"D:/software/ST/OpenOCD/share/openocd/scripts")
PROJ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OCD_CFG = os.path.join(PROJ, "openocd.cfg")
ELF = os.environ.get("FIRMWARE", os.path.join(PROJ, "build_dbg",
                                              "stm32f429_ui_music.elf"))
PORT_ENV = os.environ.get("SERIAL_PORT", "")
BAUD = 115200
CANDIDATES = ["COM4", "COM7"]
CAPTURE_SEC = 30
LAST_LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "last_music_capture.log")


def log(msg):
    print(msg, flush=True)


def flash():
    # OpenOCD's TCL `-c` string treats backslashes as escapes; use forward slashes.
    elf_fwd = ELF.replace("\\", "/")
    cfg_fwd = OCD_CFG.replace("\\", "/")
    cmd = [OCD_BIN, "-s", OCD_SCRIPTS, "-f", cfg_fwd,
           "-c", 'program "%s" verify reset exit' % elf_fwd]
    log("[OCD ] %s" % " ".join(cmd))
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        log("[OCD ] TIMEOUT")
        return False, "", "timeout"
    out = (r.stdout or "") + (r.stderr or "")
    ok = (r.returncode == 0) and ("** Programming Finished **" in out) \
        and ("** Verified OK **" in out)
    for line in out.splitlines():
        if ("Error" in line) or ("error" in line) or ("**" in line):
            log("       " + line.strip())
    return ok, out, ""


def open_port(port):
    try:
        s = serial.Serial(port, BAUD, timeout=0.25)
        s.reset_input_buffer()
        return s
    except Exception as e:  # noqa
        log("      [PORT] %s open error: %s" % (port, e))
        return None


def capture_all(ports, sec):
    """Read from every open port for `sec` seconds; return {port: text}."""
    buf = {p: [] for p in ports}
    deadline = time.time() + sec
    while time.time() < deadline:
        for p, s in ports.items():
            if s is None:
                continue
            try:
                chunk = s.read(256)
            except Exception:  # noqa
                chunk = b""
            if chunk:
                buf[p].append(chunk.decode("latin-1", "replace"))
        time.sleep(0.02)
    return {p: "".join(v) for p, v in buf.items()}


def detect_uart(ports, texts):
    for p, t in texts.items():
        if "System Init" in t:
            return p
    # fallback: any port with boot-looking ascii
    for p, t in texts.items():
        if ("SDRAM" in t) or ("FreeRTOS" in t) or ("[UI" in t):
            return p
    return None


def check_group(name, text, markers):
    res = []
    for m in markers:
        res.append((m, m in text))
    npass = sum(1 for _, ok in res if ok)
    log("  [%s] %d/%d" % (name, npass, len(markers)))
    for m, ok in res:
        log("      %s %s" % ("PASS" if ok else "FAIL", m))
    return npass, len(markers), res


def main():
    log("=== STM32F429 music player verification ===")
    log("[ELF ] %s" % ELF)
    if not os.path.exists(ELF):
        log("ELF not found: %s" % ELF)
        sys.exit(2)

    # 1) open candidate ports FIRST (so the reset during flash is captured)
    if PORT_ENV:
        cand = [PORT_ENV]
    else:
        cand = CANDIDATES
    ports = {}
    for c in cand:
        s = open_port(c)
        ports[c] = s
        log("[PORT] %s -> %s" % (c, "open" if s else "unavailable"))
    if not any(ports.values()):
        log("[FAIL] no serial port openable")
        sys.exit(1)

    # 2) flash (resets the board -> boot banner flows into the open ports)
    ok, out, err = flash()
    if not ok:
        log("[FAIL] flash failed")
        sys.exit(1)
    log("[OK  ] flash + verify + reset done")

    # 3) capture
    log("[CAP ] capturing %ds ..." % CAPTURE_SEC)
    time.sleep(0.5)
    texts = capture_all(ports, CAPTURE_SEC)

    uart = detect_uart(ports, texts)
    if uart is None:
        log("[FAIL] USART3 not detected on any candidate port")
        for p, t in texts.items():
            log("  --- %s (%d bytes) ---" % (p, len(t)))
            log(t[:600])
        sys.exit(1)
    text = texts[uart]
    log("[UART] USART3 detected on %s (%d bytes captured)" % (uart, len(text)))

    with open(LAST_LOG, "w", encoding="utf-8") as f:
        f.write(text)

    # 4) evaluate groups
    log("--- BOOT ---")
    boot_m = ["System Init", "SDRAM Init OK",
              "FreeRTOS Heap configured", "USB Host Init",
              "starting LCD + LVGL bring-up",
              "boot screen: wait for system start..."]
    bp, bt, _ = check_group("BOOT", text, boot_m)

    log("--- LOADER (font) ---")
    loaded_sd = "main screen (fonts from 1:)" in text
    loaded_usb = "main screen (fonts from 0:)" in text
    timed_out = "timeout: sdcard and usb loader failed!" in text
    if loaded_sd:
        log("  PASS fonts from microSD (1:)")
    elif loaded_usb:
        log("  PASS fonts from USB (0:)")
    elif timed_out:
        log("  INFO loader timed out -- no font media inserted")
    else:
        log("  INFO loader state unclear (still probing?)")

    log("--- PLAYER (audio) ---")
    ply_m = ["[PLY ] init OK", "music player screen up",
             "scanned", "[PLY ] WM8978 init failed",
             "[PLY ] SAI init failed", "audio device init FAILED"]
    # build a focused player check: success markers vs failure markers
    player_ok = ("[PLY ] init OK" in text) and ("music player screen up" in text)
    # count scanned tracks
    import re
    m_sc = re.search(r"scanned (\d+) track", text)
    n_tracks = int(m_sc.group(1)) if m_sc else None
    if player_ok:
        log("  PASS player entered")
        if n_tracks is not None:
            log("  INFO tracks found: %d" % n_tracks)
        # decode markers
        if "[DEC ] MP3 open" in text:
            log("  INFO MP3 decode started (Hz/ch/kbps logged)")
        if "[DEC ] unsupported extension" in text:
            log("  WARN some files had unsupported extension")
        if "[DEC ] f_open" in text and "failed" in text:
            log("  WARN file open failures during decode")
    else:
        fail_m = [x for x in ply_m if ("failed" in x.lower() or "FAILED" in x)]
        for fm in fail_m:
            if fm in text:
                log("  FAIL %s" % fm)
        if loaded_sd or loaded_usb:
            log("  WARN fonts loaded but player did not reach screen up")

    # 5) verdict
    log("=== VERDICT ===")
    if bp == bt and (loaded_sd or loaded_usb) and player_ok:
        log("BOOT+LOADER+PLAYER all PASS  (tracks=%s)" % n_tracks)
        rc = 0
    elif bp == bt and (loaded_sd or loaded_usb) and not player_ok:
        log("BOOT+LOADER PASS, PLAYER FAIL")
        rc = 1
    elif bp == bt and timed_out:
        log("BOOT PASS; LOADER timed out (insert SD/USB with SYSTEM/FONT + music/)")
        rc = 0  # firmware healthy, just no media
    else:
        log("BOOT incomplete")
        rc = 1
    log("full capture -> %s" % LAST_LOG)
    sys.exit(rc)


if __name__ == "__main__":
    main()
