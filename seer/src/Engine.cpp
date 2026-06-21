#include <Seer/Engine.hpp>

namespace Seer
{

Engine::~Engine()
{
    shutdown();
}

void
Engine::ensureWorker()
{
    if (!running_) {
        startWorker();
    }
}

void
Engine::startWorker()
{
    running_ = true;
    worker_ = std::jthread([this](std::stop_token) { workerLoop(); });
}

void
Engine::submit(std::span<std::byte const> payload,
               std::string url,
               ResultCallback onResult)
{
    {
        std::lock_guard lock(mutex_);
        ensureWorker();
        queue_.push_back(WorkItem{
            .payload  = std::vector<std::byte>(payload.begin(), payload.end()),
            .url      = std::move(url),
            .onResult = std::move(onResult),
        });
    }
    cv_.notify_one();
}

void
Engine::shutdown()
{
    {
        std::lock_guard lock(mutex_);
        if (!running_)
            return;
        running_ = false;
    }
    cv_.notify_all();

    if (worker_.joinable())
        worker_.join();
}

void
Engine::workerLoop()
{
    while (true) {
        WorkItem item;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty())
                return;

            item = std::move(queue_.front());
            queue_.pop_front();
        }

        auto result = classify(std::span<std::byte const>{item.payload});
        if (item.onResult) {
            item.onResult(std::move(result));
        }
    }
}

} // namespace Seer
