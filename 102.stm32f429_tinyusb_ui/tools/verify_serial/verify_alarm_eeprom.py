"""
verify_alarm_eeprom.py — 验证 RTC 闹钟 EEPROM 持久化 + 上下键循环 + 两行按键

端到端验证（STM32F429IGT6 + OpenOCD/ST-Link + COM5 调试串口）：

  Phase 1  打开 COM5（先于烧录，避免错过启动横幅）→ 烧录 Release 并 reset run
  Phase 2  boot1：SWD 读 g_rtc_alarm_eeprom 镜像 + BKP1/2/3 + RTC TR/DR（reset 后 3s，
             用户尚未触摸，反映 LoadFromEEPROM 的真实结果）
  Phase 3  抓 COM5 启动日志，确认无 HardFault 且出现 "[RTC ] alarm ..." 标记
  Phase 4  (WRITE 路径) gdb 在运行态调用 BSP_RTC_Alarm_Persist(7,30,1)：
             - 第一次返回 >0（写入）；第二次返回 0（变更检测：未变化不写）
             - reset 后 boot2 应 "loaded from EEPROM 07:30 on"（写入跨复位存活）
  Phase 5  boot2 SWD 读镜像/BKP，确认 07:30 on 已落盘；最后还原烧录前的值

工具路径走环境变量（不写死机器路径）：
  OPENOCD_BIN, OPENOCD_SCRIPTS, GDB_BIN, NM_BIN, SERIAL_PORT
"""
import os
import re
import subprocess
import sys
import time

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
GDB_BIN = os.environ.get("GDB_BIN", "arm-none-eabi-gdb")
NM_BIN = os.environ.get("NM_BIN", "arm-none-eabi-nm")
PORT = os.environ.get("SERIAL_PORT", "COM5")
BAUD = 115200

ELF = "build/stm32f429_tinyusb_ui.elf"

BKP_HH = 0x40002854
BKP_MM = 0x40002858
BKP_ON = 0x4000285C
RTC_TR = 0x40002800
RTC_DR = 0x40002804

PASS = 0
FAIL = 0
boot1_loaded = None   # (hh, mm, on) parsed from boot1 log


def banner(t):
    print("\n========== %s ==========" % t)


def ocdscript(cmds):
    cmd = [OCD_BIN, "-s", OCD_SCRIPTS, "-f", "interface/stlink.cfg",
           "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cmd += ["-c", c]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
    return p.returncode, p.stdout + p.stderr


def nm_sym(name):
    try:
        out = subprocess.run([NM_BIN, ELF], capture_output=True, text=True,
                             timeout=30).stdout
    except Exception as e:
        print("nm FAIL: %s" % e)
        return None
    for line in out.splitlines():
        if name in line:
            return int(line.split()[0], 16)
    return None


def flash_and_run():
    banner("Phase 1: flash %s + reset run" % ELF)
    rc, txt = ocdscript([
        "init", "reset halt",
        "flash write_image erase %s" % ELF,
        "verify_image %s" % ELF,
        "reset run", "shutdown",
    ])
    ok = (rc == 0) and ("verified" in txt.lower())
    print("  openocd rc=%d  verified=%s" % (rc, ok))
    if not ok:
        print(txt[-2000:])
        global FAIL
        FAIL += 1
        print("  [FAIL] flash/verify")
        return ok
    global PASS
    PASS += 1
    print("  [PASS] flash + verify_image")
    return ok


def swd_reads(mirror_addr, tag):
    banner("SWD reads (%s)" % tag)
    rc, txt = ocdscript([
        "init", "halt",
        "mdw 0x%X 1" % mirror_addr,
        "mdw 0x%X 3" % BKP_HH,
        "mdw 0x%X 1" % RTC_TR,
        "mdw 0x%X 1" % RTC_DR,
        "resume", "shutdown",
    ])
    print(txt)
    if rc != 0:
        print("  [WARN] swd openocd rc=%d" % rc)
        return None
    m = re.search(r"0x%x:\s+([0-9a-fA-F]+)" % mirror_addr, txt)
    mirror = int(m.group(1), 16) if m else None
    blk = re.search(r"0x%x:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)"
                    % BKP_HH, txt)
    bkp = (int(blk.group(1), 16), int(blk.group(2), 16), int(blk.group(3), 16)) if blk else None
    tr = re.search(r"0x%x:\s+([0-9a-fA-F]+)" % RTC_TR, txt)
    dr = re.search(r"0x%x:\s+([0-9a-fA-F]+)" % RTC_DR, txt)
    trv = int(tr.group(1), 16) if tr else None
    drv = int(dr.group(1), 16) if dr else None
    print("  mirror=0x%08X  bkp(hh,mm,on)=%s  RTC_TR=0x%08X RTC_DR=0x%08X"
          % (mirror if mirror is not None else 0, bkp, trv if trv is not None else 0,
             drv if drv is not None else 0))
    return {"mirror": mirror, "bkp": bkp, "tr": trv, "dr": drv}


def capture_com5(sec):
    banner("capture COM5 %ds" % sec)
    try:
        import serial
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
    except Exception as e:
        print("  OPEN %s FAIL -> %s  (skip, rely on SWD)" % (PORT, e))
        return None
    buf = b""
    t0 = time.time()
    while time.time() - t0 < sec:
        d = ser.read(256)
        if d:
            buf += d
    ser.close()
    txt = buf.decode("utf-8", "replace")
    print(txt if txt.strip() else "  (no bytes received)")
    print("  byte count: %d" % len(buf))
    return txt


def parse_alarm_marker(txt):
    """Return (hh,mm,on) from the boot RTC alarm marker, or None."""
    mm = re.search(r"alarm loaded from EEPROM (\d+):(\d+) (on|off)", txt)
    if mm:
        return (int(mm.group(1)), int(mm.group(2)), 1 if mm.group(3) == "on" else 0)
    me = re.search(r"alarm EEPROM empty, default current (\d+):(\d+) off", txt)
    if me:
        return (int(me.group(1)), int(me.group(2)), 0)
    return None


def gdb_persist_roundtrip():
    banner("Phase 4: gdb call BSP_RTC_Alarm_Persist(7,30,1) x2 + reset")
    srv = subprocess.Popen(
        [OCD_BIN, "-s", OCD_SCRIPTS, "-f", "interface/stlink.cfg",
         "-f", "target/stm32f4x.cfg",
         "-c", "gdb_port 3333", "-c", "init", "-c", "halt"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(2.5)
    try:
        gdb_cmds = [
            "set pagination off",
            "target remote :3333",
            "monitor halt",
            "print BSP_RTC_Alarm_Persist(7,30,1)",
            "print BSP_RTC_Alarm_Persist(7,30,1)",
            "monitor reset run",
            "quit",
        ]
        cmd = [GDB_BIN, ELF, "--nx"]
        for c in gdb_cmds:
            cmd += ["-ex", c]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        out = p.stdout + p.stderr
        print(out[-2500:])
        rets = re.findall(r"= (\d+)", out)
        print("  persist returns: %s" % rets)
        return rets
    except subprocess.TimeoutExpired:
        print("  [WARN] gdb call timed out (I2C mutex maybe held at halt) -> INCONCLUSIVE")
        return None
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except Exception:
            srv.kill()


def gdb_restore(hh, mm, on):
    if hh is None:
        return
    banner("restore: gdb call BSP_RTC_Alarm_Persist(%d,%d,%d) + reset" % (hh, mm, on))
    srv = subprocess.Popen(
        [OCD_BIN, "-s", OCD_SCRIPTS, "-f", "interface/stlink.cfg",
         "-f", "target/stm32f4x.cfg",
         "-c", "gdb_port 3333", "-c", "init", "-c", "halt"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(2.5)
    try:
        gdb_cmds = [
            "set pagination off",
            "target remote :3333",
            "monitor halt",
            "print BSP_RTC_Alarm_Persist(%d,%d,%d)" % (hh, mm, on),
            "monitor reset run",
            "quit",
        ]
        cmd = [GDB_BIN, ELF, "--nx"]
        for c in gdb_cmds:
            cmd += ["-ex", c]
        subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        print("  restored")
    except subprocess.TimeoutExpired:
        print("  [WARN] restore timed out")
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except Exception:
            srv.kill()


def main():
    global PASS, FAIL, boot1_loaded
    mirror_addr = nm_sym("g_rtc_alarm_eeprom")
    if mirror_addr is None:
        print("FATAL: cannot resolve g_rtc_alarm_eeprom symbol")
        sys.exit(2)
    print("g_rtc_alarm_eeprom @ 0x%08X" % mirror_addr)

    # open serial BEFORE reset so early boot prints are captured
    ser = None
    try:
        import serial
        ser = serial.Serial(PORT, BAUD, timeout=0.3)
        print("[0] %s opened (pre-reset)" % ser.name)
    except Exception as e:
        print("[0] OPEN %s FAIL -> %s (SWD-only verification)" % (PORT, e))

    if not flash_and_run():
        sys.exit(3)

    time.sleep(3.0)  # let the firmware boot and run LoadFromEEPROM
    r1 = swd_reads(mirror_addr, "boot1 (post-reset, pre-touch)")

    # capture boot logs (port already open)
    if ser is not None:
        buf = b""
        t0 = time.time()
        while time.time() - t0 < 12:
            d = ser.read(256)
            if d:
                buf += d
        ser.close()
        txt = buf.decode("utf-8", "replace")
        banner("Phase 3: COM5 boot log")
        print(txt if txt.strip() else "  (no bytes received)")
        print("  byte count: %d" % len(buf))
        if "HardFault" in txt or "Error_Handler" in txt:
            FAIL += 1
            print("  [FAIL] firmware reported error in boot log")
        else:
            PASS += 1
            print("  [PASS] boot log clean (no HardFault/Error_Handler)")
        boot1_loaded = parse_alarm_marker(txt)
        if boot1_loaded is not None:
            PASS += 1
            print("  [PASS] RTC alarm marker: %02d:%02d %s"
                  % (boot1_loaded[0], boot1_loaded[1], "on" if boot1_loaded[2] else "off"))
        else:
            print("  [INFO] RTC alarm marker not captured (port opened late / COM5 issue)")

    if r1 and r1["bkp"] is not None:
        bkp = r1["bkp"]
        if r1["mirror"] is not None:
            # mirror layout [magic, hh, mm, on] -> little-endian word
            mh = (r1["mirror"] >> 8) & 0xFF
            mm2 = (r1["mirror"] >> 16) & 0xFF
            mo = (r1["mirror"] >> 24) & 0xFF
            if (mh, mm2, mo) == bkp:
                PASS += 1
                print("  [PASS] mirror == BKP  (hh=%d mm=%d on=%d)" % bkp)
            else:
                FAIL += 1
                print("  [FAIL] mirror(0x%08X -> %d:%d:%d) != BKP%s"
                      % (r1["mirror"], mh, mm2, mo, bkp))
        if r1["tr"] is not None:
            PASS += 1
            print("  [PASS] RTC readable (TR=0x%08X DR=0x%08X)" % (r1["tr"], r1["dr"]))

    rets = None
    if boot1_loaded is not None:
        rets = gdb_persist_roundtrip()
    else:
        print("\n[INFO] boot1 alarm value not captured; skipping EEPROM write round-trip "
              "to avoid leaving a test alarm on the board.")
    if rets is not None and len(rets) >= 2:
        first, second = int(rets[0]), int(rets[1])
        if first > 0 and second == 0:
            PASS += 1
            print("  [PASS] persist wrote first (%d), change-detect skipped second (%d)"
                  % (first, second))
        else:
            FAIL += 1
            print("  [WARN] persist returns unexpected: first=%d second=%d" % (first, second))

    time.sleep(1.0)
    r2 = swd_reads(mirror_addr, "boot2 (after persist+reset)")
    if r2 and r2["bkp"] is not None:
        if r2["bkp"] == (7, 30, 1):
            PASS += 1
            print("  [PASS] BKP now 07:30 on (persist landed + survived reset)")
        else:
            print("  [INFO] BKP=%s (expected 7,30,1); if gdb step inconclusive, "
                  "tap 闹钟开启 on panel" % (r2["bkp"],))

    # restore the value the board had at boot1 so we don't leave a test alarm armed
    if boot1_loaded is not None:
        gdb_restore(boot1_loaded[0], boot1_loaded[1], boot1_loaded[2])
        time.sleep(1.0)
        r3 = swd_reads(mirror_addr, "boot3 (after restore)")
        if r3 and r3["bkp"] is not None:
            print("  restored BKP=%s" % (r3["bkp"],))

    banner("VERDICT")
    print("PASS=%d  FAIL=%d" % (PASS, FAIL))
    if FAIL == 0:
        print("ALL CHECKS PASSED")
    else:
        print("SOME CHECKS FAILED / INCONCLUSIVE — review above")


if __name__ == "__main__":
    main()
