#include "erhe_item/item_log.hpp"
#include "erhe_scene/scene_log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

void initialize_test_logging()
{
    // Hierarchy / item code logs unconditionally (Hierarchy::attach_to_parent,
    // sanity checks); without this the loggers are null and any node parenting
    // in a test dereferences null.
    erhe::item::log                = spdlog::default_logger();
    erhe::item::log_frame          = spdlog::default_logger();
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
