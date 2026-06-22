#pragma once

#include <DeepSeer/Http/HeaderMap.hpp>

#include <cstdint>
#include <string>

namespace DeepSeer
{

enum class HttpMethod : uint8_t
{
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS,
    CONNECT,
    TRACE,
    UNKNOWN,
};

inline std::string_view methodToString(HttpMethod m)
{
    switch (m) {
    case HttpMethod::GET:       return "GET";
    case HttpMethod::POST:      return "POST";
    case HttpMethod::PUT:       return "PUT";
    case HttpMethod::DELETE:    return "DELETE";
    case HttpMethod::PATCH:     return "PATCH";
    case HttpMethod::HEAD:      return "HEAD";
    case HttpMethod::OPTIONS:   return "OPTIONS";
    case HttpMethod::CONNECT:   return "CONNECT";
    case HttpMethod::TRACE:     return "TRACE";
    case HttpMethod::UNKNOWN:   return "UNKNOWN";
    }
    return "UNKNOWN";
}

inline HttpMethod stringToMethod(std::string_view s)
{

    if (s == "GET")     return HttpMethod::GET;
    if (s == "POST")	return HttpMethod::POST;
    if (s == "PUT")	    return HttpMethod::PUT;
    if (s == "DELETE")	return HttpMethod::DELETE;
    if (s == "PATCH")	return HttpMethod::PATCH;
    if (s == "HEAD")	return HttpMethod::HEAD;
    if (s == "OPTIONS")	return HttpMethod::OPTIONS;
    if (s == "CONNECT")	return HttpMethod::CONNECT;
    if (s == "TRACE")	return HttpMethod::TRACE;
    if (s == "UNKNOWN")	return HttpMethod::UNKNOWN;
    return HttpMethod::UNKNOWN;
};

struct HttpRequest
{
    HttpMethod method = HttpMethod::UNKNOWN;
    std::string url;
    std::string host;
    uint16_t port = 80;
    uint8_t versionMajor = 1;
    uint8_t versionMinor = 1;
    HeaderMap headers;
    bool hasBody = false;
};

struct HttpResponse
{
    uint16_t statusCode = 0;
    std::string reason;
    uint8_t versionMajor = 1;
    uint8_t versionMinor = 1;
    HeaderMap headers;
    bool hasBody = false;
};

} // DeepSeer
