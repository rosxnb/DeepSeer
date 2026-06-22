#include <DeepSeer/Http/Http1/Codec.hpp>

#include <format>

namespace DeepSeer
{

Http1Codec::Http1Codec(Type type)
{
    llhttp_settings_init(&settings_);
    settings_.on_message_begin = onMessageBegin;
    settings_.on_url = onUrl;
    settings_.on_status = onStatus;
    settings_.on_header_field = onHeaderField;
    settings_.on_header_value = onHeaderValue;
    settings_.on_headers_complete = onHeadersComplete;
    settings_.on_body = onBody;
    settings_.on_message_complete = onMessageComplete;

    llhttp_type_t parserType;
    switch (type) {
    case Type::Request:  parserType = HTTP_REQUEST; break;
    case Type::Response: parserType = HTTP_RESPONSE; break;
    case Type::Both:     parserType = HTTP_BOTH; break;
    }

    llhttp_init(&parser_, parserType, &settings_);
    parser_.data = this;
}

Http1Codec::Http1Codec(Http1Codec&& other) noexcept
    : parser_(other.parser_)
    , settings_(other.settings_)
    , callbacks_(std::move(other.callbacks_))
    , currentRequest_(std::move(other.currentRequest_))
    , currentResponse_(std::move(other.currentResponse_))
    , currentHeaderField_(std::move(other.currentHeaderField_))
    , currentHeaderValue_(std::move(other.currentHeaderValue_))
    , buildingRequest_(other.buildingRequest_)
    , inHeaderValue_(other.inHeaderValue_)
{
    parser_.data = this;
    parser_.settings = &settings_;
}

Http1Codec&
Http1Codec::operator=(Http1Codec&& other) noexcept
{
    Http1Codec{std::move(other)}.swap(*this);
    return *this;
}

void
Http1Codec::swap(Http1Codec& other) noexcept
{
    std::swap(parser_, other.parser_);
    std::swap(settings_, other.settings_);
    std::swap(callbacks_, other.callbacks_);
    std::swap(currentRequest_, other.currentRequest_);
    std::swap(currentResponse_, other.currentResponse_);
    std::swap(currentHeaderField_, other.currentHeaderField_);
    std::swap(currentHeaderValue_, other.currentHeaderValue_);
    std::swap(buildingRequest_, other.buildingRequest_);
    std::swap(inHeaderValue_, other.inHeaderValue_);

    // Fix up internal pointers after swap
    parser_.data = this;
    parser_.settings = &settings_;
    other.parser_.data = &other;
    other.parser_.settings = &other.settings_;
}

VoidResult
Http1Codec::decode(std::span<std::byte const> data)
{
    auto err = llhttp_execute(&parser_, reinterpret_cast<char const*>(data.data()), data.size());
    if (err != HPE_OK && err != HPE_PAUSED && err != HPE_PAUSED_UPGRADE) {
        return std::unexpected(
            Error{ ErrorCode::ParseError,
                   std::format("HTTP parse error: {} ({})", llhttp_errno_name(err),
                               llhttp_get_error_reason(&parser_)) });
    }
    return {};
}

void
Http1Codec::encodeRequest(HttpRequest const& req, Buffer& out)
{
    std::string line = std::format("{} {} HTTP/{}.{}\r\n",
                                   methodToString(req.method),
                                   req.url,
                                   req.versionMajor,
                                   req.versionMinor);
    out.add(line);

    for (auto const& [name, value] : req.headers) {
        out.add(std::format("{}: {}\r\n", name, value));
    }
    out.add("\r\n");
}

void
Http1Codec::encodeResponse(HttpResponse const& resp, Buffer& out)
{
    std::string line = std::format("HTTP/{}.{} {} {}\r\n",
                                   resp.versionMajor,
                                   resp.versionMinor,
                                   resp.statusCode,
                                   resp.reason);
    out.add(line);

    for (auto const& [name, value] : resp.headers) {
        out.add(std::format("{}: {}\r\n", name, value));
    }
    out.add("\r\n");
}

void
Http1Codec::encodeBody(std::span<std::byte const> data, Buffer& out)
{
    out.add(data);
}


void
Http1Codec::finishHeader()
{
    if (currentHeaderField_.empty())
        return;

    if (parser_.type == HTTP_REQUEST || (parser_.type == HTTP_BOTH && buildingRequest_)) {
        currentRequest_.headers.set(std::move(currentHeaderField_),
                                    std::move(currentHeaderValue_));
    } else {
        currentResponse_.headers.set(std::move(currentHeaderField_),
                                     std::move(currentHeaderValue_));
    }
    currentHeaderField_.clear();
    currentHeaderValue_.clear();
}

void
Http1Codec::parseHostFromUrl(std::string const& url)
{
    // Handle absolute URLs: http://host:port/path
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return;

    auto hostStart = schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);
    auto hostPart = (pathStart != std::string::npos)
                        ? url.substr(hostStart, pathStart - hostStart)
                        : url.substr(hostStart);

    auto colon = hostPart.find(':');
    if (colon != std::string::npos) {
        currentRequest_.host = hostPart.substr(0, colon);
        currentRequest_.port = static_cast<uint16_t>(std::stoi(hostPart.substr(colon + 1)));
    } else {
        currentRequest_.host = hostPart;
    }
}

// ---------------------------------------------------------------------------
// llhttp callbacks
// ---------------------------------------------------------------------------

int
Http1Codec::onMessageBegin(llhttp_t* parser)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    self->currentRequest_ = {};
    self->currentResponse_ = {};
    self->currentHeaderField_.clear();
    self->currentHeaderValue_.clear();
    self->inHeaderValue_ = false;
    return 0;
}

int
Http1Codec::onUrl(llhttp_t* parser, char const* at, size_t length)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    self->currentRequest_.url.append(at, length);
    return 0;
}

int
Http1Codec::onStatus(llhttp_t* parser, char const* at, size_t length)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    self->currentResponse_.reason.append(at, length);
    return 0;
}

int
Http1Codec::onHeaderField(llhttp_t* parser, char const* at, size_t length)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    if (self->inHeaderValue_) {
        self->finishHeader();
    }
    self->currentHeaderField_.append(at, length);
    self->inHeaderValue_ = false;
    return 0;

}

int
Http1Codec::onHeaderValue(llhttp_t* parser, char const* at, size_t length)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    self->currentHeaderValue_.append(at, length);
    self->inHeaderValue_ = true;
    return 0;
}

int
Http1Codec::onHeadersComplete(llhttp_t* parser)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    self->finishHeader();

    if (parser->type == HTTP_REQUEST) {
        self->buildingRequest_ = true;
        auto& req = self->currentRequest_;
        req.method = stringToMethod(
            std::string_view(llhttp_method_name(static_cast<llhttp_method_t>(parser->method))));
        req.versionMajor = static_cast<uint8_t>(parser->http_major);
        req.versionMinor = static_cast<uint8_t>(parser->http_minor);
        req.hasBody = (parser->content_length > 0 || (parser->flags & F_CHUNKED) != 0);

        // Extract host from Host header or URL
        if (auto host = req.headers.get("Host")) {
            auto h = std::string(*host);
            auto colon = h.find(':');
            if (colon != std::string::npos) {
                req.host = h.substr(0, colon);
                req.port = static_cast<uint16_t>(std::stoi(h.substr(colon + 1)));
            } else {
                req.host = h;
            }
        }
        if (req.host.empty()) {
            self->parseHostFromUrl(req.url);
        }

        if (self->callbacks_.onRequest) {
            self->callbacks_.onRequest(req);
        }
    } else {
        self->buildingRequest_ = false;
        auto& resp = self->currentResponse_;
        resp.statusCode = static_cast<uint16_t>(parser->status_code);
        resp.versionMajor = static_cast<uint8_t>(parser->http_major);
        resp.versionMinor = static_cast<uint8_t>(parser->http_minor);
        resp.hasBody = true;

        if (self->callbacks_.onResponse) {
            self->callbacks_.onResponse(resp);
        }
    }

    return 0;
}

int
Http1Codec::onBody(llhttp_t* parser, char const* at, size_t length)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    if (self->callbacks_.onBody) {
        Buffer buf;
        buf.add(std::string_view{at, length});
        self->callbacks_.onBody(buf, false);
    }
    return 0;
}

int
Http1Codec::onMessageComplete(llhttp_t* parser)
{
    auto* self = static_cast<Http1Codec*>(parser->data);
    if (self->callbacks_.onBody) {
        Buffer buf;
        self->callbacks_.onBody(buf, true);
    }
    if (self->callbacks_.onMessageComplete) {
        self->callbacks_.onMessageComplete();
    }
    return 0;
}

} // DeepSeer
