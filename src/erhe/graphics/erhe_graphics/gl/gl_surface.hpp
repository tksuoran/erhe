#pragma once

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

private:
    Device_impl& m_device_impl;
};

} // namespace erhe::graphics

