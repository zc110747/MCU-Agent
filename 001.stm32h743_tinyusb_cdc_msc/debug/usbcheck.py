"""Check how Windows enumerated the UVC device, without PowerShell.

Reads HKLM\\SYSTEM\\CurrentControlSet\\Enum\\USB directly, which is where the
PnP manager records every device it has seen, including the descriptors it
managed to read and the driver it bound.

    python debug/usbcheck.py            # default VID:PID cafe:4020
    python debug/usbcheck.py cafe 4020
"""

import sys
import winreg

VID = (sys.argv[1] if len(sys.argv) > 1 else "cafe").upper()
PID = (sys.argv[2] if len(sys.argv) > 2 else "4020").upper()

ENUM_USB = r"SYSTEM\CurrentControlSet\Enum\USB"
KEY = f"VID_{VID}&PID_{PID}"

INTERESTING = (
    "DeviceDesc", "FriendlyName", "Service", "Class", "ClassGUID",
    "Mfg", "ContainerID", "ConfigFlags", "HardwareID", "CompatibleIDs",
)

# Problem codes worth spelling out; 0 / absent means the device is happy.
CM_PROB = {
    1: "CM_PROB_NOT_CONFIGURED",
    3: "CM_PROB_OUT_OF_MEMORY",
    10: "CM_PROB_FAILED_START",
    18: "CM_PROB_REINSTALL",
    19: "CM_PROB_REGISTRY",
    22: "CM_PROB_DISABLED",
    28: "CM_PROB_FAILED_INSTALL (no driver)",
    31: "CM_PROB_FAILED_DRIVER_ENTRY",
    43: "CM_PROB_FAILED_POST_START (device reported a failure)",
}


def dump_values(key, indent="    "):
    i = 0
    while True:
        try:
            name, value, _ = winreg.EnumValue(key, i)
        except OSError:
            break
        i += 1
        if name in INTERESTING:
            if isinstance(value, list):
                value = " | ".join(value)
            print(f"{indent}{name:<16}= {value}")


def main():
    try:
        usb = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, ENUM_USB)
    except OSError as exc:
        print(f"cannot open {ENUM_USB}: {exc}")
        return 1

    # 1. Is our VID/PID there at all?
    try:
        dev = winreg.OpenKey(usb, KEY)
    except OSError:
        print(f"[MISS] {KEY} is not in the USB enumeration tree.")
        print("       Windows never completed GET_DESCRIPTOR for this device.")
        # Show anything that did show up, to help spot a wrong VID/PID.
        print("\n       VID/PIDs currently present under Enum\\USB:")
        i = 0
        while True:
            try:
                sub = winreg.EnumKey(usb, i)
            except OSError:
                break
            i += 1
            if sub.startswith("VID_"):
                print(f"         {sub}")
        return 2

    print(f"[HIT ] {KEY} found\n")

    # 2. Walk each instance (one per physical plug-in path).
    i = 0
    while True:
        try:
            inst_name = winreg.EnumKey(dev, i)
        except OSError:
            break
        i += 1

        print(f"  instance: {inst_name}")
        inst = winreg.OpenKey(dev, inst_name)
        dump_values(inst)

        # Problem code lives under Properties\{GUID}\0002 on modern Windows,
        # but ConfigFlags / the Device Parameters subkey are easier to reach.
        try:
            params = winreg.OpenKey(inst, "Device Parameters")
            print("    -- Device Parameters --")
            dump_values(params, indent="      ")
        except OSError:
            pass

        # Driver binding tells us whether usbvideo.sys took it.
        try:
            drv, _ = winreg.QueryValueEx(inst, "Driver")
            print(f"    Driver key      = {drv}")
            cls = winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                r"SYSTEM\CurrentControlSet\Control\Class\\" + drv,
            )
            for v in ("DriverDesc", "ProviderName", "DriverVersion", "InfPath"):
                try:
                    print(f"      {v:<14}= {winreg.QueryValueEx(cls, v)[0]}")
                except OSError:
                    pass
        except OSError:
            print("    Driver key      = <none> (no driver bound yet)")

        print()

    return 0


if __name__ == "__main__":
    sys.exit(main())
