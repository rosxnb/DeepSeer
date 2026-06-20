#include <DeepSeer/Server/Server.hpp>
#include <DeepSeer/Log/Logger.hpp>

namespace DeepSeer
{

Server::Server(Address listenAddr)
    : listenAddr_{std::move(listenAddr)}
{ }

VoidResult
Server::run()
{
    loop_ = EventLoop::create();
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

    auto session = std::make_shared<ProxySession>(std::move(client), *loop_);
    sessions_.push_back(session);
    session->start();
}

} // DeepSeer
