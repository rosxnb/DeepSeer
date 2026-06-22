#pragma once

/// @file Http1/Codec.hpp
/// @brief HTTP/1.1 codec wrapping the llhttp parser.
///
/// ## Implementation
///
/// llhttp is a callback-based C parser from Node.js. We wrap it behind the
/// HttpCodec interface so ProxySession doesn't know about llhttp. The key
/// mapping:
///
/// | llhttp callback        | What we do                               |
/// |------------------------|------------------------------------------|
/// | on_message_begin       | Reset parse state                        |
/// | on_url                 | Accumulate into currentRequest_.url      |
/// | on_status              | Accumulate into currentResponse_.reason  |
/// | on_header_field/value  | Accumulate header name/value pairs       |
/// | on_headers_complete    | Emit onRequest or onResponse callback    |
/// | on_body                | Emit onBody callback with chunk          |
/// | on_message_complete    | Emit onMessageComplete callback          |
///
/// ## CONNECT Handling
///
/// llhttp pauses with `HPE_PAUSED_UPGRADE` when it sees a CONNECT method
/// (since CONNECT upgrades the connection to a tunnel). Our decode()
/// treats this as a valid result, not an error.
///
/// ## Host Extraction
///
/// After headersComplete, the codec extracts the target host from:
/// 1. The `Host` header (e.g., "Host: example.com:8080")
/// 2. The absolute URL (e.g., "GET http://example.com/path")
///
/// This populates `HttpRequest::host` and `HttpRequest::port` so the proxy
/// knows where to connect upstream.
///
/// ## Type Parameter
///
/// - `Type::Request`: Parse HTTP requests (client → proxy).
/// - `Type::Response`: Parse HTTP responses (upstream → proxy).
/// - `Type::Both`: Auto-detect (rarely needed).


#include <DeepSeer/Http/Codec.hpp>
#include <llhttp.h>

namespace DeepSeer
{

class Http1Codec : public HttpCodec
{
public:
    enum class Type { Request, Response, Both };

    explicit Http1Codec(Type type = Type::Both);

    Http1Codec(Http1Codec const&) = delete;
    Http1Codec& operator=(Http1Codec const&) = delete;

    Http1Codec(Http1Codec&& other) noexcept;
    Http1Codec& operator=(Http1Codec&& other) noexcept;
    void swap(Http1Codec& other) noexcept;

    void setCallbacks(CodecCallbacks cbs) override { callbacks_ = std::move(cbs); }

    using HttpCodec::decode; // Bring in decode(Buffer&) convenience overload
    VoidResult decode(std::span<std::byte const> data) override;

    void encodeRequest(HttpRequest const& req, Buffer& out) override;
    void encodeResponse(HttpResponse const& resp, Buffer& out) override;
    void encodeBody(std::span<std::byte const>, Buffer& out) override;

private:
    // llhttp callbacks (static, context via parser.data)
    static int onMessageBegin(llhttp_t* parser);
    static int onUrl(llhttp_t* parser, char const* at, size_t length);
    static int onStatus(llhttp_t* parser, char const* at, size_t length);
    static int onHeaderField(llhttp_t* parser, char const* at, size_t length);
    static int onHeaderValue(llhttp_t* parser, char const* at, size_t length);
    static int onHeadersComplete(llhttp_t* parser);
    static int onBody(llhttp_t* parser, char const* at, size_t length);
    static int onMessageComplete(llhttp_t* parser);

    void finishHeader();
    void parseHostFromUrl(std::string const& url);

    llhttp_t parser_;
    llhttp_settings_t settings_;
    CodecCallbacks callbacks_;

    // Parse state (reset on each new message)
    HttpRequest currentRequest_;
    HttpResponse currentResponse_;
    std::string currentHeaderField_;
    std::string currentHeaderValue_;
    bool buildingRequest_ = true; ///< true if parsing a request, false for response
    bool inHeaderValue_ = false; ///< true when accumulating a header value
};

} // DeepSeer
