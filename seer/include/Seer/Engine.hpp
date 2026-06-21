#pragma once

/// @file Engine.hpp
/// @brief Abstract inference engine interface.
///
/// Concrete implementations (e.g., MagikaEngine) inherit from Engine and
/// implement classify(). The engine owns a worker thread — submit() is
/// non-blocking and safe to call from the proxy's event-loop thread.

#include <Seer/Result.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Seer
{

class Engine
{
public:
    using ResultCallback = std::function<void(InferenceResult)>;

    virtual ~Engine();

    /// Submit a payload for asynchronous classification.
    /// Safe to call from any thread. Does not block.
    void submit(std::span<std::byte const> payload,
                std::string url,
                ResultCallback onResult = {});

    /// Shut down the worker thread. Blocks until pending work drains.
    void shutdown();

protected:
    /// Subclasses implement synchronous classification on the worker thread.
    virtual InferenceResult classify(std::span<std::byte const> payload) = 0;

private:
    struct WorkItem
    {
        std::vector<std::byte> payload;
        std::string            url;
        ResultCallback         onResult;
    };

    void workerLoop();

    std::jthread            worker_;
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::deque<WorkItem>    queue_;
    bool                    running_{false};

    void startWorker();
    void ensureWorker();
};

} // namespace Seer
