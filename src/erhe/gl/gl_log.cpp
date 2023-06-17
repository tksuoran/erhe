#include "erhe/gl/gl_log.hpp"
#include "erhe/log/log.hpp"

namespace gl
{

std::shared_ptr<spdlog::logger> log_gl;

void initialize_logging()
{
    // To enable logging GL calls, uncomment this version
    //log_gl = erhe::log::make_logger("gl", spdlog::level::trace);
    log_gl = erhe::log::make_logger("gl", spdlog::level::info);
}

} // namespace gl
