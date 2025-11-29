// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(Swapchain_impl&& old) noexcept
    : m_device               { old.m_device }
    , m_vk_swapchain         { std::exchange(old.m_vk_swapchain, VK_NULL_HANDLE) }
    , m_surface              { old.m_surface }
    , m_swapchain_images     { std::move(old.m_swapchain_images) }
    , m_swapchain_image_views{ std::move(old.m_swapchain_image_views) }
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    if (m_vk_swapchain == VK_NULL_HANDLE) {
        return;
    }
    VkSwapchainKHR vulkan_swapchain = m_vk_swapchain;
    VkDevice       vulkan_device    = m_device.get_impl().get_vulkan_device();
    m_device.add_completion_handler(
        [vulkan_device, vulkan_swapchain]() {
            vkDestroySwapchainKHR(vulkan_device, vulkan_swapchain, nullptr);
        }
    );
}

Swapchain_impl::Swapchain_impl(Device& device, const Swapchain_create_info& create_info)
    : m_device      {device}
    , m_surface     {create_info.surface}
    , m_vk_swapchain{create_info.surface.get_impl().create_swapchain()}
{
    Device_impl&       device_impl    = device.get_impl();
    VkDevice           vulkan_device  = device_impl.get_vulkan_device();
    Surface_impl&      surface_impl   = m_surface.get_impl();
    VkSurfaceFormatKHR surface_format = surface_impl.get_surface_format();

    const VkFenceCreateInfo fence_create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, // VkStructureType
        .pNext = nullptr,                             // const void*
        .flags = 0                                    // VkFenceCreateFlags
    };
    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType
        .pNext = nullptr,                                 // const void*
        .flags = 0                                        // VkSemaphoreCreateFlags
    };

    VkResult result = VK_SUCCESS;
    for (size_t i = 0; i < s_number_of_frames_in_flight; ++i) {
        result = vkCreateFence    (vulkan_device, &fence_create_info,     nullptr, &m_frame_fences[i]);
        if (result != VK_SUCCESS) {
            abort(); // TODO handle error
        }
        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &m_acquire_semaphores[i]);
        if (result != VK_SUCCESS) {
            abort(); // TODO handle error
        }
    }

    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(vulkan_device, m_vk_swapchain, &image_count, nullptr);
    m_swapchain_images.resize(image_count);
    m_swapchain_image_views.resize(image_count);
    vkGetSwapchainImagesKHR(vulkan_device, m_vk_swapchain, &image_count, m_swapchain_images.data());

    for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
        VkImageViewCreateInfo image_view_create_info{
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // VkStructureType
            .pNext      = nullptr,                                  // const void*
            .flags      = 0,                                        // VkImageViewCreateFlags
            .image      = m_swapchain_images[image_index],          // VkImage
            .viewType   = VK_IMAGE_VIEW_TYPE_2D,                    // VkImageViewType
            .format     = surface_format.format,                    // VkFormat
            .components = {                                         // VkComponentMapping
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
                .a = VK_COMPONENT_SWIZZLE_IDENTITY                  // VkComponentSwizzle
            },                                                      
            .subresourceRange = {                                   // VkImageSubresourceRange
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,        // VkImageAspectFlags
                .baseMipLevel   = 0,                                // uint32_t
                .levelCount     = 1,                                // uint32_t
                .baseArrayLayer = 0,                                // uint32_t
                .layerCount     = 1                                 // uint32_t
            }
        };
        result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &m_swapchain_image_views[image_index]);
        if (result != VK_SUCCESS) {
            abort(); // TODO handle error
        }

        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &m_swapchain_image_submit_semaphores[image_index]);
        if (result != VK_SUCCESS) {
            abort(); // TODO handle error
        }
    }
}

auto Swapchain_impl::get_image_count() const -> size_t
{
    ERHE_VERIFY(m_swapchain_images.size() == m_swapchain_image_views.size());
    return m_swapchain_images.size();
}

auto Swapchain_impl::get_image(size_t image_index) const -> VkImage
{
    ERHE_VERIFY(image_index < m_swapchain_images.size());
    return m_swapchain_images[image_index];
}

auto Swapchain_impl::get_image_view(size_t image_index) const -> VkImageView
{
    ERHE_VERIFY(image_index < m_swapchain_image_views.size());
    return m_swapchain_image_views[image_index];
}

} // namespace erhe::graphics
