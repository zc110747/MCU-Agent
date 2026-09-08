#!/usr/bin/env python3
"""Probe our ESP32-S3 CMSIS-DAP HID device with pywinusb.
Tests both interrupt OUT and feature-report transports for DAP_Info(0x01).
"""
import sys
import time
import pywinusb.hid as hid

VID, PID = 0x303A, 0x8502

dev = None
for d in hid.find_all_hid_devices():
    if d.vendor_id == VID and d.product_id == PID:
        dev = d
        break

if dev is None:
    print("DEVICE NOT FOUND (VID=%04X PID=%04X)" % (VID, PID))
    sys.exit(2)

print("FOUND: %s" % dev.product_name)

dev.open()
print("  out reports :", len(dev.find_output_reports()))
print("  in reports  :", len(dev.find_input_reports()))
print("  feat reports:", len(dev.find_feature_reports()))

captured_in = []


def on_in(data):
    captured_in.append(list(data))
    print("   >> IN (raw) :", [hex(b) for b in data])


dev.set_raw_data_handler(on_in)


def decode(resp):
    """resp = list of bytes (report id stripped)."""
    if not resp:
        return "(empty)"
    cmd = resp[0]
    if cmd == 0x00:  # ID_DAP_Info echo
        plen = resp[1]
        data = bytes(resp[2:2 + plen])
        try:
            s = data.decode("latin1")
        except Exception:
            s = repr(data)
        return "DAP_Info resp len=%d data=%r" % (plen, s)
    return "cmd=0x%02X raw=%r" % (cmd, resp[1:])


def try_interrupt_out(info_type):
    captured_in.clear()
    out = dev.find_output_reports()
    if not out:
        print("   (no output report)")
        return
    # CMSIS-DAP request is up to 64 bytes; prepend report id 0 -> 65 total
    req = [0x00, info_type] + [0x00] * 62
    out[0].send([0x00] + req)
    print("INT-OUT sent DAP_Info(%02X)" % info_type)
    time.sleep(0.4)
    if captured_in:
        for c in captured_in:
            print("   INT-OUT resp:", decode(c[1:]))  # strip report id byte
    else:
        print("   INT-OUT -> NO RESPONSE")


def try_feature(info_type):
    feats = dev.find_feature_reports()
    if not feats:
        print("   (no feature report)")
        return
    fr = feats[0]
    fr.send([0x00, info_type])
    print("FEAT sent DAP_Info(%02X)" % info_type)
    time.sleep(0.4)
    try:
        resp = fr.get()
        print("   FEAT resp:", decode(list(resp)[1:]) if resp else "(empty)")
    except Exception as e:
        print("   FEAT get err:", e)


print("\n=== Test 1: interrupt OUT DAP_Info(0x01 Vendor) ===")
try_interrupt_out(0x01)
print("\n=== Test 2: interrupt OUT DAP_Info(0xFF PacketSize) ===")
try_interrupt_out(0xFF)
print("\n=== Test 3: feature report DAP_Info(0x01) ===")
try_feature(0x01)

dev.close()
print("\nDONE")
