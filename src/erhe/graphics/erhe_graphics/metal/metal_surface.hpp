#pragma once

#include "erhe_graphics/surface.hpp"

#include <cstdint>
#include <memory>

namespace CA { class MetalLayer; }

namespace erhe::graphics {

class Device_impl;
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

private:
    Device_impl&               m_device_impl;
    Surface_create_info        m_surface_create_info;
    std::unique_ptr<Swapchain> m_swapchain;
    CA::MetalLayer*            m_metal_layer{nullptr};
    void*                      m_sdl_metal_view{nullptr};
};

} // namespace erhe::graphics
