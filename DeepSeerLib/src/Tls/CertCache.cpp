#include <DeepSeer/Tls/CertCache.hpp>

namespace DeepSeer
{

CertCache::CertCache(size_t maxEntries)
    : maxEntries_{maxEntries}
{ }

CertCache::~CertCache()
{
    for (auto& entry: order_) {
        SSL_CTX_free(entry.ctx);
    }
}

SSL_CTX*
CertCache::get(std::string const& hostname)
{
    std::lock_guard lock{mu_};

    auto it = map_.find(hostname);
    if (it == map_.end())
        return nullptr;

    // Move to front (MRU)
    order_.splice(order_.begin(), order_, it->second);
    return it->second->ctx;
}

void
CertCache::put(std::string const& hostname, SSL_CTX* ctx)
{
    std::lock_guard lock{mu_};

    auto it = map_.find(hostname);
    if (it != map_.end()) {
        // Update existing entry
        SSL_CTX_free(it->second->ctx);
        it->second->ctx = ctx;
        SSL_CTX_up_ref(ctx);
        order_.splice(order_.begin(), order_, it->second);
        return;
    }

    // Evict LRU if at capacity
    if (order_.size() >= maxEntries_) {
        auto& back = order_.back();
        map_.erase(back.hostname);
        SSL_CTX_free(back.ctx);
        order_.pop_back();
    }

    // Insert new entry at front
    SSL_CTX_up_ref(ctx);
    order_.push_front({hostname, ctx});
    map_[hostname] = order_.begin();
}

size_t
CertCache::size() const
{
    std::lock_guard lock{mu_};
    return order_.size();
}

} // namespace DeepSeer
