#pragma once

/// @file Buffer.hpp
/// @brief Slice-based buffer for zero-copy network I/O.
///
/// ## Design (inspired by Envoy's Buffer::OwnedImpl)
///
/// A Buffer is a **non-contiguous** byte container made of a vector of Slices.
/// Each Slice is a contiguous 16 KiB memory region with three logical zones:
///
/// ```
///   ┌──────────┬──────────────┬──────────────┐
///   │ drained  │    data      │  reservable  │
///   │(consumed)│  (readable)  │  (writable)  │
///   └──────────┴──────────────┴──────────────┘
///   0        readPos       writePos       capacity
/// ```
///
/// ## Key operations
///
/// - **add()**: Append data. Allocates new Slices as needed.
/// - **reserve()/commit()**: Zero-copy write pattern for socket reads.
///   Call reserve() to get writable memory, read(fd, ...) into it, then commit().
/// - **drain()**: Consume data from the front (after processing).
/// - **move()**: Transfer all Slices from one Buffer to another in O(1).
///   Used to pass data between proxy components without copying.
/// - **linearize()**: Copy all Slices into a single contiguous vector.
///   Needed for parsers that require contiguous input (e.g., llhttp).
///   Avoid in hot paths — prefer streaming.
/// - **slices()**: Direct access to the Slice vector for iteration.
///
/// ## Ownership
///
/// Buffer owns its Slices. Slices own their storage (unique_ptr<byte[]>).
/// Move semantics transfer ownership. No shared state.
///
/// ## Thread safety
///
/// Not thread-safe. Each Buffer should be accessed from a single thread
/// (the EventLoop thread that owns the Connection).

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace DeepSeer
{

/// A single contiguous memory region within a Buffer.
class Slice
{
public:
    static constexpr size_t kDefaultCapacity = 16384; // 16 KiB

    /// Allocate a slice with the given capacity (all reservable, no data).
    explicit Slice(size_t capacity = kDefaultCapacity);

    /// Copy-construct a slice from existing data (data region = full capacity).
    Slice(std::byte const* data, size_t len);

    /// Get the writable (reservable) region. Write into this span, then call commit().
    std::span<std::byte> reservation();

    /// Mark n bytes of the reservation as written (advances writePos).
    void commit(size_t n);

    /// Get the readable data region.
    std::span<std::byte const> data() const;

    size_t length() const;    ///< Bytes of readable data (writePos - readPos)
    size_t capacity() const;  ///< Total allocated size
    size_t available() const; ///< Bytes of writable space (capacity - writePos)

    /// Consume n bytes from the front (advances readPos).
    void drain(size_t n);

private:
    std::unique_ptr<std::byte[]> storage_;
    size_t capacity_;
    size_t readPos_  = 0;
    size_t writePos_ = 0;
};

/// Non-contiguous buffer composed of Slices.
/// Primary data container for network I/O throughout the proxy.
class Buffer
{
public:
    /// Append raw bytes. Allocates new Slices as needed.
    void add(std::span<std::byte const> data);

    /// Append a string (convenience wrapper over add(bytes)).
    void add(std::string_view data);

    /// Get writable memory for direct socket I/O. Caller must call commit() after writing.
    /// @param hint Suggested reservation size (may get more).
    std::span<std::byte> reserve(size_t hint = Slice::kDefaultCapacity);

    /// Finalize a previous reserve() — mark n bytes as written.
    void commit(size_t n);

    /// Consume n bytes from the front of the buffer.
    void drain(size_t n);

    /// Total readable bytes across all Slices.
    size_t length() const;
    bool empty() const;

    /// Copy all data into a single contiguous vector.
    /// O(n) — use sparingly (needed for parsers requiring contiguous input).
    std::vector<std::byte> linearize() const;

    /// Convenience: linearize and convert to std::string.
    std::string toString() const;

    /// Transfer all data from `other` into this buffer. `other` becomes empty.
    /// Moves Slice objects (no data copy). Used to pass data between components.
    void move(Buffer& other);

    /// Direct read-only access to the active Slices.
    /// WARNING: Do not modify the buffer while iterating — drain() may invalidate the span.
    std::span<Slice const> slices() const;

private:
    void compactSlices();

    std::vector<Slice> slices_;
    size_t totalLength_  = 0;
    size_t frontSlice_   = 0;
};

} // namespace DeepSeer
