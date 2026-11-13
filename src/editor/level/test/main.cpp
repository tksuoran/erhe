#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/common.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// As src/erhe/geometry/test/main.cpp: geogram, then the geometry loggers the kit generators can reach.
void initialize_test_logging()
{
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    erhe::geometry::log_geometry = spdlog::default_logger();
    erhe::geometry::log_geogram  = spdlog::default_logger();
}

int main(int argc, char** argv)
{
    initialize_test_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
