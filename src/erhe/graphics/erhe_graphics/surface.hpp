#pragma once

#include <memory>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Surface_create_info
{
public:
    erhe::window::Context_window* context_window{nullptr};
    bool prefer_low_bandwidth{false};
    bool prefer_high_dynamic_range{false};
};

} // namespace erhe::graphics

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_surface.hpp"
#endif
