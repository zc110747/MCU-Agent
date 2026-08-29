"""
verify_log_switch.py — 在串口不可用的情况下（COM5 驱动故障等），用 SWD 读内存
验证 PRINT_LOG 全局开关是否真的生效。

原理
----
PRINT_LOG 关掉后，固件不应该往 UART TX 环形缓冲里放任何一个字节。因此可以直接
从 RAM 里读 bsp_uart.c 的静态变量：

    g_tx_head  (uint16)  TX 环形缓冲写指针
    g_tx_tail  (uint16)  TX 环形缓冲读指针
    g_tx_busy  (uint8)   发送器忙标志
    g_usb_state(全局)    USB 主机状态机（用来确认系统确实完整启动了）

符号地址从 .elf 里用 arm-none-eabi-nm 取（不同构建地址不同），再通过 OpenOCD
的 mdw 在目标 halt 状态下读取。

判定
----
  PRINT_LOG_ENABLE=1  ->  g_tx_head != 0（日志确实进了 TX 环） 且 g_usb_state == 4(MOUNTED)
  PRINT_LOG_ENABLE=0  ->  g_tx_head == 0（一个字节都没发）     且 g_usb_state == 4(MOUNTED)

用法：
  python tools/verify_serial/verify_log_switch.py
  FIRMWARE_ON=build/stm32f429_tinyusb_ui.elf FIRMWARE_OFF=build_nolog/... python ...
"""
import os
import re
import subprocess
import sys
import time

OCD_BIN = os.environ.get("OPENOCD_BIN", "D:/software/ST/OpenOCD/bin/openocd.exe")
OCD_SCRIPTS = os.environ.get("OPENOCD_SCRIPTS", "D:/software/ST/OpenOCD/share/openocd/scripts")
NM = os.environ.get("NM_BIN",
                    "E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-"
                    "arm-none-eabi/bin/arm-none-eabi-nm.exe")

ELF_ON = os.environ.get("FIRMWARE_ON", "build/stm32f429_tinyusb_ui.elf")
ELF_OFF = os.environ.get("FIRMWARE_OFF", "build_nolog/stm32f429_tinyusb_ui.elf")

BOOT_SEC = int(os.environ.get("BOOT_SEC", "15"))

# usb_state_t: DISCONNECTED=0 CONNECTED=1 ENUMERATED=2 MSC_READY=3 MOUNTED=4 ERROR=5
USB_MOUNTED = 4

_PASSED = [0]
_TOTAL = [0]


def check(name, cond, detail=""):
    _TOTAL[0] += 1
    if cond:
        _PASSED[0] += 1
    print("  [%s] %s%s" % ("PASS" if cond else "FAIL", name,
                           ("  (%s)" % detail) if detail else ""))
    return 1 if cond else 0


def sym_addr(elf, name):
    out = subprocess.run([NM, elf], capture_output=True, text=True, timeout=60).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == name:
            return int(parts[0], 16)
    raise RuntimeError("symbol %s not found in %s" % (name, elf))


def run_ocd(elf, reads):
    """Flash `elf`, let it boot, halt, then read `reads` = [(name, addr, size)].

    Words are read one `mdw` at a time: the TX ring counters sit at odd/half
    word addresses (a uint8 next to two uint16s), and mdw on an unaligned
    address fails.  Reading whole aligned words and slicing the bytes out in
    Python sidesteps that entirely.
    """
    cmds = ["init", "reset halt",
            "flash write_image erase %s" % elf,
            "verify_image %s" % elf,
            "reset run",
            "sleep %d" % (BOOT_SEC * 1000),
            "halt"]

    word_addrs = []
    for _name, addr, size in reads:
        base = addr & ~3
        end = addr + size
        for w in range(base, ((end + 3) & ~3), 4):
            if w not in word_addrs:
                word_addrs.append(w)
    for w in word_addrs:
        cmds.append("mdw 0x%08X" % w)
    cmds.append("shutdown")

    cmdline = [OCD_BIN, "-s", OCD_SCRIPTS,
               "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cmdline += ["-c", c]

    p = subprocess.run(cmdline, capture_output=True, text=True, timeout=300)
    if p.returncode != 0:
        print(p.stdout[-1500:])
        print(p.stderr[-1500:])
        raise RuntimeError("openocd failed (rc=%d)" % p.returncode)

    words = {}
    for line in (p.stdout + p.stderr).splitlines():
        m = re.match(r"\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$", line)
        if m:
            words[int(m.group(1), 16)] = int(m.group(2), 16)

    vals = []
    for name, addr, size in reads:
        raw = bytearray()
        for off in range(size):
            a = addr + off
            w = words.get(a & ~3)
            if w is None:
                raise RuntimeError("no mdw result for %s @0x%08X" % (name, a))
            raw.append((w >> (8 * (a & 3))) & 0xFF)
        vals.append(int.from_bytes(bytes(raw), "little"))
    return vals


def probe(elf, label, expect_tx):
    print("\n---- %s (%s) ----" % (label, elf))
    reads = [("g_tx_head", sym_addr(elf, "g_tx_head"), 2),
             ("g_tx_tail", sym_addr(elf, "g_tx_tail"), 2),
             ("g_tx_busy", sym_addr(elf, "g_tx_busy"), 1),
             ("g_usb_state", sym_addr(elf, "g_usb_state"), 4)]
    v = run_ocd(elf, reads)
    head, tail, busy = v[0] & 0xFFFF, v[1] & 0xFFFF, v[2] & 0xFF
    # Cortex-M is little-endian, so byte 0 is the LSB of g_usb_state no matter
    # how wide the compiler made the enum (values are 0..5 anyway).
    usb = v[3] & 0xFF

    print("  g_tx_head=0x%04X  g_tx_tail=0x%04X  g_tx_busy=%d  g_usb_state=%d"
          % (head, tail, busy, usb))

    check("%s: 系统完整启动（USB 已挂载, g_usb_state=%d）" % (label, usb),
          usb == USB_MOUNTED, "期望 %d" % USB_MOUNTED)

    if expect_tx:
        check("%s: 日志已送入 UART TX 环（g_tx_head != 0）" % label, head != 0,
              "实测 0x%04X" % head)
        check("%s: TX 环已被 ISR 排空（head == tail）" % label, head == tail,
              "head=0x%04X tail=0x%04X" % (head, tail))
    else:
        check("%s: 一个字节都没进 TX 环（g_tx_head == 0）" % label, head == 0,
              "实测 0x%04X" % head)
        check("%s: 发送器从未启动（g_tx_busy == 0）" % label, busy == 0,
              "实测 %d" % busy)


def main():
    print("[1] 用 SWD 读取 bsp_uart.c 的 TX 环形缓冲状态，验证 PRINT_LOG 开关")
    print("    （串口不可用时改用内存取证：关掉日志后 g_tx_head 必须恒为 0）")

    probe(ELF_ON, "PRINT_LOG 开", expect_tx=True)
    probe(ELF_OFF, "PRINT_LOG 关", expect_tx=False)

    failed = _TOTAL[0] - _PASSED[0]
    print("\n========== RESULT: %d passed, %d failed ==========" % (_PASSED[0], failed))
    print("VERDICT: %s" % ("PASS" if failed == 0 else "FAIL"))


if __name__ == "__main__":
    main()
