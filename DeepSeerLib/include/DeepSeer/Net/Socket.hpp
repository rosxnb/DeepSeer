/// @file Socket.hpp
/// @brief RAII TCP socket wrapper with non-blocking I/O support.
///
/// ## Ownership
///
/// Socket is move-only and owns its file descriptor. The destructor calls
/// close(). Construction is through the static factory `createSocket()` —
/// the raw-fd constructor is private. Use `release()` to transfer fd ownership
/// elsewhere (e.g., to a TlsConnection) without closing.
///
/// ## Server & Client
///
/// A single Socket type serves both roles:
/// - **Server**: createSocket → setReuseAddr → bindAndListen → accept (loop).
/// - **Client**: createSocket → setNonblocking → connect.
///
/// ## Non-blocking I/O
///
/// After `setNonblocking()`, all I/O operations follow non-blocking semantics
/// (abstracted cross-platform via platform helpers):
///
/// - **read()**: Returns >0 (data), 0 (EOF/peer closed), -1 (EAGAIN, retry
///   later). Hard errors return `unexpected<Error>`.
///
/// - **write()**: Returns >0 (bytes written), 0 (EAGAIN, retry later).
///   Partial writes are normal — the caller must track and retry the remainder.
///
/// - **connect()**: Returns `true` (connected immediately — common for
///   localhost) or `false` (EINPROGRESS — watch for writable event via
///   EventLoop). This distinction is critical: if connect returns true, do NOT
///   wait for a writable event — it won't fire (the state didn't change).
///
/// - **accept()**: Returns the new Socket on success, or `WouldBlock` error
///   if no pending connection is available.
///
/// ## Platform
///
/// Compiles on POSIX (macOS, Linux, BSD) and Windows (Winsock2). Platform
/// differences — SIGPIPE suppression, error codes, fd types — are handled
/// internally. DNS resolution is not performed here; the caller provides a
/// pre-resolved Address.

#pragma once

#include <DeepSeer/Core/Types.hpp>
#include <DeepSeer/Net/Address.hpp>

namespace DeepSeer
{

class Socket
{
public:
    Socket() = default;
    ~Socket();

    void swap(Socket& other) noexcept;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    Socket(Socket& other) = delete;
    Socket& operator=(Socket& other) = delete;

    /// Create a new TCP socket.
    static Expected<Socket> createSocket(Address const& addr);

    /// Bind to the given address and start listening for connections.
    /// @param backlog Maximum pending connection queue length.
    VoidResult bindAndListen(Address const& addr, int backlog = SOMAXCONN);

    /// Initiate a non-blocking TCP connect to the given address.
    /// @returns true if connected immediately, false if in-progress (EINPROGRESS).
    ///          Watch the fd for writable to detect completion.
    Expected<bool> connect(Address const& addr);

    /// Accept a new connection on a listening socket.
    /// If clientAddr is non-null, the peer's address is written into it.
    /// @returns Socket for new connected client and its address in `clientAddr` is not `nullptr`.
    Expected<Socket> accept(Address* clientAddr = nullptr);

    /// Set socket to non-blocking mode.
    VoidResult setNonblocking();

    /// Set SO_REUSEADDR.
    VoidResult setReuseAddr();

    /// Non-blocking read.
    /// @returns >0: bytes read. 0: EOF (peer closed). -1: EAGAIN (no data, retry later).
    Expected<ssize_t> read(std::byte* buf, size_t len);

    /// Non-blocking write.
    /// @returns bytes written (may be less than len — partial write). 0 = EAGAIN.
    Expected<size_t> write(std::byte const* buf, size_t len);

    void close();

    /// Release ownership of the fd without closing it. Returns the raw fd.
    /// After this call, the Socket is invalid (fd_ == kInvalidFd).
    /// Used when transferring the fd to a TlsConnection.
    Fd release()
    {
        auto fd = fd_;
        fd_ = kInvalidFd;
        return fd;
    }

    Fd   fd() const { return fd_; }
    bool valid() const { return fd_ != kInvalidFd; }

private:
    Fd fd_{kInvalidFd};

    /// Construct from a raw fd (takes ownership).
    explicit Socket(Fd fd);
};

} // DeepSeer
