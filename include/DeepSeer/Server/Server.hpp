#pragma once

/// @file Server.hpp
/// @brief Top-level DeepSeer MITM proxy server.
///
/// ## Responsibilities
///
/// - Accepts TCP connections via a Listener
/// - Creates a ProxySession per connection
/// - Optionally loads CA cert/key for TLS MITM
/// - Manages session lifecycle (cleanup of closed sessions)
/// - Signal-safe shutdown via stop()
///
/// ## Current Design (single-threaded)
///
/// The server runs on a single EventLoop. All connections share the same
/// thread. This is sufficient for development and moderate traffic.
///
/// ## Planned: Multi-threaded Worker Model (Envoy-inspired)
///
/// The intended architecture is one EventLoop per worker thread:
/// 1. Main thread: Listener accepts connections
/// 2. Main thread: round-robins each new connection to a Worker
/// 3. Worker thread: owns EventLoop + ProxySessions
/// 4. No shared mutable state on hot path
///
/// If not provided:
/// - CONNECT requests create plain TCP tunnels (no HTTPS inspection)
/// - HTTP proxy still works normally


#include <DeepSeer/Event/EventLoop.hpp>
#include <DeepSeer/Net/Address.hpp>
#include <DeepSeer/Net/Listener.hpp>
#include <DeepSeer/Proxy/ProxySession.hpp>
#include <DeepSeer/Server/Server.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DeepSeer
{

/// The DeepSeer MITM proxy server. Accepts connections and creates proxy sessions.
/// Currently single-threaded. Worker threads will be added later.
/// Callback type for payload inspection. The proxy fires this with captured
/// response bodies. Defined using only stdlib types — no Seer dependency.
using PayloadInspector = std::function<void(std::span<std::byte const> payload,
                                            std::string_view url)>;

class Server
{
public:
    /// @param listenAddr  Address to listen on (e.g., "0.0.0.0":8080).
    /// @param caCertPath Path to CA certificate PEM (empty = no MITM).
    /// @param caKeyPath  Path to CA private key PEM (empty = no MITM).
    explicit Server(Address listenAddr, std::string caCertPath = {}, std::string caKeyPath = {});

    /// Register a payload inspector callback (optional).
    void setPayloadInspector(PayloadInspector inspector);

    /// Start the server. Blocks until stop() is called.
    VoidResult run();

    /// Stop the server (thread-safe).
    void stop();

private:
    void onNewConnection(Socket client, Address addr);

    Address                                     listenAddr_;
    std::string                                 caCertPath_;
    std::string                                 caKeyPath_;
    std::unique_ptr<EventLoop>                  loop_;
    std::unique_ptr<Listener>                   listener_;
    std::optional<CertGenerator>                certGen_;
    CertCache                                   certCache_;
    std::vector<std::shared_ptr<ProxySession>>  sessions_;
    PayloadInspector                            payloadInspector_;
};

} // DeepSeer
