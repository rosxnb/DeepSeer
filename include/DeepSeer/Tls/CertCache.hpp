#pragma once

/// @file CertCache.hpp
/// @brief Thread-safe LRU cache for forged TLS certificates.
///
/// ## Purpose
///
/// Generating an RSA key + X.509 certificate for every HTTPS connection is
/// expensive (~2ms). The CertCache maps hostnames to pre-built SSL_CTX objects
/// (which contain the forged cert + key), avoiding regeneration for repeat visits.
///
/// ## Design
///
/// - **LRU eviction**: A doubly-linked list (std::list) orders entries by
///   recency. get() promotes to front; put() inserts at front and evicts from
///   back when at capacity.
/// - **O(1) lookup**: An unordered_map maps hostnames to list iterators.
/// - **SSL_CTX reference counting**: OpenSSL's SSL_CTX is reference-counted.
///   put() calls SSL_CTX_up_ref(); eviction calls SSL_CTX_free(). This means
///   active connections continue to work even after their entry is evicted.
///
/// ## Thread Safety
///
/// All public methods are protected by a mutex. Safe to call from any thread.
/// In practice, accessed from the event loop thread during CONNECT handling.

#include <openssl/ssl.h>

#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace DeepSeer
{

/// Thread-safe LRU cache mapping hostname to SSL_CTX.
class CertCache
{
public:
    /// @param maxEntries Maximum cached hostnames before LRU eviction.
    explicit CertCache(size_t maxEntries = 1024);
    ~CertCache();

    /// Look up a cached SSL_CTX for given hostname. Returns `nullptr` on miss.
    /// Promotes the entry to MRU on hit.
    SSL_CTX* get(std::string const& hostname);

    /// Cache an SSL_CTX for given hostname. Increments the SSL_CTX refcount.
    /// Evicts the LRU entry if at capacity.
    void put(std::string const& hostname, SSL_CTX* ctx);

    size_t size() const;

private:
    struct Entry
    {
        std::string hostname;
        SSL_CTX* ctx; // reference counted by OpenSSL
    };

    size_t                                                      maxEntries_;
    mutable std::mutex                                          mu_;
    std::list<Entry>                                            order_; // MRU at front
    std::unordered_map<std::string, std::list<Entry>::iterator> map_;
};

} // namespace DeepSeer
