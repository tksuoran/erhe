#include "editor_log.hpp"

#include "erhe_asset_store/asset_store_log.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_gltf/gltf_log.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_math/math_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_log/log.hpp"

#include <geogram/basic/command_line.h>
#include <geogram/basic/command_line_args.h>
#include <geogram/basic/common.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // 1. Create spdlog sinks (console, file, store)
    erhe::log::initialize_log_sinks();

    // 2. Bootstrap erhe::file::log_file with a simple logger.
    //    Cannot use erhe::file::initialize_logging() here because it calls
    //    make_logger() -> get_ini_file_section() -> erhe::file::read() ->
    //    log_file->warn() while log_file is still nullptr (circular dependency).
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");

    // 3. Now safe to initialize item logging (make_logger reads logging config
    //    via erhe::file, which now has a valid logger)
    erhe::property::initialize_logging();
    erhe::item::initialize_logging();
    erhe::asset_store::initialize_logging();
    erhe::math::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::graphics::initialize_logging();
    erhe::raytrace::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::gltf::initialize_logging();
    editor::initialize_logging();

    // As the editor does: the load's preparation builds Geometry (colocate reads the "algo" group).
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    GEO::CmdLine::import_arg_group("algo");
    erhe::geometry::register_geogram_attribute_types();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
