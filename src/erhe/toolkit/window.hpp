#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
#   include "erhe/toolkit/glfw_window.hpp"
#endif

#include <string>

namespace erhe::toolkit {

[[nodiscard]] auto format_window_title(const char* window_name) -> std::string;

} // namespace erhe::toolkit