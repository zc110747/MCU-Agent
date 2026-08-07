"""
Shared architecture definition for the STM32H743 face detector.

The network is a CenterNet-style anchor-free detector sized for CMSIS-NN on a
Cortex-M7.  It takes a 96x96 grayscale image and predicts, on a 12x12 grid
(stride 8):

    heatmap  12x12x1   face-centre confidence (logits, sigmoid applied on MCU)
    size     12x12x2   face width / height, normalised by 96
    offset   12x12x2   sub-cell centre offset in [0,1)

Everything downstream (training, post-training quantisation, C export and the
numpy reference simulator) is generated from LAYERS below, so the Keras graph
and the exported CMSIS-NN layer table can never drift apart.

Layer kinds
    'conv' : regular convolution, CMSIS-NN filter layout [out_c, k, k, in_c]
    'dw'   : depthwise convolution (channel multiplier 1),
             CMSIS-NN filter layout [1, k, k, out_c]

Only the backbone (layers 0..12) is chained through the ping-pong tensor
arena; the three heads all read the final backbone tensor.
"""

INPUT_W = 96
INPUT_H = 96
INPUT_C = 1

STRIDE = 8
GRID_W = INPUT_W // STRIDE      # 12
GRID_H = INPUT_H // STRIDE      # 12

# name, kind, kernel, stride, pad, out_channels
BACKBONE = [
    ("b0_conv",  "conv", 3, 2, 1, 16),   # 96x96x1  -> 48x48x16
    ("b1_dw",    "dw",   3, 2, 1, 16),   # 48x48x16 -> 24x24x16
    ("b2_pw",    "conv", 1, 1, 0, 48),   # 24x24x16 -> 24x24x48
    ("b3_dw",    "dw",   3, 1, 1, 48),   # 24x24x48 -> 24x24x48
    ("b4_pw",    "conv", 1, 1, 0, 48),   # 24x24x48 -> 24x24x48
    ("b5_dw",    "dw",   3, 2, 1, 48),   # 24x24x48 -> 12x12x48
    ("b6_pw",    "conv", 1, 1, 0, 96),   # 12x12x48 -> 12x12x96
    ("b7_dw",    "dw",   3, 1, 1, 96),   # 12x12x96 -> 12x12x96
    ("b8_pw",    "conv", 1, 1, 0, 96),   # 12x12x96 -> 12x12x96
    ("b9_dw",    "dw",   3, 1, 1, 96),   # 12x12x96 -> 12x12x96
    ("b10_pw",   "conv", 1, 1, 0, 96),   # 12x12x96 -> 12x12x96
    ("b11_dw",   "dw",   3, 1, 1, 96),   # 12x12x96 -> 12x12x96
    ("b12_pw",   "conv", 1, 1, 0, 96),   # 12x12x96 -> 12x12x96
]

# Heads: all 1x1 convolutions on the last backbone tensor.  They are linear
# (no ReLU) because they produce signed regression values and logits.
HEADS = [
    ("head_hm",  "conv", 1, 1, 0, 1),
    ("head_wh",  "conv", 1, 1, 0, 2),
    ("head_off", "conv", 1, 1, 0, 2),
]

RELU6_MAX = 6.0


def layer_shapes():
    """Return [(name, kind, k, s, pad, in_hwc, out_hwc, is_head), ...]."""
    out = []
    h, w, c = INPUT_H, INPUT_W, INPUT_C
    for name, kind, k, s, pad, oc in BACKBONE:
        oh = (h + 2 * pad - k) // s + 1
        ow = (w + 2 * pad - k) // s + 1
        out.append((name, kind, k, s, pad, (h, w, c), (oh, ow, oc), False))
        h, w, c = oh, ow, oc
    for name, kind, k, s, pad, oc in HEADS:
        out.append((name, kind, k, s, pad, (h, w, c), (h, w, oc), True))
    return out


def max_backbone_tensor():
    """Largest tensor (in bytes) that has to fit in one arena half."""
    biggest = INPUT_H * INPUT_W * INPUT_C
    for _, _, _, _, _, _, (oh, ow, oc), is_head in layer_shapes():
        if is_head:
            continue
        biggest = max(biggest, oh * ow * oc)
    return biggest


def build_keras():
    """Float training graph. Import TF lazily so the C export can run without it.

    IMPORTANT - padding: Keras 'same' with stride 2 pads asymmetrically (0 left,
    1 right) while CMSIS-NN always pads symmetrically.  Training with 'same' and
    deploying on CMSIS-NN would silently shift every feature map by one pixel.
    So every padded layer here is an explicit symmetric ZeroPadding2D followed
    by a 'valid' convolution, which is exactly what the MCU computes.
    """
    from tensorflow.keras import layers, Model

    inp = layers.Input(shape=(INPUT_H, INPUT_W, INPUT_C), name="image")
    x = inp
    for name, kind, k, s, pad, oc in BACKBONE:
        if pad:
            x = layers.ZeroPadding2D(padding=pad, name=name + "_pad")(x)
        if kind == "conv":
            x = layers.Conv2D(oc, k, strides=s, padding="valid",
                              use_bias=True, name=name)(x)
        else:
            x = layers.DepthwiseConv2D(k, strides=s, padding="valid",
                                       depth_multiplier=1, use_bias=True,
                                       name=name)(x)
        # ReLU6 keeps activation ranges bounded, which is what makes per-tensor
        # int8 quantisation of this network behave.
        x = layers.ReLU(max_value=RELU6_MAX, name=name + "_relu")(x)

    feat = x
    outs = {}
    for name, kind, k, s, pad, oc in HEADS:
        outs[name] = layers.Conv2D(oc, k, strides=s, padding="valid",
                                   use_bias=True, name=name)(feat)

    return Model(inp, [outs["head_hm"], outs["head_wh"], outs["head_off"]],
                 name="fd_net")


if __name__ == "__main__":
    print(f"input {INPUT_H}x{INPUT_W}x{INPUT_C} -> grid {GRID_H}x{GRID_W} (stride {STRIDE})")
    total_mac = 0
    total_w = 0
    for name, kind, k, s, pad, ihwc, ohwc, is_head in layer_shapes():
        oh, ow, oc = ohwc
        ic = ihwc[2]
        if kind == "conv":
            mac = oh * ow * oc * k * k * ic
            nw = oc * k * k * ic
        else:
            mac = oh * ow * oc * k * k
            nw = k * k * oc
        total_mac += mac
        total_w += nw
        print(f"  {name:9s} {kind:4s} k{k} s{s} p{pad}  "
              f"{ihwc[0]:3d}x{ihwc[1]:3d}x{ihwc[2]:3d} -> "
              f"{oh:3d}x{ow:3d}x{oc:3d}   w={nw:6d}  mac={mac:9d}")
    print(f"total weights {total_w}, total MAC {total_mac/1e6:.2f} M")
    print(f"max backbone tensor {max_backbone_tensor()} bytes")
