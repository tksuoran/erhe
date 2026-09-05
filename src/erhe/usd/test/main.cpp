#include "erhe_dataformat/dataformat_log.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_usd/usd_log.hpp"

#include <geogram/basic/common.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // Same bootstrap order as src/erhe/graph/test/main.cpp: the log_*
    // globals are null shared_ptrs until this runs, so the first log call
    // from a library under test would be an access violation.
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");

    erhe::property::initialize_logging();
    erhe::item::initialize_logging();
    erhe::dataformat::initialize_logging();
    erhe::geometry::initialize_logging();
    erhe::primitive::initialize_logging();
    erhe::scene::initialize_logging();
    erhe::usd::initialize_logging();

    // erhe::geometry builds on geogram; the editor does this in its own
    // startup, a test binary has to do it itself.
    GEO::initialize(GEO::GEOGRAM_INSTALL_NONE);
    erhe::geometry::register_geogram_attribute_types();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
