#include <Seer/Engine.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>

using namespace Seer;

namespace
{

/// A trivial engine that classifies everything as "test/plain".
class StubEngine : public Engine
{
protected:
    InferenceResult classify(std::span<std::byte const> /*payload*/) override
    {
        return InferenceResult{
            .label      = "test/plain",
            .confidence = 1.0f,
            .modelName  = "stub",
        };
    }
};

} // namespace

TEST(EngineTest, SubmitAndReceiveResult)
{
    StubEngine engine;
    std::latch done{1};
    InferenceResult captured;

    std::array<std::byte, 4> data{};
    engine.submit(data, "http://example.com",
        [&](InferenceResult r) {
            captured = std::move(r);
            done.count_down();
        });

    done.wait();
    EXPECT_EQ(captured.label, "test/plain");
    EXPECT_FLOAT_EQ(captured.confidence, 1.0f);
    EXPECT_EQ(captured.modelName, "stub");
}

TEST(EngineTest, MultipleSubmissions)
{
    StubEngine engine;
    constexpr int kCount = 10;
    std::latch done{kCount};
    std::atomic<int> received{0};

    std::array<std::byte, 4> data{};
    for (int i = 0; i < kCount; ++i) {
        engine.submit(data, "http://example.com",
            [&](InferenceResult) {
                received.fetch_add(1);
                done.count_down();
            });
    }

    done.wait();
    EXPECT_EQ(received.load(), kCount);
}

TEST(EngineTest, ShutdownDrainsQueue)
{
    StubEngine engine;
    std::atomic<int> received{0};

    std::array<std::byte, 4> data{};
    for (int i = 0; i < 5; ++i) {
        engine.submit(data, "http://example.com",
            [&](InferenceResult) { received.fetch_add(1); });
    }

    engine.shutdown();
    EXPECT_EQ(received.load(), 5);
}
