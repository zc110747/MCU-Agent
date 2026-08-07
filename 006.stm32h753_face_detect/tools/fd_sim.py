"""
Bit-accurate numpy replica of the on-target int8 inference (fd_infer.c).

It loads the quantised graph dumped by fd_export.py (middleware/face_detect/
fd_model_q.npz), runs it exactly the way CMSIS-NN does - same per-channel
requantisation, same padding-with-zero-point, same [-128,127] clamp - and then
runs the identical CenterNet decode.  If a face lights up the right grid cell
here, the MCU will see the same thing, so we never discover a quantisation
collapse only after flashing.

Usage
    # sanity-check the exported model on N validation crops from the cache
    python tools/fd_sim.py --cache tools/runs/cache_lim0_c3_m8.npz --num 12

    # or point it at a single image file
    python tools/fd_sim.py --image some_face.jpg
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fd_arch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
NPZ = os.path.normpath(os.path.join(HERE, "..", "middleware", "face_detect",
                                    "fd_model_q.npz"))

INPUT_W, INPUT_H = fd_arch.INPUT_W, fd_arch.INPUT_H
STRIDE = fd_arch.STRIDE
GRID_W, GRID_H = fd_arch.GRID_W, fd_arch.GRID_H
DEFAULT_THRESHOLD = 115


# --------------------------------------------------------------------------
# TFLite / CMSIS-NN fixed-point requantisation (per output channel)
# --------------------------------------------------------------------------
def _sat_round_doubling_high_mul(a, b):
    """SaturatingRoundingDoublingHighMul on int64 arrays -> int64 (int32 range)."""
    ab = a.astype(np.int64) * b.astype(np.int64)
    nudge = np.where(ab >= 0, 1 << 30, 1 - (1 << 30)).astype(np.int64)
    res = (ab + nudge) >> 31
    return np.clip(res, -(2 ** 31), 2 ** 31 - 1)


def _rounding_divide_by_pot(x, exp):
    """RoundingDivideByPOT with a per-channel exponent array."""
    x = x.astype(np.int64)
    exp = exp.astype(np.int64)
    mask = (np.int64(1) << exp) - 1
    remainder = x & mask
    threshold = (mask >> 1) + np.where(x < 0, 1, 0).astype(np.int64)
    return (x >> exp) + np.where(remainder > threshold, 1, 0).astype(np.int64)


def requantize(acc, mult, shift):
    """acc (H,W,C) int64, mult/shift (C,) -> requantised int64 (H,W,C)."""
    left = np.where(shift > 0, shift, 0).astype(np.int64)
    right = np.where(shift < 0, -shift, 0).astype(np.int64)
    x = acc.astype(np.int64) * (np.int64(1) << left)          # broadcast per-ch
    x = _sat_round_doubling_high_mul(x, mult.astype(np.int64))
    return _rounding_divide_by_pot(x, right)


# --------------------------------------------------------------------------
# Layers
# --------------------------------------------------------------------------
def _patches(xin, k, s):
    """(H,W,C) int32 -> (oh,ow,k,k,C) patches."""
    from numpy.lib.stride_tricks import sliding_window_view
    win = sliding_window_view(xin, (k, k), axis=(0, 1))   # (H-k+1,W-k+1,C,k,k)
    win = win[::s, ::s]                                    # stride subsample
    return win.transpose(0, 1, 3, 4, 2)                   # (oh,ow,k,k,C)


def run_layer(x_i8, L, meta):
    io, oo, out_zp = int(meta[0]), int(meta[1]), int(meta[2])
    kind = L["kind"]
    k, s, p = L["k"], L["stride"], L["pad"]
    Cin = L["in_c"]
    Cout = L["out_c"]

    zp_in = -io
    xin = x_i8.astype(np.int32)
    if p > 0:
        xin = np.pad(xin, ((p, p), (p, p), (0, 0)), constant_values=zp_in)
    xin = xin + io                                         # padded region -> 0

    patches = _patches(xin, k, s)                          # (oh,ow,k,k,Cin)

    if kind == "conv":
        w = L["weights"].reshape(Cout, k, k, Cin).astype(np.int64)
        acc = np.einsum("ijklm,nklm->ijn", patches.astype(np.int64), w)
    else:  # depthwise, weights [1,k,k,Cout], Cin==Cout
        w = L["weights"].reshape(k, k, Cout).astype(np.int64)
        acc = np.einsum("ijklc,klc->ijc", patches.astype(np.int64), w)

    acc = acc + L["bias"].astype(np.int64)                 # (oh,ow,Cout)
    q = requantize(acc, L["mult"], L["shift"]) + out_zp
    q = np.clip(q, -128, 127).astype(np.int8)
    return q


# --------------------------------------------------------------------------
# Load the quantised graph
# --------------------------------------------------------------------------
def load_model(npz_path):
    d = np.load(npz_path)
    shapes = fd_arch.layer_shapes()
    layers = []
    for name, kind, k, s, pad, ihwc, ohwc, is_head in shapes:
        layers.append(dict(
            name=name, kind=kind, k=k, stride=s, pad=pad,
            in_c=ihwc[2], out_c=ohwc[2],
            weights=d[f"{name}_weights"], bias=d[f"{name}_bias"],
            mult=d[f"{name}_mult"], shift=d[f"{name}_shift"],
            meta=d[f"{name}_meta"], oscale=float(d[f"{name}_oscale"][0]),
            is_head=is_head))
    return layers


def infer(layers, gray_u8):
    """gray_u8: (96,96) uint8 -> (s_hm, s_wh, s_off) int8 grids + head params."""
    x = (gray_u8.astype(np.int32) - 128).astype(np.int8)[..., None]   # (96,96,1)
    heads = {}
    for L in layers:
        if L["is_head"]:
            heads[L["name"]] = (run_layer(x, L, L["meta"]), L)
        else:
            x = run_layer(x, L, L["meta"])
    return heads


# --------------------------------------------------------------------------
# CenterNet decode (mirrors fd_infer.c fd_decode)
# --------------------------------------------------------------------------
def sigmoid(v):
    return 1.0 / (1.0 + np.exp(-v))


def decode(heads, threshold=DEFAULT_THRESHOLD):
    hm_q, hmL = heads["head_hm"]
    wh_q, _ = heads["head_wh"]
    off_q, _ = heads["head_off"]

    hm_zp = int(hmL["meta"][2]); hm_sc = hmL["oscale"]
    wh_zp = int(heads["head_wh"][1]["meta"][2]); wh_sc = heads["head_wh"][1]["oscale"]
    off_zp = int(heads["head_off"][1]["meta"][2]); off_sc = heads["head_off"][1]["oscale"]

    logit = (hm_q[..., 0].astype(np.float32) - hm_zp) * hm_sc
    prob = sigmoid(logit)
    score = np.clip((prob * 255.0 + 0.5).astype(np.int32), 0, 255)
    peak = int(score.max())

    boxes = []
    for row in range(GRID_H):
        for col in range(GRID_W):
            sc = int(score[row, col])
            if sc < threshold:
                continue
            is_peak = True
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dy == 0 and dx == 0:
                        continue
                    ny, nx = row + dy, col + dx
                    if 0 <= ny < GRID_H and 0 <= nx < GRID_W:
                        if score[ny, nx] > sc:
                            is_peak = False
            if not is_peak:
                continue
            ox = (int(off_q[row, col, 0]) - off_zp) * off_sc
            oy = (int(off_q[row, col, 1]) - off_zp) * off_sc
            bw = (int(wh_q[row, col, 0]) - wh_zp) * wh_sc
            bh = (int(wh_q[row, col, 1]) - wh_zp) * wh_sc
            ox = min(max(ox, 0.0), 1.0)
            oy = min(max(oy, 0.0), 1.0)
            if bw <= 0 or bh <= 0:
                continue
            cx = (col + ox) * STRIDE
            cy = (row + oy) * STRIDE
            w = int(bw * INPUT_W + 0.5)
            h = int(bh * INPUT_H + 0.5)
            x0 = int(cx + 0.5) - w // 2
            y0 = int(cy + 0.5) - h // 2
            if x0 < 0:
                w += x0; x0 = 0
            if y0 < 0:
                h += y0; y0 = 0
            if x0 + w > INPUT_W:
                w = INPUT_W - x0
            if y0 + h > INPUT_H:
                h = INPUT_H - y0
            if w < 6 or h < 6:
                continue
            boxes.append((x0, y0, w, h, sc))
    boxes.sort(key=lambda b: -b[4])
    return peak, boxes, score


# --------------------------------------------------------------------------
# Optional preview montage (needs OpenCV, upscaled 3x for legibility)
# --------------------------------------------------------------------------
def _annotate(gray_u8, boxes, tgt, up=3):
    import cv2
    img = cv2.cvtColor(gray_u8, cv2.COLOR_GRAY2BGR)
    img = cv2.resize(img, (INPUT_W * up, INPUT_H * up),
                     interpolation=cv2.INTER_NEAREST)
    # ground-truth centre cells as faint markers
    for r, c in np.argwhere(tgt[..., 5] > 0.5):
        w = tgt[r, c, 1] * INPUT_W
        h = tgt[r, c, 2] * INPUT_H
        cx = (c + tgt[r, c, 3]) * STRIDE
        cy = (r + tgt[r, c, 4]) * STRIDE
        x0 = int((cx - w / 2) * up); y0 = int((cy - h / 2) * up)
        x1 = int((cx + w / 2) * up); y1 = int((cy + h / 2) * up)
        cv2.rectangle(img, (x0, y0), (x1, y1), (60, 60, 60), 1)
    # predictions: green best, yellow rest
    for j, (x, y, w, h, sc) in enumerate(boxes):
        col = (0, 220, 0) if j == 0 else (0, 210, 210)
        cv2.rectangle(img, (x * up, y * up), ((x + w) * up, (y + h) * up), col, 2)
        cv2.putText(img, str(sc), (x * up, max(10, y * up - 3)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, col, 1, cv2.LINE_AA)
    return img


def _save_montage(tiles, path, cols=4):
    import cv2
    th, tw = tiles[0].shape[:2]
    rows = (len(tiles) + cols - 1) // cols
    canvas = np.full((rows * (th + 6) + 6, cols * (tw + 6) + 6, 3), 30, np.uint8)
    for i, t in enumerate(tiles):
        r, c = divmod(i, cols)
        y = 6 + r * (th + 6); x = 6 + c * (tw + 6)
        canvas[y:y + th, x:x + tw] = t
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    cv2.imwrite(path, canvas)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--npz", default=NPZ)
    ap.add_argument("--cache", default="")
    ap.add_argument("--image", default="")
    ap.add_argument("--num", type=int, default=12)
    ap.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD)
    ap.add_argument("--preview", default="",
                    help="save an annotated montage PNG of the evaluated crops")
    args = ap.parse_args()

    layers = load_model(args.npz)
    print(f"[sim] loaded {len(layers)} layers from {os.path.basename(args.npz)}")

    if args.image:
        import cv2
        g = cv2.imread(args.image, cv2.IMREAD_GRAYSCALE)
        g = cv2.resize(g, (INPUT_W, INPUT_H), interpolation=cv2.INTER_AREA)
        heads = infer(layers, g)
        peak, boxes, _ = decode(heads, args.threshold)
        print(f"[sim] {os.path.basename(args.image)}  peak={peak}  boxes={boxes}")
        return

    if not args.cache:
        # default to the full-run cache if present
        cands = [f for f in os.listdir(os.path.join(HERE, "runs"))
                 if f.startswith("cache_") and f.endswith(".npz")]
        if not cands:
            print("no --cache / --image given and no cache found"); return
        args.cache = os.path.join(HERE, "runs", sorted(cands)[-1])

    d = np.load(args.cache)
    X, Y = d["Xva"], d["Yva"]
    n = min(args.num, X.shape[0])
    print(f"[sim] evaluating {n} val crops from {os.path.basename(args.cache)}")

    hit = 0
    peaks = []
    face_peaks = []
    tiles = []
    for i in range(n):
        g = X[i]
        tgt = Y[i]
        n_faces = int(tgt[..., 5].sum())
        heads = infer(layers, g)
        peak, boxes, score = decode(heads, args.threshold)
        peaks.append(peak)
        # is the strongest predicted cell near a true centre cell?
        gt_cells = np.argwhere(tgt[..., 5] > 0.5)
        pk_cell = np.unravel_index(int(np.argmax(score)), score.shape)
        near = any(abs(pk_cell[0] - r) <= 1 and abs(pk_cell[1] - c) <= 1
                   for r, c in gt_cells)
        if n_faces > 0:
            face_peaks.append(peak)
            if near and peak >= args.threshold:
                hit += 1
        tag = "OK " if (near and peak >= args.threshold) else "   "
        print(f"  [{i:2d}] faces={n_faces} peak={peak:3d} "
              f"pk_cell={pk_cell} boxes={len(boxes)} {tag}")
        if args.preview:
            tiles.append(_annotate(g, boxes, tgt))

    if args.preview and tiles:
        _save_montage(tiles, args.preview)
        print(f"[sim] preview -> {args.preview}")

    if face_peaks:
        print(f"[sim] faces: mean peak={np.mean(face_peaks):.0f} "
              f"max={max(face_peaks)}  center-hit {hit}/{len(face_peaks)}")
    print(f"[sim] all crops: peak min/mean/max = "
          f"{min(peaks)}/{np.mean(peaks):.0f}/{max(peaks)}")


if __name__ == "__main__":
    main()
