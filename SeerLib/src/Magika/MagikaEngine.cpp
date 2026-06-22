#include <Seer/Magika/MagikaEngine.hpp>

namespace Seer
{

MagikaEngine::MagikaEngine(MagikaModel model)
    : model_{std::move(model)}
{ }

std::expected<std::unique_ptr<MagikaEngine>, MagikaModelError>
MagikaEngine::create(std::string const& modelPath)
{
    auto modelResult = MagikaModel::loadFromFile(modelPath);
    if (!modelResult)
        return std::unexpected(modelResult.error());

    // Can't use make_unique — constructor is private
    return std::unique_ptr<MagikaEngine>(
        new MagikaEngine(std::move(*modelResult)));
}

InferenceResult
MagikaEngine::classify(std::span<std::byte const> payload)
{
    auto pred = model_.predict(payload);
    return InferenceResult{
        .label      = std::move(pred.label),
        .confidence = pred.confidence,
        .modelName  = "magika-v1",
    };
}

} // namespace Seer
