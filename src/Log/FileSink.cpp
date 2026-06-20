#include <DeepSeer/Log/FileSink.hpp>

#include <filesystem>
#include <format>

namespace DeepSeer
{

FileSink::FileSink(FileSinkConfig config)
    : config_{std::move(config)}
{
    openFile();
}

FileSink::~FileSink()
{
    std::lock_guard lock(mu_);
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
    }
}

void
FileSink::write(std::string_view formatted)
{
    std::lock_guard lock(mu_);
    if (!file_)
        return;

    std::fwrite(formatted.data(), 1, formatted.size(), file_);
    currentSize_ += formatted.size();
    rotateIfNeeded();
}

void
FileSink::flush()
{
    std::lock_guard lock(mu_);
    if (file_)
        std::fflush(file_);
}

void
FileSink::rotateIfNeeded()
{
    if (currentSize_ < config_.maxFileSize)
        return;
    rotate();
}

void
FileSink::rotate()
{
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }

    namespace fs = std::filesystem;

    // Delete the oldest rotated file.
    auto const oldest = rotatedPath(config_.maxFiles);
    std::error_code ec;
    fs::remove(oldest, ec);

    // Shift rotated files up by 1: .{N-1} -> .{N}
    for (size_t i = config_.maxFiles; i >= 2; --i) {
        auto const src = rotatedPath(i - 1);
        auto const dst = rotatedPath(i);
        fs::rename(src, dst, ec); // ignore errors (file may not exist yet)
    }

    // Rename active file to .1
    fs::rename(config_.path, rotatedPath(1), ec);

    openFile();
}

void
FileSink::openFile()
{
    file_ = std::fopen(config_.path.c_str(), "ab");
    if (file_) {
        std::fseek(file_, 0, SEEK_END);
        currentSize_ = static_cast<size_t>(std::ftell(file_));
    } else {
        currentSize_ = 0;
    }
}

std::string
FileSink::rotatedPath(size_t index) const
{
    namespace fs = std::filesystem;
    auto const p    = fs::path(config_.path);
    auto const stem = p.stem().string();
    auto const ext  = p.extension().string();
    auto const dir  = p.parent_path();
    return (dir / std::format("{}.{}{}", stem, index, ext)).string();
}

} // namespace DeepSeer
