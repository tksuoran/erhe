#include "erhe/renderer/renderer_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::renderer
{

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_startup;
std::shared_ptr<spdlog::logger> log_buffer_writer;
std::shared_ptr<spdlog::logger> log_multi_buffer;

void initialize_logging()
{
    log_draw          = erhe::log::make_logger("renderer::draw"         , spdlog::level::info);
    log_render        = erhe::log::make_logger("renderer::render"       , spdlog::level::info, false);
    log_startup       = erhe::log::make_logger("renderer::startup"      , spdlog::level::info);
    log_buffer_writer = erhe::log::make_logger("renderer::buffer_writer", spdlog::level::info, false);
    log_multi_buffer  = erhe::log::make_logger("renderer::multi_buffer" , spdlog::level::info, false);
}

} // namespace erhe::renderer
