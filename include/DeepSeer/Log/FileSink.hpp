#pragma once

/// @file FileSink.hpp
/// @brief File sink with size-based log rotation.
///
/// ## Rotation Strategy
///
/// When the active log file exceeds maxFileSize, the sink performs a
/// rename chain before opening a fresh file:
///
///   app.3.log  -> deleted (if maxFiles == 3)
///   app.2.log  -> app.3.log
///   app.1.log  -> app.2.log
///   app.log    -> app.1.log
///   (new)      -> app.log
///
/// Rotation happens synchronously inside write() while the mutex is held.
/// This is acceptable because rotation is infrequent and rename(2) is fast.

#include <DeepSeer/Log/Sink.hpp>

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>

namespace DeepSeer
{

/// Configuration for file-based logging with rotation.
struct FileSinkConfig
{
    std::string path;                          ///< Path to the active log file
    size_t maxFileSize = 10 * 1024 * 1024;     ///< Max bytes before rotation (default: 10 MiB)
    size_t maxFiles    = 5;                     ///< Max rotated files to keep (default: 5)
};

/// File sink with size-based rotation.
/// Thread-safe via internal mutex.
class FileSink final : public Sink
{
public:
    explicit FileSink(FileSinkConfig config);
    ~FileSink() override;

    FileSink(FileSink const&) = delete;
    FileSink& operator=(FileSink const&) = delete;

    void write(std::string_view formatted) override;
    void flush() override;

private:
    void rotateIfNeeded();
    void rotate();
    void openFile();

    /// Build the path for rotated file N (e.g., "logs/deepseer.1.log").
    std::string rotatedPath(size_t index) const;

    FileSinkConfig config_;
    std::mutex mu_;
    std::FILE* file_ = nullptr;
    size_t currentSize_ = 0;
};

} // namespace DeepSeer
