#pragma once

/// @file Types.hpp
/// @brief Foundation types used throughout the DeepSeer proxy codebase.
///
/// This header defines the error handling strategy, platform-abstracted type
/// aliases, and common callback signatures. Every other header in the project
/// transitively depends on this one.
///
/// ## Error Handling
///
/// The project uses C++23 `std::expected<T, Error>` (aliased as `Expected<T>`)
/// instead of exceptions. Functions that can fail return `Expected<T>` where T
/// is the success value, or `VoidResult` (= `Expected<void>`) when there is no
/// value to return.
///
/// ## Platform Abstraction
///
/// `Fd` is the platform-native file/socket descriptor type:
///   - POSIX: `int` (from open/socket/accept)
///   - Windows: `uintptr_t` (SOCKET is typedef'd to this)
///
/// All event loop and networking code uses `Fd` instead of raw int.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>

namespace DeepSeer
{

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

/// Error codes categorized by subsystem. Used in the `Error` struct to allow
/// callers to branch on the category without parsing the message string.
enum class ErrorCode : uint16_t
{
    Ok = 0,
    // Network
    WouldBlock,
    FdExhausted,
    ConnectionClosed,
    ConnectionRefused,
    ConnectionTimeout,
    ReadError,
    WriteError,
    // TLS
    TlsHandshakeFailed,
    CertGenerationFailed,
    // HTTP
    ParseError,
    InvalidRequest,
    InvalidResponse,
    // Proxy
    UpstreamUnreachable,
    TunnelFailed,
    // Config
    ConfigError,
    // Generic
    InternalError,
};

/// Structured error returned via Expected<T>. Contains a machine-readable
/// code and a human-readable message.
struct Error
{
    ErrorCode code;
    std::string message;
};

/// Result type for operations that produce a value or fail.
/// Prefer this over exceptions for all recoverable errors.
template <typename T>
using Expected = std::expected<T, Error>;

/// Shorthand for operations that succeed with no return value.
using VoidResult = Expected<void>;

// ---------------------------------------------------------------------------
// Common aliases
// ---------------------------------------------------------------------------

/// Platform-native file descriptor type.
#ifdef PLATFORM_WINDOWS
using Fd = uintptr_t;
constexpr Fd kInvalidFd = static_cast<Fd>(~0);
#else
using Fd = int;
constexpr Fd kInvalidFd = -1;
#endif

using Duration  = std::chrono::steady_clock::duration;
using TimePoint = std::chrono::steady_clock::time_point;

/// Generic void callback used for timers, close events, posted tasks.
using Callback = std::function<void()>;

} // namespace DeepSeer
