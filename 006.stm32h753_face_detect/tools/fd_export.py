"""
Post-training int8 quantisation + C export for the STM32H743 face detector.

Produces middleware/face_detect/fd_model_data.{c,h}: weight blobs, per-channel
requantisation multipliers and the layer table that fd_infer.c walks.

Quantisation scheme is exactly the TFLite / CMSIS-NN one:

    real = (q - zero_point) * scale

    acc[c]  = sum_k (q_in[k] + input_offset) * q_w[c][k] + q_bias[c]
    q_out[c]= requantize(acc[c], mult[c], shift[c]) + output_offset

    mult[c]/shift[c] encode  M[c] = s_in * s_w[c] / s_out

Weights are per-output-channel symmetric (zero point 0), activations are
per-tensor asymmetric with zero point -128 (all backbone tensors are ReLU6, so
they are non-negative and -128 maps to 0.0 exactly).

Usage
    python tools/fd_export.py --placeholder      # random weights, for a build/timing smoke test
    python tools/fd_export.py --weights runs/fd_float.weights.h5 --calib runs/calib.npy
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fd_arch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.normpath(os.path.join(HERE, "..", "middleware", "face_detect"))

# The input tensor is fixed: the MCU hands over luma 0..255 as (luma - 128).
INPUT_SCALE = 1.0 / 255.0
INPUT_ZP = -128


# --------------------------------------------------------------------------
# TFLite multiplier decomposition
# --------------------------------------------------------------------------
def quantize_multiplier(m: float) -> tuple[int, int]:
    """Split a real multiplier in (0,1) into (int32 multiplier, shift)."""
    if m == 0.0:
        return 0, 0
    mantissa, exponent = np.frexp(m)          # m = mantissa * 2**exponent, 0.5<=mantissa<1
    q = int(round(mantissa * (1 << 31)))
    if q == (1 << 31):
        q //= 2
        exponent += 1
    if exponent > 31:                          # saturate, should never happen
        q, exponent = (1 << 31) - 1, 31
    if exponent < -31:
        q, exponent = 0, 0
    return q, int(exponent)


def act_quant(vmin: float, vmax: float) -> tuple[float, int]:
    """Asymmetric int8 activation quantisation with zero point pinned to -128
    when the tensor is non-negative (all ReLU6 outputs)."""
    vmin = min(0.0, float(vmin))
    vmax = max(float(vmax), vmin + 1e-6)
    scale = (vmax - vmin) / 255.0
    zp = int(round(-128.0 - vmin / scale))
    zp = max(-128, min(127, zp))
    return scale, zp


# --------------------------------------------------------------------------
# Weight layout
# --------------------------------------------------------------------------
def conv_weights_to_cmsis(kernel: np.ndarray) -> np.ndarray:
    """Keras (kh, kw, in_c, out_c) -> CMSIS-NN [C_OUT, HK, WK, C_IN]."""
    return np.transpose(kernel, (3, 0, 1, 2))


def dw_weights_to_cmsis(kernel: np.ndarray) -> np.ndarray:
    """Keras (kh, kw, in_c, 1) -> CMSIS-NN [1, HK, WK, C_OUT]."""
    kh, kw, ic, cm = kernel.shape
    assert cm == 1, "only channel multiplier 1 is supported"
    return kernel.reshape(1, kh, kw, ic)


def per_channel_scales(kernel_cmsis: np.ndarray, kind: str) -> np.ndarray:
    """max|w| per output channel -> symmetric int8 scale."""
    if kind == "conv":
        flat = kernel_cmsis.reshape(kernel_cmsis.shape[0], -1)      # [out_c, ...]
    else:
        # [1, kh, kw, out_c] -> [out_c, kh*kw]
        flat = np.transpose(kernel_cmsis[0], (2, 0, 1)).reshape(kernel_cmsis.shape[3], -1)
    amax = np.max(np.abs(flat), axis=1)
    amax[amax == 0] = 1e-8
    return amax / 127.0


def quantize_kernel(kernel_cmsis: np.ndarray, scales: np.ndarray, kind: str) -> np.ndarray:
    if kind == "conv":
        s = scales.reshape(-1, 1, 1, 1)
    else:
        s = scales.reshape(1, 1, 1, -1)
    q = np.round(kernel_cmsis / s)
    return np.clip(q, -127, 127).astype(np.int8)


# --------------------------------------------------------------------------
# Build the quantised layer description
# --------------------------------------------------------------------------
def build_layers(float_params: dict, act_ranges: dict) -> list[dict]:
    """
    float_params : {layer_name: (kernel, bias)} in Keras layout
    act_ranges   : {layer_name: (min, max)} of the layer OUTPUT (post ReLU6 for
                   the backbone, raw logits/regressions for the heads)
    """
    shapes = fd_arch.layer_shapes()
    layers = []

    in_scale, in_zp = INPUT_SCALE, INPUT_ZP
    backbone_scale, backbone_zp = in_scale, in_zp   # running input quantisation

    for name, kind, k, s, pad, ihwc, ohwc, is_head in shapes:
        kernel, bias = float_params[name]
        kc = conv_weights_to_cmsis(kernel) if kind == "conv" else dw_weights_to_cmsis(kernel)
        w_scales = per_channel_scales(kc, kind)
        qw = quantize_kernel(kc, w_scales, kind)

        vmin, vmax = act_ranges[name]
        o_scale, o_zp = act_quant(vmin, vmax)

        # Heads all consume the LAST backbone tensor, not the previous head.
        i_scale = backbone_scale
        i_zp = backbone_zp

        eff = i_scale * w_scales / o_scale
        mult, shift = [], []
        for e in eff:
            m, sh = quantize_multiplier(float(e))
            mult.append(m)
            shift.append(sh)

        qbias = np.round(bias / (i_scale * w_scales)).astype(np.int64)
        qbias = np.clip(qbias, -(2 ** 31), 2 ** 31 - 1).astype(np.int32)

        layers.append(dict(
            name=name, kind=kind, k=k, stride=s, pad=pad,
            in_hwc=ihwc, out_hwc=ohwc, is_head=is_head,
            input_offset=-i_zp, output_offset=o_zp,
            act_min=-128, act_max=127,
            weights=qw.reshape(-1), bias=qbias,
            mult=np.array(mult, dtype=np.int64),
            shift=np.array(shift, dtype=np.int32),
            out_scale=o_scale, out_zp=o_zp,
        ))

        if not is_head:
            backbone_scale, backbone_zp = o_scale, o_zp

    return layers


# --------------------------------------------------------------------------
# C emission
# --------------------------------------------------------------------------
def _c_array(name: str, ctype: str, data, per_line: int, cast=None) -> str:
    vals = [str(cast(v) if cast else v) for v in data]
    lines = []
    for i in range(0, len(vals), per_line):
        lines.append("    " + ", ".join(vals[i:i + per_line]) + ",")
    body = "\n".join(lines)
    return f"static const {ctype} {name}[{len(vals)}] = {{\n{body}\n}};\n"


def emit_c(layers: list[dict], out_dir: str, note: str) -> None:
    os.makedirs(out_dir, exist_ok=True)

    arena_half = fd_arch.max_backbone_tensor()
    n_backbone = len(fd_arch.BACKBONE)
    n_heads = len(fd_arch.HEADS)

    heads = {l["name"]: l for l in layers if l["is_head"]}
    total_bytes = 0

    # ------------------------------------------------------------ header
    h = []
    h.append("/**\n * @file    fd_model_data.h\n"
             " * @brief   GENERATED by tools/fd_export.py - do not edit.\n"
             f" *\n * {note}\n */\n")
    h.append("#ifndef __FD_MODEL_DATA_H\n#define __FD_MODEL_DATA_H\n")
    h.append('\n#ifdef __cplusplus\nextern "C" {\n#endif\n')
    h.append('\n#include "fd_infer.h"\n')
    h.append(f"\n#define FD_NUM_BACKBONE   {n_backbone}u\n")
    h.append(f"#define FD_NUM_HEADS      {n_heads}u\n")
    h.append(f"#define FD_ARENA_HALF     {arena_half}\n")
    h.append("\n/* Head output dequantisation: real = (q - ZP) * SCALE */\n")
    for key, ln in (("HM", "head_hm"), ("WH", "head_wh"), ("OFF", "head_off")):
        h.append(f"#define FD_{key}_SCALE     {heads[ln]['out_scale']:.10g}f\n")
        h.append(f"#define FD_{key}_ZP        ({heads[ln]['out_zp']})\n")
    h.append("\nextern const fd_layer_t fd_backbone[FD_NUM_BACKBONE];\n")
    h.append("extern const fd_layer_t fd_heads[FD_NUM_HEADS];\n")
    h.append("\n#ifdef __cplusplus\n}\n#endif\n#endif /* __FD_MODEL_DATA_H */\n")

    with open(os.path.join(out_dir, "fd_model_data.h"), "w", encoding="utf-8") as f:
        f.write("".join(h))

    # ------------------------------------------------------------- source
    c = []
    c.append("/**\n * @file    fd_model_data.c\n"
             " * @brief   GENERATED by tools/fd_export.py - do not edit.\n"
             f" *\n * {note}\n */\n")
    c.append('#include "fd_model_data.h"\n\n')

    for l in layers:
        n = l["name"]
        c.append(_c_array(f"{n}_w", "int8_t", l["weights"], 24, cast=int))
        c.append(_c_array(f"{n}_b", "int32_t", l["bias"], 8, cast=int))
        c.append(_c_array(f"{n}_m", "int32_t", l["mult"], 6, cast=int))
        c.append(_c_array(f"{n}_s", "int32_t", l["shift"], 12, cast=int))
        c.append("\n")
        total_bytes += l["weights"].size + 12 * l["bias"].size

    def table(varname: str, sel) -> str:
        rows = []
        for l in layers:
            if not sel(l):
                continue
            ih, iw, ic = l["in_hwc"]
            oh, ow, oc = l["out_hwc"]
            kind = "FD_LAYER_CONV" if l["kind"] == "conv" else "FD_LAYER_DW"
            rows.append(
                f'    {{ "{l["name"]}", {kind}, {l["k"]}, {l["stride"]}, {l["pad"]},\n'
                f"      {ih}, {iw}, {ic}, {oh}, {ow}, {oc},\n"
                f'      {l["input_offset"]}, {l["output_offset"]}, '
                f'{l["act_min"]}, {l["act_max"]},\n'
                f'      {l["name"]}_w, {l["name"]}_b, {l["name"]}_m, {l["name"]}_s }},'
            )
        return f"const fd_layer_t {varname}[] = {{\n" + "\n".join(rows) + "\n};\n\n"

    c.append(table("fd_backbone", lambda l: not l["is_head"]))
    c.append(table("fd_heads", lambda l: l["is_head"]))

    with open(os.path.join(out_dir, "fd_model_data.c"), "w", encoding="utf-8") as f:
        f.write("".join(c))

    print(f"[export] {out_dir}/fd_model_data.c")
    print(f"[export] weights {sum(l['weights'].size for l in layers)} B, "
          f"total const ~{total_bytes/1024:.1f} KB, arena 2x{arena_half} B")


# --------------------------------------------------------------------------
# Sources of float parameters
# --------------------------------------------------------------------------
def placeholder_params() -> tuple[dict, dict]:
    """Random but sanely scaled weights so the firmware can be built and timed
    before the real training run finishes."""
    rng = np.random.default_rng(0)
    params, ranges = {}, {}
    for name, kind, k, s, pad, ihwc, ohwc, is_head in fd_arch.layer_shapes():
        ic, oc = ihwc[2], ohwc[2]
        if kind == "conv":
            fan_in = k * k * ic
            kernel = rng.normal(0.0, (2.0 / fan_in) ** 0.5, (k, k, ic, oc))
        else:
            fan_in = k * k
            kernel = rng.normal(0.0, (2.0 / fan_in) ** 0.5, (k, k, ic, 1))
        bias = np.zeros(oc)
        params[name] = (kernel.astype(np.float32), bias.astype(np.float32))
        if is_head:
            ranges[name] = (-4.0, 4.0) if name == "head_hm" else (-0.2, 1.2)
        else:
            ranges[name] = (0.0, fd_arch.RELU6_MAX)
    return params, ranges


def params_from_keras(weights_path: str, calib_path: str | None):
    """Load the trained float model and calibrate activation ranges."""
    import tensorflow as tf  # noqa: F401

    model = fd_arch.build_keras()
    model.load_weights(weights_path)

    params = {}
    for name, kind, k, s, pad, ihwc, ohwc, is_head in fd_arch.layer_shapes():
        layer = model.get_layer(name)
        w = layer.get_weights()
        params[name] = (np.asarray(w[0]), np.asarray(w[1]))

    if calib_path and os.path.exists(calib_path):
        calib = np.load(calib_path).astype(np.float32)
        if calib.ndim == 3:
            calib = calib[..., None]
        calib = calib[:256]
        print(f"[export] calibrating on {calib.shape[0]} images")
    else:
        print("[export] WARNING: no calibration set, assuming ReLU6 full range")
        calib = None

    ranges = {}
    if calib is None:
        for name, kind, k, s, pad, ihwc, ohwc, is_head in fd_arch.layer_shapes():
            if is_head:
                ranges[name] = (-6.0, 6.0) if name == "head_hm" else (-0.2, 1.2)
            else:
                ranges[name] = (0.0, fd_arch.RELU6_MAX)
        return params, ranges

    from tensorflow.keras import Model
    # Probe the ReLU output of each backbone layer and the raw head outputs.
    probe_names = []
    probe_tensors = []
    for name, kind, k, s, pad, ihwc, ohwc, is_head in fd_arch.layer_shapes():
        ln = name if is_head else name + "_relu"
        probe_names.append(name)
        probe_tensors.append(model.get_layer(ln).output)
    probe = Model(model.input, probe_tensors)

    mins = {n: np.inf for n in probe_names}
    maxs = {n: -np.inf for n in probe_names}
    B = 32
    for i in range(0, calib.shape[0], B):
        outs = probe.predict(calib[i:i + B], verbose=0)
        for n, o in zip(probe_names, outs):
            mins[n] = min(mins[n], float(np.min(o)))
            maxs[n] = max(maxs[n], float(np.max(o)))

    for n in probe_names:
        ranges[n] = (mins[n], maxs[n])
        print(f"  {n:9s} range [{mins[n]:8.3f}, {maxs[n]:8.3f}]")
    return params, ranges


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--placeholder", action="store_true",
                    help="emit random weights (build / timing smoke test)")
    ap.add_argument("--weights", default=os.path.join(HERE, "runs", "fd_float.weights.h5"))
    ap.add_argument("--calib", default=os.path.join(HERE, "runs", "calib.npy"))
    ap.add_argument("--out", default=OUT_DIR)
    args = ap.parse_args()

    if args.placeholder:
        params, ranges = placeholder_params()
        note = "PLACEHOLDER random weights - rebuild after training!"
    else:
        params, ranges = params_from_keras(args.weights, args.calib)
        note = f"quantised from {os.path.basename(args.weights)}"

    layers = build_layers(params, ranges)
    emit_c(layers, args.out, note)

    # Dump the quantised graph so fd_sim.py can cross-check the MCU numerically.
    np.savez(os.path.join(args.out, "fd_model_q.npz"),
             **{f"{l['name']}_{k}": l[k]
                for l in layers for k in ("weights", "bias", "mult", "shift")},
             **{f"{l['name']}_meta": np.array(
                 [l["input_offset"], l["output_offset"], l["out_zp"]], dtype=np.int32)
                for l in layers},
             **{f"{l['name']}_oscale": np.array([l["out_scale"]], dtype=np.float64)
                for l in layers})


if __name__ == "__main__":
    main()
