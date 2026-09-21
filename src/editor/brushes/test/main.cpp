#include "editor_log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// Brush_geometry_slot logs through log_brush, which is a null shared_ptr
// until editor::initialize_logging() has run - and that needs the whole
// editor's logging configuration. The one logger the tests reach is bound to
// the default logger instead.
void initialize_test_logging()
{
    editor::log_brush = spdlog::default_logger();
}

int main(int argc, char** argv)
{
    initialize_test_logging();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
