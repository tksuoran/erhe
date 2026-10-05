#include "editor_log.hpp"

#include "erhe_item/item_log.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_property/property_log.hpp"
#include "erhe_scene/scene_log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// Item, property, scene and editor operation code log unconditionally;
// without this the loggers are null.
void initialize_test_logging()
{
    erhe::item::log                   = spdlog::default_logger();
    erhe::property::log               = spdlog::default_logger();
    erhe::item::log_frame             = spdlog::default_logger();
    erhe::scene::log                  = spdlog::default_logger();
    erhe::scene::log_frame            = spdlog::default_logger();
    erhe::scene::log_mesh_raytrace    = spdlog::default_logger();
    erhe::primitive::log_primitive    = spdlog::default_logger();
    erhe::primitive::log_primitive_builder = spdlog::default_logger();
    editor::log_operations            = spdlog::default_logger();
}

int main(int argc, char** argv)
{
    initialize_test_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
