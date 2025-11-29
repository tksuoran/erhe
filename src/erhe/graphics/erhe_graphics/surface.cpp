// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/surface.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_surface.hpp"
#endif

namespace erhe::graphics {
    
Surface::Surface(Device& device, const Surface_create_info& create_info)
    : m_impl{std::make_unique<Surface_impl>(device, create_info)}
{
}
Surface::~Surface()
{
}
auto Surface::get_impl() -> Surface_impl&
{
    return *m_impl.get();
}
auto Surface::get_impl() const -> const Surface_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
