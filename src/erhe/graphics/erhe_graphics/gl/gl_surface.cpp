#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl   {device_impl}
    , m_context_window{create_info.context_window}
{
}

Surface_impl::~Surface_impl() noexcept
{
}

auto Surface_impl::get_context_window() const -> erhe::window::Context_window*
{
    return m_context_window;
}

} // namespace erhe::graphics
