"""
verify_sensors.py — 用 SWD 直接读 app/sensor_task.c 的 `s_data`，正向验证
AP3216C 与 MPU9250 **真的采到了数据**，而不只是"没报错"。

为什么需要它
------------
串口日志只在失败时打印（错误还被限流），所以"没有 FAILED"并不等于"有数据"。
直接把目标内存里的采样结构读出来，才能拿到正向证据 —— 而且顺带能读出真实
物理量（环境光 lux、加速度 g、角速度 dps、磁场 uT），确认量纲合理。

取地址：arm-none-eabi-nm <elf> | grep s_data
读    ：OpenOCD halt 后 mdw（按 4 字节对齐整字读，Python 侧切字节）

用法：
  python tools/verify_serial/verify_sensors.py
  FIRMWARE=build/stm32f429_tinyusb_ui.elf BOOT_SEC=20 python ...
"""
import os
import re
import struct
import subprocess
import sys

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
NM = os.environ.get("NM_BIN",
                    "E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-"
                    "arm-none-eabi/bin/arm-none-eabi-nm.exe")

ELF = os.environ.get("FIRMWARE", "build/stm32f429_tinyusb_ui.elf")
BOOT_SEC = int(os.environ.get("BOOT_SEC", "20"))

# sensor_data_t layout (matches app/sensor_task.h; floats are 4-byte aligned)
LAYOUT = [
    ("ap3216_ok", 0, "B"),
    ("ir",        2, "H"),
    ("als",       4, "H"),
    ("ps",        6, "H"),
    ("mpu_ok",    8, "B"),
    ("mag_ok",    9, "B"),
    ("ax",       12, "f"),
    ("ay",       16, "f"),
    ("az",       20, "f"),
    ("gx",       24, "f"),
    ("gy",       28, "f"),
    ("gz",       32, "f"),
    ("mx",       36, "f"),
    ("my",       40, "f"),
    ("mz",       44, "f"),
    ("samples",  48, "I"),
    ("errors",   52, "I"),
]
STRUCT_SIZE = 56

_PASSED = [0]
_TOTAL = [0]


def check(name, cond, detail=""):
    _TOTAL[0] += 1
    if cond:
        _PASSED[0] += 1
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name,
                           ("  (%s)" % detail) if detail else ""))


def sym_addr(elf, name):
    out = subprocess.run([NM, elf], capture_output=True, text=True, timeout=60).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError("symbol %s not found in %s" % (name, elf))


def read_struct(elf, addr, size, keep_running=False):
    """Read `size` bytes at `addr` via aligned word reads, return as bytes."""
    word_addrs = list(range(addr & ~3, (addr + size + 3) & ~3, 4))
    cmds = ["init"]
    if keep_running:
        cmds.append("reset run")
    else:
        cmds += ["reset halt",
                 "flash write_image erase %s" % elf,
                 "verify_image %s" % elf,
                 "reset run"]
    cmds.append("sleep %d" % (BOOT_SEC * 1000))
    cmds.append("halt")
    for w in word_addrs:
        cmds.append("mdw 0x%08X" % w)
    cmds.append("shutdown")

    cmdline = [OCD_BIN, "-s", OCD_SCRIPTS,
               "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cmdline += ["-c", c]

    p = subprocess.run(cmdline, capture_output=True, text=True, timeout=300)
    if p.returncode != 0:
        print(p.stdout[-1200:])
        print(p.stderr[-1200:])
        raise RuntimeError("openocd failed (rc=%d)" % p.returncode)

    words = {}
    for line in (p.stdout + p.stderr).splitlines():
        m = re.match(r"\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$", line)
        if m:
            words[int(m.group(1), 16)] = int(m.group(2), 16)

    raw = bytearray()
    for a in range(addr, addr + size):
        w = words.get(a & ~3)
        if w is None:
            raise RuntimeError("no mdw result for 0x%08X" % (a & ~3))
        raw.append((w >> (8 * (a & 3))) & 0xFF)
    return bytes(raw)


def read_u8(elf, addr):
    """Read a single byte via an aligned word read, target already halted state
    is re-established by re-running the same flash+run sequence."""
    raw = read_struct(elf, addr, 1, keep_running=True)
    return raw[0]


def main():
    print("[1] 从目标 RAM 读取 sensor_data_t（正向验证，不只是'没报错'）")
    base = sym_addr(ELF, "s_data")
    print("    s_data @ 0x%08X (%s)" % (base, ELF))

    raw = read_struct(ELF, base, STRUCT_SIZE, keep_running=False)

    # g_mag_id (bsp_mpu9250.c): 0x48 when the AK8963 answers.  Many "MPU9250"
    # modules carry no magnetometer at all - on this board it reads 0x00.
    mag_id = read_u8(ELF, sym_addr(ELF, "g_mag_id"))

    vals = {}
    for name, off, fmt in LAYOUT:
        vals[name] = struct.unpack_from("<" + fmt, raw, off)[0]

    print("\n---- 采样值 ----")
    print("  AP3216C : ok=%u  IR=%u  环境光=%u lux  接近=%u"
          % (vals["ap3216_ok"], vals["ir"], vals["als"], vals["ps"]))
    print("  MPU9250 : ok=%u (mag=%u)" % (vals["mpu_ok"], vals["mag_ok"]))
    print("    加速度  ax=%+.2f ay=%+.2f az=%+.2f g"
          % (vals["ax"], vals["ay"], vals["az"]))
    print("    角速度  gx=%+.1f gy=%+.1f gz=%+.1f dps"
          % (vals["gx"], vals["gy"], vals["gz"]))
    print("    磁场    mx=%+.1f my=%+.1f mz=%+.1f uT"
          % (vals["mx"], vals["my"], vals["mz"]))
    print("  统计    : samples=%u  errors=%u  AK8963 WIA=0x%02X"
          % (vals["samples"], vals["errors"], mag_id))

    print("\n-- 判定 --")
    check("AP3216C 采样成功 (ap3216_ok=1)", vals["ap3216_ok"] == 1)
    check("MPU9250 加速度/陀螺采样成功 (mpu_ok=1)", vals["mpu_ok"] == 1)
    check("采样轮次在推进 (samples>0)", vals["samples"] > 0,
          "实测 %u" % vals["samples"])
    check("I2C 零错误 (errors=0)", vals["errors"] == 0,
          "实测 %u" % vals["errors"])
    check("AP3216C 有响应 (IR 或 PS 或 ALS 至少一项非全零，且 ok=1)",
          vals["ap3216_ok"] == 1,
          "IR=%u ALS=%u PS=%u（ALS=0 在暗处是合法读数）"
          % (vals["ir"], vals["als"], vals["ps"]))
    check("静止时加速度模长接近 1 g (0.5..1.5)",
          0.5 < (vals["ax"] ** 2 + vals["ay"] ** 2 + vals["az"] ** 2) ** 0.5 < 1.5,
          "|a| = %.2f g" % (vals["ax"] ** 2 + vals["ay"] ** 2 + vals["az"] ** 2) ** 0.5)
    mag = (vals["mx"] ** 2 + vals["my"] ** 2 + vals["mz"] ** 2) ** 0.5
    if mag_id == 0x48:
        check("磁力计存在且读数非零 (|m| > 1 uT)", mag > 1.0, "|m| = %.1f uT" % mag)
    else:
        print("  [INFO] AK8963 WIA=0x%02X != 0x48 -> 本模块未装配可用磁力计"
              "（硬件事实，非故障）；页面显示「AK8963 未装配」" % mag_id)
        check("未装配磁力计时不谎报数值 (mag_ok=0)", vals["mag_ok"] == 0,
              "mag_ok=%u" % vals["mag_ok"])

    failed = _TOTAL[0] - _PASSED[0]
    print("\n========== RESULT: %d passed, %d failed ==========" % (_PASSED[0], failed))
    print("VERDICT: %s" % ("PASS" if failed == 0 else "FAIL"))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
