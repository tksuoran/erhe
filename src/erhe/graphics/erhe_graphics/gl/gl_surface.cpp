#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl{device_impl}
{
    ERHE_VERIFY(create_info.context_window != nullptr);
    static_cast<void>(create_info);
}

Surface_impl::~Surface_impl() noexcept
{
}

} // namespace erhe::graphics
