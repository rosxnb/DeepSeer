#include <DeepSeer/Server/Server.hpp>
#include <DeepSeer/Log/Logger.hpp>

namespace DeepSeer
{

Server::Server(Address listenAddr, std::string caCertPath, std::string caKeyPath)
    : listenAddr_{std::move(listenAddr)}
    , caCertPath_{std::move(caCertPath)}
    , caKeyPath_{std::move(caKeyPath)}
{ }

void
Server::setPayloadInspector(PayloadInspector inspector)
{
    payloadInspector_ = std::move(inspector);
}

VoidResult
Server::run()
{
    loop_ = EventLoop::create();

    // Load CA certificate if path provided
    if (!caCertPath_.empty() && !caKeyPath_.empty()) {
        auto result = CertGenerator::create(caCertPath_, caKeyPath_);
        if (!result)
            return std::unexpected(result.error());

        certGen_.emplace(std::move(*result));
        Logger::info("TLS MITM enabled (CA: {})", caCertPath_);
    } else {
        Logger::info("TLS MITM disabled (no cert/key provided)", caCertPath_);
    }

    listener_ = std::make_unique<Listener>(*loop_, listenAddr_);

    auto result = listener_->start(
        [this](Socket client, Address addr) { onNewConnection(std::move(client), std::move(addr)); });

    if (!result)
        return result;

    Logger::info("MITM proxy server running on {}", listenAddr_.toString());
    loop_->run();

    // Cleanup after loop exits (signal received)
    listener_->stop();
    for (auto& session : sessions_)
        session->close();
    sessions_.clear();

    Logger::info("Server stopped");
    return {};
}

void
Server::stop()
{
    // stop() may be called from a signal handler, so do only signal-safe work
    // (atomic store + pipe write). Cleanup happens after run() returns.
    if (loop_)
        loop_->stop();
}

void
Server::onNewConnection(Socket client, [[maybe_unused]] Address addr)
{
    // Clean up closed sessions
    std::erase_if(sessions_, [](const auto& s) { return !s; });

    CertGenerator* genPtr = certGen_.has_value() ? &*certGen_ : nullptr;
    auto session = std::make_shared<ProxySession>(std::move(client), *loop_, genPtr, &certCache_);
    if (payloadInspector_)
        session->setPayloadInspector(payloadInspector_);
    sessions_.push_back(session);
    session->start();
}

} // DeepSeer
