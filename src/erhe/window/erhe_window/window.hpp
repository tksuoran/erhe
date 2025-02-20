#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)
#   include "erhe_window/glfw_window.hpp"
#endif
#if defined(ERHE_WINDOW_LIBRARY_SDL)
#   include "erhe_window/sdl_window.hpp"
#endif

#include <string>

namespace erhe::window {

[[nodiscard]] auto format_window_title(const char* window_name) -> std::string;

} // namespace erhe::window
