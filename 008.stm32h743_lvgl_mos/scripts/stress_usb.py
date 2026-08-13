#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
USB CDC 串口压测脚本 — 循环高频发送命令，探测固件死机。

目的
----
复现并暴露 "USB 串口工作一段时间死机" 的问题。脚本通过 USB CDC 虚拟串口
(COM9 / COM4 之类) 持续发送合法 / 非法 / 超长 / 突发(burst) 命令，每发一条都
等待固件回 OK/ERR。一旦连续若干次读不到任何响应，判定为死机(DEADLOCK)并退出。

用法
----
  python stress_usb.py --port COM9 [--rounds 100000] [--burst] [--timeout 3.0]
                       [--dead-guard 8] [--seed 0]

退出码
------
  0  完成全部 rounds 未死机
  1  中途死机(DEADLOCK)
  2  端口打开失败
"""
import argparse
import random
import serial
import sys
import time

BAUD = 115200

# 合法命令池（覆盖各功能，部分会触发大量回包 -> 大量 USB TX）
GOOD = [
    "status", "pages", "help", "menu", "back",
    "open 0", "open 1", "open 2", "open 3", "open 4", "open 5",
    "sel 0", "sel 1", "sel 2", "sel 3", "sel 4",
    "key up", "key down", "key left", "key right",
    "key a", "key b", "key start", "key select",
    "down a", "up a", "release", "keys",
    "rom list", "rom load 0", "rom stop",
    "img list", "img close", "img info", "time",
]
# 非法命令（走 err 分支，仍产生输出）
BAD = [
    "wibble", "key zzz", "open nosuch", "rom load 99", "img show 7",
    "sel abc", "rom", "img", "time 2026-13-40 99:99:99",
]
# 超长行（>96 字节，触发 overflow 分支）
LONG = ["x" * (120 + i) for i in range(5)]


def make_rand_line(rng):
    """构造一个随机命令字节串，含换行，模拟真实 terminal 输入。"""
    pool = GOOD + BAD
    base = rng.choice(pool)
    # 偶尔追加垃圾字符制造噪声
    if rng.random() < 0.2:
        base = base + " " + "".join(rng.choice("ab12 \t") for _ in range(rng.randint(0, 8)))
    return base


class Stresser:
    def __init__(self, port, timeout):
        self.ser = serial.Serial(port, BAUD, timeout=timeout)
        self.ser.dtr = True
        self.ser.rts = True
        self.n_ok = 0
        self.n_err = 0
        self.n_fail = 0

    def cmd(self, line, timeout):
        """发送一行并等待 OK/ERR。返回 ('OK'|'ERR'|None, [payload])。"""
        try:
            self.ser.reset_input_buffer()
            self.ser.write((line + "\r\n").encode("utf-8", "replace"))
            self.ser.flush()
        except (serial.SerialException, OSError) as e:
            return None, [f"write-exc:{e}"]

        t_deadline = time.time() + timeout
        payload = []
        while time.time() < t_deadline:
            try:
                raw = self.ser.readline()
            except (serial.SerialException, OSError) as e:
                return None, [f"read-exc:{e}"]
            if not raw:
                continue
            s = raw.decode("utf-8", "replace").rstrip("\r\n")
            if s.startswith("OK"):
                return "OK", payload
            if s.startswith("ERR"):
                return "ERR", payload
            payload.append(s)
        return None, payload

    def burst(self, rng, n_lines=400):
        """一次性灌入大量随机字节(含命令/超长/非法)，制造 USB 接收压力。"""
        blob = ""
        for _ in range(n_lines):
            r = rng.random()
            if r < 0.1:
                blob += rng.choice(LONG) + "\r\n"
            else:
                blob += make_rand_line(rng) + "\r\n"
        try:
            self.ser.write(blob.encode("utf-8", "replace"))
            self.ser.flush()
        except (serial.SerialException, OSError) as e:
            return f"burst-write-exc:{e}"
        return None

    def alive_probe(self, timeout):
        """发一个 status 看固件是否还活着。"""
        st, _ = self.cmd("status", timeout)
        return st in ("OK", "ERR")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--rounds", type=int, default=200000)
    ap.add_argument("--burst", action="store_true", help="每轮附带一次突发灌入")
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--dead-guard", type=int, default=8,
                    help="连续 N 次无响应判定死机")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    try:
        s = Stresser(args.port, args.timeout)
    except (serial.SerialException, OSError) as e:
        print(f"FAIL open {args.port}: {e}")
        return 2

    # 关回显，减少无谓流量
    s.cmd("echo off", args.timeout)
    print(f"stress start: port={args.port} rounds={args.rounds} "
          f"burst={args.burst} timeout={args.timeout}s guard={args.dead_guard}")
    print(f"{'round':>8} {'ok':>7} {'err':>7} {'fail':>7}  note")

    t_start = time.time()
    dead = False
    for i in range(args.rounds):
        line = make_rand_line(rng)
        st, _ = s.cmd(line, args.timeout)
        if st is None:
            s.n_fail += 1
            if s.n_fail >= args.dead_guard:
                # 最后再探一次活，排除单纯偶发超时
                if not s.alive_probe(args.timeout * 2):
                    dead = True
                    print(f"{i:>8} {s.n_ok:>7} {s.n_err:>7} {s.n_fail:>7}  "
                          f"DEADLOCK detected (no response x{args.dead_guard})")
                    break
                else:
                    s.n_fail = 0
            else:
                print(f"{i:>8} {s.n_ok:>7} {s.n_err:>7} {s.n_fail:>7}  "
                      f"timeout (recovered)")
        else:
            if st == "OK":
                s.n_ok += 1
            else:
                s.n_err += 1
            s.n_fail = 0

        if args.burst and (i % 50 == 0):
            err = s.burst(rng)
            if err:
                print(f"{i:>8} {'-':>7} {'-':>7} {'-':>7}  {err}")
            # burst 后用 status 探活
            if not s.alive_probe(args.timeout * 2):
                dead = True
                print(f"{i:>8} {s.n_ok:>7} {s.n_err:>7} {s.n_fail:>7}  "
                      f"DEADLOCK after burst")
                break

        if (i + 1) % 2000 == 0:
            el = time.time() - t_start
            print(f"{i+1:>8} {s.n_ok:>7} {s.n_err:>7} {s.n_fail:>7}  "
                  f"alive {el:.0f}s")

    el = time.time() - t_start
    print("=" * 60)
    if dead:
        print(f"RESULT: DEADLOCK after {el:.0f}s  "
              f"(ok={s.n_ok} err={s.n_err} fail={s.n_fail})")
        return 1
    print(f"RESULT: SURVIVED {args.rounds} rounds in {el:.0f}s  "
          f"(ok={s.n_ok} err={s.n_err})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
