// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(Swapchain_impl&& old) noexcept
    : m_device          { old.m_device }
    , m_vulkan_swapchain{ std::exchange(old.m_vulkan_swapchain, VK_NULL_HANDLE) }
    , m_surface         { old.m_surface }
    , m_frames_in_flight{ std::move(old.m_frames_in_flight) }
    , m_image_entries   { std::move(old.m_image_entries) }
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }
    VkSwapchainKHR vulkan_swapchain = m_vulkan_swapchain;
    VkDevice       vulkan_device    = m_device.get_impl().get_vulkan_device();
    m_device.add_completion_handler(
        [vulkan_device, vulkan_swapchain]() {
            vkDestroySwapchainKHR(vulkan_device, vulkan_swapchain, nullptr);
        }
    );
}

Swapchain_impl::Swapchain_impl(Device& device, const Swapchain_create_info& create_info)
    : m_device          {device}
    , m_surface         {create_info.surface}
    , m_vulkan_swapchain{create_info.surface.get_impl().create_swapchain()}
{
    Device_impl&       device_impl    = device.get_impl();
    Surface_impl&      surface_impl   = m_surface.get_impl();
    VkDevice           vulkan_device  = device_impl.get_vulkan_device();
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
    m_frames_in_flight.resize(s_number_of_frames_in_flight);
    for (size_t i = 0; i < s_number_of_frames_in_flight; ++i) {
        Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight[i];
        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_FENCE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_fence),
            fmt::format("Swapchain frame in flight fence {}", i).c_str()
        );
        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &frame_in_flight.m_acquire_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_acquire_semaphore),
            fmt::format("Swapchain frame in flight acquire semaphore {}", i).c_str()
        );
    }

    uint32_t image_count = 0;
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    std::vector<VkImage> images(image_count);
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, images.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    m_image_entries.resize(image_count);
    for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
        Swapchain_image_entry& image_entry = m_image_entries[image_index];
        image_entry.m_image = images[image_index];
        m_device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_IMAGE,
            reinterpret_cast<uint64_t>(image_entry.m_image),
            fmt::format("Swapchain image {}", image_index).c_str()
        );
        VkImageViewCreateInfo image_view_create_info{
            .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // VkStructureType
            .pNext      = nullptr,                                  // const void*
            .flags      = 0,                                        // VkImageViewCreateFlags
            .image      = images[image_index],                      // VkImage
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
        result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &image_entry.m_image_view);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateImageView() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_IMAGE_VIEW,
            reinterpret_cast<uint64_t>(image_entry.m_image_view),
            fmt::format("Swapchain image view {}", image_index).c_str()
        );

        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &image_entry.m_submit_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device.get_impl().set_debug_label(
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<uint64_t>(image_entry.m_submit_semaphore),
            fmt::format("Swapchain image submit semaphore {}", image_index).c_str()
        );
    }
}

auto Swapchain_impl::get_image_count() const -> size_t
{
    return m_image_entries.size();
}

auto Swapchain_impl::get_image_entry(size_t image_index) -> Swapchain_image_entry&
{
    ERHE_VERIFY(image_index < m_image_entries.size());
    return m_image_entries[image_index];
}

auto Swapchain_impl::get_image_entry(size_t image_index) const -> const Swapchain_image_entry&
{
    ERHE_VERIFY(image_index < m_image_entries.size());
    return m_image_entries[image_index];
}

} // namespace erhe::graphics
