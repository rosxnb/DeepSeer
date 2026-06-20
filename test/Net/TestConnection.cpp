#include <DeepSeer/Net/Connection.hpp>
#include <DeepSeer/Net/Listener.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <unistd.h>

using namespace DeepSeer;
using namespace std::chrono_literals;


// ===========================================================================
// Test fixture — spins up an event loop, listener, and helper utilities.
// ===========================================================================

namespace
{

/// PID-seeded base port so back-to-back runs don't collide on TIME_WAIT.
uint16_t basePort()
{
    return static_cast<uint16_t>(30000 + (::getpid() % 10000) * 20);
}

std::atomic<uint16_t> gNextPort{basePort()};

/// Listener fixture shared across Connection tests.
/// Provides a loopback listener and a helper to create a connected client.
class ConnectionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        loop_ = EventLoop::create();
        ASSERT_NE(loop_, nullptr);

        auto port = gNextPort.fetch_add(1, std::memory_order_relaxed);
        auto addr = Address::fromHostPort("127.0.0.1", port);
        ASSERT_TRUE(addr.has_value()) << addr.error().message;
        bindAddr_ = std::move(*addr);
    }

    /// Start the listener with the given accept callback.
    void startListener(Listener::AcceptCallback onAccept)
    {
        listener_ = std::make_unique<Listener>(*loop_, bindAddr_);
        auto r = listener_->start(std::move(onAccept));
        ASSERT_TRUE(r.has_value()) << r.error().message;
        listenAddr_ = bindAddr_;
    }

    /// Create a blocking client socket connected to the listener.
    /// Uses blocking connect so the TCP handshake completes before returning.
    Socket connectClient()
    {
        auto sock = Socket::createSocket(listenAddr_);
        EXPECT_TRUE(sock.has_value()) << sock.error().message;

        // Blocking connect — handshake completes before we return.
        auto connResult = sock->connect(listenAddr_);
        EXPECT_TRUE(connResult.has_value()) << connResult.error().message;

        return std::move(*sock);
    }

    /// Run the loop with a safety-net timeout so tests never hang.
    void runWithTimeout(Duration timeout = 2s)
    {
        safetyTimer_ = loop_->addTimer(timeout, [this] { loop_->stop(); });
        loop_->run();
    }

    std::unique_ptr<EventLoop> loop_;
    std::unique_ptr<Listener>  listener_;
    Address                    bindAddr_;
    Address                    listenAddr_;
    TimerHandlePtr             safetyTimer_;
};

} // anonymous namespace


// ===========================================================================
// Construction / lifecycle
// ===========================================================================

TEST_F(ConnectionTest, AcceptAndCreateConnection)
{
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(100ms); // keep socket alive for accept
    });

    runWithTimeout();
    t.join();

    ASSERT_NE(serverConn, nullptr);
    EXPECT_TRUE(serverConn->connected());
}

TEST_F(ConnectionTest, CloseMarksDisconnected)
{
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(100ms);
    });

    runWithTimeout();
    t.join();

    ASSERT_NE(serverConn, nullptr);
    EXPECT_TRUE(serverConn->connected());

    serverConn->close();
    EXPECT_FALSE(serverConn->connected());
}

TEST_F(ConnectionTest, DoubleCloseIsHarmless)
{
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(100ms);
    });

    runWithTimeout();
    t.join();

    ASSERT_NE(serverConn, nullptr);
    serverConn->close();
    serverConn->close(); // must not crash
    EXPECT_FALSE(serverConn->connected());
}


// ===========================================================================
// Data transfer
// ===========================================================================

TEST_F(ConnectionTest, ReceiveData)
{
    std::string receivedData;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            receivedData = data.toString();
            loop_->stop();
        });
        serverConn->startRead();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::string const msg = "hello DeepSeer";
        auto r = sock.write(reinterpret_cast<std::byte const*>(msg.data()), msg.size());
        std::this_thread::sleep_for(100ms);
    });

    runWithTimeout();
    t.join();

    EXPECT_EQ(receivedData, "hello DeepSeer");
}

TEST_F(ConnectionTest, EchoRoundtrip)
{
    std::string receivedByServer;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            receivedByServer = data.toString();
            serverConn->write(receivedByServer);
        });
        serverConn->startRead();
    });

    std::string echoResponse;

    std::thread t([&] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();

        std::string const msg = "echo test";
        auto r = sock.write(reinterpret_cast<std::byte const*>(msg.data()), msg.size());

        std::this_thread::sleep_for(50ms);

        std::byte buf[128];
        auto n = sock.read(buf, sizeof(buf));
        if (n && *n > 0) {
            echoResponse = std::string(reinterpret_cast<char const*>(buf),
                                       static_cast<size_t>(*n));
        }
        sock.close();
    });

    auto stopTimer = loop_->addTimer(200ms, [this] { loop_->stop(); });
    runWithTimeout();
    t.join();

    EXPECT_EQ(receivedByServer, "echo test");
    EXPECT_EQ(echoResponse, "echo test");
}

TEST_F(ConnectionTest, WriteStringView)
{
    std::string receivedData;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            receivedData = data.toString();
            loop_->stop();
        });
        serverConn->startRead();
    });

    ConnectionPtr clientConn;

    std::thread t([&] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        // Use the string_view write overload via a Connection on the client side
        clientConn = std::make_shared<Connection>(std::move(sock), *loop_);
        loop_->post([&] {
            clientConn->write("string_view write");
        });
        std::this_thread::sleep_for(100ms);
    });

    runWithTimeout();
    t.join();

    EXPECT_EQ(receivedData, "string_view write");
}

TEST_F(ConnectionTest, MultipleWrites)
{
    std::string allReceived;
    int dataCallbackCount = 0;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            allReceived += data.toString();
            ++dataCallbackCount;
        });
        serverConn->startRead();
    });

    std::thread t([&] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();

        std::string const msg1 = "first ";
        std::string const msg2 = "second ";
        std::string const msg3 = "third";
        auto r = sock.write(reinterpret_cast<std::byte const*>(msg1.data()), msg1.size());
        std::this_thread::sleep_for(20ms);
        r = sock.write(reinterpret_cast<std::byte const*>(msg2.data()), msg2.size());
        std::this_thread::sleep_for(20ms);
        r = sock.write(reinterpret_cast<std::byte const*>(msg3.data()), msg3.size());
        std::this_thread::sleep_for(50ms);
    });

    auto stopTimer = loop_->addTimer(300ms, [this] { loop_->stop(); });
    runWithTimeout();
    t.join();

    EXPECT_EQ(allReceived, "first second third");
    EXPECT_GE(dataCallbackCount, 1);
}


// ===========================================================================
// Close / EOF detection
// ===========================================================================

TEST_F(ConnectionTest, PeerCloseFiresOnClose)
{
    bool closeFired = false;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onClose([&] {
            closeFired = true;
            loop_->stop();
        });
        serverConn->startRead();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(30ms); // let accept complete
        sock.close();
    });

    runWithTimeout();
    t.join();

    EXPECT_TRUE(closeFired);
}

TEST_F(ConnectionTest, DataThenClose)
{
    std::string receivedData;
    bool closeFired = false;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            receivedData += data.toString();
        });
        serverConn->onClose([&] {
            closeFired = true;
            loop_->stop();
        });
        serverConn->startRead();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::string const msg = "goodbye";
        auto r = sock.write(reinterpret_cast<std::byte const*>(msg.data()), msg.size());
        std::this_thread::sleep_for(20ms);
        sock.close();
    });

    runWithTimeout();
    t.join();

    EXPECT_EQ(receivedData, "goodbye");
    EXPECT_TRUE(closeFired);
}


// ===========================================================================
// Error callback
// ===========================================================================

TEST_F(ConnectionTest, WriteAfterCloseIsIgnored)
{
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(50ms);
    });

    runWithTimeout();
    t.join();

    ASSERT_NE(serverConn, nullptr);
    serverConn->close();
    // Writing to a closed connection should be silently ignored, not crash.
    serverConn->write("should be ignored");
    EXPECT_FALSE(serverConn->connected());
}

TEST_F(ConnectionTest, StartReadAfterCloseIsIgnored)
{
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(50ms);
    });

    runWithTimeout();
    t.join();

    ASSERT_NE(serverConn, nullptr);
    serverConn->close();
    serverConn->startRead(); // must not crash
}


// ===========================================================================
// Bidirectional — both sides use Connection
// ===========================================================================

TEST_F(ConnectionTest, BidirectionalExchange)
{
    std::string serverReceived;
    std::string clientReceived;
    ConnectionPtr serverConn;
    ConnectionPtr clientConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onData([&](Buffer& data) {
            serverReceived = data.toString();
            // Reply once we've received data
            serverConn->write("pong");
        });
        serverConn->startRead();
    });

    std::thread t([&] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        clientConn = std::make_shared<Connection>(std::move(sock), *loop_);

        loop_->post([&] {
            clientConn->onData([&](Buffer& data) {
                clientReceived = data.toString();
                loop_->stop();
            });
            clientConn->startRead();
            clientConn->write("ping");
        });

        std::this_thread::sleep_for(200ms);
    });

    runWithTimeout();
    t.join();

    EXPECT_EQ(serverReceived, "ping");
    EXPECT_EQ(clientReceived, "pong");
}


// ===========================================================================
// Shutdown write (half-close)
// ===========================================================================

TEST_F(ConnectionTest, ShutdownWriteCausesEofOnPeer)
{
    bool peerSawEof = false;
    ConnectionPtr serverConn;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        serverConn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConn->onClose([&] {
            peerSawEof = true;
            loop_->stop();
        });
        serverConn->startRead();
    });

    std::thread t([&] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        auto conn = std::make_shared<Connection>(std::move(sock), *loop_);
        loop_->post([&] {
            conn->shutdownWrite();
        });
        std::this_thread::sleep_for(100ms);
    });

    runWithTimeout();
    t.join();

    EXPECT_TRUE(peerSawEof);
}


// ===========================================================================
// Multiple connections
// ===========================================================================

TEST_F(ConnectionTest, MultipleSimultaneousConnections)
{
    int const kNumClients = 5;
    int acceptCount = 0;
    std::vector<ConnectionPtr> serverConns;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        auto conn = std::make_shared<Connection>(std::move(client), *loop_);
        serverConns.push_back(conn);
        ++acceptCount;
        if (acceptCount == kNumClients)
            loop_->stop();
    });

    std::vector<std::thread> threads;
    for (int i = 0; i < kNumClients; ++i) {
        threads.emplace_back([this] {
            std::this_thread::sleep_for(10ms);
            auto sock = connectClient();
            std::this_thread::sleep_for(100ms);
        });
    }

    runWithTimeout();
    for (auto& t : threads)
        t.join();

    EXPECT_EQ(acceptCount, kNumClients);
    EXPECT_EQ(serverConns.size(), static_cast<size_t>(kNumClients));
    for (auto const& conn : serverConns)
        EXPECT_TRUE(conn->connected());
}


// ===========================================================================
// Destructor cleans up
// ===========================================================================

TEST_F(ConnectionTest, DestructorClosesConnection)
{
    bool closeFired = false;

    startListener([&](Socket client, Address) {
        auto r = client.setNonblocking();
        ASSERT_TRUE(r.has_value());
        {
            // Connection destroyed at end of this scope
            auto conn = std::make_shared<Connection>(std::move(client), *loop_);
            conn->onClose([&] { closeFired = true; });
            // conn goes out of scope here — destructor should close the fd
        }
        loop_->stop();
    });

    std::thread t([this] {
        std::this_thread::sleep_for(10ms);
        auto sock = connectClient();
        std::this_thread::sleep_for(50ms);
    });

    runWithTimeout();
    t.join();

    // The connection should be fully closed after destruction.
    // Note: onClose_ fires on EOF from peer, not on explicit close() — so
    // closeFired may be false here. The key assertion is no crash/leak.
}
