"""Show every PnP node Windows created for our device, plus any UVC bindings.

A working UVC camera looks like this:

    USB\\VID_CAFE&PID_4020\\<serial>              -> usbccgp  (composite parent)
    USB\\VID_CAFE&PID_4020&MI_00\\<...>           -> usbvideo (the camera child)

If the &MI_00 child is missing, usbccgp never split the function out, which
almost always means the IAD / device-class triple was not accepted.
"""

import winreg

ENUM = r"SYSTEM\CurrentControlSet\Enum"
SEP = "\\"


def val(key, name):
    try:
        return winreg.QueryValueEx(key, name)[0]
    except OSError:
        return None


def subkeys(key):
    out, i = [], 0
    while True:
        try:
            out.append(winreg.EnumKey(key, i))
        except OSError:
            return out
        i += 1


def main():
    usb = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, ENUM + SEP + "USB")

    print("=== PnP nodes for VID_CAFE ===")
    found_any = False
    for node in subkeys(usb):
        if "VID_CAFE" not in node.upper():
            continue
        found_any = True
        nk = winreg.OpenKey(usb, node)
        for inst in subkeys(nk):
            ik = winreg.OpenKey(nk, inst)
            print(f"  USB\\{node}\\{inst}")
            print(f"      DeviceDesc  : {val(ik, 'DeviceDesc')}")
            print(f"      Service     : {val(ik, 'Service')}")
            print(f"      CompatibleID: {val(ik, 'CompatibleIDs')}")
            print(f"      ConfigFlags : {val(ik, 'ConfigFlags')}")
    if not found_any:
        print("  (none)")

    print()
    print("=== Anything bound to usbvideo.sys ===")
    hits = 0
    root = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, ENUM)
    for bus in subkeys(root):
        bk = winreg.OpenKey(root, bus)
        for dev in subkeys(bk):
            dk = winreg.OpenKey(bk, dev)
            for inst in subkeys(dk):
                ik = winreg.OpenKey(dk, inst)
                svc = val(ik, "Service")
                if svc and svc.lower() == "usbvideo":
                    hits += 1
                    print(f"  {bus}\\{dev}\\{inst}")
                    print(f"      {val(ik, 'DeviceDesc')}")
    if hits == 0:
        print("  (none - no UVC camera is currently bound on this machine)")


if __name__ == "__main__":
    main()
