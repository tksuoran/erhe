#include "hextiles_log.hpp"
#include "erhe/log/log.hpp"

namespace hextiles {

std::shared_ptr<spdlog::logger> log_map_window;
std::shared_ptr<spdlog::logger> log_map_generator;
std::shared_ptr<spdlog::logger> log_map_editor;
std::shared_ptr<spdlog::logger> log_new_game;
std::shared_ptr<spdlog::logger> log_tiles;
std::shared_ptr<spdlog::logger> log_file;
std::shared_ptr<spdlog::logger> log_stream;
std::shared_ptr<spdlog::logger> log_image;
std::shared_ptr<spdlog::logger> log_pixel_lookup;

void initialize_logging()
{
    log_map_window    = erhe::log::make_logger("map_window",    spdlog::level::info);
    log_map_generator = erhe::log::make_logger("map_generator", spdlog::level::info);
    log_map_editor    = erhe::log::make_logger("map_editor",    spdlog::level::info);
    log_new_game      = erhe::log::make_logger("new_game",      spdlog::level::info);
    log_tiles         = erhe::log::make_logger("tiles",         spdlog::level::info);
    log_file          = erhe::log::make_logger("file",          spdlog::level::info);
    log_stream        = erhe::log::make_logger("stream",        spdlog::level::info);
    log_image         = erhe::log::make_logger("image",         spdlog::level::info);
    log_pixel_lookup  = erhe::log::make_logger("pixel_lookup",  spdlog::level::info);
}

}
