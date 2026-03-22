#include "erhe_file/file_log.hpp"
#include "erhe_item/item_log.hpp"
#include "erhe_log/log.hpp"

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

    // 3. Now safe to initialize item logging (make_logger reads logging.toml
    //    via erhe::file, which now has a valid logger)
    erhe::item::initialize_logging();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
