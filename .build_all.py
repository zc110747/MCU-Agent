#!/usr/bin/env python3
# Build every project with the NEW unified tasks to verify they compile.
import os, subprocess, time, sys

ROOT = "D:/user_project/git/Mcu_Project_Design_By_Agent"
LOG = os.path.join(ROOT, "build_all.log")

# builder, configure_cmd, build_cmd
SPEC = {
 "001.stm32h743_tinyusb_cdc_msc": ("preset", "cmake --preset debug", "cmake --build build/debug"),
 "002.stm32h743_tinyusb_uvc_ov5640": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
 "003.stm32h743_lvgl_oled": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
 "004.stm32h743_sd_oled_img": ("preset", "cmake --preset debug", "cmake --build build/debug"),
 "005.stm32h743_person_detect": ("preset", "cmake --preset debug", "cmake --build build/debug"),
 "006.stm32h743_face_detect": ("preset", "cmake --preset debug", "cmake --build build/debug"),
 "007.stm32h743_cmsis_dap": ("preset", "cmake --preset debug", "cmake --build build/debug"),
 "008.stm32h743_lvgl_mos": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
 "009.stm32h743_zephyr": ("west", "python -m west build -b nucleo_h743zi/stm32h743xx -d build -s .", "python -m west build -d build"),
 "010.stm32h743_boot": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
 "101.stm32f429_net": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
 "102.stm32f429_tinyusb_ui": ("cmake", "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug", "cmake --build build"),
}

env = dict(os.environ)
env["ZEPHYR_TOOLCHAIN_VARIANT"] = "host"  # 009: use arm-none-eabi-gcc from PATH

def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True,
                           timeout=timeout, env=env)
        return p.returncode, p.stdout + "\n" + p.stderr
    except subprocess.TimeoutExpired:
        return 124, f"TIMEOUT after {timeout}s"

results = []
with open(LOG, "w", encoding="utf-8") as log:
    log.write("=== Unified-build verification ===\n")
    for name, (b, conf, bld) in SPEC.items():
        cwd = os.path.join(ROOT, name)
        log.write(f"\n##### {name} ({b}) #####\n")
        t0 = time.time()
        rc1, o1 = run(conf, cwd, 600)
        rc2, o2 = (0, "") if b == "west" else run(bld, cwd, 900)  # west: configure already builds
        if b == "west" and rc1 == 0:
            # ensure incremental build step also runs
            rc2, o2 = run(bld, cwd, 900)
        elapsed = int(time.time() - t0)
        out = o1 + "\n" + o2
        warns = sum(1 for l in out.splitlines() if ": warning:" in l)
        errs = sum(1 for l in out.splitlines() if ": error:" in l)
        # capture tail for diagnostics
        tail = "\n".join(out.splitlines()[-25:])
        ok = (rc1 == 0 and rc2 == 0)
        status = "PASS" if ok else "FAIL"
        results.append((name, status, warns, errs, elapsed))
        log.write(out)
        log.write(f"\n--- RESULT: {status}  warnings={warns} errors={errs} elapsed={elapsed}s ---\n")
        log.write(tail + "\n")
        log.flush()
        print(f"{name:32s} {status}  warn={warns} err={errs} {elapsed}s", flush=True)

# summary
with open(LOG, "a", encoding="utf-8") as log:
    log.write("\n=== SUMMARY ===\n")
    for name, status, warns, errs, elapsed in results:
        log.write(f"{name:32s} {status} warn={warns} err={errs} {elapsed}s\n")
    npass = sum(1 for r in results if r[1] == "PASS")
    log.write(f"\n{npass}/{len(results)} projects compiled\n")
print(f"\nSUMMARY: {npass}/{len(results)} PASS")
