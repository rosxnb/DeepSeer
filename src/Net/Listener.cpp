#include <DeepSeer/Net/Listener.hpp>
#include <DeepSeer/Log/Logger.hpp>

using namespace std::chrono_literals;

namespace DeepSeer
{

Listener::Listener(EventLoop& loop, Address bindAddr)
    : loop_{loop}
    , bindAddr_{std::move(bindAddr)}
{ }

Listener::~Listener()
{
    stop();
}

VoidResult
Listener::start(AcceptCallback onAccept)
{
    onAccept_ = std::move(onAccept);

    auto socket = Socket::createSocket(bindAddr_);
    if (!socket)
        return std::unexpected(socket.error());

    socket_ = std::move(*socket);

    if (auto r = socket_.setReuseAddr(); !r) return r;
    if (auto r = socket_.setNonblocking(); !r) return r;

    if (auto r = socket_.bindAndListen(bindAddr_); !r) return r;

    loop_.watch(socket_.fd(), static_cast<uint32_t>(IoEvent::Readable),
                [this](uint32_t) { onReadable(); });

    Logger::info("Listening on {}", bindAddr_.toString());
    return {};
}

void
Listener::stop()
{
    if (socket_.valid()) {
        loop_.remove(socket_.fd());
        socket_.close();
    }
}

void
Listener::onReadable()
{
    while (true) {
        Address clientAddr;
        auto result = socket_.accept(&clientAddr);
        if (!result) {
            if (result.error().code == ErrorCode::WouldBlock)
                break;
            if (result.error().code == ErrorCode::FdExhausted) {
                Logger::error("fd exhausted, pausing accept for 1s: {}", result.error().message);
                pauseAccepting();
                break;
            }
            Logger::error("accept failed: {}", result.error().message);
            continue;
        }

        auto client = std::move(*result);
        if (auto r = client.setNonblocking(); !r) {
            Logger::warn("setNonblocking failed, dropping client: {}", r.error().message);
            continue;
        }

        Logger::debug("Accepted connection from {}", clientAddr.toString());

        if (onAccept_)
            onAccept_(std::move(client), std::move(clientAddr));
    }
}

void
Listener::pauseAccepting()
{
    loop_.remove(socket_.fd());
    resumeTimer_ = loop_.addTimer(1s, [this] { resumeAccepting(); });
}

void
Listener::resumeAccepting()
{
    resumeTimer_.reset();
    loop_.watch(socket_.fd(), static_cast<uint32_t>(IoEvent::Readable),
                [this](uint32_t) { onReadable(); });
    Logger::info("Resuming accept on {}", bindAddr_.toString());
}

} // DeepSeer
