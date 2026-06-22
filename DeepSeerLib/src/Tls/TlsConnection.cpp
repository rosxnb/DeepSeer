#include <DeepSeer/Tls/TlsConnection.hpp>
#include <DeepSeer/Log/Logger.hpp>

#include <openssl/err.h>

namespace DeepSeer
{

TlsConnection::TlsConnection(Socket socket, SSL_CTX* ctx, EventLoop& loop, bool isServer)
    : socket_(std::move(socket))
    , loop_(loop)
    , isServer_(isServer)
{
    ssl_ = SSL_new(ctx);
    SSL_set_fd(ssl_, socket_.fd());
    if (isServer) {
        SSL_set_accept_state(ssl_);
    } else {
        SSL_set_connect_state(ssl_);
    }
}

TlsConnection::~TlsConnection()
{
    close();
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
}

std::string
TlsConnection::negotiatedProtocol() const
{
    unsigned char const* proto = nullptr;
    unsigned int len = 0;
    SSL_get0_alpn_selected(ssl_, &proto, &len);
    if (proto && len > 0) {
        return {reinterpret_cast<char const*>(proto), len};
    }
    return "";
}

void
TlsConnection::setSni(std::string const& hostname)
{
    SSL_set_tlsext_host_name(ssl_, hostname.c_str());
}

void
TlsConnection::setAlpn(std::vector<uint8_t> const& protos)
{
    if (isServer_) {
        // Server-side: set the ALPN selection callback on the SSL_CTX
        // We store protos and use a static callback
        SSL_CTX* ctx = SSL_get_SSL_CTX(ssl_);
        // Copy protos into a buffer that outlives this call
        auto* protoCopy = new std::vector<uint8_t>(protos);
        SSL_CTX_set_alpn_select_cb(
            ctx,
            [](SSL*, unsigned char const** out, unsigned char* outlen,
               unsigned char const* in, unsigned int inlen, void* arg) -> int
            {
                auto* server_protos = static_cast<std::vector<uint8_t>*>(arg);
                if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen,
                                          server_protos->data(),
                                          static_cast<unsigned int>(server_protos->size()),
                                          in, inlen) == OPENSSL_NPN_NEGOTIATED) {
                    return SSL_TLSEXT_ERR_OK;
                }
                return SSL_TLSEXT_ERR_NOACK;
            },
            protoCopy);
    } else {
        // Client-side: advertise protocols
        SSL_set_alpn_protos(ssl_, protos.data(), static_cast<unsigned int>(protos.size()));
    }
}

void
TlsConnection::startHandshake(Callback onComplete, ErrorCallback onError)
{
    onHandshakeComplete_ = std::move(onComplete);
    onHandshakeError_ = std::move(onError);
    continueHandshake();
}

void
TlsConnection::continueHandshake()
{
    if (closed_)
        return;

    int rc = SSL_do_handshake(ssl_);
    if (rc == 1) {
        // Handshake complete
        handshakeDone_ = true;
        Logger::debug("TLS handshake complete ({})", isServer_ ? "server" : "client");
        if (onHandshakeComplete_)
            onHandshakeComplete_();
        return;
    }

    int err = SSL_get_error(ssl_, rc);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        watchForSsl(err);
        return;
    }

    // Handshake failed. Close BEFORE calling the error callback to prevent
    // re-entry: the callback may destroy resources, and pending events in
    // the event loop could call handle_io() again on freed memory.
    unsigned long e = ERR_get_error();
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    Logger::warn("TLS handshake failed: {}", buf);

    close(); // removes fd from event loop, prevents re-entry

    auto errCb = std::move(onHandshakeError_); // move out to prevent double-call
    if (errCb) {
        errCb(Error{ ErrorCode::TlsHandshakeFailed, std::string(buf) });
    }
}

void
TlsConnection::startRead()
{
    if (closed_ || !handshakeDone_)
        return;

    reading_ = true;
    // Watch for readable
    auto self = shared_from_this();
    loop_.watch(socket_.fd(), static_cast<uint32_t>(IoEvent::Readable),
                [self](uint32_t events) { self->handleIo(events); });
}

void
TlsConnection::write(Buffer& data)
{
    if (closed_)
        return;

    writeBuf_.move(data);
    doWrite();
}

void
TlsConnection::write(std::string_view data)
{
    if (closed_)
        return;

    writeBuf_.add(data);
    doWrite();
}

void
TlsConnection::close()
{
    if (closed_)
        return;

    closed_ = true;
    loop_.remove(socket_.fd());
    if (ssl_ && handshakeDone_) {
        SSL_shutdown(ssl_);
    }
    socket_.close();
}

void
TlsConnection::handleIo([[maybe_unused]] uint32_t events)
{
    if (closed_)
        return;

    if (!handshakeDone_) {
        continueHandshake();
        return;
    }

    if (reading_)
        doRead();
    if (!writeBuf_.empty())
        doWrite();
}

void
TlsConnection::doRead()
{
    if (closed_)
        return;

    constexpr size_t kBufSize = 16384;
    std::byte buf[kBufSize];

    while (true) {
        int n = SSL_read(ssl_, buf, static_cast<int>(kBufSize));
        if (n > 0) {
            Buffer readBuf;
            readBuf.add(std::span<std::byte const>{buf, static_cast<size_t>(n)});
            if (onData_)
                onData_(readBuf);
            continue;
        }

        int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_WANT_READ) {
            break; // No more data
        }
        if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL) {
            close();
            if (onClose_)
                onClose_();
            return;
        }

        // Error
        close();
        if (onErrorCb_) {
            onErrorCb_(Error{ErrorCode::ReadError, "SSL_read error"});
        }
        return;
    }
}

void
TlsConnection::doWrite()
{
    if (closed_)
        return;

    while (!writeBuf_.empty()) {
        auto slices = writeBuf_.slices();
        if (slices.empty())
            break;

        auto data = slices.front().data();
        if (data.empty())
            break;

        int n = SSL_write(ssl_, data.data(), static_cast<int>(data.size()));
        if (n > 0) {
            writeBuf_.drain(static_cast<size_t>(n));
            continue;
        }

        int err = SSL_get_error(ssl_, n);
        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            watchForSsl(err);
            return;
        }

        close();
        if (onErrorCb_) {
            onErrorCb_(Error{ErrorCode::WriteError, "SSL_write error"});
        }
        return;
    }

    // All data written — update event watch
    if (reading_) {
        auto self = shared_from_this();
        loop_.watch(socket_.fd(), static_cast<uint32_t>(IoEvent::Readable),
                    [self](uint32_t events) { self->handleIo(events); });
    }
}

void
TlsConnection::watchForSsl(int ssl_err)
{
    uint32_t events = 0;
    if (ssl_err == SSL_ERROR_WANT_READ)
        events = static_cast<uint32_t>(IoEvent::Readable);
    if (ssl_err == SSL_ERROR_WANT_WRITE)
        events = static_cast<uint32_t>(IoEvent::Writable);
    if (reading_)
        events |= static_cast<uint32_t>(IoEvent::Readable);

    auto self = shared_from_this();
    loop_.watch(socket_.fd(), events, [self](uint32_t ev) { self->handleIo(ev); });
}

} // namespace DeepSeer
