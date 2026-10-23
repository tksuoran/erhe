#pragma once

#include "erhe_graphics/surface.hpp"

#include <cstdint>
#include <memory>

namespace CA { class MetalLayer; }

namespace erhe::graphics {

class Device_impl;
class Display_presentation;
enum class Frame_pacing_tier : unsigned int;
class Surface_create_info;
class Swapchain;

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

    [[nodiscard]] auto get_swapchain  () -> Swapchain*;
    [[nodiscard]] auto get_metal_layer() const -> CA::MetalLayer*;

    // True when there is no CAMetalLayer to draw into: the window library is
    // `none`, so the context window hands out no native window. The swapchain
    // then renders into an emulated ring of offscreen textures instead of
    // layer drawables (see Swapchain_impl). Mirrors the Vulkan backend, which
    // keys the same decision on Context_window::has_vulkan_surface().
    [[nodiscard]] auto is_headless() const -> bool;

    // Size of the emulated backbuffer, taken from the context window.
    [[nodiscard]] auto get_backbuffer_width () const -> int;
    [[nodiscard]] auto get_backbuffer_height() const -> int;

    // Configures the layer for the resolved frame pacing tier
    // (Device_impl::init_frame_pacing), before the first drawable is taken.
    // Tier S: display sync on (the backpressure the slop servo senses; the
    // plain FIFO of the Vulkan swapchain) and three drawables. Unlike the
    // Vulkan swapchain's minimum image count, two drawables halve the
    // windowed cadence (measured on a 120 Hz display: 60 Hz with the servo
    // idle) - the compositor holds the displayed drawable until the next one
    // replaces it - while three still block in nextDrawable once the pool is
    // held (~7 ms per frame at 120 Hz on an idle scene). Other tiers keep
    // the layer defaults.
    void apply_frame_pacing_tier(Frame_pacing_tier tier);
    // Services the main thread's run loop once without blocking (its
    // sources and observers, among them the Core Animation commit that puts
    // a window ordered on screen during init onto the display; NSEvents stay
    // queued for the window layer). For the device init present calibration
    // (metal4_device_present_calibration.cpp), which presents before the
    // application's event loop first runs. No-op off the main thread.
    void service_main_run_loop();
    // How the window's display presents (claim C30): the NSScreen
    // refresh-interval range of the window's screen and the window's
    // fullscreen state. A screen whose minimum and maximum refresh
    // intervals differ is a variable refresh rate (Adaptive-Sync,
    // ProMotion) display; driven fullscreen it shows each image at its
    // target time with no compositor grid, while a window on the same
    // display presents through the compositor's fixed grid (measured on a
    // 48-120 Hz panel: 8.344 ms windowed). Main thread.
    [[nodiscard]] auto query_display_presentation() const -> Display_presentation;

private:
    Device_impl&               m_device_impl;
    Surface_create_info        m_surface_create_info;
    std::unique_ptr<Swapchain> m_swapchain;
    CA::MetalLayer*            m_metal_layer{nullptr};
    void*                      m_sdl_metal_view{nullptr};
};

} // namespace erhe::graphics
