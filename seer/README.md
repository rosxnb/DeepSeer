# Seer: AI Inference Module for DeepSeer

Seer is the AI inference library that powers content-type classification in the DeepSeer MITM proxy. It takes raw response bytes captured by the proxy and classifies them into 214 content types (HTML, JSON, PDF, JPEG, Python, etc.) using Google's Magika model -- reimplemented entirely in C++ with no external ML framework dependency.

## Architecture Overview

```
DeepSeer (proxy)                          Seer (AI)
┌─────────────────────┐                  ┌──────────────────────┐
│  ProxySession       │                  │  Engine (abstract)   │
│    onResponseBody() │──accumulate──>   │    submit()          │
│    onResponseComplete()──callback──>   │    ┌─────────────┐   │
│                     │                  │    │ worker thread│   │
│  Server             │                  │    │  classify()  │   │
│    PayloadInspector │                  │    └─────────────┘   │
└─────────────────────┘                  │                      │
        ▲                                │  MagikaEngine        │
        │ wired in main.cpp              │    MagikaModel       │
        │ #ifdef DEEPSEER_HAS_SEER       │      predict()       │
        └────────────────────────────────└──────────────────────┘
```

**Key design principle:** The proxy (`DeepSeerLib`) has zero compile-time dependency on Seer. The bridge is a `std::function` callback (`PayloadInspector`) defined using only stdlib types. Only `main.cpp` includes both `DeepSeer/` and `Seer/` headers, gated behind `#ifdef DEEPSEER_HAS_SEER`.

### Directory Structure

```
seer/
├── include/Seer/
│   ├── Result.hpp              # InferenceResult struct
│   ├── Engine.hpp              # Abstract engine with worker thread
│   └── Magika/
│       ├── MagikaEngine.hpp    # Concrete engine for Magika
│       └── MagikaModel.hpp     # Weight loading + forward pass
├── src/
│   ├── Engine.cpp              # Worker thread (mutex + condvar queue)
│   └── Magika/
│       ├── MagikaEngine.cpp    # Model creation, classify() impl
│       └── MagikaModel.cpp     # Preprocessing, all forward pass layers
├── test/
│   ├── TestEngine.cpp          # Async engine tests (submit, drain, shutdown)
│   └── TestMagikaModel.cpp     # End-to-end classification tests
├── CMakeLists.txt              # Produces libSeer.a
└── README.md                   # This file
```

### CMake Integration

Seer produces `libSeer.a` as a static library with its own include path. It is gated by the `DEEPSEER_BUILD_SEER` CMake option (default: ON).

```cmake
# Root CMakeLists.txt
option(DEEPSEER_BUILD_SEER "Build Seer AI inference library" ON)
if(DEEPSEER_BUILD_SEER)
    add_subdirectory(seer)
    target_link_libraries(DeepSeer PRIVATE Seer)
    target_compile_definitions(DeepSeer PRIVATE DEEPSEER_HAS_SEER)
endif()
```

To build without Seer (proxy-only):

```bash
cmake --preset debug -DDEEPSEER_BUILD_SEER=OFF
```

---

## Setup

### 1. Create Python environment for weight extraction

The ONNX model from Google cannot be loaded directly in C++. We extract the weights into a compact binary format using a Python script. This only needs to be done once.

```bash
cd tools
uv venv --python 3.12 .venv
source .venv/bin/activate
uv pip install onnx numpy
```

### 2. Download the Magika model

```bash
# From project root
mkdir -p models

# ONNX model (~3 MB)
curl -sL -o models/model.onnx \
  "https://raw.githubusercontent.com/google/magika/main/assets/models/standard_v3_3/model.onnx"

# Model config (label list, preprocessing params)
curl -sL -o models/config.min.json \
  "https://raw.githubusercontent.com/google/magika/main/assets/models/standard_v3_3/config.min.json"
```

### 3. Extract weights

```bash
source tools/.venv/bin/activate
python3 tools/extract_magika_weights.py \
  models/model.onnx \
  models/config.min.json \
  models/magika.weights
```

This produces `models/magika.weights` (~3 MB), a compact binary file containing all 10 weight tensors and 214 class labels.

### 4. Build and test

```bash
cmake --preset debug
cmake --build --preset debug

# Run Seer tests
./build/debug/bin/TestEngine
./build/debug/bin/TestMagikaModel
```

### 5. Run the proxy with AI classification

```bash
# Plain HTTP
./build/debug/bin/DeepSeer --model models/magika.weights

# In another terminal:
curl -v http://example.com --proxy "127.0.0.1:8080"
# Proxy logs: [AI] / -> html (0.99) [1306 ms / 1.307 s]
```

With TLS MITM (classification works on decrypted HTTPS traffic):

```bash
./build/debug/bin/DeepSeer \
  --ca-cert ca.crt --ca-key ca.key \
  --model models/magika.weights

# In another terminal:
curl -v https://httpbin.org/get --proxy "127.0.0.1:8080"
# Proxy logs: [AI] /get -> json (0.99) [1306 ms / 1.307 s]
```

---

## The Magika Model

### What is Magika?

[Magika](https://github.com/google/magika) is Google's file-type classifier, used in production at Gmail and Google Drive to classify hundreds of billions of files per week. It identifies 214 content types from raw bytes alone -- no file extensions, no MIME headers, no protocol-specific parsing.

We chose Magika because:

1. **Small model** (~785K parameters, ~3 MB weights) -- feasible to reimplement from scratch
2. **Simple architecture** -- dense layers, one convolution, no attention or RNNs
3. **Production-proven** -- 99%+ F1 across 200+ content types
4. **Apache 2.0 license** -- freely usable

### Model Architecture (MagikaV2, standard_v3_3)

```
Input: 2048 int32 tokens
  ├── tokens[0..1023]:    first 1024 bytes of file (left-stripped whitespace, right-padded with 256)
  └── tokens[1024..2047]: last 1024 bytes of file  (right-stripped whitespace, left-padded with 256)
  Token values: 0-255 = byte values, 256 = padding token

  ┌───────────────────────────────────────────────────────────────────┐
  │  Embedding: lookup table [257 vocab × 64 dim] + bias[64]        │
  │  Output: [2048 × 64]                                            │
  ├───────────────────────────────────────────────────────────────────┤
  │  GELU activation (element-wise)                                 │
  ├───────────────────────────────────────────────────────────────────┤
  │  Reshape: [2048 × 64] → [512 × 256]                            │
  │  (groups of 4 positions × 64 dims → 256-dim vectors)            │
  ├───────────────────────────────────────────────────────────────────┤
  │  LayerNorm_0: normalize over axis 0 (512 positions)             │
  │  scale[512], bias[512] — per-position, broadcast across 256 dim │
  ├───────────────────────────────────────────────────────────────────┤
  │  Conv1D: 256 input channels → 512 output channels, kernel=5    │
  │  Output: [508 × 512]  (512 - 5 + 1 = 508 positions)            │
  ├───────────────────────────────────────────────────────────────────┤
  │  GELU activation (element-wise)                                 │
  ├───────────────────────────────────────────────────────────────────┤
  │  Global Max Pool: max over 508 positions → [512]                │
  ├───────────────────────────────────────────────────────────────────┤
  │  LayerNorm_1: standard normalize over 512 dims                  │
  │  scale[512], bias[512] — per-element                            │
  ├───────────────────────────────────────────────────────────────────┤
  │  Dense: [512] → [214] (matmul + bias)                           │
  ├───────────────────────────────────────────────────────────────────┤
  │  Softmax → 214 class probabilities                              │
  └───────────────────────────────────────────────────────────────────┘
```

### Weight Tensors

| Name | Shape | Parameters | Role |
|------|-------|------------|------|
| `embed_weight` | 257 x 64 | 16,448 | Byte embedding lookup table |
| `embed_bias` | 64 | 64 | Embedding bias |
| `ln0_scale` | 512 | 512 | LayerNorm_0 scale (per-position) |
| `ln0_bias` | 512 | 512 | LayerNorm_0 bias (per-position) |
| `conv_weight` | 512 x 256 x 5 | 655,360 | Conv1D kernel (83.5% of all params) |
| `conv_bias` | 512 | 512 | Conv1D bias |
| `ln1_scale` | 512 | 512 | LayerNorm_1 scale |
| `ln1_bias` | 512 | 512 | LayerNorm_1 bias |
| `output_weight` | 512 x 214 | 109,568 | Output dense layer |
| `output_bias` | 214 | 214 | Output dense bias |
| **Total** | | **784,214** | **~3 MB as float32** |

---

## Implementation Details

### Weight Extraction (`tools/extract_magika_weights.py`)

Google distributes Magika as an ONNX model. ONNX Runtime has a C++ API, but linking it adds a ~20 MB dependency. Since the model is small and the architecture is straightforward, we extract the raw weight tensors from the ONNX file and load them directly.

The extraction script:
1. Loads the ONNX model using the `onnx` Python library
2. Extracts 10 named weight tensors from the graph's initializers via `onnx.numpy_helper.to_array()`
3. Extracts the 214 class labels from `config.min.json`
4. Writes everything into a compact binary format:

```
Binary format (DSMG v1):
  [4 bytes] magic: "DSMG"
  [4 bytes] version: uint32_le = 1
  [4 bytes] num_tensors: uint32_le
  For each tensor:
    [4 bytes] name_len: uint32_le
    [name_len bytes] name (UTF-8)
    [4 bytes] num_dims: uint32_le
    [num_dims × 4 bytes] shape (uint32_le each)
    [product(shape) × 4 bytes] data (float32_le)
  [4 bytes] num_labels: uint32_le
  For each label:
    [4 bytes] label_len: uint32_le
    [label_len bytes] label (UTF-8)
```

The ONNX tensor names are long JAX-generated strings like `jax2tf_get_logits_/pjit_get_logits_/MagikaV2/Conv_0/transpose_3:0`. The script maps them to short names (`conv_weight`, `embed_bias`, etc.) and squeezes singleton dimensions (e.g., `(1, 512, 1)` becomes `(512,)`).

### Input Preprocessing

The preprocessing mirrors Magika's Python implementation:

1. **Beginning region**: Left-strip ASCII whitespace (`\t`, `\n`, `\v`, `\f`, `\r`, space) from the input bytes. Take the first 1024 bytes. If shorter, right-pad with token 256.

2. **End region**: Right-strip whitespace from the input bytes. Take the last 1024 bytes. If shorter, left-pad with token 256.

3. **Concatenate**: `tokens[0..1023] = beginning`, `tokens[1024..2047] = end`. Total: 2048 int32 values in range [0, 256].

### Forward Pass

Each layer is implemented as a standalone method in `MagikaModel`:

- **`embedding()`**: Token lookup. `output[i] = embedWeight[token[i]] + embedBias`. No matrix multiply needed -- one-hot × matrix is just a row lookup.

- **`gelu()`**: `GELU(x) = 0.5x(1 + tanh(sqrt(2/pi)(x + 0.044715x^3)))`. Applied element-wise.

- **`layerNorm()`**: Normalizes over the sequence dimension (axis 0), not the feature dimension. Per-feature mean/variance, per-position scale/bias. See [the LayerNorm bug](#the-layernorm-bug) below.

- **`conv1d()`**: Standard 1D convolution. 256 input channels, 512 output channels, kernel size 5. This is the most expensive layer (~334M multiply-adds) and dominates inference time.

- **`globalMaxPool()`**: Takes the maximum value across all sequence positions for each channel. `output[c] = max(input[pos][c] for all pos)`.

- **`dense()`**: `output[c] = sum(input[i] * weight[i][c]) + bias[c]`.

- **`softmax()`**: Numerically stable softmax with max subtraction.

### Threading Model

The `Engine` base class owns a `std::jthread` worker:

```
Event loop thread                    Inference thread
     │                                    │
     │── submit(payload, url) ──────────> │
     │   (mutex + condvar enqueue)        │
     │                                    │── classify(payload)
     │                                    │── invoke ResultCallback
     │                                    │
```

- `submit()` is non-blocking and safe to call from the event loop thread
- The worker thread pops items from a `std::deque` protected by `std::mutex` + `std::condition_variable`
- `shutdown()` drains the queue before joining the thread
- The model is immutable after loading, so `predict()` requires no synchronization

---

## Debugging: How the Forward Pass Was Validated

The initial implementation produced wrong results -- every input was classified as "wav" with ~12% confidence. Here is the process used to find and fix the bug.

### Step 1: Validate the reference model

First, confirm the ONNX model itself works correctly using ONNX Runtime in Python. This establishes ground truth.

```bash
source tools/.venv/bin/activate
uv pip install onnxruntime
```

```python
import numpy as np
import onnxruntime as ort
import json

with open("models/config.min.json") as f:
    config = json.load(f)
labels = config["target_labels_space"]

sess = ort.InferenceSession("models/model.onnx")

def preprocess(text_bytes):
    ws = {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x20}
    data = list(text_bytes)

    beg_start = 0
    while beg_start < len(data) and data[beg_start] in ws:
        beg_start += 1
    beg = data[beg_start:beg_start+1024]
    beg = beg + [256] * (1024 - len(beg))

    end_pos = len(data)
    while end_pos > 0 and data[end_pos-1] in ws:
        end_pos -= 1
    end_data = data[:end_pos]
    end = end_data[-1024:] if len(end_data) >= 1024 else end_data
    end = [256] * (1024 - len(end)) + end

    return np.array([beg + end], dtype=np.int32)

tests = [
    ("html",   b'<!DOCTYPE html><html><head><title>Test</title></head><body>Hello</body></html>'),
    ("json",   b'{"name": "test", "version": 1, "items": [1, 2, 3]}'),
    ("python", b'#!/usr/bin/env python3\nimport sys\ndef main():\n    print("hello")\n'),
    ("shell",  b'#!/bin/bash\nset -euo pipefail\necho "Hello"\n'),
    ("cpp",    b'#include <iostream>\nint main() { std::cout << "hi"; return 0; }\n'),
]

for expected, data in tests:
    inp = preprocess(data)
    out = sess.run(None, {"bytes": inp})[0][0]
    idx = np.argmax(out)
    print(f"  {expected:10s} -> {labels[idx]:10s} ({out[idx]:.4f})")
```

Output:

```
  html       -> html       (0.9947)
  json       -> json       (0.8423)
  python     -> python     (0.9969)
  shell      -> shell      (0.9997)
  cpp        -> cpp        (0.9457)
```

This confirmed: the model works, the preprocessing is correct, and the weights are good. The bug is in the C++ forward pass.

### Step 2: Trace the ONNX graph

The ONNX model is a graph of operations. To understand exactly what the model computes, we dump every node:

```python
import onnx
model = onnx.load("models/model.onnx")
for i, node in enumerate(model.graph.node):
    print(f"[{i}] {node.op_type}: {list(node.input)} -> {list(node.output)}")
    for attr in node.attribute:
        if attr.type == 7:  # INTS
            print(f"      {attr.name} = {list(attr.ints)}")
```

This produced 95 nodes. The critical finding came from examining the LayerNorm_0 implementation (nodes 23-45).

### Step 3: Inspect the constants

```python
from onnx import numpy_helper
init = {i.name: numpy_helper.to_array(i) for i in model.graph.initializer}

# What axis does ReduceSum use?
print(init['const_fold_opt__171'])  # [1]  <-- axis 1, NOT axis -1 or 2

# What is the normalization divisor?
print(init['ConstantFolding/.../LayerNorm_1/truediv_recip:0'])  # 0.001953125 = 1/512
```

### The LayerNorm Bug

The standard LayerNorm normalizes over the **feature dimension** (last axis). For data shaped `[512, 256]`, that would mean normalizing each of the 512 rows across their 256 features.

But Magika's LayerNorm_0 normalizes over **axis 1** (the sequence/position dimension). For data `[batch, 512, 256]`:

- **Mean and variance** are computed per-feature across all 512 positions: `mean[d] = (1/512) * sum(data[s, d] for s in 0..511)`
- **Scale and bias** are per-position `[512]`, broadcast across the 256 features: `output[s, d] = (data[s, d] - mean[d]) / sqrt(var[d] + eps) * scale[s] + bias[s]`

This is more like Instance Normalization than standard LayerNorm. The shared constant `1/512` (used for both LN0 and LN1) was the clue -- it would be `1/256` if normalizing over the feature dimension.

The fix:

```cpp
// WRONG: normalize each row across columns (standard LayerNorm)
for (uint32_t s = 0; s < seqLen; ++s) {
    float mean = sum(row) / dim;
    // ...
}

// CORRECT: normalize each column across rows (axis 0)
for (uint32_t d = 0; d < dim; ++d) {
    float mean = sum(data[s][d] for all s) / seqLen;
    // ...
}
```

After this fix, all test cases matched the ONNX Runtime reference output.

### Lesson

When reimplementing a model from ONNX, don't assume standard semantics for operations like LayerNorm. Trace the actual graph operations and verify the reduction axes and constant values. The ONNX graph is the ground truth -- not the paper, not the architecture name.

---

## Test Suite

### Engine tests (`TestEngine`)

| Test | What it verifies |
|------|-----------------|
| `SubmitAndReceiveResult` | Async submit delivers correct result via callback |
| `MultipleSubmissions` | 10 submissions all complete |
| `ShutdownDrainsQueue` | Pending work completes before shutdown returns |

### Model tests (`TestMagikaModel`)

| Test | What it verifies |
|------|-----------------|
| `LoadWeights` | Binary weight file parses correctly, 214 classes loaded |
| `ClassifyHtml` | HTML content classified as "html" with >30% confidence |
| `ClassifyJson` | JSON classified as "json" |
| `ClassifyPython` | Python script classified as "python" |
| `ClassifyXml` | XML document classified as "xml" |
| `ClassifyShell` | Bash script classified as "shell" |
| `ClassifyCpp` | C++ source classified as "cpp" |
| `BadPathReturnsError` | Missing weight file returns error, no crash |

Run tests:

```bash
./build/debug/bin/TestEngine
./build/debug/bin/TestMagikaModel   # run from project root (needs models/)
```

---

## Performance

Inference timing (debug build, single-threaded, Intel Mac):

| Stage | Time |
|-------|------|
| Weight loading | ~15 ms |
| Per-prediction | ~1.3 s |

The Conv1D layer dominates (~95% of inference time): 512 output channels x 256 input channels x 5 kernel positions x 508 sequence positions = ~334M multiply-adds.

In a release build with `-O2`, expect 10-50x improvement from compiler auto-vectorization and loop optimizations. Further speedups possible via:

- **Loop reordering** in conv1d to improve cache locality
- **SIMD intrinsics** (SSE/AVX on x86, NEON on ARM)
- **Quantization** (INT8 weights, reducing memory bandwidth)

For the proxy use case, inference runs on a dedicated worker thread and does not block the event loop. At ~1.3s per classification in debug, it can handle ~0.8 responses/second, which is sufficient for development and demo purposes.

---

## Extending: Adding New Models

The `Engine` abstract class provides a clean extension point. To add a new model (e.g., NetMamba for traffic classification):

1. Create `seer/include/Seer/NetMamba/NetMambaEngine.hpp` and implement `classify()`
2. Add the source files to `seer/CMakeLists.txt`
3. Register it in `main.cpp` alongside or instead of MagikaEngine

The `Engine` interface:

```cpp
class Engine {
public:
    void submit(std::span<std::byte const> payload,
                std::string url,
                ResultCallback onResult = {});
    void shutdown();

protected:
    // Subclass implements this -- called on the worker thread
    virtual InferenceResult classify(std::span<std::byte const> payload) = 0;
};
```

The threading, queuing, and lifecycle management are handled by the base class. A new model only needs to implement the synchronous `classify()` method.
