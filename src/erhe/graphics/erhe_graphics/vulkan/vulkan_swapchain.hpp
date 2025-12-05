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
    VkFence         m_fence;
    VkSemaphore     m_acquire_semaphore;
    uint32_t        m_image_index   {0xffu};
    VkResult        m_present_result{VK_SUCCESS};
    VkCommandBuffer m_command_buffer{VK_NULL_HANDLE};
};

class Swapchain_image_entry
{
public:
    VkImage       m_image           {VK_NULL_HANDLE};
    VkImageView   m_image_view      {VK_NULL_HANDLE};
    VkSemaphore   m_submit_semaphore{VK_NULL_HANDLE};
    VkFramebuffer m_framebuffer     {VK_NULL_HANDLE}; // placeholder
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

    void start_of_frame  ();
    void resize_to_window();
    void present         ();

    [[nodiscard]] auto get_image_count() const -> size_t;
    [[nodiscard]] auto get_image_entry(size_t image_index) -> Swapchain_image_entry&;
    [[nodiscard]] auto get_image_entry(size_t image_index) const -> const Swapchain_image_entry&;

private:
    static constexpr size_t s_number_of_frames_in_flight = 2;

    void create_frames_in_flight_resources();
    void create_image_entry_resources();

    // placeholder
    void create_placeholder_renderpass_and_framebuffers();
    void destroy_placeholder_resources();
    void submit_placeholder_renderpass();

    Device&                                m_device;
    Surface&                               m_surface;
    VkSwapchainKHR                         m_vulkan_swapchain{VK_NULL_HANDLE};
    std::vector<Swapchain_frame_in_flight> m_frames_in_flight;
    std::vector<Swapchain_image_entry>     m_image_entries;
    uint64_t                               m_frame_index     {0};

    VkExtent2D                             m_extent;
    VkRenderPass                           m_vulkan_renderpass{VK_NULL_HANDLE}; // placeholder
};

} // namespace erhe::graphics
