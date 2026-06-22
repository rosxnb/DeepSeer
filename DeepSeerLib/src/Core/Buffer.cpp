#include <DeepSeer/Core/Buffer.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace DeepSeer
{

// ---------------------------------------------------------------------------
// Slice
// ---------------------------------------------------------------------------

Slice::Slice(size_t capacity)
    : storage_{std::make_unique<std::byte[]>(capacity)}
    , capacity_{capacity}
{ }

Slice::Slice(std::byte const* data, size_t len)
    : storage_{std::make_unique<std::byte[]>(len)}
    , capacity_{len}
    , writePos_{len}
{
    std::memcpy(storage_.get(), data, len);
}

std::span<std::byte>
Slice::reservation()
{
    return {storage_.get() + writePos_, capacity_ - writePos_ };
}

void
Slice::commit(size_t n)
{
    assert(writePos_ + n <= capacity_);
    writePos_ += n;
}

std::span<std::byte const>
Slice::data() const
{
    return {storage_.get() + readPos_, writePos_ - readPos_};
}

size_t Slice::length()    const { return writePos_ - readPos_; };
size_t Slice::capacity()  const { return capacity_; };
size_t Slice::available() const { return capacity_ - writePos_; };

void
Slice::drain(size_t n)
{
    assert(n <= length());
    readPos_ += n;
}

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

void
Buffer::add(std::span<std::byte const> data)
{
    totalLength_ += data.size();
    while (!data.empty()) {
        if (slices_.empty() || slices_.back().available() == 0) {
            slices_.emplace_back(std::max(data.size(), Slice::kDefaultCapacity));
        }
        auto reservation = slices_.back().reservation();
        auto n = std::min(reservation.size(), data.size());
        std::memcpy(reservation.data(), data.data(), n);
        slices_.back().commit(n);
        data = data.subspan(n);
    }
}

void
Buffer::add(std::string_view data)
{
    add(std::as_bytes(std::span{data.data(), data.size()}));
}

std::span<std::byte>
Buffer::reserve(size_t hint)
{
    if (slices_.empty() || slices_.back().available() == 0) {
        slices_.emplace_back(std::max(hint, Slice::kDefaultCapacity));
    }
    return slices_.back().reservation();
}

void
Buffer::commit(size_t n)
{
    assert(!slices_.empty());
    slices_.back().commit(n);
    totalLength_ += n;
}

void
Buffer::drain(size_t n)
{
    size_t remaining = n;

    while (remaining > 0 && frontSlice_ < slices_.size()) {
        auto& slice = slices_[frontSlice_];
        auto toDrain = std::min(remaining, slice.length());
        slice.drain(toDrain);
        remaining -= toDrain;
        if (slice.length() == 0) {
            ++frontSlice_;
        }
    }

    totalLength_ -= (n - remaining);

    if (frontSlice_ == slices_.size()) {
        slices_.clear();
        frontSlice_ = 0;
    } else if (frontSlice_ * 2 >= slices_.size()) {
        compactSlices();
    }
}

size_t
Buffer::length() const
{
    return totalLength_;
}

bool Buffer::empty() const { return totalLength_ == 0; }

std::vector<std::byte>
Buffer::linearize() const
{
    std::vector<std::byte> result;
    result.reserve(totalLength_);
    for (size_t i = frontSlice_; i < slices_.size(); ++i) {
        auto d = slices_[i].data();
        result.insert(result.end(), d.begin(), d.end());
    }
    return result;
}

void Buffer::move(Buffer& other)
{
    if (frontSlice_ > 0) compactSlices();

    auto otherBegin = other.slices_.begin() + static_cast<ptrdiff_t>(other.frontSlice_);
    slices_.reserve(slices_.size() + (other.slices_.size() - other.frontSlice_));
    slices_.insert(slices_.end(),
                   std::make_move_iterator(otherBegin),
                   std::make_move_iterator(other.slices_.end()));

    other.slices_.clear();
    other.frontSlice_ = 0;
    totalLength_ += other.totalLength_;
    other.totalLength_ = 0;
}

std::span<Slice const>
Buffer::slices() const
{
    return {slices_.data() + frontSlice_, slices_.size() - frontSlice_};
}

void
Buffer::compactSlices()
{
    slices_.erase(slices_.begin(),
                  slices_.begin() + static_cast<ptrdiff_t>(frontSlice_));
    frontSlice_ = 0;
}

std::string
Buffer::toString() const
{
    auto bytes = linearize();
    return {reinterpret_cast<char const*>(bytes.data()), bytes.size()};
}

} // namespace DeepSeer
