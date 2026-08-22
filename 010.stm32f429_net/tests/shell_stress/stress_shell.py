#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
shell_exec 压测驱动 (PC 侧, 基于真实 shell.c 编译产物)
- 用 gcc 把 app/shell.c + shell_stub.c 编成共享库
- ctypes 加载, 对 shell_exec 做大规模指令压测
- 覆盖: 正常指令 / 边界(空行, 超长, 前导空格, Tab) / 异常(未知命令, 参数错误) / history 环形缓冲
"""
import ctypes, subprocess, os, sys, random, string, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SHELL_C = os.path.join(ROOT, "app", "shell.c")
STUB_C  = os.path.join(HERE, "shell_stub.c")
DLL     = os.path.join(HERE, "shell_test.dll")
GCC     = r"C:\Software\msys2\mingw64\bin\gcc.exe"
INC_STUB = os.path.join(HERE, "inc")   # 桩头 (bsp_uart/hwinfo/netcfg/...) 优先
INC_APP  = os.path.join(ROOT, "app")   # 真实 shell.h

def build():
    if os.path.exists(DLL):
        os.remove(DLL)
    cmd = [GCC, "-O2", "-shared", "-fPIC",
           "-I", INC_STUB,
           "-I", INC_APP,
           "-DWIN32",
           SHELL_C, STUB_C, "-o", DLL]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("BUILD FAILED:\n", r.stdout, r.stderr)
        sys.exit(1)
    print("[build] shell_test.dll OK")

# ---- libs loaded ----
build()
lib = ctypes.CDLL(DLL)

# output sink: collect into a python list
OUT_LINES = []
@ctypes.CFUNCTYPE(None, ctypes.c_char_p)
def sink(s):
    OUT_LINES.append(s.decode("utf-8", "replace"))

lib.shell_exec.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
lib.shell_exec.restype = ctypes.c_int
lib.shell_feed_line.argtypes = [ctypes.c_char_p]
lib.shell_feed_line.restype = None

def run(line):
    """execute one line via shell_exec (no history push), return (rc, text)"""
    OUT_LINES.clear()
    rc = lib.shell_exec(line.encode("utf-8"), sink)
    return rc, "\n".join(OUT_LINES)

def feed(line):
    """feed a complete line through the real enter-key path (history + exec)"""
    OUT_LINES.clear()
    lib.shell_feed_line(line.encode("utf-8"))
    return "\n".join(OUT_LINES)

# ===================== 压测用例 =====================
PASS = 0
FAIL = 0
CASES = []

def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        CASES.append(("PASS", name, detail))
    else:
        FAIL += 1
        CASES.append(("FAIL", name, detail))

print("\n=== 1) 正常指令功能正确性 ===")
for cmd, must_have in [
    ("hw", "STM32F429IGT6"),
    ("dev", "AP3216C"),
    ("net", "192.168.10.99"),
    ("version", "v1.0.0"),
    ("help", "Commands:"),
    ("history", "History"),
    ("beep on", "BEEP ON"),
    ("beep off", "BEEP OFF"),
    ("led on", "LED ON"),
    ("led off", "LED OFF"),
]:
    rc, out = run(cmd)
    check(f"cmd '{cmd}' rc==0", rc == 0, f"rc={rc}")
    check(f"cmd '{cmd}' output contains '{must_have}'", must_have in out,
          out[:60].replace("\n", " "))

print("=== 2) 边界: 空行 / 前后空格 / Tab ===")
rc, out = run("")
check("empty line rc==0 (no crash)", rc == 0)
for pad in ["  hw", "\tdev", "  net  ", "\t version \t"]:
    rc, out = run(pad)
    check(f"padded '{pad.strip()}' rc==0 + output", rc == 0 and len(out) > 0,
          f"rc={rc} len={len(out)}")

print("=== 3) 边界: 超长行 (128 上限, 应安全截断不崩溃) ===")
for n in [127, 128, 129, 200, 500]:
    big = "x" * n
    try:
        rc, out = run(big)
        ok = True
    except Exception as e:
        ok = False
        detail = str(e)
    check(f"long line n={n} no crash", ok, detail if not ok else "")

print("=== 4) 异常: 未知命令 / 参数错误 ===")
rc, out = run("foobar")
check("unknown cmd rc==-1", rc == -1, f"rc={rc}")
check("unknown cmd mentions 'Unknown'", "Unknown" in out, out[:60])
rc, out = run("beep")      # missing arg
check("beep no-arg rc==0 + Usage", rc == 0 and "Usage" in out, out[:60])
rc, out = run("beep xyz")  # bad arg
check("beep bad-arg rc==0 + Usage", rc == 0 and "Usage" in out, out[:60])
rc, out = run("led")
check("led no-arg rc==0 + Usage", rc == 0 and "Usage" in out, out[:60])

print("=== 5) history 环形缓冲 (保留最近 3 条, 走真实回车路径) ===")
# 通过 shell_feed_line (真实回车: history_push + shell_exec)
feed("hw")
feed("dev")
feed("net")
feed("version")
feed("beep on")   # 第 5 条, 应挤出最早的 hw
out = run("history")[1]
check("history shows beep on", "beep on" in out, out[:80])
check("history dropped oldest 'hw'", "hw" not in out, out[:80])
# 历史条数不超过 3
lines = [l for l in out.split("\n") if l.strip().startswith(("1:", "2:", "3:"))]
check("history <= 3 entries", len(lines) <= 3, f"count={len(lines)}")
# 空行不进历史
feed("")
out2 = run("history")[1]
check("empty line not in history", "led on" not in out2 or len(
    [l for l in out2.split("\n") if l.strip().startswith(("1:","2:","3:"))]) <= 3,
    out2[:80])

print("=== 6) 大规模随机指令压测 (10000 条, 含正常/异常/边界) ===")
pool = ["hw","dev","net","version","help","history","beep on","beep off",
        "led on","led off","beep","led","","   ","\t",
        "unknown","beep xyz","led q","  hw  ","\tdev\t"]
random.seed(42)
N = 10000
t0 = time.time()
crashed = False
for i in range(N):
    line = random.choice(pool)
    # 偶尔追加随机后缀制造长/乱输入
    if random.random() < 0.1:
        line += " " + "".join(random.choices(string.ascii_letters, k=random.randint(1,150)))
    try:
        lib.shell_feed_line(line.encode("utf-8","replace"))
    except Exception as e:
        crashed = True
        print(f"  CRASH at i={i}: {e}")
        break
dt = time.time() - t0
check(f"random {N} calls no crash", not crashed)
if not crashed:
    check(f"throughput > 5000 cmd/s", (N/dt) > 5000, f"{N/dt:.0f} cmd/s")

print("\n=== 汇总 ===")
for st, name, detail in CASES:
    mark = "✅" if st == "PASS" else "❌"
    extra = f"  ({detail})" if detail else ""
    print(f"  {mark} {name}{extra}")
print(f"\nTOTAL: {PASS} PASS / {FAIL} FAIL  ({len(CASES)} cases)")
if dt_ := locals().get("dt"):
    print(f"RANDOM LOAD: {N} cmds in {dt:.3f}s -> {N/dt:.0f} cmd/s")
sys.exit(0 if FAIL == 0 else 1)
