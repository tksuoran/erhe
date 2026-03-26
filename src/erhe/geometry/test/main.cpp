#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/common.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

void initialize_test_logging()
{
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);

    erhe::geometry::log_geometry          = spdlog::default_logger();
    erhe::geometry::log_geogram           = spdlog::default_logger();
    erhe::geometry::log_build_edges       = spdlog::default_logger();
    erhe::geometry::log_tangent_gen       = spdlog::default_logger();
    erhe::geometry::log_cone              = spdlog::default_logger();
    erhe::geometry::log_torus             = spdlog::default_logger();
    erhe::geometry::log_sphere            = spdlog::default_logger();
    erhe::geometry::log_polygon_texcoords = spdlog::default_logger();
    erhe::geometry::log_interpolate       = spdlog::default_logger();
    erhe::geometry::log_operation         = spdlog::default_logger();
    erhe::geometry::log_catmull_clark     = spdlog::default_logger();
    erhe::geometry::log_triangulate       = spdlog::default_logger();
    erhe::geometry::log_subdivide         = spdlog::default_logger();
    erhe::geometry::log_attribute_maps    = spdlog::default_logger();
    erhe::geometry::log_merge             = spdlog::default_logger();
    erhe::geometry::log_weld              = spdlog::default_logger();
}

int main(int argc, char** argv)
{
    initialize_test_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
