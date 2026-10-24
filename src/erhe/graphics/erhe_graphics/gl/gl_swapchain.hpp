#pragma once

#include "erhe_graphics/swapchain.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

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

    [[nodiscard]] auto wait_frame (Frame_state& out_frame_state) -> bool;
    [[nodiscard]] auto begin_frame(const Frame_begin_info& frame_begin_info) -> bool;
    [[nodiscard]] auto end_frame  (const Frame_end_info& frame_end_info) -> bool;
    // Drive swap_buffers. Called by Device_impl::submit_command_buffers
    // for any cb that engaged this swapchain via begin_swapchain
    // (implicit-present path).
    [[nodiscard]] auto present    () -> bool;
    [[nodiscard]] auto has_depth       () const -> bool;
    [[nodiscard]] auto has_stencil    () const -> bool;
    [[nodiscard]] auto get_color_format() const -> erhe::dataformat::Format;
    [[nodiscard]] auto get_depth_format() const -> erhe::dataformat::Format;

    // Screenshot capture (Device::capture_last_frame), the same one-shot
    // arm-then-collect protocol as the Vulkan and Metal swapchains:
    // request_capture() arms it, present() reads the composited back buffer
    // just before swapping, and read_back_capture() hands out those pixels
    // as tightly packed, top-down, opaque RGBA8 rows.
    void               request_capture  ();
    [[nodiscard]] auto read_back_capture(uint32_t& out_width, uint32_t& out_height, std::vector<std::byte>& out_rgba8) -> bool;

private:
    void record_capture();

    Device_impl&                  m_device_impl;
    Surface_impl&                 m_surface_impl;
    erhe::window::Context_window* m_context_window{nullptr};

    bool                          m_capture_requested  {false};
    bool                          m_capture_ready      {false};
    uint64_t                      m_capture_frame_index{0};
    uint32_t                      m_capture_width      {0};
    uint32_t                      m_capture_height     {0};
    std::vector<std::byte>        m_capture_pixels;
};

} // namespace erhe::graphics
