#include "erhe_graphics/surface.hpp"
#include "erhe_window/window.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Surface::Surface(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
{
    ERHE_VERIFY(create_info.context_window != nullptr);
}

Surface::~Surface() noexcept
{
}

} // namespace erhe::graphics
