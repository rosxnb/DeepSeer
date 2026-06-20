#include <DeepSeer/Http/Http1/Codec.hpp>

#include <gtest/gtest.h>

using namespace DeepSeer;

TEST(Http1CodecTest, ParseSimpleGetRequest)
{
    Http1Codec codec(Http1Codec::Type::Request);

    HttpRequest parsedReq;
    codec.setCallbacks({
        .onRequest = [&](HttpRequest req) { parsedReq = std::move(req); },
    });

    std::string raw = "GET /index.html HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Accept: text/html\r\n"
                      "\r\n";

    auto result = codec.decode(std::span{reinterpret_cast<std::byte const*>(raw.data()), raw.size()});
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(parsedReq.method, HttpMethod::GET);
    EXPECT_EQ(parsedReq.url, "/index.html");
    EXPECT_EQ(parsedReq.host, "example.com");
    EXPECT_EQ(parsedReq.versionMajor, 1);
    EXPECT_EQ(parsedReq.versionMinor, 1);
    EXPECT_TRUE(parsedReq.headers.has("Host"));
    EXPECT_EQ(parsedReq.headers.get("Accept").value(), "text/html");
}

TEST(Http1CodecTest, ParsePostWithBody)
{
    Http1Codec codec(Http1Codec::Type::Request);

    HttpRequest parsedReq;
    std::string body_data;
    bool complete = false;

    codec.setCallbacks({
        .onRequest = [&](HttpRequest req) { parsedReq = std::move(req); },
        .onBody =
            [&](Buffer& buf, [[maybe_unused]] bool end_stream) {
                body_data += buf.toString();
            },
        .onMessageComplete = [&]() { complete = true; },
    });

    std::string raw = "POST /api/data HTTP/1.1\r\n"
                      "Host: api.example.com\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 13\r\n"
                      "\r\n"
                      "{\"key\":\"val\"}";

    auto result = codec.decode(std::span{reinterpret_cast<std::byte const*>(raw.data()), raw.size()});
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(parsedReq.method, HttpMethod::POST);
    EXPECT_EQ(parsedReq.host, "api.example.com");
    EXPECT_EQ(parsedReq.headers.contentType().value(), "application/json");
    EXPECT_EQ(body_data, "{\"key\":\"val\"}");
    EXPECT_TRUE(complete);
}

TEST(Http1CodecTest, ParseResponse)
{
    Http1Codec codec(Http1Codec::Type::Response);

    HttpResponse parsedResp;
    std::string bodyData;

    codec.setCallbacks({
        .onResponse = [&](HttpResponse resp) { parsedResp = std::move(resp); },
        .onBody =
            [&](Buffer& buf, bool) {
                bodyData += buf.toString();
            },
    });

    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";

    auto result = codec.decode(std::span{reinterpret_cast<std::byte const*>(raw.data()), raw.size()});
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(parsedResp.statusCode, 200);
    EXPECT_EQ(parsedResp.reason, "OK");
    EXPECT_EQ(bodyData, "hello");
}

TEST(Http1CodecTest, ParseConnectRequest)
{
    Http1Codec codec(Http1Codec::Type::Request);

    HttpRequest parsedReq;
    codec.setCallbacks({
        .onRequest = [&](HttpRequest req) { parsedReq = std::move(req); },
    });

    std::string raw = "CONNECT example.com:443 HTTP/1.1\r\n"
                      "Host: example.com:443\r\n"
                      "\r\n";

    auto result = codec.decode(std::span{reinterpret_cast<std::byte const*>(raw.data()), raw.size()});
    ASSERT_TRUE(result.has_value()) << result.error().message;

    EXPECT_EQ(parsedReq.method, HttpMethod::CONNECT);
    EXPECT_EQ(parsedReq.url, "example.com:443");
    EXPECT_EQ(parsedReq.host, "example.com");
    EXPECT_EQ(parsedReq.port, 443);
}

TEST(Http1CodecTest, EncodeRequest)
{
    Http1Codec codec;

    HttpRequest req;
    req.method = HttpMethod::GET;
    req.url = "/path";
    req.versionMajor = 1;
    req.versionMinor = 1;
    req.headers.set("Host", "example.com");
    req.headers.set("Accept", "*/*");

    Buffer out;
    codec.encodeRequest(req, out);
    auto encoded = out.toString();

    EXPECT_NE(encoded.find("GET /path HTTP/1.1\r\n"), std::string::npos);
    EXPECT_NE(encoded.find("Host: example.com\r\n"), std::string::npos);
    EXPECT_NE(encoded.find("\r\n\r\n"), std::string::npos);
}

TEST(Http1CodecTest, EncodeResponse)
{
    Http1Codec codec;

    HttpResponse resp;
    resp.statusCode = 200;
    resp.reason = "OK";
    resp.headers.set("Content-Length", "0");

    Buffer out;
    codec.encodeResponse(resp, out);
    auto encoded = out.toString();

    EXPECT_NE(encoded.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(encoded.find("Content-Length: 0\r\n"), std::string::npos);
}

TEST(Http1CodecTest, CaseInsensitiveHeaders)
{
    Http1Codec codec(Http1Codec::Type::Request);

    HttpRequest parsedReq;
    codec.setCallbacks({
        .onRequest = [&](HttpRequest req) { parsedReq = std::move(req); },
    });

    std::string raw = "GET / HTTP/1.1\r\n"
                      "host: example.com\r\n"
                      "content-type: text/html\r\n"
                      "\r\n";

    auto result = codec.decode(std::span{reinterpret_cast<std::byte const*>(raw.data()), raw.size()});
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(parsedReq.headers.get("Host").value(), "example.com");
    EXPECT_EQ(parsedReq.headers.get("CONTENT-TYPE").value(), "text/html");
}
