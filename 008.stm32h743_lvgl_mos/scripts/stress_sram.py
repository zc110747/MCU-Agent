#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
stress_sram.py - 压测 NES 动态内存池 (bsp/sram_pool) 在 STM32H743 真机上的表现。

两个相互独立的压测维度：
  1) NES 开/关循环：反复 `rom load` 打开 NES（nes_open 从 DTCM/D2 分配机器态+ROM
     镜像）、运行一会、再两次 `key back` 关闭（nes_close 释放）。每一轮结束后
     都校验 SRAM 空闲量精确回到基线——验证开/关循环无泄漏、分配器合并正确。
  2) 随机碎片压测：通过新加的 `sram stress <dtcm|d2> <iters> [seed]` 控制台命令，
     在固件内做随机大小分配/释放（释放顺序随机），每个操作后立即校验分配器
     不变量，最后全释放并确认完全回收——这是固定大小开/关循环测不出的碎片/boundary-tag
     合并健壮性测试。

用法：
  python stress_sram.py [PORT] [CYCLES] [STRESS_ITERS] [STRESS_ROUNDS]

默认：PORT=COM6  CYCLES=25  STRESS_ITERS=800  STRESS_ROUNDS=10
"""

import serial
import sys
import time
import re

BAUD = 115200


def connect(port):
    s = serial.Serial(port, BAUD, timeout=0.5)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def send(s, line, timeout=10.0):
    """发送命令，返回应答行列表（去掉回显），遇到 OK/ERR 行结束。"""
    s.reset_input_buffer()
    s.write((line + "\r").encode())
    lines = []
    t0 = time.time()
    cur = b""
    while time.time() - t0 < timeout:
        b = s.read(1)
        if not b:
            continue
        if b in (b"\r", b"\n"):
            if cur.strip():
                txt = cur.decode("utf-8", "replace")
                if txt.strip() != line.strip():   # 忽略回显的命令本身
                    lines.append(txt)
                    if txt.strip().startswith("OK") or txt.strip().startswith("ERR"):
                        return lines
            cur = b""
        else:
            cur += b
    if cur.strip():
        lines.append(cur.decode("utf-8", "replace"))
    return lines


def parse_status(lines):
    d = {}
    for ln in lines:
        m = re.search(r"sram\s+dtcm:\s*(\d+)/(\d+)", ln)
        if m:
            d["dtcm_free"] = int(m.group(1))
            d["dtcm_total"] = int(m.group(2))
        m = re.search(r"sram\s+d2\s*:\s*(\d+)/(\d+)", ln)
        if m:
            d["d2_free"] = int(m.group(1))
            d["d2_total"] = int(m.group(2))
        m = re.search(r"nes\s*:\s*(\w+)", ln)
        if m:
            d["nes"] = m.group(1)
        m = re.search(r"view\s*:\s*(\S+)", ln)
        if m:
            d["view"] = m.group(1)
        m = re.search(r"fps\s+(\d+)", ln)
        if m:
            d["fps"] = int(m.group(1))
    return d


def status_until(s, pred, timeout=15.0, poll=0.3):
    """轮询 status 直到 pred(d) 为真，返回最终 dict 或 None。"""
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        ls = send(s, "status", timeout=5.0)
        d = parse_status(ls)
        last = d
        if pred(d):
            return d
        time.sleep(poll)
    return last


def ensure_menu(s):
    d = status_until(s, lambda x: "view" in x, timeout=5.0)
    if d is None:
        return None
    if d.get("view") != "menu":
        send(s, "menu", timeout=5.0)          # 强制退回主菜单
        time.sleep(1.0)
        d = status_until(s, lambda x: x.get("view") == "menu", timeout=5.0)
    return d


def run_stress_round(s, iters):
    res = {}
    for reg in ("dtcm", "d2"):
        ls = send(s, "sram stress %s %d" % (reg, iters), timeout=30.0)
        ok = any(l.startswith("OK") for l in ls)
        fail = any(l.startswith("ERR") for l in ls)
        res[reg] = "PASS" if ok else ("FAIL" if fail else "NORESP")
        # 失败码在 ERR 行里
        for l in ls:
            if l.startswith("ERR"):
                res[reg] = l.strip()
    return res


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM6"
    cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    stress_iters = int(sys.argv[3]) if len(sys.argv) > 3 else 800
    stress_rounds = int(sys.argv[4]) if len(sys.argv) > 4 else 10

    print("=== SRAM 动态内存池压测 ===")
    print("port=%s cycles=%d stress_iters=%d stress_rounds=%d"
          % (port, cycles, stress_iters, stress_rounds))

    s = connect(port)
    # 先软复位，确保内存池从干净状态开始（上一次失败运行可能残留泄漏/损坏块）
    send(s, "reset", timeout=3.0)
    time.sleep(2.5)
    # 关回显，减少噪声
    send(s, "echo off", timeout=3.0)

    # ---- 初始：确保主菜单 + 取基线 ----
    base = ensure_menu(s)
    if base is None or "dtcm_free" not in base:
        print("ERR: 无法读取初始 status，串口连接异常？")
        return 1
    base_dtcm = base["dtcm_free"]
    base_d2 = base["d2_free"]
    print("基线: dtcm free=%d  d2 free=%d (total %d/%d)"
          % (base_dtcm, base_d2, base.get("dtcm_total", 0), base.get("d2_total", 0)))

    # ---- 确认卡上有 ROM ----
    ls = send(s, "rom list", timeout=8.0)
    rom_count = 0
    for l in ls:
        m = re.search(r"OK rom list (\d+)", l)
        if m:
            rom_count = int(m.group(1))
    if rom_count == 0:
        print("ERR: 卡上无 .nes（rom list 返回 0），无法打开 NES 做开/关循环。")
        return 1
    print("rom list: %d 个 ROM，使用 index 0" % rom_count)

    # ---- 阶段 B：NES 开/关循环 ----
    print("\n--- 阶段 B: NES 开/关循环 (%d 轮) ---" % cycles)
    cyc_pass = 0
    cyc_fail = 0
    fail_drops = []
    fps_samples = []
    for i in range(cycles):
        # 打开
        send(s, "rom load 0", timeout=5.0)
        d_open = status_until(
            s,
            lambda x: x.get("dtcm_free", 1e9) < base_dtcm - 4096,
            timeout=15.0,
        )
        if d_open is None or d_open.get("dtcm_free", 1e9) >= base_dtcm - 4096:
            print("  [%2d] FAIL 打开超时/未分配 (dtcm=%s)"
                  % (i, d_open.get("dtcm_free") if d_open else None))
            cyc_fail += 1
            fail_drops.append(i)
            # 尝试恢复：强制退回菜单
            ensure_menu(s)
            continue

        drop_dtcm = base_dtcm - d_open["dtcm_free"]
        drop_d2 = base_d2 - d_open["d2_free"]
        # 等 2s 让帧率稳定后采样 fps
        time.sleep(2.0)
        d_fps = send(s, "status", timeout=5.0)
        fps = parse_status(d_fps).get("fps", 0)
        fps_samples.append(fps)

        # 关闭：两次 key back（第一次停游戏回浏览器，第二次退出页面）
        send(s, "key back", timeout=3.0)
        time.sleep(1.0)
        send(s, "key back", timeout=3.0)

        d_close = status_until(
            s,
            lambda x: x.get("view") == "menu"
            and x.get("dtcm_free") == base_dtcm
            and x.get("d2_free") == base_d2,
            timeout=10.0,
        )
        if d_close is not None and d_close.get("dtcm_free") == base_dtcm \
                and d_close.get("d2_free") == base_d2 and d_close.get("view") == "menu":
            cyc_pass += 1
            print("  [%2d] PASS 分配 dtcm-%dB d2-%dB fps~%d 释放后回到基线"
                  % (i, drop_dtcm, drop_d2, fps))
        else:
            cyc_fail += 1
            fail_drops.append(i)
            got_d = d_close.get("dtcm_free") if d_close else None
            got_d2 = d_close.get("d2_free") if d_close else None
            got_v = d_close.get("view") if d_close else None
            print("  [%2d] FAIL 释放后未回基线 dtcm=%s(基%d) d2=%s(基%d) view=%s"
                  % (i, got_d, base_dtcm, got_d2, base_d2, got_v))
            # 强制回菜单，避免卡死影响后续
            ensure_menu(s)

        # 每 5 轮插一组随机碎片压测，制造混合负载
        if (i + 1) % 5 == 0:
            sr = run_stress_round(s, stress_iters)
            print("       [混合] sram stress dtcm=%s d2=%s" % (sr["dtcm"], sr["d2"]))
            for reg in ("dtcm", "d2"):
                if not sr[reg].startswith("PASS"):
                    print("       [混合] !! %s 碎片压测 %s" % (reg, sr[reg]))

    # ---- 阶段 C：独立的随机碎片压测轮次 ----
    print("\n--- 阶段 C: 随机碎片压测 (%d 轮, iters=%d) ---" % (stress_rounds, stress_iters))
    stress_pass = 0
    stress_fail = 0
    for r in range(stress_rounds):
        sr = run_stress_round(s, stress_iters)
        tag = "PASS" if (sr["dtcm"].startswith("PASS") and sr["d2"].startswith("PASS")) else "FAIL"
        if tag == "PASS":
            stress_pass += 1
        else:
            stress_fail += 1
        print("  round %2d: dtcm=%s  d2=%s" % (r, sr["dtcm"], sr["d2"]))

    # ---- 收尾：完整性快照 ----
    info = send(s, "sram info", timeout=5.0)
    print("\n--- 收尾: sram info ---")
    for l in info:
        if l.startswith(("OK", "dtcm", "d2")):
            print("  " + l)

    # ---- 汇总 ----
    print("\n=== 汇总 ===")
    print("NES 开/关循环 : %d PASS / %d FAIL" % (cyc_pass, cyc_fail))
    if fps_samples:
        print("fps 样本     : min=%d avg=%d max=%d"
              % (min(fps_samples), sum(fps_samples)//len(fps_samples), max(fps_samples)))
    print("碎片压测      : %d PASS / %d FAIL (iters=%d)"
          % (stress_pass, stress_fail, stress_iters))
    if cyc_fail == 0 and stress_fail == 0:
        print("结论: 全部通过 — 动态内存池开/关无泄漏、随机碎片分配/释放完全回收。")
        return 0
    print("结论: 存在失败项，见上方明细。")
    return 2


if __name__ == "__main__":
    sys.exit(main())
