#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
#   include "erhe_window/glfw_window.hpp"
#endif

#include <string>

namespace erhe::window {

[[nodiscard]] auto format_window_title(const char* window_name) -> std::string;

}
