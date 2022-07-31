#include "erhe/gl/gl_log.hpp"
#include "erhe/log/log.hpp"

namespace gl
{

std::shared_ptr<spdlog::logger> log_gl;

void initialize_logging()
{
    log_gl = erhe::log::make_logger("gl", spdlog::level::info);
}

} // namespace gl
