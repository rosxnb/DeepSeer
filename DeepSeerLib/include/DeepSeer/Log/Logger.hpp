#pragma once

/// @file Logger.hpp
/// @brief Static function-based logging API.
///
/// ## Usage
///
///   Logger::init(LogLevel::Info, {consoleSink, fileSink});
///   Logger::info("Server started on {}", addr.toString());
///   Logger::warn("Connection from {} dropped: {}", peer, reason);
///   Logger::shutdown();
///
/// ## Swappability
///
/// The Logger API is the stable contract. To switch backends:
/// - Option A: Write a Sink adapter (e.g., SpdlogSink) and pass it to init().
/// - Option B: Rewrite Logger.cpp to delegate to spdlog directly.
/// Either way, call sites (Logger::info(...)) remain unchanged.

#include <DeepSeer/Log/Sink.hpp>

#include <format>
#include <vector>

namespace DeepSeer
{

/// Static logging API. All methods are thread-safe.
class Logger
{
public:
    Logger() = delete;

    /// Initialize the logger with a minimum level and one or more sinks.
    /// Must be called once before any log calls (typically in main()).
    /// Calling init() again replaces all previous configuration.
    static void init(LogLevel minLevel, std::vector<SinkPtr> sinks);

    /// Flush all sinks and release them.
    static void shutdown();

    /// Change the minimum log level at runtime.
    static void setLevel(LogLevel level);

    /// Return the current minimum log level.
    static LogLevel level();

    template <typename... Args>
    static void trace(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(std::format_string<Args...> fmt, Args&&... args)
    {
        log(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    static void log(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!shouldLog(level))
            return;

        std::string message = std::format(fmt, std::forward<Args>(args)...);
        dispatch(level, message);
    }

    static bool shouldLog(LogLevel level);
    static void dispatch(LogLevel level, std::string_view message);
};

} // namespace DeepSeer
