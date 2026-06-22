#pragma once

/// @file Sink.hpp
/// @brief Log levels and abstract sink interface.
///
/// A Sink receives pre-formatted log lines and writes them to a destination
/// (file, console, network, etc.). Each sink is independently thread-safe.
///
/// To swap the underlying logging library (e.g., spdlog), either:
/// 1. Write an adapter sink that delegates to spdlog, or
/// 2. Rewrite Logger.cpp internals while keeping the Logger API stable.

#include <cstdint>
#include <memory>
#include <string_view>

namespace DeepSeer
{

/// Severity levels, ordered from most to least verbose.
/// Numeric values enable >= comparison for level filtering.
enum class LogLevel : uint8_t
{
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
};

/// Convert a LogLevel to its short uppercase tag.
constexpr std::string_view
logLevelTag(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
    }
    return "???";
}

/// Abstract output destination for log messages.
/// Implementations must be thread-safe.
class Sink
{
public:
    virtual ~Sink() = default;

    /// Write a fully-formatted log line (includes timestamp, level, message, newline).
    virtual void write(std::string_view formatted) = 0;

    /// Flush any buffered output.
    virtual void flush() = 0;
};

using SinkPtr = std::shared_ptr<Sink>;

} // namespace DeepSeer
