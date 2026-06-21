#!/usr/bin/env python3
"""Extract Magika ONNX weights into a compact binary format for C++ inference.

Usage:
    python3 tools/extract_magika_weights.py models/model.onnx models/config.min.json models/magika.weights

Binary format:
    [4 bytes] magic: "DSMG"
    [4 bytes] version: uint32 = 1
    [4 bytes] num_tensors: uint32
    For each tensor:
        [4 bytes] name_len: uint32
        [name_len bytes] name (UTF-8, no null terminator)
        [4 bytes] num_dims: uint32
        [num_dims * 4 bytes] shape (uint32 each)
        [product(shape) * 4 bytes] data (float32, little-endian)
    [4 bytes] num_labels: uint32
    For each label:
        [4 bytes] label_len: uint32
        [label_len bytes] label (UTF-8)
"""

import json
import struct
import sys

import numpy as np
import onnx
from onnx import numpy_helper


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} model.onnx config.json output.weights")
        sys.exit(1)

    onnx_path, config_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    model = onnx.load(onnx_path)
    with open(config_path) as f:
        config = json.load(f)

    # Weight tensors we need (short name -> ONNX initializer name)
    tensor_map = {
        "embed_weight":  "jax2tf_get_logits_/Const:0",                                          # (257, 64)
        "embed_bias":    "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/Dense_0/Reshape:0",       # (1, 1, 64)
        "ln0_scale":     "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/LayerNorm_0/Reshape_2:0", # (1, 512, 1)
        "ln0_bias":      "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/LayerNorm_0/Reshape_3:0", # (1, 512, 1)
        "conv_weight":   "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/Conv_0/transpose_3:0",    # (512, 256, 5, 1)
        "conv_bias":     "const_fold_opt__209",                                                   # (1, 512, 1)
        "ln1_scale":     "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/LayerNorm_1/Reshape_2:0", # (1, 512)
        "ln1_bias":      "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/LayerNorm_1/Reshape_3:0", # (1, 512)
        "output_weight": "jax2tf_get_logits_/Const_24:0",                                        # (512, 214)
        "output_bias":   "jax2tf_get_logits_/pjit_get_logits_/MagikaV2/Dense_1/Reshape:0",       # (1, 214)
    }

    # Build lookup
    init_lookup = {}
    for init in model.graph.initializer:
        init_lookup[init.name] = numpy_helper.to_array(init)

    # Flatten and prepare tensors
    tensors = []
    for short_name, onnx_name in tensor_map.items():
        arr = init_lookup[onnx_name].astype(np.float32).flatten()
        # Store with squeezed shape for simplicity
        orig = init_lookup[onnx_name]
        shape = [d for d in orig.shape if d > 1]  # squeeze singleton dims
        if len(shape) == 0:
            shape = [1]
        print(f"  {short_name}: {orig.shape} -> squeezed {shape}, {arr.size} floats")
        tensors.append((short_name, shape, arr))

    # Labels from config
    labels = config["target_labels_space"]
    print(f"  Labels: {len(labels)} classes")

    # Write binary
    with open(out_path, "wb") as f:
        # Header
        f.write(b"DSMG")
        f.write(struct.pack("<I", 1))  # version
        f.write(struct.pack("<I", len(tensors)))

        # Tensors
        for name, shape, data in tensors:
            name_bytes = name.encode("utf-8")
            f.write(struct.pack("<I", len(name_bytes)))
            f.write(name_bytes)
            f.write(struct.pack("<I", len(shape)))
            for d in shape:
                f.write(struct.pack("<I", d))
            f.write(data.tobytes())

        # Labels
        f.write(struct.pack("<I", len(labels)))
        for label in labels:
            label_bytes = label.encode("utf-8")
            f.write(struct.pack("<I", len(label_bytes)))
            f.write(label_bytes)

    file_size = sum(4 + len(n.encode()) + 4 + len(s)*4 + d.nbytes for n, s, d in tensors)
    file_size += 12  # header
    file_size += 4 + sum(4 + len(l.encode()) for l in labels)
    print(f"\nWrote {out_path} ({file_size} bytes, {file_size/1024:.1f} KB)")


if __name__ == "__main__":
    main()
