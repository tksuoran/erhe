#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"

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

    [[nodiscard]] auto get_image_count() const -> size_t;
    [[nodiscard]] auto get_image      (size_t image_index) const -> VkImage;
    [[nodiscard]] auto get_image_view (size_t image_index) const -> VkImageView;

private:
    static constexpr size_t s_number_of_frames_in_flight = 2;
    Device&                  m_device;
    VkSwapchainKHR           m_vk_swapchain{VK_NULL_HANDLE};
    Surface&                 m_surface;
    std::array<VkFence,     s_number_of_frames_in_flight> m_frame_fences;
    std::array<VkSemaphore, s_number_of_frames_in_flight> m_acquire_semaphores;
    std::vector<VkImage>     m_swapchain_images;
    std::vector<VkImageView> m_swapchain_image_views;
    std::vector<VkSemaphore> m_swapchain_image_submit_semaphores;
};

} // namespace erhe::graphics
