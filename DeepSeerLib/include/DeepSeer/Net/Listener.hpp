/// @file Listener.hpp
/// @brief Event-driven TCP accept loop.
///
/// Listener binds a Socket to an address, registers it for readable events
/// on the EventLoop, and drains incoming connections in a non-blocking loop.
/// Each accepted client is handed to the caller's `AcceptCallback` as a
/// move-only Socket + peer Address pair.
///
/// ## Lifecycle
///
/// `start()` creates the socket, sets SO_REUSEADDR + non-blocking, calls
/// bindAndListen, and begins watching for readable events. `stop()` (also
/// called by the destructor) unregisters the fd and closes the socket.
///
/// ## Back-pressure
///
/// When accept(2) fails with fd exhaustion (EMFILE/ENFILE), the listener
/// pauses by removing its fd from the event loop and scheduling a 1-second
/// timer to resume. This avoids a busy spin under resource pressure.

#pragma once

#include <DeepSeer/Core/Types.hpp>
#include <DeepSeer/Event/EventLoop.hpp>
#include <DeepSeer/Net/Address.hpp>
#include <DeepSeer/Net/Socket.hpp>

#include <functional>

namespace DeepSeer
{

/// Accepts TCP connection on a given addresss using the event loop.
class Listener
{
public:
    using AcceptCallback = std::function<void(Socket client, Address addr)>;

    Listener(EventLoop& loop, Address bindAddr);
    ~Listener();

    Listener(Listener const&) = delete;
    Listener& operator=(Listener const&) = delete;

    /// Start listening and accepting connections.
    VoidResult start(AcceptCallback onAccept);

    /// Stop accepting
    void stop();

    Address const& getListenAddr() const { return bindAddr_; }

private:
    void onReadable();
    void pauseAccepting();
    void resumeAccepting();

    EventLoop&      loop_;
    Address         bindAddr_;
    Socket          socket_;
    AcceptCallback  onAccept_;
    TimerHandlePtr  resumeTimer_;
};

} // DeepSeer
