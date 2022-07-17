#include "erhe/raytrace/raytrace_log.hpp"
#include "erhe/log/log.hpp"

namespace erhe::raytrace
{

std::shared_ptr<spdlog::logger> log_buffer  ;
std::shared_ptr<spdlog::logger> log_device  ;
std::shared_ptr<spdlog::logger> log_geometry;
std::shared_ptr<spdlog::logger> log_scene   ;
std::shared_ptr<spdlog::logger> log_embree  ;

void initialize_logging()
{
    log_buffer   = erhe::log::make_logger("erhe::raytrace::buffer",   spdlog::level::info);
    log_device   = erhe::log::make_logger("erhe::raytrace::device",   spdlog::level::info);
    log_geometry = erhe::log::make_logger("erhe::raytrace::geometry", spdlog::level::info);
    log_scene    = erhe::log::make_logger("erhe::raytrace::scene",    spdlog::level::info);
    log_embree   = erhe::log::make_logger("erhe::raytrace::embree",   spdlog::level::info);
}

} // namespace erhe::raytrace
