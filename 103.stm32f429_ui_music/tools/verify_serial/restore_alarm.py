"""
restore_alarm.py - restore the RTC alarm EEPROM to a given value.

Robust variant: instead of calling BSP_RTC_Alarm_Persist at a random running
point (where a suspended FreeRTOS task may hold the I2C mutex and deadlock the
gdb 'print' call), we halt the target at a hardware breakpoint on
BSP_RTC_Alarm_LoadFromEEPROM().  That function runs in main() BEFORE
vTaskStartScheduler(), so BSP_I2C_Lock is a no-op (xTaskGetSchedulerState()
!= taskSCHEDULER_RUNNING) and the EEPROM write goes through with zero
contention.  After the write we reset+run so the firmware re-reads the
restored value.

Usage:
  python restore_alarm.py HH MM ON
  (defaults to 0 2 1 = the user's pre-verification setting 00:02 on)
"""
import os
import re
import subprocess
import sys
import time

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
GDB_BIN = os.environ.get("GDB_BIN", "arm-none-eabi-gdb")
ELF = "build/stm32f429_tinyusb_ui.elf"
BKP_HH, BKP_MM, BKP_ON = 0x40002854, 0x40002858, 0x4000285C

hh = int(sys.argv[1]) if len(sys.argv) > 1 else 0
mm = int(sys.argv[2]) if len(sys.argv) > 2 else 2
on = int(sys.argv[3]) if len(sys.argv) > 3 else 1


def read_bkp():
    """Read BKP1/2/3 via a fresh OpenOCD session. Retries because the ST-Link
    can be briefly locked by the just-terminated debug session. Returns (hh,mm,on)."""
    for _ in range(4):
        subprocess.run(["taskkill", "/IM", "openocd.exe", "/F"],
                       capture_output=True, timeout=10)
        time.sleep(2.0)
        out = subprocess.run(
            [OCD_BIN, "-s", OCD_SCRIPTS, "-f", "interface/stlink.cfg",
             "-f", "target/stm32f4x.cfg", "-c", "init", "-c", "halt",
             "-c", "mdw 0x%X 3" % BKP_HH, "-c", "resume", "-c", "shutdown"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=40).stdout
        m = re.search(r"0x%x:\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)"
                      % BKP_HH, out)
        if m:
            return (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16))
    return None


def restore_once():
    """Halt, break at LoadFromEEPROM (pre-scheduler, I2C lock no-op), persist."""
    srv = subprocess.Popen(
        [OCD_BIN, "-s", OCD_SCRIPTS, "-f", "interface/stlink.cfg",
         "-f", "target/stm32f4x.cfg",
         "-c", "gdb_port 3333", "-c", "init", "-c", "reset halt"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(2.5)
    try:
        gdb_cmds = [
            "set pagination off",
            "set confirm off",
            "target remote :3333",
            "monitor reset halt",
            "thbreak BSP_RTC_Alarm_LoadFromEEPROM",
            "continue",
            "print BSP_RTC_Alarm_Persist(%d,%d,%d)" % (hh, mm, on),
            "monitor reset run",
            "quit",
        ]
        cmd = [GDB_BIN, ELF, "--nx"]
        for c in gdb_cmds:
            cmd += ["-ex", c]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        out = p.stdout + p.stderr
        print(out[-1800:])
        return ("= 0" in out) or ("= 1" in out) or ("= 2" in out)
    except subprocess.TimeoutExpired:
        print("  [WARN] gdb persist timed out")
        return False
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except Exception:
            srv.kill()
        subprocess.run(["taskkill", "/IM", "openocd.exe", "/F"],
                       capture_output=True, timeout=10)


for attempt in range(4):
    print("attempt %d: restore(%d,%d,%d) via pre-scheduler breakpoint ..."
          % (attempt + 1, hh, mm, on))
    ok = restore_once()
    if ok:
        time.sleep(1.5)
        bkp = read_bkp()
        print("  BKP now %s (want %s)" % (bkp, (hh, mm, on)))
        if bkp == (hh, mm, on):
            print("RESTORE OK")
            break
        else:
            print("  BKP mismatch, retrying")
    else:
        print("  attempt inconclusive, retrying")
else:
    print("RESTORE INCONCLUSIVE - set via UI: 上/下 调到 00:02, 再点 闹钟开启")
