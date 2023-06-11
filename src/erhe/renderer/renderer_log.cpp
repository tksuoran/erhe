#include "erhe/renderer/renderer_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::renderer
{

std::shared_ptr<spdlog::logger> log_draw;
std::shared_ptr<spdlog::logger> log_render;
std::shared_ptr<spdlog::logger> log_program_interface;

void initialize_logging()
{
    log_draw              = erhe::log::make_logger("erhe::renderer::draw"                 , spdlog::level::info);
    log_render            = erhe::log::make_logger("erhe::renderer::render"               , spdlog::level::info, false);
    log_program_interface = erhe::log::make_logger("erhe::renderer::log_program_interface", spdlog::level::info);
}

} // namespace erhe::renderer
