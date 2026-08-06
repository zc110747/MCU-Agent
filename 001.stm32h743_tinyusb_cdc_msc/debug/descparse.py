"""Parse the UVC configuration descriptor straight out of the firmware binary.

Catching a malformed descriptor here is far cheaper than guessing why the host
refuses to stream. Checks performed:

  * every bLength walks cleanly to wTotalLength (no gaps, no overruns)
  * the IAD spans the VideoControl + VideoStreaming pair
  * VC header wTotalLength covers all class-specific VC descriptors
  * VS input header wTotalLength covers all class-specific VS descriptors
  * the isochronous endpoint fits in a full-speed 1 ms frame

    python debug/descparse.py [build/stm32h743_uvc.bin]
"""

import struct
import sys

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/stm32h743_uvc.bin"

DESC_DEVICE, DESC_CONFIG, DESC_STRING = 0x01, 0x02, 0x03
DESC_INTERFACE, DESC_ENDPOINT = 0x04, 0x05
DESC_IAD = 0x0B
DESC_CS_INTERFACE = 0x24

VC_SUBTYPE = {1: "VC_HEADER", 2: "INPUT_TERMINAL", 3: "OUTPUT_TERMINAL",
              4: "SELECTOR_UNIT", 5: "PROCESSING_UNIT", 6: "EXTENSION_UNIT"}
VS_SUBTYPE = {1: "VS_INPUT_HEADER", 2: "VS_OUTPUT_HEADER", 3: "STILL_FRAME",
              4: "FORMAT_UNCOMPRESSED", 5: "FRAME_UNCOMPRESSED",
              6: "FORMAT_MJPEG", 7: "FRAME_MJPEG", 13: "COLORFORMAT"}
XFER = {0: "control", 1: "isochronous", 2: "bulk", 3: "interrupt"}

data = open(BIN, "rb").read()

# ---- locate the configuration descriptor -----------------------------------
# bLength=9, bDescriptorType=2, then wTotalLength; require a video IAD nearby.
cfg_off = None
for i in range(len(data) - 9):
    if data[i] == 9 and data[i + 1] == DESC_CONFIG:
        total = data[i + 2] | (data[i + 3] << 8)
        if 40 < total < 512 and i + total <= len(data):
            blob = data[i:i + total]
            if bytes([DESC_IAD, 0x00, 0x02, 0x0E]) in blob or b"\x0b\x00\x02\x0e" in blob:
                cfg_off = i
                break
if cfg_off is None:
    print("could not find a video configuration descriptor in", BIN)
    sys.exit(1)

total = data[cfg_off + 2] | (data[cfg_off + 3] << 8)
cfg = data[cfg_off:cfg_off + total]
print(f"configuration descriptor @ flash 0x{0x08000000 + cfg_off:08X}, "
      f"wTotalLength={total}\n")

errors, warnings = [], []
off = 0
cur_itf = None          # (bInterfaceNumber, bAlternateSetting, bInterfaceSubClass)
vc_declared = vc_seen = 0
vs_declared = vs_seen = 0
ep_info = None
iad = None
itf_numbers = set()

while off < total:
    blen = cfg[off]
    btype = cfg[off + 1]
    if blen == 0:
        errors.append(f"zero bLength at offset {off} - descriptor chain broken")
        break
    if off + blen > total:
        errors.append(f"descriptor at {off} (len {blen}) overruns wTotalLength")
        break

    d = cfg[off:off + blen]
    ind = "  "

    if btype == DESC_CONFIG:
        n_itf, cfg_val, _, attr, power = d[4], d[5], d[6], d[7], d[8]
        print(f"CONFIGURATION  bNumInterfaces={n_itf} bConfigurationValue={cfg_val} "
              f"bmAttributes=0x{attr:02X} bMaxPower={power * 2} mA")

    elif btype == DESC_IAD:
        iad = (d[2], d[3], d[4], d[5], d[6])
        print(f"{ind}IAD          first={d[2]} count={d[3]} "
              f"class=0x{d[4]:02X} subclass=0x{d[5]:02X} protocol=0x{d[6]:02X}")
        if d[4] != 0x0E:
            errors.append(f"IAD bFunctionClass is 0x{d[4]:02X}, expected 0x0E (video)")

    elif btype == DESC_INTERFACE:
        num, alt, neps, cls, sub, proto = d[2], d[3], d[4], d[5], d[6], d[7]
        itf_numbers.add(num)
        cur_itf = (num, alt, sub)
        kind = {1: "VideoControl", 2: "VideoStreaming"}.get(sub, f"subclass 0x{sub:02X}")
        print(f"{ind}INTERFACE    #{num} alt={alt} eps={neps} "
              f"class=0x{cls:02X} ({kind}) protocol=0x{proto:02X}")
        if cls != 0x0E:
            errors.append(f"interface {num}/{alt} class is 0x{cls:02X}, expected 0x0E")

    elif btype == DESC_CS_INTERFACE:
        sub = d[2]
        if cur_itf and cur_itf[2] == 1:      # VideoControl
            name = VC_SUBTYPE.get(sub, f"0x{sub:02X}")
            if sub == 1:
                vc_declared = d[5] | (d[6] << 8)
                bcd = d[3] | (d[4] << 8)
                clk = struct.unpack_from("<I", d, 7)[0]
                print(f"{ind}  CS_VC      {name} bcdUVC=0x{bcd:04X} "
                      f"wTotalLength={vc_declared} clock={clk} Hz")
            else:
                print(f"{ind}  CS_VC      {name} (len {blen})")
            vc_seen += blen
        elif cur_itf and cur_itf[2] == 2:    # VideoStreaming
            name = VS_SUBTYPE.get(sub, f"0x{sub:02X}")
            extra = ""
            if sub == 1:
                vs_declared = d[4] | (d[5] << 8)
                extra = (f" bNumFormats={d[3]} wTotalLength={vs_declared} "
                         f"bEndpointAddress=0x{d[6]:02X}")
            elif sub == 4:
                guid = d[5:21]
                extra = f" bFormatIndex={d[3]} bpp={d[21]} guid={guid[:4].hex()}..."
            elif sub == 5:
                w = d[5] | (d[6] << 8)
                h = d[7] | (d[8] << 8)
                maxbuf = struct.unpack_from("<I", d, 17)[0]
                deflt = struct.unpack_from("<I", d, 21)[0]
                fps = 10_000_000 / deflt if deflt else 0
                extra = (f" {w}x{h} dwMaxVideoFrameBufferSize={maxbuf} "
                         f"defaultInterval={deflt} ({fps:.1f} fps)")
                if maxbuf != w * h * 2:
                    warnings.append(
                        f"dwMaxVideoFrameBufferSize={maxbuf} != {w}*{h}*2={w * h * 2}")
            print(f"{ind}  CS_VS      {name}{extra}")
            vs_seen += blen

    elif btype == DESC_ENDPOINT:
        addr, attr = d[2], d[3]
        mps = d[4] | (d[5] << 8)
        interval = d[6]
        size = mps & 0x7FF
        mult = ((mps >> 11) & 0x3) + 1
        ep_info = (addr, attr & 0x3, size, mult, interval)
        print(f"{ind}  ENDPOINT   0x{addr:02X} {XFER[attr & 3]} "
              f"wMaxPacketSize={size} x{mult} bInterval={interval}")

    off += blen

# ---- cross-checks ----------------------------------------------------------
print("\n--- consistency ---")

if off != total:
    errors.append(f"descriptor chain ended at {off}, wTotalLength says {total}")
else:
    print("bLength chain walks exactly to wTotalLength")

if vc_declared and vc_declared != vc_seen:
    errors.append(f"VC header wTotalLength={vc_declared} but class-specific "
                  f"VC bytes total {vc_seen}")
elif vc_declared:
    print(f"VideoControl  wTotalLength matches ({vc_declared} bytes)")

if vs_declared and vs_declared != vs_seen:
    errors.append(f"VS input header wTotalLength={vs_declared} but class-specific "
                  f"VS bytes total {vs_seen}")
elif vs_declared:
    print(f"VideoStreaming wTotalLength matches ({vs_declared} bytes)")

if iad:
    first, count = iad[0], iad[1]
    if set(range(first, first + count)) != itf_numbers:
        errors.append(f"IAD covers interfaces {first}..{first + count - 1} but the "
                      f"config defines {sorted(itf_numbers)}")
    else:
        print(f"IAD spans exactly interfaces {sorted(itf_numbers)}")
else:
    errors.append("no Interface Association Descriptor - Windows will not load usbvideo.sys")

if ep_info:
    addr, xfer, size, mult, interval = ep_info
    if xfer != 1:
        errors.append("streaming endpoint is not isochronous")
    if size > 1023:
        errors.append(f"wMaxPacketSize={size} exceeds the full-speed limit of 1023")
    else:
        bw = size * 1000 // (1 << (interval - 1))
        print(f"full-speed isochronous budget: {size} B every "
              f"{1 << (interval - 1)} ms = {bw / 1000:.1f} kB/s")
        # 2-byte UVC payload header on every packet
        payload = size - 2
        frame_bytes = 240 * 240 * 2
        ms = -(-frame_bytes // payload) * (1 << (interval - 1))
        print(f"240x240 YUY2 frame = {frame_bytes} B -> {ms} ms -> "
              f"{1000 / ms:.1f} fps sustained")

print()
for w in warnings:
    print("WARN :", w)
for e in errors:
    print("ERROR:", e)
if not errors:
    print("descriptor set is self-consistent")
sys.exit(1 if errors else 0)
