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
    using namespace erhe::log;
    log_startup       = make_logger      ("hextiles.startup"      );
    log_frame         = make_frame_logger("hextiles.frame"        );
    log_map_window    = make_logger      ("hextiles.map_window"   );
    log_map_generator = make_logger      ("hextiles.map_generator");
    log_map_editor    = make_logger      ("hextiles.map_editor"   );
    log_new_game      = make_logger      ("hextiles.new_game"     );
    log_tiles         = make_logger      ("hextiles.tiles"        );
    log_file          = make_logger      ("hextiles.file"         );
    log_stream        = make_logger      ("hextiles.stream"       );
    log_image         = make_logger      ("hextiles.image"        );
    log_pixel_lookup  = make_logger      ("hextiles.pixel_lookup" );
}

}
