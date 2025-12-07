#pragma once

#include "erhe_graphics/swapchain.hpp"
#include "erhe_verify/verify.hpp"

#include "volk.h"

#include <array>
#include <vector>

namespace erhe::graphics {

class Device_impl;
class Surface_impl;

class Swapchain_frame_in_flight
{
public:
    VkFence         m_submit_fence     {VK_NULL_HANDLE};
    VkFence         m_present_fence    {VK_NULL_HANDLE};
    VkSemaphore     m_acquire_semaphore{VK_NULL_HANDLE};;
    uint32_t        m_image_index      {0xffu};
    VkResult        m_present_result   {VK_SUCCESS};
    VkCommandBuffer m_command_buffer   {VK_NULL_HANDLE};
    bool            m_present_submitted{false};
    bool            m_presented        {false};
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
    Swapchain_impl(
        Device_impl&   device_impl,
        Surface_impl&  surface_impl,
        VkSwapchainKHR vulkan_swapchain,
        VkExtent2D     extent
    );
    ~Swapchain_impl() noexcept;

    void start_of_frame  ();
    void present         ();

    void update_vulkan_swapchain(VkSwapchainKHR vulkan_swapchain, VkExtent2D extent);

    [[nodiscard]] auto get_image_count() const -> size_t;
    [[nodiscard]] auto get_image_entry(size_t image_index) -> Swapchain_image_entry&;
    [[nodiscard]] auto get_image_entry(size_t image_index) const -> const Swapchain_image_entry&;

private:
    void create_frames_in_flight_resources();
    void create_image_entry_resources();

    // placeholder
    void create_placeholder_renderpass_and_framebuffers();
    void destroy_placeholder_resources();
    void submit_placeholder_renderpass();

    Device_impl&                           m_device_impl;
    Surface_impl&                          m_surface_impl;
    VkSwapchainKHR                         m_vulkan_swapchain{VK_NULL_HANDLE};
    std::vector<Swapchain_frame_in_flight> m_frames_in_flight;
    std::vector<Swapchain_image_entry>     m_image_entries;
    VkExtent2D                             m_extent;
    VkRenderPass                           m_vulkan_renderpass{VK_NULL_HANDLE}; // placeholder
    bool                                   m_is_valid         {true};
};

} // namespace erhe::graphics
