#pragma once

/// @file event_loop.h
/// @brief Platform-abstracted event loop for async I/O.
///
/// ## Design
///
/// The EventLoop is the core scheduling primitive. It multiplexes I/O readiness
/// notifications, timers, and cross-thread callbacks into a single-threaded
/// run loop. Inspired by Envoy's Dispatcher (which wraps libevent), but we
/// implement directly on platform APIs for full control.
///
/// ## Threading Model
///
/// - **One EventLoop per thread.** Each worker thread owns exactly one.
/// - **Not thread-safe** except for `post()`, which is the only way to
///   communicate between threads safely.
/// - All callbacks (I/O, timers, posted) execute on the loop's owning thread.
///
/// ## Platform Implementations
///
/// - macOS/BSD: `KqueueLoop` using kqueue(2) with level-triggered events.
/// - Linux: `EpollLoop` using epoll(7) — planned.
/// - Windows: `IocpLoop` using I/O Completion Ports — planned.
///
/// The factory `EventLoop::create()` selects the right implementation at
/// compile time via `#ifdef` in EventLoop.cpp.
///
/// ## Watch Semantics
///
/// `watch(fd, events, callback)` replaces any previous registration for that fd.
/// It internally calls `remove(fd)` first, then registers new events.
/// The callback receives a bitmask of which events fired.
///
/// IMPORTANT: The callback may modify fdEntries_ (e.g., by calling watch() or
/// remove() on other fds). The implementation copies the callback before
/// invoking it to avoid iterator invalidation.
///
/// ## Timer Semantics
///
/// Timers are one-shot. The returned TimerHandlePtr controls the timer's
/// lifetime — destroying or calling cancel() on it prevents the callback
/// from firing. Internally, timers use kqueue's EVFILT_TIMER.

#include <DeepSeer/Core/Types.hpp>

#include <functional>
#include <memory>

namespace DeepSeer
{

/// Bitmask flags for I/O event interest.
/// Combine with bitwise OR: `IoEvent::Readable | IoEvent::Writable`.
enum class IoEvent : uint32_t
{
    Readable = 1 << 0,
    Writable = 1 << 1,
};

/// Callback for I/O events. Receives a bitmask of IoEvent flags that fired.
using IoCallback = std::function<void(uint32_t events)>;

/// RAII handle for a registered timer. Destroying it cancels the timer.
/// Calling cancel() explicitly also prevents the callback from firing.
class TimerHandle
{
public:
    virtual ~TimerHandle() = default;
    virtual void cancel() = 0;
};

using TimerHandlePtr = std::unique_ptr<TimerHandle>;

/// Abstract event loop interface. One per thread.
/// See file-level docs for threading model and watch semantics.
class EventLoop
{
public:
    virtual ~EventLoop() = default;

    /// Enter the event loop. Blocks the calling thread until stop() is called.
    /// Processes I/O events, timers, and posted callbacks each iteration.
    virtual void run() = 0;

    /// Signal the loop to exit after the current iteration completes.
    /// Safe to call from within a callback (the loop finishes the current
    /// batch of events before checking the stop flag).
    virtual void stop() = 0;

    /// Register interest in fd readability/writability.
    /// Replaces any existing registration for this fd.
    /// @param fd     The file descriptor to watch.
    /// @param events Bitmask of IoEvent flags.
    /// @param cb     Invoked on the loop's thread when events fire.
    virtual void watch(Fd fd, uint32_t events, IoCallback cb) = 0;

    /// Remove all watches for a given fd. No-op if fd is not registered.
    virtual void remove(Fd fd) = 0;

    /// Schedule a one-shot timer. Returns a handle that cancels on destruction.
    /// @param delay Time until the callback fires.
    /// @param cb    Invoked on the loop's thread.
    virtual TimerHandlePtr addTimer(Duration delay, Callback cb) = 0;

    /// Post a callback to execute on this loop's thread.
    /// **Thread-safe** — this is the only thread-safe method on EventLoop.
    /// Used for cross-thread communication (e.g., main thread dispatching
    /// a new connection to a worker thread).
    /// Internally writes to a pipe/eventfd to wake the loop from kevent/epoll.
    virtual void post(Callback cb) = 0;

    /// Factory: creates the platform-appropriate EventLoop implementation.
    /// Selects KqueueLoop on macOS/BSD, EpollLoop on Linux, etc.
    static std::unique_ptr<EventLoop> create();
};

} // namespace DeepSeer
