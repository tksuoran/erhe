#pragma once

#include "erhe_graphics/surface.hpp"

#include <vector>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;

class Surface_impl final
{
public:
    Surface_impl(Device& device, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

private:
    Device&             m_device;
    Surface_create_info m_surface_create_info;
};

} // namespace erhe::graphics

