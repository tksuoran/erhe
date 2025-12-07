#pragma once

#include "erhe_graphics/surface.hpp"

#include <cstdint>
#include <memory>

namespace erhe::window { class Context_window; }

namespace erhe::graphics {

class Device_impl;
class Surface_create_info;
class Swapchain;

class Surface_impl final
{
public:
    Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info);
    ~Surface_impl() noexcept;

    void resize_swapchain_to_surface(uint32_t& width, uint32_t& height);

    [[nodiscard]] auto get_swapchain() -> Swapchain*;

private:
    Device_impl&               m_device_impl;
    Surface_create_info        m_surface_create_info;
    std::unique_ptr<Swapchain> m_swapchain;
};

} // namespace erhe::graphics

