#pragma once

#include <spdlog/spdlog.h>

#include <memory>

namespace erhe::graphics {

extern std::shared_ptr<spdlog::logger> log_context;
extern std::shared_ptr<spdlog::logger> log_swapchain;
extern std::shared_ptr<spdlog::logger> log_debug;
extern std::shared_ptr<spdlog::logger> log_framebuffer;
extern std::shared_ptr<spdlog::logger> log_threads;
extern std::shared_ptr<spdlog::logger> log_render_pass;
extern std::shared_ptr<spdlog::logger> log_startup;

void initialize_logging();

} // namespace erhe::graphics
