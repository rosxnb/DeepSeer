#pragma once

/// @file Codec.hpp
/// @brief Abstract HTTP codec interface for protocol-agnostic proxy logic.
///
/// ## Design (inspired by Envoy's Http::ServerConnection / Http::ClientConnection)
///
/// The proxy needs to parse HTTP from clients (requests) and from upstream
/// servers (responses), and encode HTTP in both directions. The HttpCodec
/// interface abstracts away the protocol version (HTTP/1.1 vs HTTP/2), so
/// ProxySession can operate protocol-agnostically.
///
/// ## Codec Lifecycle
///
/// 1. Create codec: `Http1Codec(Type::Request)` for client-side parsing.
/// 2. Set callbacks: `setCallbacks({.onRequest = ..., .onBody = ...})`.
/// 3. Feed raw bytes: `decode(dataSpan)` — fires callbacks as parsing progresses.
/// 4. Encode outbound: `encodeRequest(req, out)` / `encodeResponse(resp, out)`.
///
/// ## Callback Semantics
///
/// - `onRequest` / `onResponse`: Fired when all headers are parsed. The
///   message object is passed by value (caller owns it).
/// - `onBody`: Fired for each body chunk. `endStream=true` on the LAST chunk.
///   Also fired with an empty buffer + `end_stream=true` for message-complete.
/// - `onMessageComplete`: Fired after the entire message (headers + body).
///
/// ## The decode(Buffer&) Convenience
///
/// The base class provides `decode(Buffer& buf)` which linearizes the buffer
/// (needed because llhttp requires contiguous input), calls the virtual
/// `decode(dataSpan)`, and drains the buffer. This is O(n) due to the copy.
/// For streaming, feed data directly via `decode(dataSpan)`.

#include <DeepSeer/Core/Buffer.hpp>
#include <DeepSeer/Core/Types.hpp>
#include <DeepSeer/Http/Message.hpp>

#include <functional>

namespace DeepSeer
{

/// Callbacks fired by the codec during parsing.
/// Set via `HttpCodec::setCallbacks()`. All are optional — unset callbacks
/// are simply not invoked.
struct CodecCallbacks
{
    std::function<void(HttpRequest)>             onRequest = {};
    std::function<void(HttpResponse)>            onResponse = {};
    std::function<void(Buffer&, bool endStream)> onBody = {};
    std::function<void()>                        onMessageComplete = {};
};

class HttpCodec
{
public:
    virtual ~HttpCodec() = default;

    virtual void setCallbacks(CodecCallbacks cbs) = 0;

    /// Feed raw bytes for parsing. Fires callbacks as parsing progresses.
    virtual VoidResult decode(std::span<std::byte const> data) = 0;

    /// Convenience: linearize a Buffer, decode, and drain.
    VoidResult decode(Buffer& buf)
    {
        auto linear = buf.linearize();
        auto result = decode(linear);
        buf.drain(buf.length());
        return result;
    }

    /// Encode an HTTP request into raw bytes appended to `out`.
    virtual void encodeRequest(HttpRequest const& req, Buffer& out) = 0;

    /// Encode an HTTP response into raw bytes appended to `out`.
    virtual void encodeResponse(HttpResponse const& res, Buffer& out) = 0;

    /// Encode raw body data (appended directly — no framing for HTTP/1.1).
    virtual void encodeBody(std::span<std::byte const> data, Buffer& out) = 0;
};

} // DeepSeer
