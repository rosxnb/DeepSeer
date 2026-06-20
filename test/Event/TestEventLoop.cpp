#include <DeepSeer/Event/EventLoop.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace DeepSeer;
using namespace std::chrono_literals;

// ===========================================================================
// Factory / lifecycle
// ===========================================================================

TEST(EventLoopTest, CreateReturnsNonNull)
{
    auto loop = EventLoop::create();
    ASSERT_NE(loop, nullptr);
}

TEST(EventLoopTest, StopBeforeRunIsHarmless)
{
    auto loop = EventLoop::create();
    loop->stop();
}

TEST(EventLoopTest, StopFromPostedCallback)
{
    auto loop = EventLoop::create();
    loop->post([&] { loop->stop(); });
    loop->run(); // must return
}

TEST(EventLoopTest, RunAndStopFromAnotherThread)
{
    auto loop = EventLoop::create();
    std::atomic<bool> entered{false};

    std::thread t([&] {
        entered.store(true, std::memory_order_release);
        loop->run();
    });

    // Spin until the loop thread has started.
    while (!entered.load(std::memory_order_acquire))
        std::this_thread::yield();

    // Give the loop a moment to actually block in kevent/epoll.
    std::this_thread::sleep_for(5ms);
    loop->stop();
    t.join();
}

// ===========================================================================
// I/O watch — pipe-based tests
// ===========================================================================

// Helper: creates a pipe, returns {read_fd, write_fd}. Caller must close both.
namespace {

struct Pipe
{
    int readFd  = -1;
    int writeFd = -1;

    Pipe()
    {
        int fds[2];
        if (pipe(fds) != 0)
            throw std::runtime_error(std::string{"pipe() failed: "} + std::strerror(errno));
        readFd  = fds[0];
        writeFd = fds[1];
    }

    ~Pipe()
    {
        if (readFd  >= 0) close(readFd);
        if (writeFd >= 0) close(writeFd);
    }

    Pipe(Pipe const&) = delete;
    Pipe& operator=(Pipe const&) = delete;
};

} // anonymous namespace

TEST(EventLoopTest, WatchReadable)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool readFired = false;
    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t events) {
        EXPECT_TRUE(events & static_cast<uint32_t>(IoEvent::Readable));
        readFired = true;
        loop->stop();
    });

    // Write a byte so the read end becomes readable.
    char byte = 'x';
    ASSERT_EQ(write(p.writeFd, &byte, 1), 1);

    loop->run();
    EXPECT_TRUE(readFired);
}

TEST(EventLoopTest, WatchWritable)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool writeFired = false;
    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t events) {
        EXPECT_TRUE(events & static_cast<uint32_t>(IoEvent::Writable));
        writeFired = true;
        loop->stop();
    });

    loop->run();
    EXPECT_TRUE(writeFired);
}

TEST(EventLoopTest, WatchReadableAndWritable)
{
    auto loop = EventLoop::create();
    Pipe p;

    uint32_t interest = static_cast<uint32_t>(IoEvent::Readable)
                      | static_cast<uint32_t>(IoEvent::Writable);

    int callbackCount = 0;
    bool gotReadable = false;
    bool gotWritable = false;

    loop->watch(p.readFd, interest, [&](uint32_t events) {
        if (events & static_cast<uint32_t>(IoEvent::Readable))
            gotReadable = true;
        if (events & static_cast<uint32_t>(IoEvent::Writable))
            gotWritable = true;
        ++callbackCount;
        if (gotReadable)
            loop->stop();
    });

    // Write a byte so the read end becomes readable.
    char byte = 'r';
    ASSERT_EQ(write(p.writeFd, &byte, 1), 1);

    loop->run();
    EXPECT_TRUE(gotReadable);
}

TEST(EventLoopTest, WatchReplacesExistingRegistration)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool firstCalled = false;
    bool secondCalled = false;

    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t) {
        firstCalled = true;
        loop->stop();
    });

    // Replace the watch — only the second callback should fire.
    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t) {
        secondCalled = true;
        loop->stop();
    });

    loop->run();
    EXPECT_FALSE(firstCalled);
    EXPECT_TRUE(secondCalled);
}

TEST(EventLoopTest, RemoveStopsEvents)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool callbackFired = false;
    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t) {
        callbackFired = true;
    });

    loop->remove(p.writeFd);

    // Post a stop so the loop doesn't hang forever.
    loop->post([&] { loop->stop(); });

    loop->run();
    EXPECT_FALSE(callbackFired);
}

TEST(EventLoopTest, RemoveNonexistentFdIsNoop)
{
    auto loop = EventLoop::create();
    loop->remove(9999); // should not crash
}

TEST(EventLoopTest, MultipleWatchedFds)
{
    auto loop = EventLoop::create();
    Pipe p1, p2;

    bool fd1Fired = false;
    bool fd2Fired = false;

    loop->watch(p1.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        fd1Fired = true;
        if (fd1Fired && fd2Fired)
            loop->stop();
    });
    loop->watch(p2.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        fd2Fired = true;
        if (fd1Fired && fd2Fired)
            loop->stop();
    });

    char byte = 'a';
    ASSERT_EQ(write(p1.writeFd, &byte, 1), 1);
    ASSERT_EQ(write(p2.writeFd, &byte, 1), 1);

    loop->run();
    EXPECT_TRUE(fd1Fired);
    EXPECT_TRUE(fd2Fired);
}

TEST(EventLoopTest, CallbackCanRewatch)
{
    auto loop = EventLoop::create();
    Pipe p;

    int callCount = 0;
    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        ++callCount;
        if (callCount < 3) {
            // Re-register inside callback — must not crash.
            loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
                ++callCount;
                if (callCount >= 3)
                    loop->stop();
            });
        } else {
            loop->stop();
        }
    });

    // Write enough data so the pipe stays readable across iterations.
    char buf[64];
    memset(buf, 'x', sizeof(buf));
    ASSERT_GT(write(p.writeFd, buf, sizeof(buf)), 0);

    loop->run();
    EXPECT_GE(callCount, 3);
}

TEST(EventLoopTest, CallbackCanRemoveOtherFd)
{
    auto loop = EventLoop::create();
    Pipe p1, p2;

    bool p2Fired = false;

    // When p1 fires, remove p2's watch.
    loop->watch(p1.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        loop->remove(p2.readFd);
        // Give event loop one more iteration to verify p2 doesn't fire.
        loop->post([&] { loop->stop(); });
    });

    loop->watch(p2.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        p2Fired = true;
    });

    char byte = 'z';
    ASSERT_EQ(write(p1.writeFd, &byte, 1), 1);
    ASSERT_EQ(write(p2.writeFd, &byte, 1), 1);

    loop->run();
    // p2Fired may or may not be true depending on event delivery order.
    // The key assertion is that we didn't crash from iterator invalidation.
}

TEST(EventLoopTest, EofOnPipeClose)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool eofDetected = false;
    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t events) {
        if (events & static_cast<uint32_t>(IoEvent::Readable)) {
            char buf[16];
            ssize_t n = read(p.readFd, buf, sizeof(buf));
            if (n == 0)
                eofDetected = true;
        }
        loop->stop();
    });

    // Closing the write end signals EOF on the read end.
    close(p.writeFd);
    p.writeFd = -1;

    loop->run();
    EXPECT_TRUE(eofDetected);
}

// ===========================================================================
// Timers
// ===========================================================================

TEST(EventLoopTest, TimerFiresOnce)
{
    auto loop = EventLoop::create();

    int fireCount = 0;
    auto handle = loop->addTimer(1ms, [&] {
        ++fireCount;
        loop->stop();
    });

    loop->run();
    EXPECT_EQ(fireCount, 1);
}

TEST(EventLoopTest, TimerCancelPreventsCallback)
{
    auto loop = EventLoop::create();

    bool timerFired = false;
    auto handle = loop->addTimer(1ms, [&] {
        timerFired = true;
    });

    handle->cancel();

    // Schedule a later stop to let the cancelled timer's deadline pass.
    auto stop = loop->addTimer(20ms, [&] {
        loop->stop();
    });

    loop->run();
    EXPECT_FALSE(timerFired);
}

TEST(EventLoopTest, TimerHandleDestructionCancels)
{
    auto loop = EventLoop::create();

    bool timerFired = false;
    {
        auto handle = loop->addTimer(1ms, [&] {
            timerFired = true;
        });
        // handle destroyed here — should cancel the timer.
    }

    auto stop = loop->addTimer(20ms, [&] {
        loop->stop();
    });

    loop->run();
    EXPECT_FALSE(timerFired);
}

TEST(EventLoopTest, MultipleTimersFireInOrder)
{
    auto loop = EventLoop::create();

    std::vector<int> order;

    auto h1 = loop->addTimer(5ms, [&] { order.push_back(1); });
    auto h2 = loop->addTimer(10ms, [&] { order.push_back(2); });
    auto h3 = loop->addTimer(15ms, [&] {
        order.push_back(3);
        loop->stop();
    });

    loop->run();
    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(EventLoopTest, TimerWithZeroDelay)
{
    auto loop = EventLoop::create();

    bool fired = false;
    auto handle = loop->addTimer(Duration::zero(), [&] {
        fired = true;
        loop->stop();
    });

    loop->run();
    EXPECT_TRUE(fired);
}

TEST(EventLoopTest, TimerAndIoInterleaved)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool ioFired = false;
    bool timerFired = false;

    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        ioFired = true;
        if (ioFired && timerFired)
            loop->stop();
    });

    auto handle = loop->addTimer(5ms, [&] {
        timerFired = true;
        if (ioFired && timerFired)
            loop->stop();
    });

    char byte = 't';
    ASSERT_EQ(write(p.writeFd, &byte, 1), 1);

    loop->run();
    EXPECT_TRUE(ioFired);
    EXPECT_TRUE(timerFired);
}

// ===========================================================================
// post() — cross-thread callback delivery
// ===========================================================================

TEST(EventLoopTest, PostCallbackExecutes)
{
    auto loop = EventLoop::create();

    bool posted = false;
    loop->post([&] {
        posted = true;
        loop->stop();
    });

    loop->run();
    EXPECT_TRUE(posted);
}

TEST(EventLoopTest, PostMultipleCallbacksPreservesOrder)
{
    auto loop = EventLoop::create();

    std::vector<int> order;
    loop->post([&] { order.push_back(1); });
    loop->post([&] { order.push_back(2); });
    loop->post([&] { order.push_back(3); });
    loop->post([&] {
        order.push_back(4);
        loop->stop();
    });

    loop->run();
    ASSERT_EQ(order.size(), 4);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_EQ(order[3], 4);
}

TEST(EventLoopTest, PostFromAnotherThread)
{
    auto loop = EventLoop::create();

    std::atomic<bool> posted{false};

    std::thread t([&] {
        // Small delay to let the loop start blocking.
        std::this_thread::sleep_for(10ms);
        loop->post([&] {
            posted.store(true, std::memory_order_release);
            loop->stop();
        });
    });

    loop->run();
    t.join();
    EXPECT_TRUE(posted.load(std::memory_order_acquire));
}

TEST(EventLoopTest, PostFromMultipleThreads)
{
    auto loop = EventLoop::create();

    int const numThreads = 8;
    int const postsPerThread = 100;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < postsPerThread; ++j) {
                loop->post([&] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    // Wait for all threads to finish posting, then post a stop.
    for (auto& t : threads)
        t.join();

    loop->post([&] { loop->stop(); });

    loop->run();
    EXPECT_EQ(counter.load(), numThreads * postsPerThread);
}

TEST(EventLoopTest, PostFromWithinCallback)
{
    auto loop = EventLoop::create();

    int value = 0;
    loop->post([&] {
        value = 1;
        loop->post([&] {
            value = 2;
            loop->stop();
        });
    });

    loop->run();
    EXPECT_EQ(value, 2);
}

// ===========================================================================
// Edge cases / stress
// ===========================================================================

TEST(EventLoopTest, WatchAfterRemoveReregisters)
{
    auto loop = EventLoop::create();
    Pipe p;

    bool fired = false;
    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t) {
        loop->stop();
    });
    loop->remove(p.writeFd);

    // Re-register after remove.
    loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [&](uint32_t) {
        fired = true;
        loop->stop();
    });

    loop->run();
    EXPECT_TRUE(fired);
}

TEST(EventLoopTest, RapidWatchRemoveCycles)
{
    auto loop = EventLoop::create();
    Pipe p;

    // Rapidly register/unregister — should not crash or leak kqueue registrations.
    for (int i = 0; i < 100; ++i) {
        loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [](uint32_t) {});
        loop->remove(p.readFd);
    }

    // Final registration that actually fires.
    bool fired = false;
    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        fired = true;
        loop->stop();
    });

    char byte = 'r';
    ASSERT_EQ(write(p.writeFd, &byte, 1), 1);

    loop->run();
    EXPECT_TRUE(fired);
}

TEST(EventLoopTest, ManyTimersStress)
{
    auto loop = EventLoop::create();

    int const numTimers = 50;
    std::atomic<int> fired{0};
    std::vector<TimerHandlePtr> handles;

    for (int i = 0; i < numTimers; ++i) {
        handles.push_back(loop->addTimer(1ms, [&] {
            if (fired.fetch_add(1, std::memory_order_relaxed) + 1 == numTimers)
                loop->stop();
        }));
    }

    // Safety net: stop after 2 seconds even if not all timers fired.
    auto safety = loop->addTimer(2000ms, [&] {
        loop->stop();
    });

    loop->run();
    EXPECT_EQ(fired.load(), numTimers);
}

TEST(EventLoopTest, CancelAllTimersBeforeRun)
{
    auto loop = EventLoop::create();

    bool anyFired = false;
    std::vector<TimerHandlePtr> handles;

    for (int i = 0; i < 10; ++i) {
        handles.push_back(loop->addTimer(1ms, [&] {
            anyFired = true;
        }));
    }

    // Cancel all of them.
    handles.clear();

    auto stop = loop->addTimer(20ms, [&] {
        loop->stop();
    });

    loop->run();
    EXPECT_FALSE(anyFired);
}

TEST(EventLoopTest, StopFromIoCallback)
{
    auto loop = EventLoop::create();
    Pipe p;

    loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [&](uint32_t) {
        loop->stop();
    });

    char byte = 's';
    ASSERT_EQ(write(p.writeFd, &byte, 1), 1);

    loop->run();
    // If we reach here, stop() from an IO callback works.
}

TEST(EventLoopTest, StopFromTimerCallback)
{
    auto loop = EventLoop::create();

    auto handle = loop->addTimer(1ms, [&] {
        loop->stop();
    });

    loop->run();
    // If we reach here, stop() from a timer callback works.
}

TEST(EventLoopTest, DestroyLoopWithActiveWatches)
{
    Pipe p;
    {
        auto loop = EventLoop::create();
        loop->watch(p.readFd, static_cast<uint32_t>(IoEvent::Readable), [](uint32_t) {});
        loop->watch(p.writeFd, static_cast<uint32_t>(IoEvent::Writable), [](uint32_t) {});
        auto handle = loop->addTimer(1h, [] {});
        // loop destroyed with active watches and timers — must not crash.
    }
}
