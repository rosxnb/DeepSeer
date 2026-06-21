#pragma once

#include <string>

namespace Seer
{

struct InferenceResult
{
    std::string label;       // e.g., "image/png", "application/pdf"
    float       confidence;  // 0.0 – 1.0
    std::string modelName;   // e.g., "magika-v1"
};

} // namespace Seer
