#pragma once

#include <string_view>

namespace erhe::window {

class Context_window;

// Initializes RenderDoc frame capture support by loading the RenderDoc
// capture library. When library_path_override is non-empty, that library is
// loaded instead of the system-installed one (for an experimental RenderDoc
// fork); on Vulkan the loader's implicit layer search path is pointed at the
// override's directory and the system RenderDoc capture layers are disabled.
// This must be called before vkCreateInstance so the env-var changes take
// effect for the Vulkan loader.
void initialize_frame_capture(std::string_view library_path_override = {});
[[nodiscard]] auto get_renderdoc_api() -> void*;

} // namespace erhe::window
