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
/// **fd ownership**: Connection owns the Socket and its fd. Use `release_socket()`
/// to extract the Socket (e.g., when transferring to TlsConnection). After
/// release, the Connection is invalid — do not call any methods on it.
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

/// Callback receiving a Buffer of data from the connection.
/// The Buffer is valid only during the callback — move data out if needed.
using DataCallback  = std::function<void(Buffer& data)>;

/// Callback receiving an Error when something goes wrong.
using ErrorCallback = std::function<void(Error error)>;

/// Event-driven bidirectional TCP connection.
class Connection : public std::enable_shared_from_this<Connection>
{
public:
    /// @param socket Non-blocking socket (caller must have called set_nonblocking).
    /// @param loop   The event loop to register I/O events with.
    Connection(Socket socket, EventLoop& loop);
    ~Connection();

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;

    /// Set callbacks. Must be called before `startRead()`.
    void onData(DataCallback cb)    { onData_  = std::move(cb); }
    void onClose(Callback cb)       { onClose_ = std::move(cb); }
    void onError(ErrorCallback cb)  { onError_ = std::move(cb); }

    /// Begin reading from the socket. Registers EVFILT_READ with the EventLoop.
    void startRead();

    /// Stop reading from the socket. Removes the readable watch.
    /// Used before transferring the socket to TlsConnection for MITM.
    void stopRead();

    /// Send data to the peer. Buffers internally, attempts immediate flush.
    /// The Buffer is consumed (moved into the internal write buffer).
    void write(Buffer& data);

    /// Send a string. Convenience wrapper.
    void write(std::string_view data);

    /// Half-close the write side (TCP FIN). Peer receives EOF.
    void shutdownWrite();

    /// Fully close the connection. Removes all event registrations.
    void close();

    bool connected() const { return !closed_; }
    Fd fd() const { return socket_.fd(); }
    EventLoop& loop() { return loop_; }

    /// Extract the underlying Socket, transferring fd ownership.
    /// Removes event registrations. Connection is invalid after this call.
    /// Used when upgrading a plain connection to TLS (MITM).
    Socket releaseSocket()
    {
        loop_.remove(socket_.fd());
        closed_ = true;
        return std::move(socket_);
    }

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
    DataCallback onData_;
    Callback onClose_;
    ErrorCallback onError_;
};

/// Connection is always heap-allocated and shared (see ownership docs above).
using ConnectionPtr = std::shared_ptr<Connection>;

} // DeepSeer
