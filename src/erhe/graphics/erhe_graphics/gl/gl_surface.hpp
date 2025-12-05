#pragma once

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

    [[nodiscard]] auto get_context_window() const -> erhe::window::Context_window*;

private:
    Device_impl&                  m_device_impl;
    erhe::window::Context_window* m_context_window{nullptr};
};

} // namespace erhe::graphics

