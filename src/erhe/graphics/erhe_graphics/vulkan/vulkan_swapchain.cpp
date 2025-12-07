#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&   device_impl,
    Surface_impl&  surface_impl,
    VkSwapchainKHR vulkan_swapchain,
    VkExtent2D     extent
)
    : m_device_impl     {device_impl}
    , m_surface_impl    {surface_impl}
    , m_vulkan_swapchain{vulkan_swapchain}
    , m_extent          {extent}
{
    create_frames_in_flight_resources();
    create_image_entry_resources();

    // TODO Once we have renderpass implemented properly, this may go away
    create_placeholder_renderpass_and_framebuffers();
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    log_context->debug("Swapchain_impl::~Swapchain_impl()");
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }

    VkSwapchainKHR vulkan_swapchain  = m_vulkan_swapchain;
    VkRenderPass   vulkan_renderpass = m_vulkan_renderpass;
    VkDevice       vulkan_device     = m_device_impl.get_vulkan_device();
    std::vector<Swapchain_frame_in_flight> frames_in_flight = std::move(m_frames_in_flight);
    std::vector<Swapchain_image_entry>     image_entries    = std::move(m_image_entries);

    m_device_impl.add_completion_handler(
        [vulkan_device, vulkan_swapchain, vulkan_renderpass, image_entries, frames_in_flight]() {
            log_context->debug("Swapchain destructor completion handler");
            for (const Swapchain_image_entry& entry : image_entries) {
                vkDestroySemaphore  (vulkan_device, entry.m_submit_semaphore, nullptr);
                vkDestroyImageView  (vulkan_device, entry.m_image_view,       nullptr);
                vkDestroyFramebuffer(vulkan_device, entry.m_framebuffer,      nullptr); // placeholder
            }
            for (const Swapchain_frame_in_flight& frame_in_flight : frames_in_flight) {
                vkDestroyFence    (vulkan_device, frame_in_flight.m_submit_fence,      nullptr);
                vkDestroySemaphore(vulkan_device, frame_in_flight.m_acquire_semaphore, nullptr);
            }
            vkDestroySwapchainKHR(vulkan_device, vulkan_swapchain,  nullptr);
            vkDestroyRenderPass  (vulkan_device, vulkan_renderpass, nullptr); // placeholder
        }
    );
}

void Swapchain_impl::destroy_placeholder_resources()
{
    log_context->debug("Swapchain_impl::destroy_placeholder_resources()");

    log_context->debug("Swapchain_impl::~Swapchain_impl()");
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }

    VkRenderPass vulkan_renderpass = m_vulkan_renderpass;
    VkDevice     vulkan_device     = m_device_impl.get_vulkan_device();
    std::vector<Swapchain_image_entry> image_entries = std::move(m_image_entries);

    m_device_impl.add_completion_handler(
        [vulkan_device, vulkan_renderpass, image_entries]() {
            log_context->debug("Swapchain destructor completion handler");
            for (const Swapchain_image_entry& entry : image_entries) {
                vkDestroySemaphore  (vulkan_device, entry.m_submit_semaphore, nullptr);
                vkDestroyImageView  (vulkan_device, entry.m_image_view,       nullptr);
                vkDestroyFramebuffer(vulkan_device, entry.m_framebuffer,      nullptr); // placeholder
            }
            vkDestroyRenderPass(vulkan_device, vulkan_renderpass, nullptr);
        }
    );
    image_entries.clear();
    vulkan_renderpass = VK_NULL_HANDLE;
}

void Swapchain_impl::create_frames_in_flight_resources()
{
    if (!m_is_valid) {
        return;
    }

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

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

    ERHE_VERIFY(m_frames_in_flight.empty());
    const size_t number_of_frames_in_flight = m_device_impl.get_number_of_frames_in_flight();
    m_frames_in_flight.resize(number_of_frames_in_flight);
    for (size_t i = 0; i < number_of_frames_in_flight; ++i) {
        Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight[i];

        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_submit_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_FENCE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_submit_fence),
            fmt::format("Swapchain frame in flight submit fence {}", i).c_str()
        );

        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_present_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_FENCE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_present_fence),
            fmt::format("Swapchain frame in flight present fence {}", i).c_str()
        );

        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &frame_in_flight.m_acquire_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<uint64_t>(frame_in_flight.m_acquire_semaphore),
            fmt::format("Swapchain frame in flight acquire semaphore {}", i).c_str()
        );
        frame_in_flight.m_command_buffer = m_device_impl.allocate_command_buffer();
        if (frame_in_flight.m_command_buffer == VK_NULL_HANDLE) {
            log_context->critical("Allocating command buffer failed");
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            reinterpret_cast<uint64_t>(frame_in_flight.m_command_buffer),
            fmt::format("Swapchain frame in flight command buffer {}", i).c_str()
        );
    }
}

void Swapchain_impl::create_image_entry_resources()
{
    if (!m_is_valid) {
        return;
    }

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

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

    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType
        .pNext = nullptr,                                 // const void*
        .flags = 0                                        // VkSemaphoreCreateFlags
    };

    m_image_entries.resize(image_count);
    const VkSurfaceFormatKHR surface_format = m_surface_impl.get_surface_format();
    for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
        Swapchain_image_entry& image_entry = m_image_entries[image_index];
        image_entry.m_image = images[image_index];
        m_device_impl.set_debug_label(
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
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_IMAGE_VIEW,
            reinterpret_cast<uint64_t>(image_entry.m_image_view),
            fmt::format("Swapchain image view {}", image_index).c_str()
        );

        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &image_entry.m_submit_semaphore);
        if (result != VK_SUCCESS) {
            log_context->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_SEMAPHORE,
            reinterpret_cast<uint64_t>(image_entry.m_submit_semaphore),
            fmt::format("Swapchain image submit semaphore {}", image_index).c_str()
        );
    }
}

void Swapchain_impl::create_placeholder_renderpass_and_framebuffers()
{
    if (!m_is_valid) {
        return;
    }

    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    // temp stuff
    const VkAttachmentDescription color_attachment_description{
        .flags          = 0,                                            // VkAttachmentDescriptionFlags
        .format         = m_surface_impl.get_surface_format().format,   // VkFormat
        .samples        = VK_SAMPLE_COUNT_1_BIT,                        // VkSampleCountFlagBits
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,                  // VkAttachmentLoadOp
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,                 // VkAttachmentStoreOp
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,              // VkAttachmentLoadOp
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,             // VkAttachmentStoreOp
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,                    // VkImageLayout
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR               // VkImageLayout
    };

    const VkAttachmentReference color_attachment_reference{
        .attachment = 0,                                       // uint32_t
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // VkImageLayout
    };

    const VkSubpassDescription subpass_description{
        .flags                   = 0,                               // VkSubpassDescriptionFlag
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint
        .inputAttachmentCount    = 0,                               // uint32_t
        .pInputAttachments       = nullptr,                         // const VkAttachmentReference*
        .colorAttachmentCount    = 1,                               // uint32_t
        .pColorAttachments       = &color_attachment_reference,     // const VkAttachmentReference*
        .pResolveAttachments     = nullptr,                         // const VkAttachmentReference*
        .pDepthStencilAttachment = nullptr,                         // const VkAttachmentReference*
        .preserveAttachmentCount = 0,                               // uint32_t
        .pPreserveAttachments    = nullptr                          // const uint32_t*
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
    result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_vulkan_renderpass);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateRenderPass() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_RENDER_PASS,
        reinterpret_cast<uint64_t>(m_vulkan_renderpass),
        "Swapchain renderpass"
    );

    log_context->debug("Creating image entry framebuffers");
    for (
        uint32_t image_index = 0, image_count = static_cast<uint32_t>(m_image_entries.size());
        image_index < image_count;
        ++image_index
    ) {
        Swapchain_image_entry& image_entry = m_image_entries[image_index];

        VkFramebufferCreateInfo framebuffer_create_info{
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .renderPass      = m_vulkan_renderpass,
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
        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_FRAMEBUFFER,
            reinterpret_cast<uint64_t>(image_entry.m_framebuffer),
            fmt::format("Swapchain framebuffer {}", image_index)
        );
    }
}

void Swapchain_impl::update_vulkan_swapchain(VkSwapchainKHR vulkan_swapchain, VkExtent2D extent)
{
    if (m_vulkan_swapchain != VK_NULL_HANDLE) {
        destroy_placeholder_resources();
    }

    m_vulkan_swapchain = vulkan_swapchain;
    m_extent           = extent;
    if (
        (vulkan_swapchain == VK_NULL_HANDLE) ||
        (extent.width     == 0) ||
        (extent.height    == 0)
    ) {
        m_is_valid = false;
    }

    create_image_entry_resources();

    // TODO Once we have renderpass implemented properly, this may go away
    create_placeholder_renderpass_and_framebuffers();
}

void Swapchain_impl::submit_placeholder_renderpass()
{
    if (!m_is_valid) {
        return;
    }

    const uint64_t             frame_index           = m_device_impl.get_frame_index();
    const uint64_t             fif_index             = m_device_impl.get_frame_in_flight_index();
    VkQueue                    vulkan_graphics_queue = m_device_impl.get_graphics_queue();
    Swapchain_frame_in_flight& frame_in_flight       = m_frames_in_flight.at(fif_index);
    Swapchain_image_entry&     image_entry           = m_image_entries.at(frame_in_flight.m_image_index);
    VkResult                   result                = VK_SUCCESS;

    log_context->debug("submit_placeholder_renderpass() fif_index = {} image_index = {} frame_index = {}", fif_index, frame_in_flight.m_image_index, frame_index);

    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pInheritanceInfo = nullptr
    };

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

    const VkClearValue clear_values[3] = {
        {.color = { .float32 = { 0.01f, 0.0f, 0.0f, 1.0f }}},
        {.color = { .float32 = { 0.0f, 0.01f, 0.0f, 1.0f }}},
        {.color = { .float32 = { 0.0f, 0.0f, 0.01f, 1.0f }}}
    };

    const uint64_t clear_color_index = frame_index % 3;
    const VkRenderPassBeginInfo render_pass_begin_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType
        .pNext           = nullptr,                                  // const void*
        .renderPass      = m_vulkan_renderpass,                      // VkRenderPass
        .framebuffer     = image_entry.m_framebuffer,                // VkFramebuffer
        .renderArea      = VkRect2D{{0,0}, m_extent},                // VkRect2D
        .clearValueCount = 1,                                        // uint32_t
        .pClearValues    = &clear_values[clear_color_index]          // const VkClearValue*
    };

    log_context->debug(
        "vkCmdBeginRenderPass( clear color = {}: {}, {}, {}, {})",
        clear_color_index,
        render_pass_begin_info.pClearValues->color.float32[0],
        render_pass_begin_info.pClearValues->color.float32[1],
        render_pass_begin_info.pClearValues->color.float32[2],
        render_pass_begin_info.pClearValues->color.float32[3]
    );
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
    result = vkQueueSubmit(vulkan_graphics_queue, 1, &submit_info, frame_in_flight.m_submit_fence);
    if (result != VK_SUCCESS) {
        log_context->critical("vkQueueSubmit() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
}

void Swapchain_impl::start_of_frame()
{
    if (!m_is_valid) {
        return;
    }

    const uint64_t frame_index = m_device_impl.get_frame_index();
    const uint64_t fif_index   = m_device_impl.get_frame_in_flight_index();

    log_context->debug("Swapchain_impl::start_of_frame() fif_index = {} frame_index = {}", fif_index, frame_index);

    VkDevice                   vulkan_device   = m_device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight.at(fif_index);
    VkResult                   result          = VK_SUCCESS;

    log_context->debug("vkWaitForFences() frame_in_flight.m_submit_fence");
    result = vkWaitForFences(vulkan_device, 1, &frame_in_flight.m_submit_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        log_context->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_context->debug("vkResetFences() frame_in_flight.m_submit_fence");
    result = vkResetFences(vulkan_device, 1, &frame_in_flight.m_submit_fence);
    if (result != VK_SUCCESS) {
        log_context->critical("vkResetFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_context->debug("vkAcquireNextImageKHR()");
    result = vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame_in_flight.m_acquire_semaphore, VK_NULL_HANDLE, &frame_in_flight.m_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    }
    if (result != VK_SUCCESS) {
        log_context->critical("vkAcquireNextImageKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_context->debug("Received image index = {}", frame_in_flight.m_image_index);
}

void Swapchain_impl::present()
{
    if (!m_is_valid) {
        return;
    }

    const uint64_t frame_index = m_device_impl.get_frame_index();
    const uint64_t fif_index   = m_device_impl.get_frame_in_flight_index();

    VkDevice                   vulkan_device        = m_device_impl.get_vulkan_device();
    VkQueue                    vulkan_present_queue = m_device_impl.get_present_queue();
    Swapchain_frame_in_flight& frame_in_flight      = m_frames_in_flight.at(fif_index);
    Swapchain_image_entry&     image_entry          = m_image_entries.at(frame_in_flight.m_image_index);
    VkResult                   result               = VK_SUCCESS;
    const bool                 use_present_fence    = m_device_impl.get_capabilities().m_swapchain_maintenance1;

    log_context->debug("Swapchain_impl::end_of_frame() fif_index = {} image_index = {} frame_index = {}", fif_index, frame_in_flight.m_image_index, frame_index);

    // TODO Once we have renderpass implemented properly, this may go away
    submit_placeholder_renderpass();

    if (use_present_fence) {
        log_context->debug("vkWaitForFences() frame_in_flight.m_present_fence");
        result = vkWaitForFences(vulkan_device, 1, &frame_in_flight.m_present_fence, VK_TRUE, 0);
        if (result != VK_SUCCESS) {
            log_context->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        log_context->debug("vkResetFences() frame_in_flight.m_present_fence");
        result = vkResetFences(vulkan_device, 1, &frame_in_flight.m_present_fence);
        if (result != VK_SUCCESS) {
            log_context->critical("vkResetFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }
    }

    const VkSwapchainPresentFenceInfoKHR present_fence_info{
        .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR, // VkStructureType
        .pNext          = nullptr,                                            // const void*
        .swapchainCount = 1,                                                  // uint32_t
        .pFences        = &frame_in_flight.m_present_fence,                   // const VkFence*
    };
    const VkPresentInfoKHR present_info{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,                // VkStructureType
        .pNext              = use_present_fence ? &present_fence_info : nullptr, // const void*
        .waitSemaphoreCount = 1,                                                 // uint32_t
        .pWaitSemaphores    = &image_entry.m_submit_semaphore,                   // const VkSemaphore*
        .swapchainCount     = 1,                                                 // uint32_t
        .pSwapchains        = &m_vulkan_swapchain,                               // const VkSwapchainKHR*
        .pImageIndices      = &frame_in_flight.m_image_index,                    // const uint32_t*
        .pResults           = &frame_in_flight.m_present_result                  // VkResult*
    };
    log_context->debug("vkQueuePresentKHR()");
    result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    switch (result) {
        case VK_SUCCESS:
            break;
        case VK_SUBOPTIMAL_KHR:
            log_context->warn("  VK_SUBOPTIMAL_KHR");
            break;
        case VK_ERROR_OUT_OF_DATE_KHR: {
            log_context->warn("  VK_ERROR_OUT_OF_DATE_KHR");
            m_is_valid = false;
            break;
        }
        default: {
            log_context->critical("vkAcquireNextImageKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
            break;
        }
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
