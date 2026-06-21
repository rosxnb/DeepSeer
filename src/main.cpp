#include <DeepSeer/Log/ConsoleSink.hpp>
#include <DeepSeer/Log/FileSink.hpp>
#include <DeepSeer/Log/Logger.hpp>
#include <DeepSeer/Server/Server.hpp>

#ifdef DEEPSEER_HAS_SEER
#include <Seer/Magika/MagikaEngine.hpp>
#endif

#include <csignal>

namespace
{
    
constexpr auto kVersion = "0.1.0";
DeepSeer::Server* gServer = nullptr;

void signalHandler(int)
{
    if (gServer)
        gServer->stop();
}

} // namespace



int main(
    [[maybe_unused]] int argc,
    [[maybe_unused]] char** argv)
{
    DeepSeer::FileSinkConfig logFileConfig { .path = std::string{"DeepSeerLogs/proxylog"} };
    auto fileSink = std::make_shared<DeepSeer::FileSink>(logFileConfig);
    auto consoleSink = std::make_shared<DeepSeer::ConsoleSink>();
    DeepSeer::Logger::init(DeepSeer::LogLevel::Debug, { fileSink, consoleSink });
    DeepSeer::Logger::info("DeepSeer version: {}", kVersion);

    uint16_t port = 8080;
    std::string caCert, caKey;
    std::string modelPath;

    for (int i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--version") return 0;
        if (arg == "--port" && i + 1 < argc)
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        if (arg == "--ca-cert" && i + 1 < argc)
            caCert = argv[++i];
        if (arg == "--ca-key" && i + 1 < argc)
            caKey = argv[++i];
        if (arg == "--model" && i + 1 < argc)
            modelPath = argv[++i];
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto addr = DeepSeer::Address::fromHostPort("0.0.0.0", port);
    if (!addr) {
        DeepSeer::Logger::error("Failed to resolve listen address: {}", addr.error().message);
        return 1;
    }
    DeepSeer::Server server{ *addr, caCert, caKey };
    gServer = &server;

#ifdef DEEPSEER_HAS_SEER
    std::unique_ptr<Seer::MagikaEngine> engine;
    if (!modelPath.empty()) {
        auto engineResult = Seer::MagikaEngine::create(modelPath);
        if (!engineResult) {
            DeepSeer::Logger::warn("Failed to load AI model: {}", engineResult.error().message);
        } else {
            engine = std::move(*engineResult);
            server.setPayloadInspector(
                [&engine](std::span<std::byte const> payload, std::string_view url) {
                    engine->submit(payload, std::string{url},
                        [url = std::string{url}](Seer::InferenceResult r) {
                            DeepSeer::Logger::info("[AI] {} -> {} ({:.2f})",
                                url, r.label, r.confidence);
                        });
                });
            DeepSeer::Logger::info("Seer AI engine loaded (model: {})", modelPath);
        }
    }
#endif

    auto result = server.run();
    if (!result) {
        DeepSeer::Logger::error("Error: {}", result.error().message);
        return 1;
    }

    DeepSeer::Logger::shutdown();
    return 0;
}
