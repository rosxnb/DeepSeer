#include <Seer/Magika/MagikaModel.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace Seer
{

// ---------------------------------------------------------------------------
// Weight loading
// ---------------------------------------------------------------------------

namespace
{

template <typename T>
bool readVal(std::ifstream& f, T& val)
{
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&val), sizeof(T)));
}

bool readString(std::ifstream& f, std::string& out)
{
    uint32_t len = 0;
    if (!readVal(f, len))
        return false;
    out.resize(len);
    return static_cast<bool>(f.read(out.data(), len));
}

bool readFloats(std::ifstream& f, std::vector<float>& out, size_t count)
{
    out.resize(count);
    return static_cast<bool>(
        f.read(reinterpret_cast<char*>(out.data()),
               static_cast<std::streamsize>(count * sizeof(float))));
}

} // namespace

std::expected<MagikaModel, MagikaModelError>
MagikaModel::loadFromFile(std::string const& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::unexpected(MagikaModelError{"Cannot open model file: " + path});

    // Magic
    char magic[4];
    file.read(magic, 4);
    if (std::memcmp(magic, "DSMG", 4) != 0)
        return std::unexpected(MagikaModelError{"Invalid magic in model file"});

    // Version
    uint32_t version = 0;
    readVal(file, version);
    if (version != 1)
        return std::unexpected(MagikaModelError{"Unsupported model version: " + std::to_string(version)});

    // Tensors
    uint32_t numTensors = 0;
    readVal(file, numTensors);

    // Map of name -> (shape, data)
    struct TensorData {
        std::vector<uint32_t> shape;
        std::vector<float> data;
    };
    std::unordered_map<std::string, TensorData> tensors;

    for (uint32_t i = 0; i < numTensors; ++i) {
        std::string name;
        if (!readString(file, name))
            return std::unexpected(MagikaModelError{"Failed reading tensor name"});

        uint32_t numDims = 0;
        readVal(file, numDims);

        TensorData td;
        td.shape.resize(numDims);
        for (uint32_t d = 0; d < numDims; ++d)
            readVal(file, td.shape[d]);

        size_t count = 1;
        for (auto d : td.shape)
            count *= d;

        if (!readFloats(file, td.data, count))
            return std::unexpected(MagikaModelError{"Failed reading tensor data for: " + name});

        tensors[name] = std::move(td);
    }

    // Labels
    uint32_t numLabels = 0;
    readVal(file, numLabels);

    std::vector<std::string> labels(numLabels);
    for (uint32_t i = 0; i < numLabels; ++i) {
        if (!readString(file, labels[i]))
            return std::unexpected(MagikaModelError{"Failed reading label"});
    }

    // Validate required tensors exist
    auto get = [&](std::string const& name) -> std::vector<float>* {
        auto it = tensors.find(name);
        return it != tensors.end() ? &it->second.data : nullptr;
    };

    auto* ew = get("embed_weight");
    auto* eb = get("embed_bias");
    auto* l0s = get("ln0_scale");
    auto* l0b = get("ln0_bias");
    auto* cw = get("conv_weight");
    auto* cb = get("conv_bias");
    auto* l1s = get("ln1_scale");
    auto* l1b = get("ln1_bias");
    auto* ow = get("output_weight");
    auto* ob = get("output_bias");

    if (!ew || !eb || !l0s || !l0b || !cw || !cb || !l1s || !l1b || !ow || !ob)
        return std::unexpected(MagikaModelError{"Missing required tensors in model file"});

    MagikaModel model;
    model.embedWeight_  = std::move(*ew);
    model.embedBias_    = std::move(*eb);
    model.ln0Scale_     = std::move(*l0s);
    model.ln0Bias_      = std::move(*l0b);
    model.convWeight_   = std::move(*cw);
    model.convBias_     = std::move(*cb);
    model.ln1Scale_     = std::move(*l1s);
    model.ln1Bias_      = std::move(*l1b);
    model.outputWeight_ = std::move(*ow);
    model.outputBias_   = std::move(*ob);
    model.numClasses_   = numLabels;
    model.labels_       = std::move(labels);

    return model;
}

// ---------------------------------------------------------------------------
// Preprocessing: extract input tokens from raw bytes
// ---------------------------------------------------------------------------

std::vector<uint32_t>
MagikaModel::extractInputTokens(std::span<std::byte const> payload) const
{
    // Whitespace bytes to strip (ASCII whitespace + 0x0b)
    auto isWhitespace = [](std::byte b) -> bool {
        auto v = static_cast<uint8_t>(b);
        return v == 0x09 || v == 0x0a || v == 0x0b || v == 0x0c || v == 0x0d || v == 0x20;
    };

    std::vector<uint32_t> tokens(kInputLen, kPaddingToken);

    if (payload.empty())
        return tokens;

    // Beginning region: lstrip whitespace, take first kBegSize bytes
    size_t begStart = 0;
    while (begStart < payload.size() && isWhitespace(payload[begStart]))
        ++begStart;

    size_t begAvail = payload.size() - begStart;
    size_t begCount = std::min(static_cast<size_t>(kBegSize), begAvail);
    for (size_t i = 0; i < begCount; ++i)
        tokens[i] = static_cast<uint32_t>(static_cast<uint8_t>(payload[begStart + i]));

    // End region: rstrip whitespace, take last kEndSize bytes
    size_t endPos = payload.size();
    while (endPos > 0 && isWhitespace(payload[endPos - 1]))
        --endPos;

    size_t endAvail = endPos;
    size_t endCount = std::min(static_cast<size_t>(kEndSize), endAvail);

    // End tokens are right-aligned in the second half, padded at the beginning
    size_t endOffset = kBegSize + (kEndSize - endCount);
    size_t endStart = endPos - endCount;
    for (size_t i = 0; i < endCount; ++i)
        tokens[endOffset + i] = static_cast<uint32_t>(static_cast<uint8_t>(payload[endStart + i]));

    return tokens;
}

// ---------------------------------------------------------------------------
// Forward pass
// ---------------------------------------------------------------------------

void
MagikaModel::embedding(std::vector<uint32_t> const& tokens, std::vector<float>& out) const
{
    // tokens: [kInputLen] with values in [0, 256]
    // embedWeight_: [kVocabSize × kEmbedDim] = [257 × 64]
    // out: [kInputLen × kEmbedDim] = [2048 × 64]
    out.resize(kInputLen * kEmbedDim);
    for (uint32_t i = 0; i < kInputLen; ++i) {
        auto tok = tokens[i];
        for (uint32_t d = 0; d < kEmbedDim; ++d) {
            out[i * kEmbedDim + d] = embedWeight_[tok * kEmbedDim + d] + embedBias_[d];
        }
    }
}

void
MagikaModel::gelu(float* data, size_t n)
{
    constexpr float kSqrt2OverPi = 0.7978845608f;
    for (size_t i = 0; i < n; ++i) {
        float x = data[i];
        float inner = kSqrt2OverPi * (x + 0.044715f * x * x * x);
        data[i] = 0.5f * x * (1.0f + std::tanh(inner));
    }
}

void
MagikaModel::layerNorm(float* data, float const* scale, float const* bias,
                        uint32_t seqLen, uint32_t dim)
{
    // LN0: data [seqLen=512, dim=256]. Normalizes over axis 0 (seqLen).
    //   mean/var computed per feature (dim) across all sequence positions (seqLen).
    //   scale/bias are per-position [seqLen], broadcast across features.
    //   output[s,d] = (data[s,d] - mean[d]) * scale[s] / sqrt(var[d] + eps) + bias[s]

    float invSeq = 1.0f / static_cast<float>(seqLen);

    // Compute per-feature mean and variance across sequence positions
    std::vector<float> mean(dim, 0.0f);
    std::vector<float> var(dim, 0.0f);

    for (uint32_t s = 0; s < seqLen; ++s) {
        for (uint32_t d = 0; d < dim; ++d) {
            float v = data[s * dim + d];
            mean[d] += v;
            var[d] += v * v;
        }
    }

    // var = E[x^2] - E[x]^2, clamped to >= 0
    std::vector<float> invStd(dim);
    for (uint32_t d = 0; d < dim; ++d) {
        mean[d] *= invSeq;
        var[d] = std::max(0.0f, var[d] * invSeq - mean[d] * mean[d]);
        invStd[d] = 1.0f / std::sqrt(var[d] + kLnEps);
    }

    // Normalize and apply per-position scale/bias
    for (uint32_t s = 0; s < seqLen; ++s) {
        for (uint32_t d = 0; d < dim; ++d) {
            data[s * dim + d] = (data[s * dim + d] - mean[d]) * invStd[d] * scale[s] + bias[s];
        }
    }
}

void
MagikaModel::conv1d(float const* input, float* output, uint32_t seqLen) const
{
    // Conv1D: input [seqLen × kReshapedDim], weight [kConvOut × kReshapedDim × kKernelSize]
    // Output seq length: seqLen - kKernelSize + 1
    uint32_t outLen = seqLen - kKernelSize + 1;

    // For each output channel
    for (uint32_t oc = 0; oc < kConvOut; ++oc) {
        for (uint32_t pos = 0; pos < outLen; ++pos) {
            float sum = convBias_[oc];

            // Convolve across input channels and kernel positions
            for (uint32_t ic = 0; ic < kReshapedDim; ++ic) {
                for (uint32_t k = 0; k < kKernelSize; ++k) {
                    // input: [seqLen × kReshapedDim], access input[(pos+k), ic]
                    // weight: [kConvOut × kReshapedDim × kKernelSize], access weight[oc, ic, k]
                    sum += input[(pos + k) * kReshapedDim + ic]
                         * convWeight_[(oc * kReshapedDim + ic) * kKernelSize + k];
                }
            }

            // output: [kConvOut × outLen], stored as [outLen × kConvOut] for max-pool
            output[pos * kConvOut + oc] = sum;
        }
    }
}

void
MagikaModel::globalMaxPool(float const* input, float* output, uint32_t seqLen) const
{
    // input: [seqLen × kConvOut], output: [kConvOut]
    for (uint32_t c = 0; c < kConvOut; ++c)
        output[c] = -std::numeric_limits<float>::infinity();

    for (uint32_t s = 0; s < seqLen; ++s) {
        for (uint32_t c = 0; c < kConvOut; ++c) {
            output[c] = std::max(output[c], input[s * kConvOut + c]);
        }
    }
}

void
MagikaModel::dense(float const* input, float* output) const
{
    // output = input × outputWeight^T + outputBias
    // outputWeight_: [kConvOut × numClasses_] — but stored as (512, 214)
    // We need output[c] = sum_i(input[i] * weight[i, c]) + bias[c]
    for (uint32_t c = 0; c < numClasses_; ++c) {
        float sum = outputBias_[c];
        for (uint32_t i = 0; i < kConvOut; ++i) {
            sum += input[i] * outputWeight_[i * numClasses_ + c];
        }
        output[c] = sum;
    }
}

void
MagikaModel::softmax(float* data, uint32_t n)
{
    float maxVal = *std::max_element(data, data + n);
    float sum = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        data[i] = std::exp(data[i] - maxVal);
        sum += data[i];
    }
    for (uint32_t i = 0; i < n; ++i)
        data[i] /= sum;
}

MagikaModel::Prediction
MagikaModel::predict(std::span<std::byte const> payload) const
{
    // 1. Extract input tokens
    auto tokens = extractInputTokens(payload);

    // 2. Embedding + GELU → [2048 × 64]
    std::vector<float> embedded;
    embedding(tokens, embedded);
    gelu(embedded.data(), embedded.size());

    // 3. Reshape [2048 × 64] → [512 × 256]
    //    (data stays the same, just reinterpret dimensions)
    //    512 positions, each with 256 features

    // 4. LayerNorm0 — normalize each of 512 channels across 256 features
    layerNorm(embedded.data(), ln0Scale_.data(), ln0Bias_.data(),
              kReshapedSeq, kReshapedDim);

    // 5. Conv1D → [outLen × 512]
    uint32_t convOutLen = kReshapedSeq - kKernelSize + 1; // 508
    std::vector<float> convOut(convOutLen * kConvOut);
    conv1d(embedded.data(), convOut.data(), kReshapedSeq);

    // 6. GELU on conv output
    gelu(convOut.data(), convOut.size());

    // 7. Global max pool → [512]
    std::vector<float> pooled(kConvOut);
    globalMaxPool(convOut.data(), pooled.data(), convOutLen);

    // 8. LayerNorm1 — normalize the 512-dim vector
    //    Here seqLen=1, dim=512, scale/bias are per-element
    //    We need standard LN: normalize across 512 dims
    {
        float mean = 0.0f;
        for (uint32_t i = 0; i < kConvOut; ++i)
            mean += pooled[i];
        mean /= static_cast<float>(kConvOut);

        float var = 0.0f;
        for (uint32_t i = 0; i < kConvOut; ++i) {
            float diff = pooled[i] - mean;
            var += diff * diff;
        }
        var /= static_cast<float>(kConvOut);

        float invStd = 1.0f / std::sqrt(var + kLnEps);
        for (uint32_t i = 0; i < kConvOut; ++i) {
            pooled[i] = (pooled[i] - mean) * invStd * ln1Scale_[i] + ln1Bias_[i];
        }
    }

    // 9. Dense → [214]
    std::vector<float> logits(numClasses_);
    dense(pooled.data(), logits.data());

    // 10. Softmax
    softmax(logits.data(), numClasses_);

    // 11. Argmax
    auto maxIt = std::max_element(logits.begin(), logits.end());
    auto idx = static_cast<uint32_t>(std::distance(logits.begin(), maxIt));

    return Prediction{
        .classIndex = idx,
        .confidence = *maxIt,
        .label      = idx < labels_.size() ? labels_[idx] : "unknown",
    };
}

} // namespace Seer
