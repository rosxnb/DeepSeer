#pragma once

/// @file MagikaModel.hpp
/// @brief Magika model weight storage and forward pass.
///
/// Architecture (MagikaV2, standard_v3_3):
///   Input: 2048 ints (1024 beg + 1024 end), values 0–256 (256 = padding)
///   → Embedding(257 → 64) + GELU
///   → Reshape(2048×64 → 512×256)
///   → LayerNorm(256)
///   → Conv1D(in=256, out=512, kernel=5)  + GELU
///   → GlobalMaxPool
///   → LayerNorm(512)
///   → Dense(512 → 214) + Softmax
///   → 214 content-type probabilities
///
/// ~785K parameters, ~3 MB weights. Thread-safe after loading.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace Seer
{

struct MagikaModelError
{
    std::string message;
};

class MagikaModel
{
public:
    struct Prediction
    {
        uint32_t    classIndex;
        float       confidence;
        std::string label;
    };

    /// Load model weights from binary file (produced by extract_magika_weights.py).
    static std::expected<MagikaModel, MagikaModelError>
    loadFromFile(std::string const& path);

    /// Run forward pass on raw bytes. Thread-safe (model is immutable).
    Prediction predict(std::span<std::byte const> payload) const;

    uint32_t numClasses() const { return numClasses_; }

private:
    MagikaModel() = default;

    // --- Constants ---
    static constexpr uint32_t kBegSize     = 1024;
    static constexpr uint32_t kEndSize     = 1024;
    static constexpr uint32_t kInputLen    = kBegSize + kEndSize; // 2048
    static constexpr uint32_t kVocabSize   = 257;  // 0–255 = bytes, 256 = padding
    static constexpr uint32_t kPaddingToken = 256;
    static constexpr uint32_t kEmbedDim    = 64;
    static constexpr uint32_t kReshapedSeq = 512;  // 2048*64 / 256
    static constexpr uint32_t kReshapedDim = 256;  // 2048*64 / 512
    static constexpr uint32_t kConvOut     = 512;
    static constexpr uint32_t kKernelSize  = 5;
    static constexpr float    kLnEps       = 1e-6f;

    // --- Weights ---
    std::vector<float> embedWeight_;    // [257 × 64]
    std::vector<float> embedBias_;      // [64]

    std::vector<float> ln0Scale_;       // [512]
    std::vector<float> ln0Bias_;        // [512]

    std::vector<float> convWeight_;     // [512 × 256 × 5]  (out_ch, in_ch, kernel)
    std::vector<float> convBias_;       // [512]

    std::vector<float> ln1Scale_;       // [512]
    std::vector<float> ln1Bias_;        // [512]

    std::vector<float> outputWeight_;   // [512 × 214]
    std::vector<float> outputBias_;     // [214]

    uint32_t numClasses_{0};
    std::vector<std::string> labels_;

    // --- Preprocessing ---
    std::vector<uint32_t> extractInputTokens(std::span<std::byte const> payload) const;

    // --- Forward pass layers ---
    void embedding(std::vector<uint32_t> const& tokens, std::vector<float>& out) const;
    static void gelu(float* data, size_t n);
    static void layerNorm(float* data, float const* scale, float const* bias,
                          uint32_t seqLen, uint32_t dim);
    void conv1d(float const* input, float* output, uint32_t seqLen) const;
    void globalMaxPool(float const* input, float* output, uint32_t seqLen) const;
    void dense(float const* input, float* output) const;
    static void softmax(float* data, uint32_t n);
};

} // namespace Seer
