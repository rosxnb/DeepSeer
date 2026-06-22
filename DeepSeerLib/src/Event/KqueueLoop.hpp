#pragma once

#include <DeepSeer/Event/EventLoop.hpp>

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace DeepSeer
{

class KqueueLoop final : public EventLoop
{
public:
    KqueueLoop();
    ~KqueueLoop() override;

    KqueueLoop(KqueueLoop const&) = delete;
    KqueueLoop& operator=(KqueueLoop const&) = delete;

    void run() override;
    void stop() override;
    void watch(Fd fd, uint32_t events, IoCallback cb) override;
    void remove(Fd fd) override;
    TimerHandlePtr addTimer(Duration delay, Callback cb) override;
    void post(Callback cb) override;

private:
    void processPosted();
    void wake();

    int kqFd_ = -1;
    std::atomic<bool> running_{false};

    // Pipe used by post() to wake the loop from another thread
    int wakeReadFd_ = -1;
    int wakeWriteFd_ = -1;

    // Fd watch registrations
    struct FdEntry
    {
        uint32_t events;
        IoCallback callback;
    };
    std::unordered_map<Fd, FdEntry> fdEntries_;

    // Timer management (public so KqueueTimerHandle can access)
public:
    struct TimerEntry
    {
        uint64_t id;
        Callback callback;
        bool cancelled = false;
    };

private:
    std::unordered_map<uint64_t, std::shared_ptr<TimerEntry>> timers_;
    uint64_t nextTimerId_ = 1;

    // Thread-safe posted callbacks
    std::mutex postMu_;
    std::vector<Callback> postedCallbacks_;
};

} // namespace DeepSeer
