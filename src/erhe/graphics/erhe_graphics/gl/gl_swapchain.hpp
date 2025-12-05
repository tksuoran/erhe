#pragma once

#include "erhe_graphics/swapchain.hpp"

namespace erhe::window { class Context_window; }

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

    void resize_to_window();
    void present         ();

private:
    Device&                       m_device;
    Surface&                      m_surface;
    erhe::window::Context_window* m_context_window{nullptr};
};

} // namespace erhe::graphics
