#pragma once

/// @file ConsoleSink.hpp
/// @brief Sink that writes log messages to stderr.

#include <DeepSeer/Log/Sink.hpp>

#include <mutex>

namespace DeepSeer
{

/// Writes formatted log lines to stderr.
/// Thread-safe via internal mutex.
class ConsoleSink final : public Sink
{
public:
    void write(std::string_view formatted) override;
    void flush() override;

private:
    std::mutex mu_;
};

} // namespace DeepSeer
