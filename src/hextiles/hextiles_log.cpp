#include "hextiles_log.hpp"
#include "erhe/log/log.hpp"

namespace hextiles {

std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_frame;
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
    log_startup       = erhe::log::make_logger("hextiles::startup",       spdlog::level::info);
    log_frame         = erhe::log::make_logger("hextiles::frame",         spdlog::level::info, false);
    log_map_window    = erhe::log::make_logger("hextiles::map_window",    spdlog::level::info);
    log_map_generator = erhe::log::make_logger("hextiles::map_generator", spdlog::level::info);
    log_map_editor    = erhe::log::make_logger("hextiles::map_editor",    spdlog::level::info);
    log_new_game      = erhe::log::make_logger("hextiles::new_game",      spdlog::level::info);
    log_tiles         = erhe::log::make_logger("hextiles::tiles",         spdlog::level::info);
    log_file          = erhe::log::make_logger("hextiles::file",          spdlog::level::info);
    log_stream        = erhe::log::make_logger("hextiles::stream",        spdlog::level::info);
    log_image         = erhe::log::make_logger("hextiles::image",         spdlog::level::info);
    log_pixel_lookup  = erhe::log::make_logger("hextiles::pixel_lookup",  spdlog::level::info);
}

}
