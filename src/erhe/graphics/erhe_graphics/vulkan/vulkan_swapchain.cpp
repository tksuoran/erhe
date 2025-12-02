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
    log_context->debug("Swapchain_impl::Swapchain_impl(Swapchain_impl&&)");
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    log_context->debug("Swapchain_impl::~Swapchain_impl()");
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }

    VkSwapchainKHR vulkan_swapchain = m_vulkan_swapchain;
    VkDevice       vulkan_device    = m_device.get_impl().get_vulkan_device();
    std::vector<Swapchain_frame_in_flight> frames_in_flight = std::move(m_frames_in_flight);
    std::vector<Swapchain_image_entry>     image_entries    = std::move(m_image_entries);

    m_device.add_completion_handler(
        [vulkan_device, vulkan_swapchain, image_entries, frames_in_flight]() {
            log_context->debug("Swapchain destructor completion handler");
            for (const Swapchain_image_entry& entry : image_entries) {
                vkDestroySemaphore  (vulkan_device, entry.m_submit_semaphore, nullptr);
                vkDestroyImageView  (vulkan_device, entry.m_image_view,       nullptr);
                vkDestroyFramebuffer(vulkan_device, entry.m_framebuffer,      nullptr);
            }
            for (const Swapchain_frame_in_flight& frame_in_flight : frames_in_flight) {
                vkDestroyFence    (vulkan_device, frame_in_flight.m_fence,             nullptr);
                vkDestroySemaphore(vulkan_device, frame_in_flight.m_acquire_semaphore, nullptr);
            }
            vkDestroySwapchainKHR(vulkan_device, vulkan_swapchain, nullptr);
        }
    );
}

Swapchain_impl::Swapchain_impl(Device& device, const Swapchain_create_info& create_info)
    : m_device          {device}
    , m_surface         {create_info.surface}
    , m_vulkan_swapchain{create_info.surface.get_impl().update_swapchain(m_extent)}
{
    Device_impl&  device_impl   = device.get_impl();
    Surface_impl& surface_impl  = m_surface.get_impl();
    VkDevice      vulkan_device = device_impl.get_vulkan_device();
    VkResult      result        = VK_SUCCESS;

    // Fences are created initially signaled as swapchain frames in flight
    // are not initially in use and thus we want vkWaitForFences() not to
    // wait the first time.
    const VkFenceCreateInfo fence_create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType
        .pNext = nullptr,                                 // const void*
        .flags = 0                                        // VkSemaphoreCreateFlags
    };

    m_frames_in_flight.resize(s_number_of_frames_in_flight);
    for (size_t i = 0; i < s_number_of_frames_in_flight; ++i) {
        Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight[i];
        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_FENCE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_fence),
            fmt::format("Swapchain frame in flight fence {}", i).c_str()
        );
        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &frame_in_flight.m_acquire_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_acquire_semaphore),
            fmt::format("Swapchain frame in flight acquire semaphore {}", i).c_str()
        );
        frame_in_flight.m_command_buffer = device_impl.allocate_command_buffer();
        if (frame_in_flight.m_command_buffer == VK_NULL_HANDLE) {
            log_context->critical("Allocating command buffer failed");
            abort();
        }
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            reinterpret_cast<uint64_t>(frame_in_flight.m_command_buffer),
            fmt::format("Swapchain frame in flight command buffer {}", i).c_str()
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
    log_context->debug("Got {} swaphain images", image_count);

    m_image_entries.resize(image_count);
    const VkSurfaceFormatKHR surface_format = surface_impl.get_surface_format();
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

    // temp stuff
    const VkAttachmentDescription color_attachment_description{
        .flags          = 0,                                // VkAttachmentDescriptionFlags
        .format         = VK_FORMAT_B8G8R8A8_SRGB,          // VkFormat
        .samples        = VK_SAMPLE_COUNT_1_BIT,            // VkSampleCountFlagBits
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,      // VkAttachmentLoadOp
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,     // VkAttachmentStoreOp
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // VkAttachmentLoadOp
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // VkAttachmentStoreOp
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,        // VkImageLayout
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR   // VkImageLayout
    };

    const VkAttachmentReference color_attachment_reference{
        .attachment = 0,                                       // uint32_t
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // VkImageLayout
    };

    const VkSubpassDescription subpass_description{
        .flags                   = 0,                               // VkSubpassDescriptionFlag
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint
        .inputAttachmentCount    = 0,                               // uint32_t
        .pInputAttachments       = 0,                               // const VkAttachmentReference*
        .colorAttachmentCount    = 1,                               // uint32_t
        .pColorAttachments       = &color_attachment_reference,     // const VkAttachmentReference*
        .pResolveAttachments     = 0,                               // const VkAttachmentReference*
        .pDepthStencilAttachment = 0,                               // const VkAttachmentReference*
        .preserveAttachmentCount = 0,                               // uint32_t
        .pPreserveAttachments    = 0                                // const uint32_t*
    };
    // Make the render pass wait for the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT stage
    // - wait for the implicit subpass before or after the render pass
    // - wait for the swap chain to finish reading from the image
    // - wait before doing writes to color attachment
    const VkSubpassDependency subpass_dependency{
        .srcSubpass      = VK_SUBPASS_EXTERNAL,                           // uint32_t
        .dstSubpass      = 0,                                             // uint32_t
        .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
        .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
        .srcAccessMask   = 0,                                             // VkAccessFlags
        .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // VkAccessFlags
        .dependencyFlags = 0,                                             // VkDependencyFlags
    };

    const VkRenderPassCreateInfo render_pass_create_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, // VkStructureType
        .pNext           = nullptr,                                   // const void*
        .flags           = 0,                                         // VkRenderPassCreateFlags
        .attachmentCount = 1,                                         // uint32_t
        .pAttachments    = &color_attachment_description,             // const VkAttachmentDescription*
        .subpassCount    = 1,                                         // uint32_t
        .pSubpasses      = &subpass_description,                      // const VkSubpassDescription*
        .dependencyCount = 1,                                         // uint32_t
        .pDependencies   = &subpass_dependency,                       // const VkSubpassDependency*
    };

    log_context->debug("Creating renderpass");
    result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_renderpass);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateRenderPass() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_context->debug("Creating image entry framebuffers");
    for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
        Swapchain_image_entry& image_entry = m_image_entries[image_index];

        VkFramebufferCreateInfo framebuffer_create_info{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0, // VkFramebufferCreateFlags    flags;
            .renderPass      = m_renderpass,
            .attachmentCount = 1,
            .pAttachments    = &image_entry.m_image_view,
            .width           = m_extent.width,
            .height          = m_extent.height,
            .layers          = 1
        };

        result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &image_entry.m_framebuffer);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFramebuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
    }
}

void Swapchain_impl::start_of_frame()
{
    size_t fif_index = m_frame_index % m_frames_in_flight.size();
    log_context->debug("Swapchain_impl::start_of_frame() fif_index = {} frame_index = {}", fif_index, m_frame_index);

    Device_impl&               device_impl     = m_device.get_impl();
    VkDevice                   vulkan_device   = device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight.at(fif_index);

    log_context->debug("vkWaitForFences()");
    vkWaitForFences      (vulkan_device, 1, &frame_in_flight.m_fence, VK_TRUE, UINT64_MAX);

    log_context->debug("vkResetFences()");
    vkResetFences        (vulkan_device, 1, &frame_in_flight.m_fence);

    log_context->debug("vkAcquireNextImageKHR()");
    vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame_in_flight.m_acquire_semaphore, VK_NULL_HANDLE, &frame_in_flight.m_image_index);

    log_context->debug("Received image index = {}", frame_in_flight.m_image_index);
}

void Swapchain_impl::end_of_frame()
{
    size_t fif_index = m_frame_index % m_frames_in_flight.size();

    Device_impl&               device_impl           = m_device.get_impl();
    //VkDevice                   vulkan_device         = device_impl.get_vulkan_device();
    VkQueue                    vulkan_graphics_queue = device_impl.get_graphics_queue();
    VkQueue                    vulkan_present_queue  = device_impl.get_present_queue();
    Swapchain_frame_in_flight& frame_in_flight       = m_frames_in_flight.at(fif_index);
    Swapchain_image_entry&     image_entry           = m_image_entries.at(frame_in_flight.m_image_index);

    log_context->debug("Swapchain_impl::end_of_frame() fif_index = {} image_index = {} frame_index = {}", fif_index, frame_in_flight.m_image_index, m_frame_index);

    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pInheritanceInfo = nullptr
    };

    VkResult result = VK_SUCCESS;

    log_context->debug("vkResetCommandBuffer()");
    result = vkResetCommandBuffer(frame_in_flight.m_command_buffer, 0);
    if (result != VK_SUCCESS) {
        log_context->critical("vkResetCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_context->debug("vkBeginCommandBuffer()");
    result = vkBeginCommandBuffer(frame_in_flight.m_command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        log_context->critical("vkBeginCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkClearValue clear_values[2] = {
        {.color = { .float32 = { 0.1f, 0.2f, 0.3f, 1.0f }}},
        {.color = { .float32 = { 0.1f, 0.3f, 0.2f, 1.0f }}}
    };

    const VkRenderPassBeginInfo render_pass_begin_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType
        .pNext           = nullptr,                                  // const void*
        .renderPass      = m_renderpass,                             // VkRenderPass
        .framebuffer     = image_entry.m_framebuffer,                // VkFramebuffer
        .renderArea      = VkRect2D{{0,0}, m_extent},                // VkRect2D
        .clearValueCount = 1,                                        // uint32_t
        .pClearValues    = &clear_values[m_frame_index & 1]          // const VkClearValue*
    };

    log_context->debug("vkCmdBeginRenderPass()");
    vkCmdBeginRenderPass(frame_in_flight.m_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    log_context->debug("vkCmdEndRenderPass()");
    vkCmdEndRenderPass(frame_in_flight.m_command_buffer);

    log_context->debug("vkEndCommandBuffer()");
    result = vkEndCommandBuffer(frame_in_flight.m_command_buffer);
    if (result != VK_SUCCESS) {
        log_context->critical("vkEndCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,          // VkStructureType
        .pNext                = nullptr,                                // const void*
        .waitSemaphoreCount   = 1,                                      // uint32_t
        .pWaitSemaphores      = &frame_in_flight.m_acquire_semaphore,   // const VkSemaphore*
        .pWaitDstStageMask    = &wait_stages,                           // const VkPipelineStageFlags*
        .commandBufferCount   = 1,                                      // uint32_t
        .pCommandBuffers      = &frame_in_flight.m_command_buffer,      // const VkCommandBuffer*
        .signalSemaphoreCount = 1,                                      // uint32_t
        .pSignalSemaphores    = &image_entry.m_submit_semaphore,        // const VkSemaphore*
    };
    log_context->debug("vkQueueSubmit()");
    result = vkQueueSubmit(vulkan_graphics_queue, 1, &submit_info, frame_in_flight.m_fence);
    if (result != VK_SUCCESS) {
        log_context->critical("vkQueueSubmit() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkPresentInfoKHR present_info{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, // VkStructureType
        .pNext              = nullptr,                            // const void*
        .waitSemaphoreCount = 1,                                  // uint32_t
        .pWaitSemaphores    = &image_entry.m_submit_semaphore,    // const VkSemaphore*
        .swapchainCount     = 1,                                  // uint32_t
        .pSwapchains        = &m_vulkan_swapchain,                // const VkSwapchainKHR*
        .pImageIndices      = &frame_in_flight.m_image_index,     // const uint32_t*
        .pResults           = &frame_in_flight.m_present_result   // VkResult*
    };
    log_context->debug("vkQueuePresentKHR()");
    result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    switch (result) {
        case VK_SUCCESS:
            break;
        case VK_SUBOPTIMAL_KHR:
            break;
        default:
            break;
    }

    log_context->debug("Advancing to next frame");
    ++m_frame_index;
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
