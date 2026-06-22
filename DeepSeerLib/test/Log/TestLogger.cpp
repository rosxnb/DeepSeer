#include <DeepSeer/Log/Logger.hpp>
#include <DeepSeer/Log/ConsoleSink.hpp>
#include <DeepSeer/Log/FileSink.hpp>
#include <DeepSeer/Log/Sink.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace DeepSeer;

// ===========================================================================
// Test sink — captures formatted output for assertions
// ===========================================================================

namespace {

class CaptureSink final : public Sink
{
public:
    void write(std::string_view formatted) override
    {
        std::lock_guard lock(mu_);
        lines_.emplace_back(formatted);
    }

    void flush() override { }

    std::vector<std::string> lines()
    {
        std::lock_guard lock(mu_);
        return lines_;
    }

    void clear()
    {
        std::lock_guard lock(mu_);
        lines_.clear();
    }

private:
    std::mutex mu_;
    std::vector<std::string> lines_;
};

std::string readFile(std::string const& path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

} // anonymous namespace

// ===========================================================================
// Lifecycle
// ===========================================================================

class LoggerTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        Logger::shutdown();
    }
};

TEST_F(LoggerTest, InitAndShutdown)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Trace, {sink});
    Logger::shutdown();
    // Double shutdown is safe.
    Logger::shutdown();
}

TEST_F(LoggerTest, LogBeforeInitIsSilent)
{
    // No init — should not crash, just drop the message.
    Logger::info("dropped");
}

TEST_F(LoggerTest, LogAfterShutdownIsSilent)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Trace, {sink});
    Logger::shutdown();
    Logger::info("dropped");
    EXPECT_TRUE(sink->lines().empty());
}

// ===========================================================================
// Level filtering
// ===========================================================================

TEST_F(LoggerTest, LevelFiltering)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Warn, {sink});

    Logger::trace("no");
    Logger::debug("no");
    Logger::info("no");
    Logger::warn("yes");
    Logger::error("yes");

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_NE(lines[0].find("[WRN]"), std::string::npos);
    EXPECT_NE(lines[1].find("[ERR]"), std::string::npos);
}

TEST_F(LoggerTest, SetLevelAtRuntime)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Error, {sink});

    Logger::warn("filtered");
    EXPECT_TRUE(sink->lines().empty());

    Logger::setLevel(LogLevel::Warn);
    EXPECT_EQ(Logger::level(), LogLevel::Warn);

    Logger::warn("visible");
    EXPECT_EQ(sink->lines().size(), 1);
}

TEST_F(LoggerTest, TracePassesEverything)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Trace, {sink});

    Logger::trace("t");
    Logger::debug("d");
    Logger::info("i");
    Logger::warn("w");
    Logger::error("e");

    EXPECT_EQ(sink->lines().size(), 5);
}

// ===========================================================================
// Output format
// ===========================================================================

TEST_F(LoggerTest, OutputContainsLevelTag)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Trace, {sink});

    Logger::trace("msg");
    Logger::debug("msg");
    Logger::info("msg");
    Logger::warn("msg");
    Logger::error("msg");

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 5);
    EXPECT_NE(lines[0].find("[TRC]"), std::string::npos);
    EXPECT_NE(lines[1].find("[DBG]"), std::string::npos);
    EXPECT_NE(lines[2].find("[INF]"), std::string::npos);
    EXPECT_NE(lines[3].find("[WRN]"), std::string::npos);
    EXPECT_NE(lines[4].find("[ERR]"), std::string::npos);
}

TEST_F(LoggerTest, OutputContainsMessage)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink});

    Logger::info("hello {}", 42);

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_NE(lines[0].find("hello 42"), std::string::npos);
}

TEST_F(LoggerTest, OutputContainsThreadId)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink});

    Logger::info("test");

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_NE(lines[0].find("[tid:"), std::string::npos);
}

TEST_F(LoggerTest, OutputEndsWithNewline)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink});

    Logger::info("msg");

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].ends_with('\n'));
}

// ===========================================================================
// Multi-sink
// ===========================================================================

TEST_F(LoggerTest, MultipleSinksReceiveSameOutput)
{
    auto sink1 = std::make_shared<CaptureSink>();
    auto sink2 = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink1, sink2});

    Logger::info("hello");

    ASSERT_EQ(sink1->lines().size(), 1);
    ASSERT_EQ(sink2->lines().size(), 1);
    EXPECT_EQ(sink1->lines()[0], sink2->lines()[0]);
}

// ===========================================================================
// Format string — compile-time validation
// ===========================================================================

TEST_F(LoggerTest, FormatWithMultipleArgs)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink});

    Logger::info("fd={} addr={} port={}", 5, "127.0.0.1", 8080);

    auto const& lines = sink->lines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_NE(lines[0].find("fd=5 addr=127.0.0.1 port=8080"), std::string::npos);
}

TEST_F(LoggerTest, FormatWithNoArgs)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Info, {sink});

    Logger::info("plain message");

    ASSERT_EQ(sink->lines().size(), 1);
    EXPECT_NE(sink->lines()[0].find("plain message"), std::string::npos);
}

// ===========================================================================
// Thread safety
// ===========================================================================

TEST_F(LoggerTest, ConcurrentLogging)
{
    auto sink = std::make_shared<CaptureSink>();
    Logger::init(LogLevel::Trace, {sink});

    int const numThreads = 8;
    int const logsPerThread = 200;

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i] {
            for (int j = 0; j < logsPerThread; ++j) {
                Logger::info("thread={} iter={}", i, j);
            }
        });
    }

    for (auto& t : threads)
        t.join();

    EXPECT_EQ(sink->lines().size(), static_cast<size_t>(numThreads * logsPerThread));
}

// ===========================================================================
// ConsoleSink — basic smoke test
// ===========================================================================

TEST_F(LoggerTest, ConsoleSinkDoesNotCrash)
{
    auto console = std::make_shared<ConsoleSink>();
    Logger::init(LogLevel::Info, {console});

    Logger::info("console smoke test");
    Logger::shutdown();
}

// ===========================================================================
// FileSink — file I/O and rotation
// ===========================================================================

class FileSinkTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpDir_;

    void SetUp() override
    {
        tmpDir_ = std::filesystem::temp_directory_path() / "deepseer_test_log";
        std::filesystem::remove_all(tmpDir_);
        std::filesystem::create_directories(tmpDir_);
    }

    void TearDown() override
    {
        Logger::shutdown();
        std::filesystem::remove_all(tmpDir_);
    }

    std::string logPath() const
    {
        return (tmpDir_ / "test.log").string();
    }
};

TEST_F(FileSinkTest, WritesToFile)
{
    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path = logPath(),
    });
    Logger::init(LogLevel::Info, {file});

    Logger::info("hello file");
    Logger::shutdown();

    auto const content = readFile(logPath());
    EXPECT_NE(content.find("hello file"), std::string::npos);
}

TEST_F(FileSinkTest, AppendsToExistingFile)
{
    // Write initial content.
    {
        std::ofstream f(logPath());
        f << "existing\n";
    }

    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path = logPath(),
    });
    Logger::init(LogLevel::Info, {file});
    Logger::info("appended");
    Logger::shutdown();

    auto const content = readFile(logPath());
    EXPECT_NE(content.find("existing"), std::string::npos);
    EXPECT_NE(content.find("appended"), std::string::npos);
}

TEST_F(FileSinkTest, RotationCreatesNumberedFiles)
{
    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path        = logPath(),
        .maxFileSize = 128,
        .maxFiles    = 3,
    });
    Logger::init(LogLevel::Info, {file});

    // Write enough to trigger at least 2 rotations.
    for (int i = 0; i < 50; ++i) {
        Logger::info("rotation test line {}", i);
    }
    Logger::shutdown();

    namespace fs = std::filesystem;
    EXPECT_TRUE(fs::exists(logPath()));

    // At least one rotated file should exist.
    auto const rotated1 = (tmpDir_ / "test.1.log").string();
    EXPECT_TRUE(fs::exists(rotated1));
}

TEST_F(FileSinkTest, RotationRespectsMaxFiles)
{
    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path        = logPath(),
        .maxFileSize = 128,
        .maxFiles    = 2,
    });
    Logger::init(LogLevel::Info, {file});

    // Write a lot to trigger many rotations.
    for (int i = 0; i < 100; ++i) {
        Logger::info("overflow test line number {}", i);
    }
    Logger::shutdown();

    namespace fs = std::filesystem;

    // Active + .1 + .2 should exist.
    EXPECT_TRUE(fs::exists(logPath()));
    EXPECT_TRUE(fs::exists((tmpDir_ / "test.1.log").string()));
    EXPECT_TRUE(fs::exists((tmpDir_ / "test.2.log").string()));

    // .3 should NOT exist — maxFiles is 2.
    EXPECT_FALSE(fs::exists((tmpDir_ / "test.3.log").string()));
}

TEST_F(FileSinkTest, RotatedFileContent)
{
    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path        = logPath(),
        .maxFileSize = 128,
        .maxFiles    = 3,
    });
    Logger::init(LogLevel::Info, {file});

    for (int i = 0; i < 50; ++i) {
        Logger::info("line {}", i);
    }
    Logger::shutdown();

    // The active file should have the most recent lines.
    auto const active = readFile(logPath());
    EXPECT_FALSE(active.empty());

    // Rotated .1 should have older lines.
    auto const rotated = readFile((tmpDir_ / "test.1.log").string());
    EXPECT_FALSE(rotated.empty());

    // Both should contain formatted log lines with the [INF] tag.
    EXPECT_NE(active.find("[INF]"), std::string::npos);
    EXPECT_NE(rotated.find("[INF]"), std::string::npos);
}

TEST_F(FileSinkTest, ConcurrentWritesWithRotation)
{
    auto file = std::make_shared<FileSink>(FileSinkConfig{
        .path        = logPath(),
        .maxFileSize = 256,
        .maxFiles    = 5,
    });
    Logger::init(LogLevel::Info, {file});

    int const numThreads = 4;
    int const logsPerThread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([i] {
            for (int j = 0; j < logsPerThread; ++j) {
                Logger::info("t={} j={}", i, j);
            }
        });
    }

    for (auto& t : threads)
        t.join();
    Logger::shutdown();

    // Verify no crash and files were created.
    EXPECT_TRUE(std::filesystem::exists(logPath()));
}
