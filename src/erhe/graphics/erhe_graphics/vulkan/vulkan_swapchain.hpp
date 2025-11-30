#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"

#include <array>
#include <vector>

namespace erhe::graphics {

class Device_impl;

class Swapchain_frame_in_flight
{
public:
    VkFence     m_fence;
    VkSemaphore m_acquire_semaphore;
};

class Swapchain_image_entry
{
public:
    VkImage     m_image;
    VkImageView m_image_view;
    VkSemaphore m_submit_semaphore;
};

class Swapchain_impl final
{
public:
    Swapchain_impl           (const Swapchain_impl&) = delete;
    Swapchain_impl& operator=(const Swapchain_impl&) = delete;
    Swapchain_impl(Swapchain_impl&&) noexcept;
    Swapchain_impl& operator=(Swapchain_impl&&) = delete;
    ~Swapchain_impl() noexcept;

    Swapchain_impl(Device& device, const Swapchain_create_info& create_info);

    [[nodiscard]] auto get_image_count() const -> size_t;
    [[nodiscard]] auto get_image_entry(size_t image_index) -> Swapchain_image_entry&;
    [[nodiscard]] auto get_image_entry(size_t image_index) const -> const Swapchain_image_entry&;

private:
    static constexpr size_t s_number_of_frames_in_flight = 2;

    Device&                                m_device;
    VkSwapchainKHR                         m_vulkan_swapchain{VK_NULL_HANDLE};
    Surface&                               m_surface;
    std::vector<Swapchain_frame_in_flight> m_frames_in_flight;
    std::vector<Swapchain_image_entry>     m_image_entries;
};

} // namespace erhe::graphics
