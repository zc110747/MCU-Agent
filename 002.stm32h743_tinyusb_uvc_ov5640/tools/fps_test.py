#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OpenOCD 帧率 / 管线健康度探针 —— STM32H743 + OV5640 UVC 摄像头

板载没有 UART（见 stm32h7xx_it.c 注释），所以帧率测试完全走 SWD/OpenOCD
读取 uvc_app.c 留在 RAM 里的遥测计数器。本脚本是纯标准库实现，跨 Windows /
Linux / macOS 运行，不需要任何第三方包。

它做的事
--------
1. 用 arm-none-eabi-nm 解析 .elf，拿到一组全局计数器符号的地址。
2. 通过 OpenOCD 的 telnet 端口（默认 4444），用 `mdw` 读取这些地址。
   - 默认用 DAP 总线读（不 halt 核心，不打断实时 DCMI/USB 流）。
   - 极端情况可用 --halt 让每次采样前 halt。

3. 多次采样（隔 --interval 秒，共 --count 次），算：
   - 真实吞吐 fps = Δuvc_frames_sent / Δwall_clock（不依赖固件变量，独立校验）
   - 固件自算的 uvc_fps_x10（单次 SWD 可读的那个）
   - 丢帧率 = Δdropped / (Δsent + Δdropped)
   - xfer 拒绝率 / tx 超时次数 / uvc_state 位域 / usb_mounted

用法
----
    # 先 build + flash，再在主机打开相机应用，然后：
    python tools/fps_test.py                 # 自动找 .elf 和 cfg，自启 OpenOCD
    python tools/fps_test.py --no-launch     # 复用已经在跑的 OpenOCD
    python tools/fps_test.py --interval 1 --count 10

退出码：0 = 通过（真实 fps 在 [7,9] 且丢帧≈0），1 = 失败，2 = 环境错误。

预期结果（USB FS，未压缩 YUY2 240x240）
------------------------------------------
   真实 fps ≈ 8.0（USB FS 带宽上限约 8~9 fps），uvc_fps_x10 ≈ 80，
   uvc_frames_dropped ≈ 0，uvc_xfer_rejected ≈ 0，uvc_tx_timeouts ≈ 0。
"""

from __future__ import annotations

import argparse
import os
import re
import socket
import subprocess
import sys
import time

OPENOCD_TELNET_PORT = 4444
PROMPT = b"> "

# 关心的计数器（显示顺序）。名字必须和 uvc_app.c 里的全局变量一致。
SYMBOLS = [
    "uvc_fps_x10",
    "uvc_frames_sent",
    "uvc_frames_dropped",
    "uvc_xfer_started",
    "uvc_xfer_rejected",
    "uvc_tx_timeouts",
    "uvc_fps_frames",
    "uvc_fps_ms",
    "uvc_state",
    "usb_mounted",
]

# uvc_state 位域含义（见 uvc_app.c）。
STATE_BITS = [
    (0x01, "streaming"),
    (0x02, "tx_busy"),
    (0x04, "capture_busy"),
    (0x08, "frame_ready"),
    (0x10, "camera_ok"),
    (0x20, "cam_running"),
]


def resolve_symbols(elf: str, nm: str, wanted) -> dict:
    """用 nm 解析 .elf，返回 {符号名: 地址}。"""
    out = subprocess.run([nm, "--extern-only", elf],
                         capture_output=True, text=True)
    if out.returncode != 0:
        raise RuntimeError("nm 执行失败:\n" + out.stderr)
    addrs = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        typ, name = parts[1], parts[2]
        # 只收强符号定义（排除 w/W/v/U 这类弱/未定义）。
        if name in wanted and typ not in "wWvU":
            addrs[name] = int(parts[0], 16)
    missing = [w for w in wanted if w not in addrs]
    if missing:
        raise RuntimeError("找不到符号（确认用当前 .elf 构建且带 -g）: %s" % missing)
    return addrs


class OpenOCD:
    """极简 OpenOCD telnet 客户端。"""

    def __init__(self, host: str, port: int):
        self.s = socket.create_connection((host, port), timeout=5)
        self._drain()  # 吃掉登录 banner

    def _drain(self, timeout: float = 3.0) -> bytes:
        self.s.settimeout(timeout)
        buf = b""
        try:
            while True:
                chunk = self.s.recv(4096)
                if not chunk:
                    break
                buf += chunk
                if buf.rstrip().endswith(PROMPT):
                    break
        except socket.timeout:
            pass
        return buf

    def cmd(self, c: str) -> str:
        self.s.sendall((c + "\n").encode())
        return self._drain().decode(errors="replace")

    def mdw(self, addr: int) -> int:
        out = self.cmd("mdw 0x%08X" % addr)
        # OpenOCD 返回形如 "0x24001234: 0x00000050"
        m = re.findall(r"0x[0-9a-fA-F]+:\s+0x([0-9a-fA-F]+)", out)
        if m:
            return int(m[-1], 16)
        m = re.findall(r"0x([0-9a-fA-F]+)", out)
        if len(m) >= 2:
            return int(m[-1], 16)
        raise ValueError("无法解析 mdw 输出: %r" % out)

    def halt(self):
        self.cmd("halt")

    def resume(self):
        self.cmd("resume")

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def launch_openocd(openocd: str, cfg: str):
    """后台启动 OpenOCD server（只做调试服务器，不 program/exit），等 telnet 就绪。"""
    proc = subprocess.Popen([openocd, "-f", cfg],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    deadline = time.time() + 15
    while time.time() < deadline:
        if proc.poll() is not None:
            out = proc.stdout.read().decode(errors="replace")
            raise RuntimeError("OpenOCD 提前退出:\n" + out)
        try:
            with socket.create_connection(("127.0.0.1", OPENOCD_TELNET_PORT), timeout=1):
                return proc
        except OSError:
            time.sleep(0.3)
    raise RuntimeError("OpenOCD telnet 15s 内未就绪")


def decode_state(v: int) -> str:
    bits = [name for mask, name in STATE_BITS if v & mask]
    return "+".join(bits) if bits else "idle"


def fmt_fps(x10: int) -> str:
    return "%d.%d" % (x10 // 10, x10 % 10)


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description="OpenOCD UVC 帧率/健康度探针")
    ap.add_argument("--elf", default=os.path.join(root, "build", "stm32h743_uvc.elf"),
                    help="固件 .elf 路径")
    ap.add_argument("--nm", default="arm-none-eabi-nm", help="nm 可执行文件")
    ap.add_argument("--openocd", default="openocd", help="openocd 可执行文件")
    ap.add_argument("--cfg", default=None, help="openocd.cfg 路径（默认自动探测）")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=OPENOCD_TELNET_PORT)
    ap.add_argument("--interval", type=float, default=2.0, help="采样间隔（秒）")
    ap.add_argument("--count", type=int, default=5, help="采样次数")
    ap.add_argument("--no-launch", action="store_true", help="不自动启动 OpenOCD")
    ap.add_argument("--halt", action="store_true", help="每次采样前 halt 核心")
    ap.add_argument("--fps-lo", type=float, default=7.0, help="通过阈值下限")
    ap.add_argument("--fps-hi", type=float, default=9.0, help="通过阈值上限")
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        print("[ERR] 找不到 .elf: %s\n      请先构建（cmake --build build）" % args.elf)
        return 2

    # 自动探测 cfg
    cfg = args.cfg
    if cfg is None:
        for cand in ("debug/openocd.cfg", "openocd.cfg"):
            p = os.path.join(root, cand)
            if os.path.exists(p):
                cfg = p
                break
    if cfg is None or not os.path.exists(cfg):
        print("[ERR] 找不到 openocd.cfg（试过 debug/openocd.cfg 与 openocd.cfg）")
        return 2

    try:
        addrs = resolve_symbols(args.elf, args.nm, SYMBOLS)
    except RuntimeError as e:
        print("[ERR] " + str(e))
        return 2

    # 连接 / 启动 OpenOCD
    proc = None
    try:
        try:
            oc = OpenOCD(args.host, args.port)
            launched = False
        except OSError:
            if args.no_launch:
                print("[ERR] %s:%d 无 OpenOCD 且 --no-launch 已设" %
                      (args.host, args.port))
                return 2
            proc = launch_openocd(args.openocd, cfg)
            oc = OpenOCD(args.host, args.port)
            launched = True
    except RuntimeError as e:
        print("[ERR] " + str(e))
        return 2

    print("OpenOCD: %s | cfg: %s" %
          ("复用已有" if not launched else "已启动", os.path.relpath(cfg, root)))
    print("采样 %d 次, 间隔 %.1fs, fps 通过窗 [%.1f, %.1f]" %
          (args.count, args.interval, args.fps_lo, args.fps_hi))
    print("-" * 78)

    samples = []
    try:
        for i in range(args.count):
            if args.halt:
                oc.halt()
            vals = {name: oc.mdw(addrs[name]) for name in SYMBOLS}
            if args.halt:
                oc.resume()
            vals["_wall"] = time.perf_counter()
            samples.append(vals)

            print("样本 %2d  t=%6.1fs  fps(固件)=%5s  sent=%7d  dropped=%4d  "
                  "xfer_rej=%3d  tx_to=%3d  state=%s  mounted=%d" % (
                      i, vals["_wall"],
                      fmt_fps(vals["uvc_fps_x10"]),
                      vals["uvc_frames_sent"], vals["uvc_frames_dropped"],
                      vals["uvc_xfer_rejected"], vals["uvc_tx_timeouts"],
                      decode_state(vals["uvc_state"]), vals["usb_mounted"]))

            if i < args.count - 1:
                time.sleep(args.interval)
    finally:
        oc.close()
        if proc is not None:
            proc.terminate()

    if len(samples) < 2:
        print("[WARN] 采样不足 2 次，无法算吞吐")
        return 0

    # 逐窗口真实 fps（Δsent / Δwall），独立于固件变量
    win_fps = []
    drop_total = 0
    sent_total = 0
    rej_total = 0
    for k in range(1, len(samples)):
        d_sent = samples[k]["uvc_frames_sent"] - samples[k - 1]["uvc_frames_sent"]
        d_wall = samples[k]["_wall"] - samples[k - 1]["_wall"]
        if d_wall > 0:
            win_fps.append(d_sent / d_wall)
        sent_total += d_sent
        drop_total += (samples[k]["uvc_frames_dropped"] -
                       samples[k - 1]["uvc_frames_dropped"])
        rej_total += (samples[k]["uvc_xfer_rejected"] -
                      samples[k - 1]["uvc_xfer_rejected"])

    avg_fps = sum(win_fps) / len(win_fps) if win_fps else 0.0
    worst_fps = min(win_fps) if win_fps else 0.0
    last_fps_x10 = samples[-1]["uvc_fps_x10"]
    drop_rate = (drop_total / (sent_total + drop_total) * 100.0
                 if (sent_total + drop_total) > 0 else 0.0)

    print("-" * 78)
    print("汇总")
    print("  逐窗口真实 fps : %s" % ", ".join("%.2f" % f for f in win_fps))
    print("  平均真实 fps   : %.2f" % avg_fps)
    print("  最低真实 fps   : %.2f" % worst_fps)
    print("  固件 uvc_fps_x10(末次): %s (%d.%d fps)" %
          (last_fps_x10, last_fps_x10 // 10, last_fps_x10 % 10))
    print("  累计发送帧     : %d" % sent_total)
    print("  累计丢帧       : %d  (丢帧率 %.2f%%)" % (drop_total, drop_rate))
    print("  累计 xfer 拒绝 : %d" % rej_total)
    print("  末次 tx 超时   : %d" % samples[-1]["uvc_tx_timeouts"])
    print("  末次 state     : %s" % decode_state(samples[-1]["uvc_state"]))
    print("  末次 usb_mounted: %d" % samples[-1]["usb_mounted"])

    # 判定
    problems = []
    if not (args.fps_lo <= avg_fps <= args.fps_hi):
        problems.append("平均真实 fps %.2f 不在 [%.1f,%.1f]（USB FS 240x240 YUY2 理论≈8）"
                        % (avg_fps, args.fps_lo, args.fps_hi))
    if drop_total != 0:
        problems.append("存在丢帧 %d（%.2f%%）" % (drop_total, drop_rate))
    if rej_total != 0:
        problems.append("存在 xfer 拒绝 %d（USB 来不及收）" % rej_total)
    if samples[-1]["uvc_tx_timeouts"] != 0:
        problems.append("存在 tx 超时（USB 总线挂死过，看 uvc_tx_timeouts）")
    if samples[-1]["usb_mounted"] == 0:
        problems.append("USB 未挂载（主机没打开相机/没枚举）")
    if not (samples[-1]["uvc_state"] & 0x01):
        problems.append("uvc_state 未置 streaming 位（主机未开始拉流）")

    print("=" * 78)
    if problems:
        print("[FAIL] 帧率/管线测试未通过:")
        for p in problems:
            print("  - " + p)
        return 1
    print("[PASS] 帧率测试通过: 真实 fps ≈ %.2f（≈8fps），丢帧率 %.2f%%，"
          "xfer 拒绝 %d，无 tx 超时。" % (avg_fps, drop_rate, rej_total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
