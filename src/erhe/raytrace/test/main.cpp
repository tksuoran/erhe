#include "erhe_file/file_log.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_log/log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    erhe::log::initialize_log_sinks();
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");
    erhe::raytrace::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
