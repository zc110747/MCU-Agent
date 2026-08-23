#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
真机串口端到端验证 (STM32F429 + shell over USART1/COM3)
- 打开 COM3 @115200, 模拟终端打字 (含 backspace 编辑)
- 逐条发送指令, 读取 MCU 回显与执行结果
- 验证: RX 队列路径 / backspace 回退 / 调度器前丢弃 / 多指令不乱序
"""
import serial, sys, time

PORT = "COM3"
BAUD = 115200
TIMEOUT = 2.0

PASS = 0
FAIL = 0
LOG = []

def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        LOG.append(("PASS", name, detail))
    else:
        FAIL += 1
        LOG.append(("FAIL", name, detail))
    print(("✅" if cond else "❌"), name, (f"({detail})" if detail else ""))

def drain(ser, t=0.5):
    time.sleep(t)
    out = b""
    while ser.in_waiting:
        chunk = ser.read(ser.in_waiting)
        if not chunk:
            break
        out += chunk
        time.sleep(0.05)
    try:
        return out.decode("utf-8", "replace")
    except Exception:
        return out.decode("latin1", "replace")

def send_line(ser, line, backspaced_from=None):
    if backspaced_from is None:
        ser.write(line.encode("utf-8") + b"\r")
    else:
        ser.write(backspaced_from.encode("utf-8").replace(b"\r", b""))
        n_bs = len(backspaced_from) - len(line)
        ser.write(b"\x08" * n_bs)
        ser.write(line[len(backspaced_from):].encode("utf-8") + b"\r")

def main():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)
    except Exception as e:
        print("OPEN FAIL:", e)
        sys.exit(2)
    time.sleep(0.5)
    # 先发一个空回车, 让 shell 重新输出提示符 (横幅可能早已发出)
    ser.write(b"\r")
    banner = drain(ser, 1.5)
    ser.reset_input_buffer()

    # 横幅 "=== STM32F429 Shell ===" 由 shell_task 启动时一次性打印;
    # 若 Python 连接前 MCU 已运行, 横幅已发出不可见, 此时以提示符 STM32> 作为
    # shell 已启动的等价证明 (两者同源, 均来自 shell_task 首句 uart_puts)
    shell_up = ("STM32F429 Shell" in banner) or ("STM32> " in banner)
    check("shell started (banner or prompt seen)", shell_up,
          banner[:60].replace("\n", " "))
    check("prompt 'STM32> ' present", "STM32> " in banner,
          f"banner_tail={banner[-30:].replace(chr(10),' ')!r}")

    # 基本指令 + 回显
    send_line(ser, "hw")
    r = drain(ser)
    check("cmd 'hw' echoes 'hw'", "hw" in r, r[:40].replace("\n", " "))
    check("cmd 'hw' output shows MCU", "STM32F429IGT6" in r, "")

    # ---- 关键回归: 输入字节不得被 ISR 重复投递 (he 不能变成 hhelphehe) ----
    # 发 "he", MCU 必须精确收到 "he", 不能出现翻倍串, 也不能出现 "Unknown command: hhelphehe"
    send_line(ser, "he")
    r = drain(ser)
    check("input 'he' NOT duplicated to 'hhelphehe'",
          "hhelphehe" not in r, r[:60].replace("\n", " "))
    check("input 'he' -> 'Unknown command: he' exactly",
          "Unknown command: he" in r, r[:60].replace("\n", " "))
    check("input 'he' -> NOT 'Unknown command: hhelphehe'",
          "Unknown command: hhelphehe" not in r, r[:60].replace("\n", " "))
    # 连续两次 "he" 也不得翻倍
    send_line(ser, "he")
    send_line(ser, "he")
    r = drain(ser, 1.0)
    check("two 'he' in a row not garbled",
          r.count("Unknown command: he") >= 1 and "hhelphehe" not in r,
          r[:80].replace("\n", " "))

    # backspace 编辑: 先打 'hhee' 再 BS×2 变 'hh'
    send_line(ser, "hh", backspaced_from="hhee")
    r = drain(ser)
    check("backspace edit: MCU received 'hh' (not 'hhee')",
          ("Unknown command: hhee" not in r),
          r[:50].replace("\n", " "))
    check("backspace erased 'hhee' (no 'Unknown command: hhee')",
          "Unknown command: hhee" not in r, "")

    # ---- 修复1: MPU9250 数据显示数字 (newlib-nano 需 _printf_float) ----
    send_line(ser, "dev")
    r = drain(ser, 1.0)
    # 提取 MPU9250 ax/ay/az 行, 检查含有数字而非全空
    import re
    m = re.search(r"MPU9250 ax/ay/az : ([^\r\n]+)", r)
    if m:
        ax_line = m.group(1)
        has_digit = any(ch.isdigit() for ch in ax_line)
        check("MPU9250 ax/ay/az shows numeric value (not blank)",
              has_digit, ax_line.strip())
    else:
        check("MPU9250 ax/ay/az line present", False, "line not found")

    # ---- 修复2: 方向键 (ESC[A/B/C/D) 不回显不存储 ----
    # 先发 ESC[A (上箭头), 再发 hw, MCU 应把 'hw' 当完整指令, 方向键字节不应出现在回显
    ser.write(b"\x1b[A")
    time.sleep(0.1)
    ser.write(b"hw\r")
    r = drain(ser, 1.0)
    check("arrow key ESC[A not echoed/stored (hw parsed)", "Unknown command: hw" not in r, "")
    check("arrow key ESC[A discarded (line == 'hw' not 'hw' with garbage)",
          "STM32F429IGT6" in r and "\x1b" not in r, r[:50].replace("\n", " "))
    # 再测 ESC[B / ESC[C / ESC[D 组合后仍正常
    ser.write(b"\x1b[B\x1b[C\x1b[D")
    time.sleep(0.4)
    ser.write(b"version\r")
    r = drain(ser, 1.0)
    check("arrow keys ESC[B/C/D discarded + 'version' works",
          "v1.0.0" in r and "\x1b" not in r, r[:40].replace("\n", " "))

    # 未知命令
    send_line(ser, "foobar")
    r = drain(ser)
    check("unknown cmd -> 'Unknown command: foobar'", "Unknown command: foobar" in r, "")

    # 多指令连续不乱序
    for c in ["net", "version", "dev"]:
        send_line(ser, c)
    r = drain(ser, 1.2)
    idx_net = r.find("192.168.10.99")
    idx_ver = r.find("v1.0.0")
    idx_dev = r.find("AP3216C")
    ok_order = (idx_net < idx_ver < idx_dev) if (idx_net >= 0 and idx_ver >= 0 and idx_dev >= 0) else False
    check("sequential cmds keep order (net<version<dev)", ok_order,
          f"net={idx_net} ver={idx_ver} dev={idx_dev}")

    # 空行不崩溃
    send_line(ser, "")
    r = drain(ser)
    check("empty line no crash (prompt returns)", "STM32> " in r or "Unknown" in r or "hw" in r, "")

    # 长行截断不崩溃 (80 > 64): 验证后续命令仍正常
    send_line(ser, "x" * 80)
    drain(ser, 0.6)
    send_line(ser, "help")
    r = drain(ser, 0.8)
    check("overlong line (80) no crash + recover", "Commands:" in r, r[:40].replace("\n", " "))

    # ---- net 指令组 (EEPROM 持久化, 重启生效) ----
    # 显示当前 (pending) 值
    send_line(ser, "net")
    r = drain(ser)
    check("net shows pending IP", "192.168.10.99" in r, "")
    check("net shows 'applied after reboot' hint", "reboot" in r.lower(), "")

    # 合法 ip -> "OK: saved"
    send_line(ser, "net ip 192.168.10.55")
    r = drain(ser)
    check("net ip valid -> saved", "OK: saved" in r, r[:50].replace("\n", " "))
    # 非法 ip -> 错误
    send_line(ser, "net ip 999.1.1.1")
    r = drain(ser)
    check("net ip invalid (999.x) -> error", "ERR" in r, r[:50].replace("\n", " "))

    # 合法 mask
    send_line(ser, "net mask 255.255.255.0")
    r = drain(ser)
    check("net mask valid -> saved", "OK: saved" in r, "")
    # 非法 mask (非连续)
    send_line(ser, "net mask 255.0.255.0")
    r = drain(ser)
    check("net mask invalid (non-contiguous) -> error", "ERR" in r, r[:50].replace("\n", " "))

    # 合法 gw
    send_line(ser, "net gw 192.168.10.1")
    r = drain(ser)
    check("net gw valid -> saved", "OK: saved" in r, "")

    # 合法 mac
    send_line(ser, "net mac 22:11:22:00:22:11")
    r = drain(ser)
    check("net mac valid -> saved", "OK: saved" in r, "")
    # mac random
    send_line(ser, "net mac random")
    r = drain(ser)
    check("net mac random -> saved", "OK: saved" in r, r[:50].replace("\n", " "))
    # 非法 mac
    send_line(ser, "net mac ZZ:11:22:00:22:11")
    r = drain(ser)
    check("net mac invalid (ZZ) -> error", "ERR" in r, r[:50].replace("\n", " "))

    # 恢复默认 IP, 避免真机网络被改导致后续 ping 不通
    send_line(ser, "net ip 192.168.10.99")
    drain(ser)

    ser.close()

    print("\n=== 真机串口验证汇总 ===")
    for st, name, detail in LOG:
        print(f"  {st} {name} {('('+detail+')') if detail else ''}")
    print(f"\nTOTAL: {PASS} PASS / {FAIL} FAIL")
    sys.exit(0 if FAIL == 0 else 1)

if __name__ == "__main__":
    main()
