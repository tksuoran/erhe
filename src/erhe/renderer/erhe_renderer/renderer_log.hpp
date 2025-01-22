#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::renderer {

extern std::shared_ptr<spdlog::logger> log_draw;
extern std::shared_ptr<spdlog::logger> log_render;
extern std::shared_ptr<spdlog::logger> log_startup;
extern std::shared_ptr<spdlog::logger> log_buffer_writer;
extern std::shared_ptr<spdlog::logger> log_gpu_ring_buffer;

void initialize_logging();

}
