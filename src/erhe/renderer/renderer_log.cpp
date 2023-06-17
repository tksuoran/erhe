#include "erhe/renderer/renderer_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::renderer
{

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_program_interface;
std::shared_ptr<spdlog::logger> log_shadow_renderer;

void initialize_logging()
{
    log_draw              = erhe::log::make_logger("draw"                 , spdlog::level::info);
    log_render            = erhe::log::make_logger("render"               , spdlog::level::info, false);
    log_program_interface = erhe::log::make_logger("log_program_interface", spdlog::level::info);
    log_shadow_renderer   = erhe::log::make_logger("shadow_renderer"      , spdlog::level::info, false);
}

} // namespace erhe::renderer
