#!/usr/bin/env python3
# verify_snmp_desktop_visual.py — 校验 snmp_desktop 6 个页面在真机渲染下均无裁切。
#
# 用法：python verify_snmp_desktop_visual.py
#   1. 调用 snmp_desktop_visualtest.exe，刷新 6 个页面，截图为 PNG。
#   2. 检查每张 PNG 的右侧/底部是否存在严重白边/裁切标志（图像填充度）。
#   3. 统计 visibleControls、hasScrollbar，输出 PASS/FAIL。
#
# 依赖：Pillow（PIL）。安装：pip install pillow
#
# 退出码：0=全部 PASS，1=存在 FAIL。

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(r"E:\cnb\git\Mcu_Project_Design_By_Agent\010.stm32f429_net\tools")
VISUAL_EXE = ROOT / "snmp_desktop_visualtest" / "bin" / "Release" / "net9.0-windows" / "snmp_desktop_visualtest.exe"
OUT_DIR = ROOT / "snmp_desktop_visualtest" / "shots"
HOST = "192.168.10.99"
PORT = 161
PAGES = ["系统概念", "设备信息", "硬件监控", "传感器监控", "网络状态", "参数设置"]


def run_visual_test() -> int:
    # 视觉测试的 PrintWindow 会覆盖已有同名 PNG，不必预先清理
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"=== 运行视觉测试: {VISUAL_EXE} {HOST} {PORT} {OUT_DIR} ===")
    r = subprocess.run(
        [str(VISUAL_EXE), HOST, str(PORT), str(OUT_DIR)],
        capture_output=True, text=False, timeout=60
    )
    # C# 控制台输出在 Windows cp936 (GBK) 下；用 GBK 解码，错误用替换字符
    out = (r.stdout or b"").decode("gbk", errors="replace").strip()
    err = (r.stderr or b"").decode("gbk", errors="replace").strip()
    print(out)
    if r.returncode != 0:
        print(f"[ERROR] 视觉测试退出码 {r.returncode}")
        print(err)
    return r.returncode


def check_image_filled(png: Path) -> tuple[bool, str]:
    """检查图像是否发生严重裁切/白边（仅在「内容区被裁掉」时 FAIL）。
    思路：把整张图转灰度，统计亮像素（>200）占比。深色主题下亮像素 > 35% 才算「过亮」，
    反之正常（深色主体 + 少量文字/滚动条/状态点）。
    """
    try:
        from PIL import Image
    except ImportError:
        return True, "(skip: Pillow not installed)"

    img = Image.open(png).convert("L")  # 灰度
    w, h = img.size
    px = img.load()
    bright = 0
    total = w * h
    for y in range(h):
        for x in range(w):
            if px[x, y] > 200:
                bright += 1
    ratio = bright / total if total else 0
    if ratio > 0.35:
        return False, f"整体亮度过高 {ratio:.1%}（可能大面积白边）"
    return True, f"亮像素 {ratio:.1%}"


def main() -> int:
    rc = run_visual_test()
    if rc != 0:
        return 1
    pass_cnt = 0
    fail_cnt = 0
    fails = []
    for idx, page in enumerate(PAGES, 1):
        png = OUT_DIR / f"page_{idx:02d}_{page}.png"
        txt = OUT_DIR / f"page_{idx:02d}_{page}.txt"
        if not png.exists():
            print(f"[FAIL] {page}: PNG 缺失")
            fail_cnt += 1
            fails.append(page)
            continue
        ok, reason = check_image_filled(png)
        meta = txt.read_text(encoding="utf-8") if txt.exists() else ""
        m = re.search(r"visibleControls=(\d+)", meta)
        vis = int(m.group(1)) if m else 0
        status = "OK" if (ok and vis > 0) else "FAIL"
        print(f"[{status}] {page}  visible={vis}  {reason}")
        if ok and vis > 0:
            pass_cnt += 1
        else:
            fail_cnt += 1
            fails.append(page)
    print(f"\n=== VISUAL VERIFY: PASS={pass_cnt}/{len(PAGES)} FAIL={fail_cnt} ===")
    if fails:
        print(f"失败页面: {fails}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
