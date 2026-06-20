#include <DeepSeer/Net/Socket.hpp>

#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

#ifdef PLATFORM_WINDOWS
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace DeepSeer
{

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

namespace {

#ifdef PLATFORM_WINDOWS

int         platformError()              { return WSAGetLastError(); }
bool        isWouldBlock(int e)          { return e == WSAEWOULDBLOCK; }
bool        isInProgress(int e)          { return e == WSAEWOULDBLOCK; }
bool        isFdExhausted(int e)         { return e == WSAEMFILE; }
void        platformClose(Fd fd)         { ::closesocket(static_cast<SOCKET>(fd)); }

std::string platformErrorStr(int e)
{
    char buf[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(e), 0, buf, sizeof(buf), nullptr);
    return buf;
}

#else // POSIX (macOS, Linux, BSD)

int         platformError()              { return errno; }
bool        isWouldBlock(int e)          { return e == EAGAIN || e == EWOULDBLOCK; }
bool        isInProgress(int e)          { return e == EINPROGRESS; }
bool        isFdExhausted(int e)         { return e == EMFILE || e == ENFILE; }
void        platformClose(Fd fd)         { ::close(fd); }
std::string platformErrorStr(int e)      { return std::strerror(e); }

#endif

void suppressSigpipe([[maybe_unused]] Fd fd)
{
#ifdef PLATFORM_MACOS
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
}

} // anonymous namespace


// ---------------------------------------------------------------------------
// Socket Implementation
// ---------------------------------------------------------------------------

Socket::Socket(Fd fd)
    : fd_{fd}
{ }

Socket::~Socket()
{
    close();
}

void
Socket::swap(Socket& other) noexcept
{
    std::swap(fd_, other.fd_);
}

Socket::Socket(Socket&& other) noexcept
    : fd_{std::exchange(other.fd_, kInvalidFd)}
{ }

Socket&
Socket::operator=(Socket&& other) noexcept
{
    Socket { std::move(other) }.swap(*this);
    return *this;
}

Expected<Socket>
Socket::createSocket(Address const& addr)
{
    Fd fd = ::socket(addr.family(), SOCK_STREAM, 0);
    if (fd == kInvalidFd)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("socket() failed: {}", platformErrorStr(platformError())) });


    suppressSigpipe(fd);
    return Socket{ fd };
}

VoidResult
Socket::bindAndListen(Address const& addr, int backlog)
{
    if (::bind(fd_, addr.data(), addr.size()) < 0)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("bind() failed: {}", platformErrorStr(platformError())) });

    if (::listen(fd_, backlog) < 0)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("listen() failed: {}", platformErrorStr(platformError())) });

    return {};
}

Expected<bool>
Socket::connect(Address const& addr)
{
    if (::connect(fd_, addr.data(), addr.size()) == 0)
        return true; // connected immediately

    int err = platformError();
    if (isInProgress(err))
        return false; // in progress -- watch for writable

    return std::unexpected(Error{ ErrorCode::ConnectionRefused,
                                  std::format("connect() failed: {}", platformErrorStr(err)) });
}

Expected<Socket>
Socket::accept(Address* clientAddr)
{
    sockaddr_storage storage{};
    socklen_t len = sizeof(storage);

    Fd clientFd = ::accept(fd_,
                           reinterpret_cast<sockaddr*>(&storage),
                           clientAddr ? &len : nullptr);
    if (clientFd == kInvalidFd) {
        int err = platformError();
        if (isWouldBlock(err))
            return std::unexpected(Error{ ErrorCode::WouldBlock, "accept() would block" });
        if (isFdExhausted(err))
            return std::unexpected(Error{ ErrorCode::FdExhausted,
                                          std::format("accept() fd exhausted: {}", platformErrorStr(err)) });
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("accept() failed: {}", platformErrorStr(err)) });
    }

    if (clientAddr)
        *clientAddr = Address::fromSockaddr(reinterpret_cast<sockaddr const*>(&storage), len);

    suppressSigpipe(clientFd);
    return Socket{clientFd};
}

VoidResult
Socket::setNonblocking()
{
#ifdef PLATFORM_WINDOWS
    u_long mode = 1;
    if (::ioctlsocket(static_cast<SOCKET>(fd_), FIONBIO, &mode) != 0)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("ioctlsocket(FIONBIO) failed: {}",
                                                  platformErrorStr(platformError())) });
#else
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("fcntl(O_NONBLOCK) failed: {}",
                                                  platformErrorStr(platformError())) });
#endif
    return {};
}

VoidResult
Socket::setReuseAddr()
{
    int on = 1;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<char const*>(&on), sizeof(on)) < 0)
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("setsockopt(SO_REUSEADDR) failed: {}",
                                                  platformErrorStr(platformError())) });
    return {};
}

Expected<ssize_t>
Socket::read(std::byte* buf, size_t len)
{
#ifdef PLATFORM_WINDOWS
    int n = ::recv(static_cast<SOCKET>(fd_),
                   reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
#else
    ssize_t n = ::recv(fd_, buf, len, 0);
#endif

    if (n < 0) {
        int err = platformError();
        if (isWouldBlock(err))
            return static_cast<ssize_t>(-1); // EAGAIN
        return std::unexpected(Error{ ErrorCode::ReadError,
                                      std::format("recv() failed: {}", platformErrorStr(err)) });
    }
    return static_cast<ssize_t>(n); // 0 = EOF, >0 = bytes read
}

Expected<size_t>
Socket::write(std::byte const* buf, size_t len)
{
    int flags = 0;
#ifdef PLATFORM_LINUX
    flags |= MSG_NOSIGNAL;
#endif

#ifdef PLATFORM_WINDOWS
    int n = ::send(static_cast<SOCKET>(fd_),
                   reinterpret_cast<char const*>(buf), static_cast<int>(len), flags);
#else
    ssize_t n = ::send(fd_, buf, len, flags);
#endif

    if (n < 0) {
        int err = platformError();
        if (isWouldBlock(err))
            return static_cast<size_t>(0);
        return std::unexpected(Error{ ErrorCode::WriteError,
                                      std::format("send() failed: {}", platformErrorStr(err)) });
    }
    return static_cast<size_t>(n);
}

void
Socket::close()
{
    if (valid()) {
        platformClose(fd_);
        fd_ = kInvalidFd;
    }
}

} // namespace DeepSeer
