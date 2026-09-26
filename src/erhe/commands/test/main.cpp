#include "erhe_commands/commands_log.hpp"
#include "erhe_file/file_log.hpp"
#include "erhe_log/log.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    erhe::log::initialize_log_sinks();

    // make_logger() reads the logging config through erhe::file, which logs
    // through erhe::file::log_file: bootstrap that one with a plain logger
    // (erhe::file::initialize_logging() would itself call make_logger()).
    erhe::file::log_file = spdlog::stdout_color_mt("erhe.file.bootstrap");

    erhe::commands::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
