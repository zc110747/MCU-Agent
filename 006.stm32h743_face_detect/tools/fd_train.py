"""
Train the STM32H743 CenterNet-style face detector on WIDER FACE.

The whole point of this script is to produce two artefacts that tools/fd_export.py
turns into the int8 C model:

    tools/runs/fd_float.weights.h5   float weights, exactly build_keras() layout
    tools/runs/calib.npy             ~256 grey images in the network input domain

Everything is pinned to what the MCU actually computes so nothing drifts:

  * Input domain.  On the MCU the tensor is (luma - 128) with scale 1/255,
    zero point -128, i.e. real = luma/255 in [0,1].  So we train on grayscale
    images normalised to [0,1].

  * Targets.  fd_infer.c decodes a peak at grid cell (row,col) as

        cx = (col + off_x) * STRIDE
        cy = (row + off_y) * STRIDE
        w  = wh_x * INPUT_W
        h  = wh_y * INPUT_H

    so the supervision is, at the centre cell of every face:

        heatmap  : Gaussian bump, peak 1.0 at the centre cell (focal loss)
        off      : (cx/STRIDE - col, cy/STRIDE - row)   in [0,1)   (L1)
        wh       : (w/INPUT_W, h/INPUT_H)                            (L1)

  * Heads are linear.  head_hm outputs logits (sigmoid lives in the loss and on
    the MCU); head_wh / head_off output the regression values directly.

Data pipeline (CPU friendly): we make a one-off crop cache so per-epoch cost is
the tiny model, not JPEG decode.  Geometric augmentation (face-centric random
crop + h-flip) is baked into the cache; photometric augmentation (brightness /
contrast / gamma / noise) is applied online and does not touch the targets.

Usage
    # quick end-to-end smoke test
    python tools/fd_train.py --limit-images 300 --epochs 2 --crops-per-image 1

    # full run, then export straight away
    python tools/fd_train.py --epochs 80 --export
"""
from __future__ import annotations

import argparse
import os
import sys
import time

import numpy as np

os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
os.environ.setdefault("TF_ENABLE_ONEDNN_OPTS", "1")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fd_arch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DATASET = os.path.join(HERE, "dataset")
RUNS = os.path.join(HERE, "runs")

INPUT_W = fd_arch.INPUT_W          # 96
INPUT_H = fd_arch.INPUT_H          # 96
STRIDE = fd_arch.STRIDE            # 8
GRID_W = fd_arch.GRID_W            # 12
GRID_H = fd_arch.GRID_H            # 12
NUM_TGT_CH = 6                     # hm(1) + wh(2) + off(2) + mask(1)


# ==========================================================================
# WIDER FACE annotation parsing
# ==========================================================================
def parse_wider(gt_path: str, img_root: str, limit: int | None = None):
    """Return [(abs_image_path, np.float32 boxes[N,4] xywh), ...].

    Only faces with the WIDER 'invalid' flag clear are kept.  Images that end up
    with no usable face are dropped.
    """
    samples = []
    with open(gt_path, "r", encoding="utf-8") as f:
        lines = [ln.rstrip("\n") for ln in f]

    i, n = 0, len(lines)
    while i < n:
        rel = lines[i].strip()
        i += 1
        if not rel:
            continue
        if i >= n:
            break
        try:
            cnt = int(lines[i].strip())
        except ValueError:
            # some entries with 0 faces still carry a placeholder box line
            cnt = 0
        i += 1
        boxes = []
        # WIDER writes a single "0 0 0 0 ..." line even when count == 0
        rows = cnt if cnt > 0 else 1
        for _ in range(rows):
            if i >= n:
                break
            parts = lines[i].split()
            i += 1
            if len(parts) < 4:
                continue
            x, y, w, h = (float(parts[0]), float(parts[1]),
                          float(parts[2]), float(parts[3]))
            invalid = int(parts[7]) if len(parts) >= 8 else 0
            if cnt == 0:
                continue
            if invalid or w < 1 or h < 1:
                continue
            boxes.append((x, y, w, h))
        if not boxes:
            continue
        samples.append((os.path.join(img_root, rel.replace("/", os.sep)),
                        np.asarray(boxes, dtype=np.float32)))
        if limit and len(samples) >= limit:
            break
    return samples


# ==========================================================================
# Crop cache construction
# ==========================================================================
def _clip_boxes_to_crop(boxes, cx0, cy0, side, out_size, min_px):
    """Map absolute xywh boxes into an out_size crop and keep the usable ones."""
    scale = out_size / float(side)
    kept = []
    for (x, y, w, h) in boxes:
        nx = (x - cx0) * scale
        ny = (y - cy0) * scale
        nw = w * scale
        nh = h * scale
        cxc = nx + nw / 2.0
        cyc = ny + nh / 2.0
        if cxc < 0 or cxc >= out_size or cyc < 0 or cyc >= out_size:
            continue                        # centre fell outside the crop
        # clip extent to the crop frame
        x0 = max(0.0, nx)
        y0 = max(0.0, ny)
        x1 = min(float(out_size), nx + nw)
        y1 = min(float(out_size), ny + nh)
        cw = x1 - x0
        ch = y1 - y0
        if cw < min_px or ch < min_px:
            continue
        kept.append((x0, y0, cw, ch))
    return kept


def _make_crops(img_gray, boxes, rng, crops_per_image, min_px):
    """Yield (96x96 uint8 grey, list[xywh]) crops for one image."""
    import cv2

    H, W = img_gray.shape[:2]
    out = []

    for _ in range(crops_per_image):
        mode = rng.random()
        if mode < 0.15 or len(boxes) == 0:
            # whole-image letterbox into a square, then resize
            side = max(H, W)
            cy0 = (H - side) / 2.0
            cx0 = (W - side) / 2.0
            canvas = np.zeros((side, side), np.uint8)
            y_off = int(round(-cy0)) if cy0 < 0 else 0
            x_off = int(round(-cx0)) if cx0 < 0 else 0
            canvas[y_off:y_off + H, x_off:x_off + W] = img_gray
            crop = canvas
            eff_cx0, eff_cy0, eff_side = cx0, cy0, float(side)
        else:
            # face-centric crop: pick a face, size the window so it fills
            # ~[frac_lo, frac_hi] of the crop, jitter the centre a little
            bi = rng.integers(0, len(boxes))
            x, y, w, h = boxes[bi]
            fdim = max(w, h)
            frac = rng.uniform(0.22, 0.55)
            side = fdim / frac
            side = float(np.clip(side, fdim * 1.1, max(H, W) * 1.3))
            fcx = x + w / 2.0
            fcy = y + h / 2.0
            jit = side * 0.18
            cx = fcx + rng.uniform(-jit, jit)
            cy = fcy + rng.uniform(-jit, jit)
            eff_cx0 = cx - side / 2.0
            eff_cy0 = cy - side / 2.0
            eff_side = side
            # sample the (possibly out-of-bounds) window with border padding
            m = int(np.ceil(side))
            src_x0 = int(np.floor(eff_cx0))
            src_y0 = int(np.floor(eff_cy0))
            pad_l = max(0, -src_x0)
            pad_t = max(0, -src_y0)
            pad_r = max(0, src_x0 + m - W)
            pad_b = max(0, src_y0 + m - H)
            padded = cv2.copyMakeBorder(img_gray, pad_t, pad_b, pad_l, pad_r,
                                        cv2.BORDER_REFLECT_101)
            sx = src_x0 + pad_l
            sy = src_y0 + pad_t
            crop = padded[sy:sy + m, sx:sx + m]
            eff_cx0 = float(src_x0)
            eff_cy0 = float(src_y0)
            eff_side = float(m)

        if crop.shape[0] < 2 or crop.shape[1] < 2:
            continue
        crop96 = cv2.resize(crop, (INPUT_W, INPUT_H), interpolation=cv2.INTER_AREA)

        kept = _clip_boxes_to_crop(boxes, eff_cx0, eff_cy0, eff_side,
                                   INPUT_W, min_px)
        if not kept and mode >= 0.15 and len(boxes) > 0:
            # face-centric crop that lost its face (bad jitter) - skip
            continue

        # h-flip half the time (baked, so flip the boxes too)
        if rng.random() < 0.5:
            crop96 = crop96[:, ::-1].copy()
            kept = [(INPUT_W - (x0 + w0), y0, w0, h0) for (x0, y0, w0, h0) in kept]

        out.append((crop96, kept))
    return out


def build_cache(samples, rng, crops_per_image, min_px, tag):
    """Decode every image once, emit crops + CenterNet targets."""
    import cv2

    imgs = []
    tgts = []
    t0 = time.time()
    n = len(samples)
    for si, (path, boxes) in enumerate(samples):
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is None:
            continue
        for crop96, kept in _make_crops(img, boxes, rng, crops_per_image, min_px):
            imgs.append(crop96)
            tgts.append(make_target(kept))
        if (si + 1) % 1000 == 0 or si + 1 == n:
            dt = time.time() - t0
            print(f"[cache:{tag}] {si + 1}/{n} images -> {len(imgs)} crops "
                  f"({dt:.0f}s)", flush=True)

    X = np.asarray(imgs, dtype=np.uint8)
    Y = np.asarray(tgts, dtype=np.float32)
    return X, Y


# ==========================================================================
# CenterNet target encoding
# ==========================================================================
def gaussian_radius(box_h, box_w, min_overlap=0.7):
    """CenterNet radius that guarantees >= min_overlap IoU for a shifted box."""
    a1 = 1.0
    b1 = box_h + box_w
    c1 = box_w * box_h * (1 - min_overlap) / (1 + min_overlap)
    r1 = (b1 - np.sqrt(max(b1 * b1 - 4 * a1 * c1, 0.0))) / (2 * a1)

    a2 = 4.0
    b2 = 2 * (box_h + box_w)
    c2 = (1 - min_overlap) * box_w * box_h
    r2 = (b2 - np.sqrt(max(b2 * b2 - 4 * a2 * c2, 0.0))) / (2 * a2)

    a3 = 4 * min_overlap
    b3 = -2 * min_overlap * (box_h + box_w)
    c3 = (min_overlap - 1) * box_w * box_h
    r3 = (b3 + np.sqrt(max(b3 * b3 - 4 * a3 * c3, 0.0))) / (2 * a3)
    return max(0.0, min(r1, r2, r3))


def draw_gaussian(hm, cx, cy, radius):
    """Splat a 2D Gaussian (max-merged) onto a single-channel heatmap grid."""
    radius = int(max(1, round(radius)))
    sigma = radius / 3.0
    diameter = 2 * radius + 1
    ax = np.arange(-radius, radius + 1)
    gx, gy = np.meshgrid(ax, ax)
    g = np.exp(-(gx * gx + gy * gy) / (2 * sigma * sigma + 1e-6))

    left, right = min(cx, radius), min(GRID_W - cx, radius + 1)
    top, bottom = min(cy, radius), min(GRID_H - cy, radius + 1)
    if right <= -left or bottom <= -top:
        return
    masked = hm[cy - top:cy + bottom, cx - left:cx + right]
    gsub = g[radius - top:radius + bottom, radius - left:radius + right]
    np.maximum(masked, gsub, out=masked)


def make_target(boxes):
    """boxes: list of xywh in 96x96 -> (GRID_H, GRID_W, 6) target tensor."""
    hm = np.zeros((GRID_H, GRID_W), np.float32)
    wh = np.zeros((GRID_H, GRID_W, 2), np.float32)
    off = np.zeros((GRID_H, GRID_W, 2), np.float32)
    mask = np.zeros((GRID_H, GRID_W), np.float32)

    for (x, y, w, h) in boxes:
        cx = x + w / 2.0
        cy = y + h / 2.0
        fx = cx / STRIDE
        fy = cy / STRIDE
        col = int(np.clip(int(fx), 0, GRID_W - 1))
        row = int(np.clip(int(fy), 0, GRID_H - 1))

        r = gaussian_radius(h / STRIDE, w / STRIDE)
        draw_gaussian(hm, col, row, r)

        hm[row, col] = 1.0
        wh[row, col] = (w / INPUT_W, h / INPUT_H)
        off[row, col] = (fx - col, fy - row)
        mask[row, col] = 1.0

    return np.concatenate([hm[..., None], wh, off, mask[..., None]], axis=-1)


# ==========================================================================
# Losses  (custom Keras 3 loss over the 5-channel concat head output)
# ==========================================================================
def make_loss(wh_weight, off_weight):
    import tensorflow as tf

    def loss_fn(y_true, y_pred):
        # y_true : (B,12,12,6)  [hm, wh(2), off(2), mask]
        # y_pred : (B,12,12,5)  [hm_logit, wh(2), off(2)]
        hm_t = y_true[..., 0:1]
        wh_t = y_true[..., 1:3]
        off_t = y_true[..., 3:5]
        mask = y_true[..., 5:6]

        hm_logit = y_pred[..., 0:1]
        wh_p = y_pred[..., 1:3]
        off_p = y_pred[..., 3:5]

        # --- CenterNet Gaussian focal loss on the heatmap ---
        p = tf.sigmoid(hm_logit)
        p = tf.clip_by_value(p, 1e-6, 1.0 - 1e-6)
        pos = tf.cast(tf.equal(hm_t, 1.0), tf.float32)
        neg = 1.0 - pos
        neg_w = tf.pow(1.0 - hm_t, 4.0)
        pos_loss = tf.math.log(p) * tf.pow(1.0 - p, 2.0) * pos
        neg_loss = tf.math.log(1.0 - p) * tf.pow(p, 2.0) * neg_w * neg
        n_pos = tf.maximum(tf.reduce_sum(pos, axis=[1, 2, 3]), 1.0)
        hm_loss = -(tf.reduce_sum(pos_loss + neg_loss, axis=[1, 2, 3]) / n_pos)

        # --- masked L1 for regression heads ---
        n_reg = tf.maximum(tf.reduce_sum(mask, axis=[1, 2, 3]), 1.0)
        wh_l1 = tf.reduce_sum(tf.abs(wh_p - wh_t) * mask, axis=[1, 2, 3]) / n_reg
        off_l1 = tf.reduce_sum(tf.abs(off_p - off_t) * mask, axis=[1, 2, 3]) / n_reg

        return hm_loss + wh_weight * wh_l1 + off_weight * off_l1

    return loss_fn


# ==========================================================================
# Online photometric augmentation (targets unchanged)
# ==========================================================================
def photometric(x_uint8, rng):
    """x_uint8: (B,96,96) -> (B,96,96,1) float32 in [0,1] with jitter."""
    x = x_uint8.astype(np.float32) / 255.0
    B = x.shape[0]
    gain = rng.uniform(0.75, 1.3, size=(B, 1, 1)).astype(np.float32)
    bias = rng.uniform(-0.12, 0.12, size=(B, 1, 1)).astype(np.float32)
    x = np.clip(x * gain + bias, 0.0, 1.0)
    gamma = rng.uniform(0.7, 1.4, size=(B, 1, 1)).astype(np.float32)
    x = np.clip(np.power(x, gamma), 0.0, 1.0)
    if rng.random() < 0.5:
        x = x + rng.normal(0, 0.03, size=x.shape).astype(np.float32)
        x = np.clip(x, 0.0, 1.0)
    return x[..., None]


class CropSequence:
    """Minimal generator feeding photometrically-augmented batches."""

    def __init__(self, X, Y, batch, rng, augment):
        self.X, self.Y = X, Y
        self.batch = batch
        self.rng = rng
        self.augment = augment
        self.n = X.shape[0]

    def __call__(self):
        idx = np.arange(self.n)
        while True:
            self.rng.shuffle(idx)
            for i in range(0, self.n - self.batch + 1, self.batch):
                sel = idx[i:i + self.batch]
                xb = self.X[sel]
                if self.augment:
                    xb = photometric(xb, self.rng)
                else:
                    xb = (xb.astype(np.float32) / 255.0)[..., None]
                yield xb, self.Y[sel]


# ==========================================================================
# Training driver
# ==========================================================================
def build_train_model(base):
    from tensorflow.keras import layers, Model
    concat = layers.Concatenate(axis=-1, name="head_concat")(base.outputs)
    return Model(base.input, concat, name="fd_train")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--epochs", type=int, default=80)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--crops-per-image", type=int, default=2)
    ap.add_argument("--val-crops-per-image", type=int, default=1)
    ap.add_argument("--min-face-px", type=float, default=8.0)
    ap.add_argument("--limit-images", type=int, default=0,
                    help="cap #train images (0 = all) for a quick smoke test")
    ap.add_argument("--val-limit-images", type=int, default=1500)
    ap.add_argument("--wh-weight", type=float, default=0.1)
    ap.add_argument("--off-weight", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--rebuild-cache", action="store_true")
    ap.add_argument("--export", action="store_true",
                    help="run fd_export.py right after training")
    args = ap.parse_args()

    os.makedirs(RUNS, exist_ok=True)
    rng = np.random.default_rng(args.seed)

    tr_gt = os.path.join(DATASET, "wider_face_split", "wider_face_train_bbx_gt.txt")
    tr_root = os.path.join(DATASET, "WIDER_train", "images")
    va_gt = os.path.join(DATASET, "wider_face_split", "wider_face_val_bbx_gt.txt")
    va_root = os.path.join(DATASET, "WIDER_val", "images")

    tag = f"lim{args.limit_images}_c{args.crops_per_image}_m{int(args.min_face_px)}"
    cache_path = os.path.join(RUNS, f"cache_{tag}.npz")

    if os.path.exists(cache_path) and not args.rebuild_cache:
        print(f"[cache] loading {cache_path}")
        d = np.load(cache_path)
        Xtr, Ytr, Xva, Yva = d["Xtr"], d["Ytr"], d["Xva"], d["Yva"]
    else:
        print("[data] parsing annotations ...")
        tr = parse_wider(tr_gt, tr_root,
                         args.limit_images or None)
        va = parse_wider(va_gt, va_root, args.val_limit_images or None)
        print(f"[data] train images {len(tr)}, val images {len(va)}")

        Xtr, Ytr = build_cache(tr, rng, args.crops_per_image,
                               args.min_face_px, "train")
        Xva, Yva = build_cache(va, rng, args.val_crops_per_image,
                               args.min_face_px, "val")
        np.savez(cache_path, Xtr=Xtr, Ytr=Ytr, Xva=Xva, Yva=Yva)
        print(f"[cache] saved {cache_path}")

    print(f"[data] train crops {Xtr.shape}, val crops {Xva.shape}")
    pos_per = Ytr[..., 5].sum(axis=(1, 2)).mean()
    print(f"[data] avg faces / train crop = {pos_per:.2f}")

    import tensorflow as tf
    base = fd_arch.build_keras()
    model = build_train_model(base)
    steps = max(1, Xtr.shape[0] // args.batch)
    val_steps = max(1, Xva.shape[0] // args.batch)

    lr = tf.keras.optimizers.schedules.CosineDecay(
        args.lr, decay_steps=args.epochs * steps, alpha=0.05)
    model.compile(optimizer=tf.keras.optimizers.Adam(lr),
                  loss=make_loss(args.wh_weight, args.off_weight))
    base.summary(line_length=96)

    tr_seq = CropSequence(Xtr, Ytr, args.batch, rng, augment=True)
    va_seq = CropSequence(Xva, Yva, args.batch, rng, augment=False)

    sig = (tf.TensorSpec((None, INPUT_H, INPUT_W, 1), tf.float32),
           tf.TensorSpec((None, GRID_H, GRID_W, NUM_TGT_CH), tf.float32))
    tr_ds = tf.data.Dataset.from_generator(tr_seq, output_signature=sig).prefetch(4)
    va_ds = tf.data.Dataset.from_generator(va_seq, output_signature=sig).prefetch(2)

    wpath = os.path.join(RUNS, "fd_float.weights.h5")
    ckpt = tf.keras.callbacks.ModelCheckpoint(
        wpath, monitor="val_loss", save_best_only=True,
        save_weights_only=True, verbose=1)

    t0 = time.time()
    model.fit(tr_ds, steps_per_epoch=steps, epochs=args.epochs,
              validation_data=va_ds, validation_steps=val_steps,
              callbacks=[ckpt], verbose=2)
    print(f"[train] done in {(time.time() - t0) / 60:.1f} min")

    # ModelCheckpoint saved the best weights into the train model; reload them
    # into `base` (identical layers) so calib probing uses the best epoch, then
    # persist base weights in the exact build_keras() layout fd_export expects.
    model.load_weights(wpath)
    base.save_weights(wpath)
    print(f"[train] best weights -> {wpath}")

    # calibration set: raw val crops in the network input domain [0,1]
    calib = (Xva[:256].astype(np.float32) / 255.0)
    cpath = os.path.join(RUNS, "calib.npy")
    np.save(cpath, calib)
    print(f"[train] calib -> {cpath}  {calib.shape}")

    if args.export:
        import subprocess
        cmd = [sys.executable, os.path.join(HERE, "fd_export.py"),
               "--weights", wpath, "--calib", cpath]
        print("[export]", " ".join(cmd))
        subprocess.run(cmd, check=True)


if __name__ == "__main__":
    main()
