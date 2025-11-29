// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_window/window.hpp"
#include "erhe_verify/verify.hpp"

#include <sstream>
#include <vector>

namespace erhe::graphics {

Surface_impl::Surface_impl(Device& device, const Surface_create_info& create_info)
    : m_device             {device}
    , m_surface_create_info{create_info}
{
    ERHE_VERIFY(create_info.context_window != nullptr);
}


Surface_impl::~Surface_impl() noexcept
{
}

} // namespace erhe::graphics
