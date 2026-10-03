#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_scene/scene_log.hpp"

#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/common.h>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char** argv)
{
    // Bootstrap the way src/erhe/graph/test/main.cpp does (doc/testing.md):
    // sinks, then erhe::file's logger by hand, then each library's loggers.
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    erhe::property::initialize_logging();
    erhe::item::initialize_logging();
    erhe::math::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::gltf::initialize_logging();

    // As the editor does (editor.cpp "initialize geogram"): Geometry
    // processing (colocate's nearest neighbor search) reads the "algo"
    // command line group.
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    GEO::CmdLine::import_arg_group("algo");
    erhe::geometry::register_geogram_attribute_types();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
