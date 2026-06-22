#include <DeepSeer/Event/EventLoop.hpp>

#if PLATFORM_MACOS
#include "KqueueLoop.hpp"
#elif PLATFORM_LINUX
#include "EpollLoop.hpp"
#elif PLATFORM_WINDOWS
#include "IocpLoop.hpp"
#else
#error "unsupported platform for eventloop"
#endif

namespace DeepSeer
{

std::unique_ptr<EventLoop>
EventLoop::create()
{
#if PLATFORM_MACOS
    return std::make_unique<KqueueLoop>();
#elif PLATFORM_LINUX
    return std::make_unique<EpollLoop>();
#elif PLATFORM_WINDOWS
    return std::make_unique<IocpLoop>();
#endif
}

} // namespace DeepSeer
