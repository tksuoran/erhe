#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include <array>
#include <vector>

namespace erhe::graphics {

class Device_impl;

class Swapchain_impl final
{
public:
    Swapchain_impl           (const Swapchain_impl&) = delete;
    Swapchain_impl& operator=(const Swapchain_impl&) = delete;
    Swapchain_impl(Swapchain_impl&&) noexcept;
    Swapchain_impl& operator=(Swapchain_impl&&) = delete;
    ~Swapchain_impl() noexcept;

    Swapchain_impl(Device& device, const Swapchain_create_info& create_info);

private:
    Device&  m_device;
    Surface& m_surface;
};

} // namespace erhe::graphics
