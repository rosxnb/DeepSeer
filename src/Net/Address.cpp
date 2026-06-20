#include <DeepSeer/Net/Address.hpp>

#ifndef PLATFORM_WINDOWS
#  include <netdb.h>
#  include <netinet/in.h>
#endif

#include <cstring>
#include <format>

namespace DeepSeer
{

Expected<Address>
Address::fromString(std::string const& hostPort)
{
    std::string host;
    std::string port;

    auto colonPos = hostPort.rfind(':');
    if (colonPos == std::string::npos) {
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("Address construction got invalid address format") });
    }

    if (hostPort.front() == '[') {
        // IPV6 format: [1999:cda::1]:8080
        auto end = hostPort.find(']');
        if (end == std::string::npos || end + 1 != colonPos) {
            return std::unexpected(Error{ ErrorCode::InternalError,
                                          std::format("Address construction got invalide IPV6 address format") });
        }

        host = hostPort.substr(1, end - 1);
        port = hostPort.substr(colonPos + 1);
    }
    
    else {
        // IPV4 format
        host = hostPort.substr(0, colonPos);
        port = hostPort.substr(colonPos + 1);
    }

    uint16_t portNum = static_cast<uint16_t>(std::stoul(std::string{ port }));
    return fromHostPort(host, portNum);
}

Expected<Address>
Address::fromHostPort(std::string const& host, uint16_t port)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result { nullptr };
    auto portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0) {
        return std::unexpected(Error{ ErrorCode::InternalError,
                                      std::format("getaddrinfo failed: {}", gai_strerror(rc)) });
    }

    Address addr;
    memcpy(&addr.storage_, result->ai_addr, result->ai_addrlen);
    addr.len_ = result->ai_addrlen;

    freeaddrinfo(result);

    return addr;
}

Address
Address::fromSockaddr(struct sockaddr const* sa, socklen_t len)
{
    Address addr;
    std::memcpy(&addr.storage_, sa, len);
    addr.len_ = len;
    return addr;
}

std::string
Address::toString() const
{
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];

    int rc = getnameinfo(data(), len_,
                         host, sizeof(host),
                         port, sizeof(port),
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0)
        return {};

    return isIpv6()
        ? '[' + std::string{ host } + "]:" + port
        : std::string{ host } + ':' + port;
}

uint16_t
Address::port() const
{
    if (isIpv6()) {
        auto* sin = reinterpret_cast<struct sockaddr_in6 const*>(&storage_);
        return ntohs(sin->sin6_port);
    }
    
    else if (isIpv4()) {
        auto* sin = reinterpret_cast<struct sockaddr_in const*>(&storage_);
        return ntohs(sin->sin_port);
    }

    return 0;
}

} // namespace DeepSeer
