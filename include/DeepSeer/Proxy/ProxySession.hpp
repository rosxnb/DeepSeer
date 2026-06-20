#pragma once

#include <DeepSeer/Core/Buffer.hpp>
#include <DeepSeer/Event/EventLoop.hpp>
#include <DeepSeer/Http/Http1/Codec.hpp>
#include <DeepSeer/Net/Connection.hpp>
#include <DeepSeer/Tls/CertCache.hpp>
#include <DeepSeer/Tls/CertGenerator.hpp>
#include <DeepSeer/Tls/TlsConnection.hpp>

#include <memory>
#include <string>

namespace DeepSeer
{

class ProxySession : public std::enable_shared_from_this<ProxySession>
{
public:
    ProxySession(Socket clientSocket, EventLoop& loop,
                 CertGenerator* certGen = nullptr, CertCache* certCache = nullptr);

    /// Begin processing the client connection
    void start();

    void close();

private:
    // --- Downstream (client) handling ---
    void onClientData(Buffer& data);
    void onClientClose();

    // --- HTTP codec callbacks ---
    void onRequest(HttpRequest req);
    void onRequestBody(Buffer& body, bool endStream);
    void onResponse(HttpResponse resp);
    void onResponseBody(Buffer& body, bool endStream);
    void onResponseComplete();

    // --- CONNECT tunnel ---
    void handleConnect(HttpRequest const& req);
    void startTunnel();
    void startTlsMitm(std::string const& hostname);

    // --- Upstream ---
    void connectUpstream(std::string const& host, uint16_t port);
    void onUpstreamConnected();
    void forwardRequest();
    void onUpstreamData(Buffer& data);
    void onUpstreamClose();

    // --- Helpers ---
    void sendError(uint16_t status, const std::string& reason);
    void setupUpstreamCallbacks();

    EventLoop& loop_;
    ConnectionPtr client_;
    ConnectionPtr upstream_;
    TlsConnectionPtr tlsClient_; // TLS wrapped client for MITM
    TlsConnectionPtr tlsUpstream_; // TLS wrapped upstream for MITM
    Socket connectingSocket_; // Held during non-blocking connect, before Connection wraps it

    CertGenerator* certGen_ {nullptr};
    CertCache* certCache_ {nullptr};

    Http1Codec clientCodec_{Http1Codec::Type::Request};
    Http1Codec upstreamCodec_{Http1Codec::Type::Response};

    HttpRequest currentRequest_;
    std::string connectHostname_;
    Buffer pendingBody_;
    bool isTunnel_ {false};
    bool isConnect_ {false}; // True if handling CONNECT
    bool isTlsMitm_ {false};
    bool closed_ {false};
};

} // DeepSeer
