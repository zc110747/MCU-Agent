#!/usr/bin/env python3
# One-shot generator: unify .vscode build/sim configs across the 12 STM32 projects.
import os, json, shutil

ROOT = os.path.dirname(os.path.abspath(__file__))

# Per-project spec
#  builder: "preset" | "cmake" | "west"
#  elf: relative path from project root to the built elf (used by flash + launch)
#  builddir: build dir used by configure/build/clean (relative)
#  device: cortex-debug device string
#  rtos: optional
#  svd: final svd filename at root (None if none)
#  src_svd: list of source svd paths (relative) to move to root
#  cfg_moves: list of (src cfg, dst cfg-at-root)
#  cfg_create: if True, author a default H743 openocd.cfg at root
#  extra_dap: if True, also keep an F429-through-DAP attach config (007)
SPEC = {
 "001.stm32h743_tinyusb_cdc_msc": dict(builder="preset", elf="build/debug/h743_tinyusb_cdc.elf",
     builddir="build/debug", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=[".vscode/STM32H743.svd"],
     cfg_moves=[("openocd/stm32h743_stlink.cfg","openocd.cfg")]),
 "002.stm32h743_tinyusb_uvc_ov5640": dict(builder="cmake", elf="build/stm32h743_uvc.elf",
     builddir="build", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=["debug/STM32H743.svd"],
     cfg_moves=[("debug/openocd.cfg","openocd.cfg")]),
 "003.stm32h743_lvgl_oled": dict(builder="cmake", elf="build/lvgl_oled.elf",
     builddir="build", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=["debug/STM32H743.svd"],
     cfg_moves=[]),
 "004.stm32h743_sd_oled_img": dict(builder="preset", elf="build/debug/stm32_sd_oled.elf",
     builddir="build/debug", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=["tools/STM32H743.svd"],
     cfg_moves=[], cfg_create=True),
 "005.stm32h743_person_detect": dict(builder="preset", elf="build/debug/stm32h7_person_detect.elf",
     builddir="build/debug", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=[],  # svd already at root
     cfg_moves=[], cfg_create=True),
 "006.stm32h743_face_detect": dict(builder="preset", elf="build/debug/stm32h7_face_detect.elf",
     builddir="build/debug", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=["tools/STM32H743.svd"],
     cfg_moves=[], cfg_create=True),
 "007.stm32h743_cmsis_dap": dict(builder="preset", elf="build/debug/h743_cmsis_dap.elf",
     builddir="build/debug", device="STM32H743ZI", svd="STM32H743.svd",
     src_svd=[".vscode/STM32H743.svd"],
     cfg_moves=[("openocd/stm32h743_stlink.cfg","openocd.cfg"),
                ("openocd/stm32f429_cmsisdap.cfg","openocd_cmsisdap.cfg")],
     extra_dap=True),
 "008.stm32h743_lvgl_mos": dict(builder="cmake", elf="build/nes_h743.elf",
     builddir="build", device="STM32H743ZI", svd=None,
     src_svd=[],
     cfg_moves=[]),
 "009.stm32h743_zephyr": dict(builder="west", elf="build/zephyr/zephyr.elf",
     builddir="build", device="STM32H743ZI", rtos="Zephyr", svd=None,
     src_svd=[],
     cfg_moves=[("tools/openocd.cfg","openocd.cfg")]),
 "010.stm32h743_boot": dict(builder="cmake", elf="build/stm32h7_boot.elf",
     builddir="build", device="STM32H743ZI", svd=None,
     src_svd=[],
     cfg_moves=[]),
 "101.stm32f429_net": dict(builder="cmake", elf="build/stm32f429_net.elf",
     builddir="build", device="STM32F429ZIT6", svd=None,
     src_svd=[],
     cfg_moves=[("openocd/openocd.cfg","openocd.cfg")]),
 "102.stm32f429_tinyusb_ui": dict(builder="cmake", elf="build/stm32f429_tinyusb_ui.elf",
     builddir="build", device="STM32F429ZIT6", svd="STM32F429x.svd",
     src_svd=[".vscode/STM32F429x.svd"],
     cfg_moves=[("openocd/stm32f429_stlink.cfg","openocd.cfg")]),
}

OPENOCD_H743 = """# OpenOCD configuration - STM32H743ZIT6 over ST-Link (SWD)
# Auto-resolved from OpenOCD's own script search path (no absolute paths).
source [find interface/stlink.cfg]
transport select swd
source [find target/stm32h7x.cfg]
reset_config srst_only
adapter speed 4000
"""

def tasks_json(s):
    b = s["builder"]; elf = s["elf"]; bd = s["builddir"]; cfg = "openocd.cfg"
    if b == "preset":
        configure = "cmake --preset debug"
        build = f"cmake --build {bd}"
        clean = f"cmake --build {bd} --target clean"
    elif b == "cmake":
        configure = "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug"
        build = "cmake --build build"
        clean = "cmake --build build --target clean"
    else:  # west
        configure = "python -m west build -b nucleo_h743zi/stm32h743xx -d build -s ."
        build = "python -m west build -d build"
        clean = "python -m west build -t clean -d build"
    flash_cmd = f'openocd -f {cfg} -c "program {elf} verify reset exit"'
    return {
        "version": "2.0.0",
        "tasks": [
            {"label": "configure", "type": "shell", "command": configure,
             "problemMatcher": [], "group": "build"},
            {"label": "build", "type": "shell", "command": build,
             "group": {"kind": "build", "isDefault": True},
             "problemMatcher": ["$gcc"],
             "presentation": {"reveal": "always", "panel": "shared", "clear": True}},
            {"label": "clean", "type": "shell", "command": clean,
             "problemMatcher": []},
            {"label": "flash", "type": "shell", "command": flash_cmd,
             "dependsOn": "build", "problemMatcher": [],
             "presentation": {"reveal": "always", "panel": "shared"}}
        ]
    }

def launch_json(s):
    cfgs = []
    dbg = {
        "name": "Debug (OpenOCD + ST-Link)",
        "type": "cortex-debug", "request": "launch", "servertype": "openocd",
        "cwd": "${workspaceFolder}",
        "executable": "${workspaceFolder}/" + s["elf"],
        "configFiles": ["${workspaceFolder}/openocd.cfg"],
        "openocdPath": "openocd", "gdbPath": "arm-none-eabi-gdb",
        "device": s["device"], "interface": "swd",
        "runToEntryPoint": "main", "preLaunchTask": "build",
        "showDevDebugOutput": "none"
    }
    if s.get("rtos"): dbg["rtos"] = s["rtos"]
    if s.get("svd"): dbg["svdFile"] = "${workspaceFolder}/" + s["svd"]
    cfgs.append(dbg)
    # attach
    att = dict(dbg); att["name"] = "Attach (no reflash)"; att["request"] = "attach"
    del att["preLaunchTask"]; del att["runToEntryPoint"]
    cfgs.append(att)
    if s.get("extra_dap"):
        dap = {
            "name": "Debug STM32F429 THROUGH our probe",
            "type": "cortex-debug", "request": "attach", "servertype": "openocd",
            "cwd": "${workspaceFolder}",
            "executable": "${workspaceFolder}/" + s["elf"],
            "configFiles": ["${workspaceFolder}/openocd_cmsisdap.cfg"],
            "openocdPath": "openocd", "gdbPath": "arm-none-eabi-gdb",
            "device": "STM32F429ZI", "interface": "swd",
            "showDevDebugOutput": "none"
        }
        cfgs.append(dap)
    return {"version": "0.2.0", "configurations": cfgs}

def settings_json(s):
    st = {
        "cmake.generator": "Ninja",
        "cmake.configureOnOpen": False,
        "cortex-debug.openocdPath": "openocd",
        "cortex-debug.gdbPath": "arm-none-eabi-gdb",
        "cortex-debug.armToolchainPath": "",
        "C_Cpp.default.compilerPath": "arm-none-eabi-gcc",
        "C_Cpp.default.intelliSenseMode": "gcc-arm"
    }
    if s["builder"] == "preset":
        st["cmake.useCMakePresets"] = "always"
    else:
        st["cmake.buildDirectory"] = "${workspaceFolder}/build"
    return st

def move_cfg(p, src, dst):
    sp = os.path.join(p, src); dp = os.path.join(p, dst)
    if os.path.isfile(sp):
        shutil.move(sp, dp); return f"moved {src} -> {dst}"
    return f"(skip {src} not found)"

def move_svd(p, srcs, svdname):
    if not svdname: return ""
    dp = os.path.join(p, svdname)
    if os.path.isfile(dp): return f"(svd {svdname} already at root)"
    for sp in srcs:
        full = os.path.join(p, sp)
        if os.path.isfile(full):
            shutil.move(full, dp); return f"moved {sp} -> {svdname}"
    return "(no svd source found)"

def remove_if_empty(d):
    if os.path.isdir(d) and not os.listdir(d):
        os.rmdir(d); return f"removed empty {d}"
    return ""

log = []
for name, s in SPEC.items():
    p = os.path.join(ROOT, name)
    vd = os.path.join(p, ".vscode")
    os.makedirs(vd, exist_ok=True)
    # write configs
    with open(os.path.join(vd, "tasks.json"), "w", encoding="utf-8") as f:
        json.dump(tasks_json(s), f, indent=2, ensure_ascii=False); f.write("\n")
    with open(os.path.join(vd, "launch.json"), "w", encoding="utf-8") as f:
        json.dump(launch_json(s), f, indent=2, ensure_ascii=False); f.write("\n")
    with open(os.path.join(vd, "settings.json"), "w", encoding="utf-8") as f:
        json.dump(settings_json(s), f, indent=2, ensure_ascii=False); f.write("\n")
    # fix c_cpp_properties compilerPath if it used env/absolute
    ccp = os.path.join(vd, "c_cpp_properties.json")
    if os.path.isfile(ccp):
        try:
            d = json.load(open(ccp, encoding="utf-8"))
            changed = False
            for c in d.get("configurations", []):
                cp = c.get("compilerPath", "")
                if "env:" in cp or ":" in cp and "\\" in cp:
                    c["compilerPath"] = "arm-none-eabi-gcc"; changed = True
            if changed:
                json.dump(d, open(ccp, "w", encoding="utf-8"), indent=4); 
        except Exception as e:
            log.append(f"{name}: c_cpp parse warn {e}")
    # svd
    r = move_svd(p, s.get("src_svd", []), s.get("svd"))
    if r: log.append(f"{name}: svd {r}")
    # cfg moves
    for src, dst in s.get("cfg_moves", []):
        log.append(f"{name}: cfg {move_cfg(p, src, dst)}")
    # create openocd.cfg if needed
    if s.get("cfg_create"):
        dp = os.path.join(p, "openocd.cfg")
        if not os.path.isfile(dp):
            with open(dp, "w", encoding="utf-8") as f: f.write(OPENOCD_H743)
            log.append(f"{name}: created openocd.cfg (H743)")
        else:
            log.append(f"{name}: openocd.cfg already present")
    # remove empty openocd/ dir
    for d in ["openocd", "debug", "tools"]:
        full = os.path.join(p, d)
        if os.path.isdir(full) and not os.listdir(full):
            log.append(f"{name}: {remove_if_empty(full)}")

print("\n".join(log))
print("DONE")
