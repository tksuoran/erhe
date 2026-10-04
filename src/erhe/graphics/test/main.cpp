#include "erhe_graphics/graphics_log.hpp"
#include "erhe_log/log.hpp"

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    erhe::log::initialize_log_sinks();
    erhe::graphics::initialize_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
