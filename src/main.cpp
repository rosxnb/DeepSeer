#include <print>

#include <DeepSeer/Config/Config.hpp>
#include <DeepSeer/Event/EventLoop.hpp>


int main()
{
    std::println("Hello DeepSeer");

    std::println("Config: {}", GetConfig());
    std::println("Events: {}", GetConfig());

    return 0;
}
