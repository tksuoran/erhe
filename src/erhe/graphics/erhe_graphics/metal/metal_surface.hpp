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

private:
    Device_impl&               m_device_impl;
    Surface_create_info        m_surface_create_info;
    std::unique_ptr<Swapchain> m_swapchain;
    CA::MetalLayer*            m_metal_layer{nullptr};
    void*                      m_sdl_metal_view{nullptr};
};

} // namespace erhe::graphics
