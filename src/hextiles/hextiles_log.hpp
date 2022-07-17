#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace hextiles {

extern std::shared_ptr<spdlog::logger> log_map_window;
extern std::shared_ptr<spdlog::logger> log_map_generator;
extern std::shared_ptr<spdlog::logger> log_map_editor;
extern std::shared_ptr<spdlog::logger> log_new_game;
extern std::shared_ptr<spdlog::logger> log_tiles;
extern std::shared_ptr<spdlog::logger> log_file;
extern std::shared_ptr<spdlog::logger> log_stream;
extern std::shared_ptr<spdlog::logger> log_image;
extern std::shared_ptr<spdlog::logger> log_pixel_lookup;

void initialize_logging();

}
