"""Open the STM32 UVC camera from the host and prove frames really arrive.

This is the end-to-end test: it forces the host to run the UVC probe/commit
handshake and pull isochronous data, which is exactly the path the firmware
telemetry cannot exercise on its own.

    python debug/capture_test.py              # auto-pick, grab 20 frames
    python debug/capture_test.py --index 1    # force a device index
    python debug/capture_test.py --frames 40 --out debug/out

Saves the first and last frame as PNG so the image content can be eyeballed.
"""

import argparse
import os
import sys
import time

import cv2
import numpy as np

TARGET_W, TARGET_H = 240, 240


def try_open(index, backend, name):
    cap = cv2.VideoCapture(index, backend)
    if not cap.isOpened():
        cap.release()
        return None
    # Ask for exactly what the firmware advertises.
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, TARGET_W)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, TARGET_H)
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    print(f"  [{name} idx {index}] opened, reports {w}x{h}")
    if (w, h) != (TARGET_W, TARGET_H):
        print(f"  [{name} idx {index}] not our 240x240 device, skipping")
        cap.release()
        return None
    return cap


def find_camera(forced):
    backends = [(cv2.CAP_MSMF, "MSMF"), (cv2.CAP_DSHOW, "DSHOW")]
    indices = [forced] if forced is not None else range(0, 6)

    for backend, name in backends:
        for i in indices:
            print(f"probing {name} index {i} ...")
            cap = try_open(i, backend, name)
            if cap is not None:
                return cap, f"{name}:{i}"
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index", type=int, default=None)
    ap.add_argument("--frames", type=int, default=20)
    ap.add_argument("--out", default="debug/out")
    args = ap.parse_args()

    cap, who = find_camera(args.index)
    if cap is None:
        print("\nno 240x240 camera found.")
        print("The device enumerates, but the host could not open a stream.")
        return 2

    print(f"\nstreaming from {who}, grabbing {args.frames} frames ...")
    os.makedirs(args.out, exist_ok=True)

    frames, first, last = 0, None, None
    t0 = time.time()
    deadline = t0 + 30.0

    while frames < args.frames and time.time() < deadline:
        ok, img = cap.read()
        if not ok or img is None:
            continue
        frames += 1
        if first is None:
            first = img.copy()
            print(f"  first frame after {time.time() - t0:.2f} s  "
                  f"shape={img.shape} dtype={img.dtype}")
        last = img

    elapsed = time.time() - t0
    cap.release()

    if frames == 0:
        print("opened the device but received zero frames.")
        return 3

    fps = frames / elapsed
    print(f"\n{frames} frames in {elapsed:.2f} s  ->  {fps:.2f} fps")

    # Content sanity: a frozen or all-black stream is a failure mode worth naming.
    d = cv2.absdiff(first, last)
    changed = int(np.count_nonzero(d.max(axis=2) > 8))
    total = first.shape[0] * first.shape[1]
    mean = float(last.mean())
    print(f"mean pixel level : {mean:.1f}  (0 = black, 255 = white)")
    print(f"first vs last    : {changed}/{total} pixels differ "
          f"({100.0 * changed / total:.1f}%)")

    if mean < 2.0:
        print("WARN: the image is essentially black - check sensor exposure / lens cap")
    if changed == 0:
        print("WARN: first and last frame are identical - the stream may be frozen")

    p1 = os.path.join(args.out, "frame_first.png")
    p2 = os.path.join(args.out, "frame_last.png")
    cv2.imwrite(p1, first)
    cv2.imwrite(p2, last)
    print(f"\nsaved {p1}\nsaved {p2}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
