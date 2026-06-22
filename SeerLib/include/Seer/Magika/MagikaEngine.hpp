#pragma once

#include <Seer/Engine.hpp>
#include <Seer/Magika/MagikaModel.hpp>

#include <expected>
#include <memory>
#include <string>

namespace Seer
{

class MagikaEngine : public Engine
{
public:
    /// Create engine by loading model weights from file.
    static std::expected<std::unique_ptr<MagikaEngine>, MagikaModelError>
    create(std::string const& modelPath);

protected:
    InferenceResult classify(std::span<std::byte const> payload) override;

private:
    explicit MagikaEngine(MagikaModel model);
    MagikaModel model_;
};

} // namespace Seer
