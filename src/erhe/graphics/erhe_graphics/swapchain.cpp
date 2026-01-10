// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/swapchain.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_swapchain.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#endif

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {
    
Swapchain::Swapchain(std::unique_ptr<Swapchain_impl>&& swapchain_impl)
    : m_impl{std::move(swapchain_impl)}
{
}

Swapchain::~Swapchain() noexcept
{
}

auto Swapchain::wait_frame(Frame_state& out_frame_state) -> bool
{
    return m_impl->wait_frame(out_frame_state);
}

auto Swapchain::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    return m_impl->begin_frame(frame_begin_info);
}

auto Swapchain::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    return m_impl->end_frame(frame_end_info);
}

auto Swapchain::has_depth() const -> bool
{
    return m_impl->has_depth();
}
auto Swapchain::has_stencil() const -> bool
{
    return m_impl->has_stencil();
}

auto Swapchain::get_impl() -> Swapchain_impl&
{
    return *m_impl.get();
}

auto Swapchain::get_impl() const -> const Swapchain_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
