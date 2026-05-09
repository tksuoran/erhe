#include "erhe_graphics/scoped_debug_group.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_scoped_debug_group.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_scoped_debug_group.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_scoped_debug_group.hpp"
#endif

namespace erhe::graphics {

Scoped_debug_group::Scoped_debug_group(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label)
    : m_impl{command_buffer, debug_label}
{
}

Scoped_debug_group::~Scoped_debug_group() noexcept = default;

} // namespace erhe::graphics
