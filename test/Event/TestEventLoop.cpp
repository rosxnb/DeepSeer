#include "DeepSeer/Event/EventLoop.hpp"

#include <gtest/gtest.h>


TEST(TestEventLoop, CreateAndStop)
{
    auto len = GetEventLoop().length();
    ASSERT_EQ(len, std::string{"Event Loop not programmed yet!"}.length());
}
