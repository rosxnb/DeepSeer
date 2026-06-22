#pragma once

/// @file TlsConnection.hpp
/// @brief TLS-wrapped connection with non-blocking handshake and I/O.
///
/// ## Design
///
/// TlsConnection is the TLS equivalent of Connection. It wraps a raw Socket
/// with an OpenSSL SSL object and integrates with the EventLoop for non-blocking
/// TLS operations.
///
/// ## Non-blocking TLS
///
/// OpenSSL's SSL_do_handshake(), SSL_read(), and SSL_write() may return
/// SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE, meaning "I need to do
/// more network I/O before I can complete this operation." When this happens,
/// we register the appropriate event (readable or writable) with the EventLoop
/// and retry when it fires.
///
/// ## MITM Usage (the main use case)
///
/// In the MITM flow, two TlsConnections are created per HTTPS session:
///
/// 1. **tls_upstream** (isServer=false): TLS client connecting to the origin.
///    Uses a permissive SSL_CTX (SSL_VERIFY_NONE) since we don't validate the
///    upstream cert — we're intercepting, not relying on it.
///
/// 2. **tls_client** (isServer=true): TLS server presenting the forged cert
///    to the browser. Uses an SSL_CTX loaded with the CertGenerator output.
///
/// After both handshakes complete, data flows:
///   Browser → tls_client.read → tls_upstream.write → Origin Server
///   Origin Server → tls_upstream.read → tls_client.write → Browser
///
/// ## Ownership
///
/// TlsConnection takes ownership of the Socket (moved in). It also holds a
/// raw SSL* pointer created from the provided SSL_CTX. The SSL* is freed in
/// the destructor. The SSL_CTX is NOT owned — the caller manages its lifetime
/// (typically freed after the handshake completes or stored in CertCache).
///
/// ## Callbacks
///
/// Same pattern as Connection: set onData/onClose/onError, then startRead().
/// The startHandshake() method takes separate completion/error callbacks.

#include <DeepSeer/Core/Buffer.hpp>
#include <DeepSeer/Core/Types.hpp>
#include <DeepSeer/Event/EventLoop.hpp>
#include <DeepSeer/Net/Socket.hpp>

#include <openssl/ssl.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace DeepSeer
{

/// Callback receiving a Buffer of decrypted data.
using DataCallback  = std::function<void(Buffer& data)>;

/// Callback receiving an Error.
using ErrorCallback = std::function<void(Error err)>;

/// TLS connection wrapping a Socket with OpenSSL.
class TlsConnection : public std::enable_shared_from_this<TlsConnection>
{
public:
    /// @param socket    Non-blocking socket (ownership transferred).
    /// @param ctx       SSL_CTX to create the SSL object from (NOT owned).
    /// @param loop      EventLoop for I/O registration.
    /// @param isServer true = TLS server (accept), false = TLS client (connect).
    TlsConnection(Socket socket, SSL_CTX* ctx, EventLoop& loop, bool isServer);
    ~TlsConnection();

    TlsConnection(TlsConnection const&) = delete;
    TlsConnection& operator=(TlsConnection const&) = delete;

    /// Begin the TLS handshake. Non-blocking — may require multiple event loop
    /// iterations. Calls on_complete when done, on_error on failure.
    void startHandshake(Callback on_complete, ErrorCallback on_error);

    void onData(DataCallback cb) { onData_ = std::move(cb); }
    void onClose(Callback cb) { onClose_ = std::move(cb); }
    void onError(ErrorCallback cb) { onErrorCb_ = std::move(cb); }

    /// Begin reading decrypted data. Must be called AFTER handshake completes.
    void startRead();

    /// Write data (will be encrypted by SSL). Buffers internally.
    void write(Buffer& data);
    void write(std::string_view data);

    void close();
    bool connected() const { return !closed_ && handshakeDone_; }
    Fd fd() const { return socket_.fd(); }

    /// Get the negotiated ALPN protocol after handshake completes.
    /// Returns "h2" for HTTP/2, "http/1.1" for HTTP/1.1, or "" if no ALPN.
    std::string negotiatedProtocol() const;

    /// Set the SNI hostname for the TLS handshake. Call before start_handshake().
    /// For client mode: tells the server which hostname we're connecting to.
    /// Many servers REQUIRE this — without it they reject with handshake failure.
    void setSni(std::string const& hostname);

    /// Configure ALPN protocols to advertise. Call before start_handshake().
    /// For client: advertises these protocols.
    /// For server: selects from client's list using this preference order.
    /// @param protos Wire-format ALPN list (e.g., "\x02h2\x08http/1.1")
    void setAlpn(std::vector<uint8_t> const& protos);

private:
    /// Continue a pending handshake after WANT_READ/WANT_WRITE resolves.
    void continueHandshake();

    /// Main I/O callback registered with the EventLoop.
    void handleIo(uint32_t events);

    /// Read decrypted data from SSL and deliver to on_data_ callback.
    void doRead();

    /// Flush write_buf_ through SSL_write.
    void doWrite();

    /// Register the appropriate EventLoop watch for SSL_ERROR_WANT_READ/WRITE.
    void watchForSsl(int sslErr);

    Socket socket_;
    SSL* ssl_ = nullptr; // Owned — freed in destructor
    EventLoop& loop_;
    bool isServer_;
    bool closed_ = false;
    bool handshakeDone_ = false;
    bool reading_ = false;

    Buffer writeBuf_;

    DataCallback onData_;
    Callback onClose_;
    ErrorCallback onErrorCb_;
    Callback onHandshakeComplete_;
    ErrorCallback onHandshakeError_;
};

using TlsConnectionPtr = std::shared_ptr<TlsConnection>;

} // namespace DeepSeer
