#include "KqueueLoop.hpp"

#include <fcntl.h>
#include <sys/event.h>
#include <unistd.h>

#include <cstring>
#include <format>
#include <stdexcept>

#include <DeepSeer/Log/Logger.hpp>

namespace DeepSeer
{

// ---------------------------------------------------------------------------
// Timer handle implementation
// ---------------------------------------------------------------------------

class KqueueTimerHandle final : public TimerHandle
{
public:
    explicit KqueueTimerHandle(std::shared_ptr<KqueueLoop::TimerEntry> entry)
        : entry_{std::move(entry)}
    { }

    ~KqueueTimerHandle() override
    { cancel(); }

    void cancel() override
    { entry_->cancelled = true; }

private:
    std::shared_ptr<KqueueLoop::TimerEntry> entry_;
};


// ---------------------------------------------------------------------------
// KqueueLoop
// ---------------------------------------------------------------------------

KqueueLoop::KqueueLoop()
{
    kqFd_ = kqueue();
    if (kqFd_ < 0) {
        throw std::runtime_error(std::format("kqueue() failed: {}", std::strerror(errno)));
    }

    // Wake-up pipes for cross-thread post()
    int fds[2];
    if (pipe(fds) != 0) {
        close(kqFd_);
        throw std::runtime_error(std::format("pip() failed: {}", std::strerror(errno)));
    }
    wakeReadFd_  = fds[0];
    wakeWriteFd_ = fds[1];

    // Set pipe to non-blocking so the drain loop in run() doesn't block
    fcntl(wakeReadFd_,  F_SETFL, O_NONBLOCK);
    fcntl(wakeWriteFd_, F_SETFL, O_NONBLOCK);

    // Register wake pipe read end with kqueue
    struct kevent ev {};
    EV_SET(&ev, static_cast<uintptr_t>(wakeReadFd_), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    kevent(kqFd_, &ev, 1, nullptr, 0, nullptr);
}

KqueueLoop::~KqueueLoop()
{
    if (kqFd_ >= 0)         close(kqFd_);
    if (wakeReadFd_ >= 0)   close(wakeReadFd_);
    if (wakeWriteFd_ >= 0)  close(wakeWriteFd_);
}

void
KqueueLoop::run()
{
    running_.store(true, std::memory_order_release);

    constexpr int kMaxEvents = 64;
    struct kevent events[kMaxEvents];

    // Main loop -- blocks indefinitely until an event occurs
    while (running_.load(std::memory_order_acquire)) {

        int n = kevent(kqFd_, nullptr, 0, events, kMaxEvents, nullptr);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            auto const& ev = events[i];

            // Wake pipe event -- from `post()` and `stop()`
            if (ev.filter == EVFILT_READ && static_cast<int>(ev.ident) == wakeReadFd_) {
                char buf[64];
                while (read(wakeReadFd_, buf, sizeof(buf)) > 0);
                processPosted();
                continue;
            }

            // Timer event
            if (ev.filter == EVFILT_TIMER) {
                auto it = timers_.find(ev.ident);
                if (it != timers_.end()) {
                    auto entry = it->second;
                    timers_.erase(it);
                    if (!entry->cancelled) {
                        entry->callback();
                    }
                }
                continue;
            }

            // IO event -- read/write on monitored file discriptor
            auto fd = static_cast<Fd>(ev.ident);
            auto it = fdEntries_.find(fd);
            if (it != fdEntries_.end()) {
                uint32_t fired = 0;
                if (ev.filter == EVFILT_READ)
                    fired |= static_cast<uint32_t>(IoEvent::Readable);
                if (ev.filter == EVFILT_WRITE)
                    fired |= static_cast<uint32_t>(IoEvent::Writable);
                if (ev.flags & EV_EOF)
                    fired |= static_cast<uint32_t>(IoEvent::Readable);

                auto cb = it->second.callback; // copy -- callback may invalidate iterator
                cb(fired);
            }
        } // for (each active events)
    } // while (running_)
}

void
KqueueLoop::stop()
{
    running_.store(false, std::memory_order_release);
    wake();
}

void
KqueueLoop::watch(Fd fd, uint32_t events, IoCallback cb)
{
    // Remove old registration if exist
    remove(fd);

    fdEntries_[fd] = {events, std::move(cb)};

    struct kevent evs[2];
    int count = 0;

    if (events & static_cast<uint32_t>(IoEvent::Readable)) {
        EV_SET(&evs[count++], static_cast<uintptr_t>(fd), EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & static_cast<uint32_t>(IoEvent::Writable)) {
        EV_SET(&evs[count++], static_cast<uintptr_t>(fd), EVFILT_WRITE,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (count > 0) {
        int rc = kevent(kqFd_, evs, count, nullptr, 0, nullptr);
        if (rc < 0) {
            Logger::error("kevent register failed for fd {}: {}", fd, std::strerror(errno));
        }
    }
}

void
KqueueLoop::remove(Fd fd)
{
    auto it = fdEntries_.find(fd);
    if (it == fdEntries_.end())
        return;

    struct kevent evs[2];
    int count = 0;

    if (it->second.events & static_cast<uint32_t>(IoEvent::Readable))
        EV_SET(&evs[count++], static_cast<uintptr_t>(fd), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    if (it->second.events & static_cast<uint32_t>(IoEvent::Writable))
        EV_SET(&evs[count++], static_cast<uintptr_t>(fd), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);

    kevent(kqFd_, evs, count, nullptr, 0, nullptr);
    fdEntries_.erase(it);
}

TimerHandlePtr
KqueueLoop::addTimer(Duration delay, Callback cb)
{
    auto id = nextTimerId_++;
    auto entry = std::make_shared<TimerEntry>(id, std::move(cb), false);
    timers_[id] = entry;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delay).count();
    if (ms <= 0)
        ms = 1;

    struct kevent ev {};
    EV_SET(&ev, id, EVFILT_TIMER, EV_ADD | EV_ONESHOT, NOTE_USECONDS,
           static_cast<int64_t>(ms) * 1000, nullptr);

    kevent(kqFd_, &ev, 1, nullptr, 0, nullptr);
    return std::make_unique<KqueueTimerHandle>(std::move(entry));
}

void
KqueueLoop::post(Callback cb)
{
    {
        std::lock_guard lock(postMu_);
        postedCallbacks_.emplace_back(std::move(cb));
    }
    wake();
}

void
KqueueLoop::processPosted()
{
    std::vector<Callback> cbs;

    {
        std::lock_guard lock(postMu_);
        cbs.swap(postedCallbacks_);
    }

    for (auto const& cb : cbs) {
        cb();
    }
}

void
KqueueLoop::wake()
{
    char byte = 1;

    [[maybe_unused]]
    auto r = write(wakeWriteFd_, &byte, 1);
}

} // namespace DeepSeer
