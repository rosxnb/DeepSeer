/// @file Address.hpp
/// @brief Resolved network address (IPv4 or IPv6) for use with Socket.
///
/// Address is a thin, value-type wrapper around `sockaddr_storage` that holds
/// a fully resolved endpoint (IP + port). It is passed to Socket::createSocket,
/// Socket::connect, and Socket::bindAndListen.
///
/// ## Construction
///
/// Three factories cover the common cases:
/// - **fromString()**: Parses "host:port" or "[ipv6]:port", then resolves via
///   getaddrinfo. Returns the first result.
/// - **fromHostPort()**: Same resolution, separate host and port arguments.
/// - **fromSockaddr()**: Zero-copy wrap of a raw sockaddr (e.g., from accept(2)).
///
/// ## DNS Resolution
///
/// `fromString()` and `fromHostPort()` perform **blocking** DNS resolution via
/// getaddrinfo(). This is acceptable for the current single-threaded proxy but
/// should be made async (via a thread pool or c-ares) for production use.
///
/// ## Platform
///
/// Uses `sockaddr_storage` internally, so it is family-agnostic. Compiles on
/// POSIX and Windows (Winsock2).

#pragma once

#include <DeepSeer/Core/Types.hpp>

#include <cstdint>
#include <string>

#ifdef PLATFORM_WINDOWS
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#endif

namespace DeepSeer
{

class Address
{
public:
    /// Construct from host:port string (e.g. "192.168.0.0:8080" or "[::1]:8080")
    static Expected<Address> fromString(std::string const& hostPort);

    /// Construct from host and port
    static Expected<Address> fromHostPort(std::string const& host, uint16_t port);

    /// Construct from raw sockaddr (e.g., from accept(2))
    static Address fromSockaddr(struct sockaddr const* sa, socklen_t len);

    std::string toString() const;
    uint16_t port() const;

    /// Get protocol family (AF_INET or AF_INET6)
    int family() const { return storage_.ss_family; }

    /// Get pointer to sockaddr* for syscall
    struct sockaddr const* data() const
    { return reinterpret_cast<struct sockaddr const*>(&storage_); }

    struct sockaddr* data()
    { return reinterpret_cast<struct sockaddr*>(&storage_); }

    socklen_t size() const { return len_; }

    bool isIpv4() const { return storage_.ss_family == AF_INET; }
    bool isIpv6() const { return storage_.ss_family == AF_INET6; }

private:
    struct sockaddr_storage storage_{};
    socklen_t len_{0};
};

} // DeepSeer
