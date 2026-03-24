#pragma once

#include "erhe_graphics/swapchain.hpp"

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

class Swapchain_impl final
{
public:
    Swapchain_impl(
        Device_impl&  device_impl,
        Surface_impl& surface_impl
    );
    ~Swapchain_impl() noexcept;

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;
    [[nodiscard]] auto has_depth  () const -> bool;
    [[nodiscard]] auto has_stencil() const -> bool;

private:
    Device_impl&  m_device_impl;
    Surface_impl& m_surface_impl;
};

} // namespace erhe::graphics
