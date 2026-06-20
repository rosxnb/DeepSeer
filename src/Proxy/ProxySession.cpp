#include <DeepSeer/Proxy/ProxySession.hpp>
#include <DeepSeer/Log/Logger.hpp>

namespace DeepSeer
{

ProxySession::ProxySession(Socket clientSocket, EventLoop& loop,
                           CertGenerator* certGen, CertCache* certCache)
    : loop_{loop}
    , certGen_{certGen}
    , certCache_{certCache}
{
    auto r = clientSocket.setNonblocking();
    client_ = std::make_shared<Connection>(std::move(clientSocket), loop_);
}

void
ProxySession::start()
{
    auto self = shared_from_this();

    client_->onData([self](Buffer& data) { self->onClientData(data); });
    client_->onClose([self]() { self->onClientClose(); });
    client_->onError([self](Error err) {
        Logger::debug("Client error: {}", err.message);
        self->close();
    });

    clientCodec_.setCallbacks({
        .onRequest = [self](HttpRequest req) { self->onRequest(std::move(req)); },
        .onBody = [self](Buffer& body, bool end) { self->onRequestBody(body, end); },
    });

    upstreamCodec_.setCallbacks({
        .onResponse = [self](HttpResponse resp) { self->onResponse(std::move(resp)); },
        .onBody = [self](Buffer& body, bool end) { self->onResponseBody(body, end); },
        .onMessageComplete = [self]() { self->onResponseComplete(); },
    });

    client_->startRead();
}

void
ProxySession::close()
{
    if (closed_)
        return;
    closed_ = true;

    // Clean up connecting socket if mid-connect
    if (connectingSocket_.valid()) {
        loop_.remove(connectingSocket_.fd());
        connectingSocket_.close();
    }

    if (client_)
        client_->close();
    if (upstream_)
        upstream_->close();
}

// ---------------------------------------------------------------------------
// Client data handling
// ---------------------------------------------------------------------------

void
ProxySession::onClientData(Buffer& data)
{
    if (isTunnel_ && upstream_) {
        upstream_->write(data);
        return;
    }

    auto result = clientCodec_.decode(data);
    if (!result) {
        Logger::warn("Client parse error: {}", result.error().message);
        sendError(400, "Bad Request");
        close();
    }
}

void
ProxySession::onClientClose()
{
    close();
}

// ---------------------------------------------------------------------------
// HTTP request handling
// ---------------------------------------------------------------------------

void
ProxySession::onRequest(HttpRequest req)
{
    Logger::info("{} {} (host: {})", methodToString(req.method), req.url, req.host);

    currentRequest_ = std::move(req);

    if (currentRequest_.method == HttpMethod::CONNECT) {
        isConnect_ = true;
        handleConnect(currentRequest_);
        return;
    }

    auto port = currentRequest_.port;
    if (port == 0)
        port = 80;
    connectUpstream(currentRequest_.host, port);
}

void
ProxySession::onRequestBody(Buffer& body, bool /*end_stream*/)
{
    if (body.empty())
        return;
    pendingBody_.move(body);

    if (upstream_ && upstream_->connected()) {
        upstream_->write(pendingBody_);
    }
}

// ---------------------------------------------------------------------------
// Response handling
// ---------------------------------------------------------------------------

void ProxySession::onResponse(HttpResponse resp)
{
    // llhttp delivers decoded body (de-chunked), so these framing headers
    // from the upstream no longer apply. Remove them — the client will use
    // connection close to detect the end of the response.
    resp.headers.remove("Transfer-Encoding");
    resp.headers.remove("Content-Length");

    Buffer out;
    Http1Codec encoder;
    encoder.encodeResponse(resp, out);
    if (client_ && client_->connected()) {
        client_->write(out);
    }
}

void ProxySession::onResponseBody(Buffer& body, bool /*end_stream*/)
{
    if (!body.empty() && client_ && client_->connected()) {
        client_->write(body);
    }
}

void
ProxySession::onResponseComplete()
{
    pendingBody_ = Buffer{};

    // Signal end-of-response to the client by closing the connection.
    // Without Content-Length or chunked encoding, close is the only
    // way the client knows the body is complete.
    close();
}

// ---------------------------------------------------------------------------
// CONNECT tunnel
// ---------------------------------------------------------------------------

void
ProxySession::handleConnect(HttpRequest const& req)
{
    std::string host;
    uint16_t port = 443;

    auto colon = req.url.rfind(':');
    if (colon != std::string::npos) {
        host = req.url.substr(0, colon);
        port = static_cast<uint16_t>(std::stoi(req.url.substr(colon + 1)));
    } else {
        host = req.url;
    }

    connectHostname_ = host;
    Logger::info("CONNECT tunnel to {}:{}", host, port);
    connectUpstream(host, port);
}

void
ProxySession::startTunnel()
{
    // If we have CertGenerator, perform TLS interception
    if (certGen_) {
        startTlsMitm(connectHostname_);
        return ;
    }

    // Otherwise, plain tunnel (passthrough, no interception)
    isTunnel_ = true;
    client_->write("HTTP/1.1 200 Connection Established\r\n\r\n");
    setupUpstreamCallbacks();
    upstream_->startRead();
}

void
ProxySession::startTlsMitm(std::string const& hostname)
{
    isTlsMitm_ = true;
    auto self = shared_from_this();

    // Send 200 to client and stop reading (TLS will take over the socket).
    client_->write("HTTP/1.1 200 Connection Established\r\n\r\n");
    client_->stopRead();

    // 2. Establish TLS with upstream — extract socket from Connection
    auto upstreamCtx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(upstreamCtx, SSL_VERIFY_NONE, nullptr);

    auto upstreamSock = upstream_->releaseSocket();
    upstream_.reset();
    tlsUpstream_ = std::make_shared<TlsConnection>(
        std::move(upstreamSock), upstreamCtx, loop_, false);

    tlsUpstream_->startHandshake(
        [self, upstreamCtx, hostname]() {
            Logger::debug("TLS handshake with upstream complete for {}", hostname);

            // Generate a forged certificate for this hostname
            auto certResult = self->certGen_->generate(hostname);
            if (!certResult) {
                Logger::warn("Cert generation failed: {}", certResult.error().message);
                SSL_CTX_free(upstreamCtx);
                self->close();
                return;
            }

            // Create SSL_CTX with the forged cert for the client side
            auto clientCtx = SSL_CTX_new(TLS_server_method());
            SSL_CTX_use_certificate(clientCtx, certResult->cert.get());
            SSL_CTX_use_PrivateKey(clientCtx, certResult->key.get());

            if (self->certCache_) {
                self->certCache_->put(hostname, clientCtx);
            }

            // Start TLS handshake with the client (as server, using forged cert)
            auto clientSock = self->client_->releaseSocket();
            self->client_.reset();
            self->tlsClient_ = std::make_shared<TlsConnection>(
                std::move(clientSock), clientCtx, self->loop_, true);

            self->tlsClient_->startHandshake(
                [self, clientCtx, upstreamCtx]() {
                    Logger::debug("TLS handshake with client complete (MITM active)");

                    // Both TLS connections established. Bridge decrypted data.
                    self->tlsClient_->onData([self](Buffer& data) {
                        Logger::debug("MITM client->upstream: {} bytes", data.length());
                        self->tlsUpstream_->write(data);
                    });
                    self->tlsClient_->onClose([self]() { self->close(); });

                    self->tlsUpstream_->onData([self](Buffer& data) {
                        Logger::debug("MITM upstream->client: {} bytes", data.length());
                        self->tlsClient_->write(data);
                    });
                    self->tlsUpstream_->onClose([self]() { self->close(); });

                    self->tlsClient_->startRead();
                    self->tlsUpstream_->startRead();

                    SSL_CTX_free(clientCtx);
                    SSL_CTX_free(upstreamCtx);
                },
                [self, clientCtx, upstreamCtx](Error err) {
                    Logger::warn("Client TLS handshake failed: {}", err.message);
                    SSL_CTX_free(clientCtx);
                    SSL_CTX_free(upstreamCtx);
                    self->close();
                });
        },
        [self, upstreamCtx](Error err) {
            Logger::warn("Upstream TLS handshake failed: {}", err.message);
            SSL_CTX_free(upstreamCtx);
            self->close();
        });
}

// ---------------------------------------------------------------------------
// Upstream connection (non-blocking connect using raw socket)
// ---------------------------------------------------------------------------

void ProxySession::connectUpstream(std::string const& host, uint16_t port)
{
    auto addressResult = Address::fromHostPort(host, port);
    auto sockResult = Socket::createSocket(*addressResult);
    if (!sockResult) {
        sendError(502, "Bad Gateway");
        return;
    }

    connectingSocket_ = std::move(*sockResult);
    auto r = connectingSocket_.setNonblocking();
    auto connResult = connectingSocket_.connect(*addressResult);

    if (!connResult) {
        Logger::warn("Connect to {}:{} failed: {}", host, port, connResult.error().message);
        sendError(502, "Bad Gateway");
        return;
    }

    if (*connResult) {
        // Connected immediately (common for localhost)
        Logger::debug("Connected to {}:{} (immediate)", host, port);
        onUpstreamConnected();
    } else {
        // In progress — watch for writable to know when connect completes
        Logger::debug("Connecting to {}:{} (in progress)", host, port);
        auto self = shared_from_this();
        auto fd = connectingSocket_.fd();
        loop_.watch(fd, static_cast<uint32_t>(IoEvent::Writable), [self, fd](uint32_t) {
            self->loop_.remove(fd);
            self->onUpstreamConnected();
        });
    }
}

void
ProxySession::onUpstreamConnected()
{
    if (closed_)
        return;

    // Now wrap the connected socket in a Connection
    upstream_ = std::make_shared<Connection>(std::move(connectingSocket_), loop_);

    if (isConnect_) {
        startTunnel();
    } else {
        forwardRequest();
    }
}

void
ProxySession::setupUpstreamCallbacks()
{
    auto self = shared_from_this();
    upstream_->onData([self](Buffer& data) { self->onUpstreamData(data); });
    upstream_->onClose([self]() { self->onUpstreamClose(); });
    upstream_->onError([self](Error err) {
        Logger::debug("Upstream error: {}", err.message);
        self->close();
    });
}

void
ProxySession::forwardRequest()
{
    setupUpstreamCallbacks();
    upstream_->startRead();

    // Convert proxy-style absolute URL to origin-form path
    auto& url = currentRequest_.url;
    auto schemeEnd = url.find("://");
    if (schemeEnd != std::string::npos) {
        auto path_start = url.find('/', schemeEnd + 3);
        if (path_start != std::string::npos) {
            url = url.substr(path_start);
        } else {
            url = "/";
        }
    }

    // Encode and send request
    Buffer out;
    Http1Codec encoder;
    encoder.encodeRequest(currentRequest_, out);
    Logger::debug("Forwarding request ({} bytes): {}", out.length(), out.toString().substr(0, 80));
    upstream_->write(out);

    if (!pendingBody_.empty()) {
        upstream_->write(pendingBody_);
    }
}

void
ProxySession::onUpstreamData(Buffer& data)
{
    Logger::debug("Upstream data received: {} bytes", data.length());
    if (isTunnel_) {
        if (client_ && client_->connected()) {
            client_->write(data);
        }
        return;
    }

    auto result = upstreamCodec_.decode(data);
    if (!result) {
        Logger::warn("Upstream parse error: {}", result.error().message);
        close();
    }
}

void
ProxySession::onUpstreamClose()
{
    close();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void
ProxySession::sendError(uint16_t status, std::string const& reason)
{
    HttpResponse resp;
    resp.statusCode = status;
    resp.reason = reason;
    resp.headers.set("Content-Length", "0");
    resp.headers.set("Connection", "close");

    Buffer out;
    Http1Codec encoder;
    encoder.encodeResponse(resp, out);
    if (client_ && client_->connected()) {
        client_->write(out);
    }
}

} // DeepSeer
