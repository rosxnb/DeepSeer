#include <DeepSeer/Log/ConsoleSink.hpp>

#include <cstdio>

namespace DeepSeer
{

void
ConsoleSink::write(std::string_view formatted)
{
    std::lock_guard lock(mu_);
    std::fwrite(formatted.data(), 1, formatted.size(), stderr);
}

void
ConsoleSink::flush()
{
    std::lock_guard lock(mu_);
    std::fflush(stderr);
}

} // namespace DeepSeer
