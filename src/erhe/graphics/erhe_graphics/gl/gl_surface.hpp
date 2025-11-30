#pragma once

namespace erhe::graphics {

class Device_impl;

class Surface final
{
public:
    Surface(Device_impl& device_impl, const Surface_create_info& create_info);
    ~Surface() noexcept;

private:
    Device_impl&        m_device_impl;
    Surface_create_info m_surface_create_info;
};

} // namespace erhe::graphics

