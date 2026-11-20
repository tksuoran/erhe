#include "erhe_geometry/geometry_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_scene/scene_log.hpp"

#include <geogram/basic/common.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// As src/erhe/geometry/test/main.cpp and src/erhe/scene/test/main.cpp: geogram, then the loggers the kit generators and the
// kit layout node tests reach.
void initialize_test_logging()
{
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    erhe::geometry::log_geometry = spdlog::default_logger();
    erhe::geometry::log_geogram  = spdlog::default_logger();
    erhe::item::log                = spdlog::default_logger();
    erhe::item::log_frame          = spdlog::default_logger();
    erhe::property::log            = spdlog::default_logger();
    erhe::scene::log               = spdlog::default_logger();
    erhe::scene::log_frame         = spdlog::default_logger();
    erhe::scene::log_mesh_raytrace = spdlog::default_logger();
}

int main(int argc, char** argv)
{
    initialize_test_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
