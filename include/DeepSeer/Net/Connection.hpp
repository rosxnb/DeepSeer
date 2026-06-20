#pragma once

/// @file Connection.hpp
/// @brief Event-driven bidirectional TCP connection with buffered writes.
///
/// ## Design
///
/// Connection wraps a non-blocking Socket and integrates it with an EventLoop.
/// It handles the read/write/close lifecycle:
///
/// 1. Set callbacks: onData, onClose, onError
/// 2. Call startRead() to begin receiving data
/// 3. Data arrives → onData callback fires with a Buffer
/// 4. Call write() to send data — internally buffers and flushes
/// 5. Peer closes → onClose callback fires
///
/// ## Ownership
///
/// Connection is always managed via `std::shared_ptr<Connection>` (ConnectionPtr).
/// This is required because EventLoop callbacks capture `this` — the Connection
/// must outlive its event registrations. The shared_ptr prevents use-after-free
/// even if the owning ProxySession drops its reference.
///
/// **fd ownership**: Connection owns the Socket and its fd.
///
/// ## Write Buffering
///
/// `write()` moves data into an internal write buffer and attempts an immediate
/// flush via `handleWrite()`. If the socket isn't writable (partial write),
/// the EventLoop watches for EVFILT_WRITE to continue flushing.
///
/// IMPORTANT: `handleWrite()` iterates `writeBuf_.slices()` using a while
/// loop () and drains after each write. This avoids iterator invalidation 
/// since `drain()` may erase elements from the slice vector.
///
/// ## Thread Safety
///
/// Not thread-safe. All methods must be called from the EventLoop's owning thread.
/// Use `EventLoop::post()` for cross-thread access.

#include <DeepSeer/Core/Buffer.hpp>
#include <DeepSeer/Core/Types.hpp>
#include <DeepSeer/Event/EventLoop.hpp>
#include <DeepSeer/Net/Socket.hpp>

#include <functional>
#include <memory>

namespace DeepSeer
{

/// Callback types for Connection Events.
using DataCallback  = std::function<void(Buffer& data)>;
using ErrorCallback = std::function<void(Error error)>;

/// Event-driven bidirectional TCP connection.
class Connection : public std::enable_shared_from_this<Connection>
{
public:
    Connection(Socket socket, EventLoop& loop);
    ~Connection();

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;

    /// Set callbacks. Must be called before `startRead()`.
    void onData(DataCallback cb)    { onData_  = std::move(cb); }
    void onClose(Callback cb)       { onClose_ = std::move(cb); }
    void onError(ErrorCallback cb)  { onError_ = std::move(cb); }

    /// Register with event loop for reading.
    void startRead();

    /// Write data to the connection. Buffers internally if socket no writeable.
    void write(Buffer& data);
    void write(std::string_view data);

    /// Shutdown write side (half-close).
    void shutdownWrite();

    /// Close the connection fully.
    void close();

    bool connected() const { return !closed_; }
    Fd fd() const { return socket_.fd(); }
    EventLoop& loop() { return loop_; }

private:
    /// Recalculate which events to watch based on reading_ and writeBuf_ state.
    /// Called after any state change (startRead, write, drain).
    void updateEvents();

    /// Read available data from socket and deliver to onData_ callback.
    void handleRead();

    /// Flush writeBuf_ to socket. Registered as EVFILT_WRITE callback.
    void handleWrite();

    Socket socket_;
    EventLoop& loop_;
    bool closed_ {false};
    bool reading_ {false};

    Buffer writeBuf_;
    bool writeResgistered_ {false};

    DataCallback onData_;
    Callback onClose_;
    ErrorCallback onError_;
};

/// Connection is always heap-allocated and shared (see ownership docs above).
using ConnectionPtr = std::shared_ptr<Connection>;

} // DeepSeer
