#include "erhe/scene_renderer/scene_renderer_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::scene_renderer
{

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_program_interface;
std::shared_ptr<spdlog::logger> log_shadow_renderer;
std::shared_ptr<spdlog::logger> log_startup;

void initialize_logging()
{
    log_draw              = erhe::log::make_logger("scene_renderer::draw"                 , spdlog::level::info);
    log_render            = erhe::log::make_logger("scene_renderer::render"               , spdlog::level::info, false);
    log_program_interface = erhe::log::make_logger("scene_renderer::log_program_interface", spdlog::level::info);
    log_shadow_renderer   = erhe::log::make_logger("scene_renderer::shadow_renderer"      , spdlog::level::info, false);
    log_startup           = erhe::log::make_logger("scene_renderer::startup"              , spdlog::level::info);
}

} // namespace erhe::scene_renderer
