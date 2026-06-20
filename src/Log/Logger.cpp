#include <DeepSeer/Log/Logger.hpp>

#include <atomic>
#include <chrono>
#include <format>
#include <shared_mutex>
#include <thread>

namespace DeepSeer
{

// ---------------------------------------------------------------------------
// Global state (file-scoped)
// ---------------------------------------------------------------------------

namespace {

std::atomic<LogLevel> gMinLevel{LogLevel::Info};

// shared_mutex: many concurrent readers (log calls) vs. rare writers (init/shutdown).
std::shared_mutex gSinksMu;
std::vector<SinkPtr> gSinks;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Logger static methods
// ---------------------------------------------------------------------------

void
Logger::init(LogLevel minLevel, std::vector<SinkPtr> sinks)
{
    gMinLevel.store(minLevel, std::memory_order_relaxed);
    std::unique_lock lock(gSinksMu);
    gSinks = std::move(sinks);
}

void
Logger::shutdown()
{
    std::unique_lock lock(gSinksMu);
    for (auto const& sink : gSinks)
        sink->flush();
    gSinks.clear();
}

void
Logger::setLevel(LogLevel level)
{
    gMinLevel.store(level, std::memory_order_relaxed);
}

LogLevel
Logger::level()
{
    return gMinLevel.load(std::memory_order_relaxed);
}

bool
Logger::shouldLog(LogLevel level)
{
    return static_cast<uint8_t>(level)
        >= static_cast<uint8_t>(gMinLevel.load(std::memory_order_relaxed));
}

void
Logger::dispatch(LogLevel level, std::string_view message)
{
    auto const now   = std::chrono::system_clock::now();
    auto const timeT = std::chrono::system_clock::to_time_t(now);
    auto const ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef PLATFORM_WINDOWS
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif

    auto const tid = std::hash<std::thread::id>{}(std::this_thread::get_id());

    // Format: "2026-06-16 14:23:05.123 [INF] [tid:a3f2] message\n"
    std::string line = std::format(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} [{}] [tid:{:x}] {}\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<int>(ms.count()),
        logLevelTag(level),
        tid & 0xFFFFu,
        message
    );

    std::shared_lock lock(gSinksMu);
    for (auto const& sink : gSinks)
        sink->write(line);
}

} // namespace DeepSeer
