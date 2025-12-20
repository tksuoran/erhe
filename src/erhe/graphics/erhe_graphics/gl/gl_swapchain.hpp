#pragma once

#include "erhe_graphics/swapchain.hpp"

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

class Swapchain_impl final
{
public:
    Swapchain_impl(
        Device_impl&                  device_impl,
        Surface_impl&                 surface_impl,
        erhe::window::Context_window* context_window
    );
    ~Swapchain_impl() noexcept;

    void wait_frame (Frame_state& out_frame_state);
    void begin_frame(const Frame_begin_info& frame_begin_info);
    void end_frame  (const Frame_end_info& frame_end_info);

private:
    Device_impl&                  m_device_impl;
    Surface_impl&                 m_surface_impl;
    erhe::window::Context_window* m_context_window{nullptr};
};

} // namespace erhe::graphics
