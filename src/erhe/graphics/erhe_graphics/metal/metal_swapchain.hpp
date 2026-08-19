#pragma once

#include "erhe_graphics/swapchain.hpp"

namespace CA { class MetalDrawable; }
namespace MTL { class CommandBuffer; }

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
    [[nodiscard]] auto has_depth       () const -> bool;
    [[nodiscard]] auto has_stencil    () const -> bool;
    [[nodiscard]] auto get_color_format() const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format() const -> erhe::dataformat::Format;

    [[nodiscard]] auto get_current_drawable() const -> CA::MetalDrawable*;

    // Releases the reference taken in begin_frame(). Called by
    // Device_impl::submit_command_buffers once presentDrawable has been
    // encoded - the drawable must not be held past that point, or the
    // layer's small drawable pool runs dry.
    void clear_current_drawable();

private:
    Device_impl&      m_device_impl;
    Surface_impl&     m_surface_impl;
    CA::MetalDrawable* m_current_drawable{nullptr};
};

} // namespace erhe::graphics
