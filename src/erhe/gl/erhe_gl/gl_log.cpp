#include "erhe_gl/gl_log.hpp"
#include "erhe_log/log.hpp"

namespace gl {

std::shared_ptr<spdlog::logger> log_gl;

void initialize_logging()
{
    // To enable logging:
    //  - Edit wrapper_functions.cpp, enable #define ERHE_LOG_GL_FUNCTIONS
    //  - edit erhe.json, set gl.trace from off to trace
    log_gl = erhe::log::make_logger("erhe.gl.trace");
}

} // namespace gl
