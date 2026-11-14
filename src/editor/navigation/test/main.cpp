#include "editor_log.hpp"

#include "erhe_file/file_log.hpp"
#include "erhe_log/log.hpp"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char** argv)
{
    // As src/erhe/graph/test/main.cpp: sinks, then erhe::file's logger by hand, then the editor categories.
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    editor::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
