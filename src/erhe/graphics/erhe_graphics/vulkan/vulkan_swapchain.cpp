#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&  device_impl,
    Surface_impl& surface_impl
)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    log_swapchain->debug("Swapchain_impl::~Swapchain_impl()");
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        return;
    }

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result = VK_SUCCESS;

	result = vkDeviceWaitIdle(vulkan_device);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkDeviceWaitIdle() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    for (Swapchain_frame_in_flight& frame : m_submit_history) {
        recycle_fence    (frame.submit_fence);
        recycle_semaphore(frame.acquire_semaphore);
        vkDestroyCommandPool(vulkan_device, frame.command_pool, nullptr);
        for (Swapchain_objects& garbage : frame.swapchain_garbage) {
            cleanup_swapchain_objects(garbage);
        }
        frame.swapchain_garbage.clear();
        ERHE_VERIFY(frame.present_semaphore == VK_NULL_HANDLE);
    }

    for (Present_history_entry& present_history_entry : m_present_history) {
        if (present_history_entry.cleanup_fence != VK_NULL_HANDLE) {
            result = vkWaitForFences(vulkan_device, 1, &present_history_entry.cleanup_fence, true, UINT64_MAX);
            if (result != VK_SUCCESS) {
                log_swapchain->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
                abort();
            }
        }
        cleanup_present_info(present_history_entry);
    }

    //log_swapchain->trace("During the lifetime, {} swapchains were created", m_swapchain_creation_count);
    log_swapchain->debug("Old swapchain count at destruction: {}", m_old_swapchains.size());

    for (Swapchain_cleanup_data& old_swapchain : m_old_swapchains) {
        cleanup_old_swapchain(old_swapchain);
    }

    log_swapchain->debug("Semaphore pool size at destruction: {}", m_semaphore_pool.size());
    log_swapchain->debug("Fence pool size at destruction: {}", m_fence_pool.size());

    for (VkSemaphore semaphore : m_semaphore_pool) {
        vkDestroySemaphore(vulkan_device, semaphore, nullptr);
    }

    for (VkFence fence : m_fence_pool) {
        vkDestroyFence(vulkan_device, fence, nullptr);
    }

    cleanup_swapchain_objects(m_swapchain_objects);
    if (m_vulkan_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan_device, m_vulkan_swapchain, nullptr);
    }

    // This is temporary, placeholder renderpass related
    if (m_vulkan_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkan_device, m_vulkan_render_pass, nullptr);
    }
}

void Swapchain_impl::create_placeholder_renderpass()
{
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

    log_swapchain->debug("Creating renderpass");
    result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_vulkan_render_pass);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateRenderPass() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_RENDER_PASS,
        reinterpret_cast<uint64_t>(m_vulkan_render_pass),
        "Swapchain renderpass"
    );
}

void Swapchain_impl::submit_placeholder_renderpass()
{
    if (!is_valid()) {
        return;
    }

    const Swapchain_frame_in_flight& frame = m_submit_history[m_submit_history_index];
    VkQueue  vulkan_graphics_queue = m_device_impl.get_graphics_queue();
    VkResult result                = VK_SUCCESS;
    uint64_t frame_index           = m_device_impl.get_frame_index();

    log_swapchain->debug(
        "submit_placeholder_renderpass() m_submit_history_index = {} m_acquired_image_index = {} frame_index = {}",
        m_submit_history_index, m_acquired_image_index, frame_index
    );

    const VkCommandBuffer vulkan_command_buffer = get_command_buffer();
    if (vulkan_command_buffer == VK_NULL_HANDLE) {
        return;
    }

    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr
    };

    log_swapchain->debug("vkBeginCommandBuffer()");
    result = vkBeginCommandBuffer(vulkan_command_buffer, &begin_info);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkBeginCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    const VkClearValue clear_values[3] = {
        {.color = { .float32 = { 0.01f, 0.0f, 0.0f, 1.0f }}},
        {.color = { .float32 = { 0.0f, 0.01f, 0.0f, 1.0f }}},
        {.color = { .float32 = { 0.0f, 0.0f, 0.01f, 1.0f }}}
    };

    const uint64_t clear_color_index = frame_index % 3;
    const VkRenderPassBeginInfo render_pass_begin_info{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,                  // VkStructureType
        .pNext           = nullptr,                                                   // const void*
        .renderPass      = m_vulkan_render_pass,                                      // VkRenderPass
        .framebuffer     = m_swapchain_objects.framebuffers[m_acquired_image_index],  // VkFramebuffer
        .renderArea      = VkRect2D{{0,0}, m_swapchain_extent},                       // VkRect2D
        .clearValueCount = 1,                                                         // uint32_t
        .pClearValues    = &clear_values[clear_color_index]                           // const VkClearValue*
    };

    log_swapchain->debug(
        "vkCmdBeginRenderPass( clear color = {}: {}, {}, {}, {})",
        clear_color_index,
        render_pass_begin_info.pClearValues->color.float32[0],
        render_pass_begin_info.pClearValues->color.float32[1],
        render_pass_begin_info.pClearValues->color.float32[2],
        render_pass_begin_info.pClearValues->color.float32[3]
    );
    vkCmdBeginRenderPass(vulkan_command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    log_swapchain->debug("vkCmdEndRenderPass()");
    vkCmdEndRenderPass(vulkan_command_buffer);

    log_swapchain->debug("vkEndCommandBuffer()");
    result = vkEndCommandBuffer(vulkan_command_buffer);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkEndCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    // Make a submission. Wait on the acquire semaphore and signal the present semaphore.
    const VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,  // VkStructureType
        .pNext                = nullptr,                        // const void*
        .waitSemaphoreCount   = 1,                              // uint32_t
        .pWaitSemaphores      = &frame.acquire_semaphore,       // const VkSemaphore*
        .pWaitDstStageMask    = &wait_stages,                   // const VkPipelineStageFlags*
        .commandBufferCount   = 1,                              // uint32_t
        .pCommandBuffers      = &frame.command_buffer,          // const VkCommandBuffer*
        .signalSemaphoreCount = 1,                              // uint32_t
        .pSignalSemaphores    = &frame.present_semaphore        // const VkSemaphore*
    };
    log_swapchain->debug("vkQueueSubmit()");
    result = vkQueueSubmit(vulkan_graphics_queue, 1, &submit_info, frame.submit_fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkQueueSubmit() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
}

void Swapchain_impl::wait_frame(Frame_state& out_frame_state)
{
    if (m_vulkan_swapchain == VK_NULL_HANDLE) {
        init_swapchain();
    }

    out_frame_state = Frame_state{};
    setup_frame();
}

void Swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info)
{
    VkResult result = VK_SUCCESS;

    if (!frame_begin_info.request_resize) {
        result = acquire_next_image(&m_acquired_image_index);
    }

    if (frame_begin_info.request_resize || (result == VK_SUBOPTIMAL_KHR) || (result == VK_ERROR_OUT_OF_DATE_KHR)) {
        init_swapchain();
        if (!m_is_valid) {
            m_acquired_image_index = 0xffu;
            return;
        }
        result = acquire_next_image(&m_acquired_image_index);
    }
    if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
        log_context->warn("Could not obtain swapchain image, skipping frame");
        m_acquired_image_index = 0xffu;
    }
}

void Swapchain_impl::end_frame(const Frame_end_info& frame_end_info)
{
    static_cast<void>(frame_end_info);

    submit_placeholder_renderpass();

    VkResult result = present_image(m_acquired_image_index);

    // Handle Outdated error in present.
    if ((result == VK_SUBOPTIMAL_KHR) || (result == VK_ERROR_OUT_OF_DATE_KHR)) {
        init_swapchain();
        if (!m_is_valid) {
            return;
        }
    } else if (result != VK_SUCCESS) {
        log_context->critical("presenting frame failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
}

auto Swapchain_impl::has_maintenance1() const -> bool
{
    return m_device_impl.get_capabilities().m_swapchain_maintenance1;
}

auto Swapchain_impl::is_valid() const -> bool
{
    return m_surface_impl.is_valid() && !m_surface_impl.is_empty();
}

//////////////////////////////////////////////////////////////////////////////////////////////////

auto Swapchain_impl::get_semaphore() -> VkSemaphore
{
    // If there is a free semaphore, return it
    if (!m_semaphore_pool.empty()) {
        const VkSemaphore semaphore = m_semaphore_pool.back();
        m_semaphore_pool.pop_back();
        return semaphore;
    }

    const VkSemaphoreCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };
    VkSemaphore vulkan_semaphore{VK_NULL_HANDLE};
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    const VkResult result = vkCreateSemaphore(vulkan_device, &create_info, nullptr, &vulkan_semaphore);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_SEMAPHORE,
        reinterpret_cast<uint64_t>(vulkan_semaphore),
        fmt::format("Swapchain semaphore {}", m_semaphore_serial++).c_str()
    );

    return vulkan_semaphore;
}

auto Swapchain_impl::get_fence() -> VkFence
{
    // If there is a free fence, return it
    if (!m_fence_pool.empty()) {
        const VkFence fence = m_fence_pool.back();
        m_fence_pool.pop_back();
        return fence;
    }

    VkFence vulkan_fence{VK_NULL_HANDLE};
    const VkFenceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    const VkResult result = vkCreateFence(vulkan_device, &create_info, nullptr, &vulkan_fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_FENCE,
        reinterpret_cast<uint64_t>(vulkan_fence),
        fmt::format("Swapchain fence {}", m_fence_serial++).c_str()
    );

    return vulkan_fence;
}

void Swapchain_impl::recycle_semaphore(const VkSemaphore semaphore)
{
    ERHE_VERIFY(semaphore != VK_NULL_HANDLE);
    m_semaphore_pool.push_back(semaphore);
}

void Swapchain_impl::recycle_fence(const VkFence fence)
{
	m_fence_pool.push_back(fence);
    ERHE_VERIFY(fence != VK_NULL_HANDLE);

    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    const VkResult result = vkResetFences(vulkan_device, 1, &fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkResetFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }
}

void Swapchain_impl::cleanup_swapchain_objects(Swapchain_objects& garbage)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    for (const VkImageView view : garbage.views) {
        vkDestroyImageView(vulkan_device, view, nullptr);
    }
    for (const VkFramebuffer framebuffer : garbage.framebuffers) {
        vkDestroyFramebuffer(vulkan_device, framebuffer, nullptr);
    }

    garbage.images      .clear();
    garbage.views       .clear();
    garbage.framebuffers.clear();
}

void Swapchain_impl::add_present_to_history(const uint32_t index, const VkFence present_fence)
{
    Swapchain_frame_in_flight& frame = m_submit_history[m_submit_history_index];

    m_present_history.emplace_back();
    m_present_history.back().present_semaphore = frame.present_semaphore;
    m_present_history.back().old_swapchains    = std::move(m_old_swapchains);

    frame.present_semaphore = VK_NULL_HANDLE;

    if (has_maintenance1()) {
        m_present_history.back().image_index   = INVALID_IMAGE_INDEX;
        m_present_history.back().cleanup_fence = present_fence;
    } else {
        // The fence needed to know when the semaphore can be recycled will be one that is
        // passed to vkAcquireNextImageKHR that returns the same image index. That is why
        // the image index needs to be tracked in this case.
        m_present_history.back().image_index = index;
    }
}

void Swapchain_impl::cleanup_present_history()
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    while (!m_present_history.empty()) {
        Present_history_entry& entry = m_present_history.front();

        // If there is no fence associated with the history, it can't be cleaned up yet.
        if (entry.cleanup_fence == VK_NULL_HANDLE) {
            // Can't have an old present operation without a fence that doesn't have an
            // image index used to later associate a fence with it.
            ERHE_VERIFY(entry.image_index != INVALID_IMAGE_INDEX);
            break;
        }

        // Otherwise check to see if the fence is signaled.
        const VkResult result = vkGetFenceStatus(vulkan_device, entry.cleanup_fence);
        if (result == VK_NOT_READY) {
            // Not yet
            break;
        } else if (result != VK_SUCCESS) {
            log_swapchain->critical("vkGetFenceStatus() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        cleanup_present_info(entry);
        m_present_history.pop_front();
    }

    // The present history can grow indefinitely if a present operation is done on an index
    // that's never acquired in the future.  In that case, there's no fence associated with that
    // present operation.  Move the offending entry to last, so the resources associated with
    // the rest of the present operations can be duly freed.
    if (
        (m_present_history.size() > m_swapchain_objects.images.size() * 2) &&
        (m_present_history.front().cleanup_fence == VK_NULL_HANDLE)
    ) {
        Present_history_entry entry = std::move(m_present_history.front());
        m_present_history.pop_front();

        // We can't be stuck on a presentation to an old swapchain without a fence.
        assert(entry.image_index != INVALID_IMAGE_INDEX);

        // Move clean up data to the next (now first) present operation, if any.  Note that
        // there cannot be any clean up data on the rest of the present operations, because
        // the first present already gathers every old swapchain to clean up.
        assert(
            std::ranges::all_of(
                m_present_history, [](const Present_history_entry& entry) {
                    return entry.old_swapchains.empty();
                }
            )
        );
        m_present_history.front().old_swapchains = std::move(entry.old_swapchains);

        // Put the present operation at the end of the queue, so it's revisited after the
        // rest of the present operations are cleaned up.
        m_present_history.push_back(std::move(entry));
    }
}

void Swapchain_impl::cleanup_present_info(Present_history_entry& entry)
{
    // Called when it's safe to destroy resources associated with a present operation.
    if (entry.cleanup_fence != VK_NULL_HANDLE) {
        recycle_fence(entry.cleanup_fence);
    }

    // On the first acquire of the image, a fence is used but there is no present semaphore to
    // clean up.  That fence is placed in the present history just for clean up purposes.
    if (entry.present_semaphore != VK_NULL_HANDLE) {
        recycle_semaphore(entry.present_semaphore);
    }

    // Destroy old swapchains
    for (Swapchain_cleanup_data& old_swapchain : entry.old_swapchains) {
        cleanup_old_swapchain(old_swapchain);
    }

    entry.cleanup_fence     = VK_NULL_HANDLE;
    entry.present_semaphore = VK_NULL_HANDLE;
    entry.old_swapchains.clear();
    entry.image_index = std::numeric_limits<uint32_t>::max();
}

void Swapchain_impl::cleanup_old_swapchain(Swapchain_cleanup_data& old_swapchain)
{
    if (old_swapchain.swapchain != VK_NULL_HANDLE) {
        const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
        vkDestroySwapchainKHR(vulkan_device, old_swapchain.swapchain, nullptr);
    }

    for (VkSemaphore semaphore : old_swapchain.semaphores) {
        recycle_semaphore(semaphore);
    }

    old_swapchain.swapchain = VK_NULL_HANDLE;
    old_swapchain.semaphores.clear();
}

void Swapchain_impl::associate_fence_with_present_history(const uint32_t index, const VkFence acquire_fence)
{
    // The history looks like this:
    //
    // <entries for old swapchains, imageIndex == UINT32_MAX> <entries for this swapchain>
    //
    // Walk the list backwards and find the entry for the given image index. That's the last
    // present with that image. Associate the fence with that present operation.
    for (size_t entry_index = 0, end = m_present_history.size(); entry_index < end; ++entry_index) {
        Present_history_entry& entry = m_present_history[end - entry_index - 1];
        if (entry.image_index == INVALID_IMAGE_INDEX) {
            // No previous presentation with this index.
            break;
        }

        if (entry.image_index == index) {
            assert(entry.cleanup_fence == VK_NULL_HANDLE);
            entry.cleanup_fence = acquire_fence;
            return;
        }
    }

    // If no previous presentation with this index, add an empty entry just so the fence can be
    // cleaned up.
    m_present_history.emplace_back();
    m_present_history.back().cleanup_fence = acquire_fence;
    m_present_history.back().image_index   = index;
}

void Swapchain_impl::schedule_old_swapchain_for_destruction(const VkSwapchainKHR old_swapchain)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();

    // If no presentation is done on the swapchain, destroy it right away.
    if (!m_present_history.empty() && (m_present_history.back().image_index == INVALID_IMAGE_INDEX)) {
        vkDestroySwapchainKHR(vulkan_device, old_swapchain, nullptr);
        return;
    }

    Swapchain_cleanup_data cleanup_data{
        .swapchain = old_swapchain
    };

    // Place any present operation that's not associated with a fence into old_swapchains.  That
    // gets scheduled for destruction when the semaphore of the first image of the next
    // swapchain can be recycled.
    std::vector<Present_history_entry> history_to_keep;
    while (!m_present_history.empty()) {
        Present_history_entry& entry = m_present_history.back();

        // If this is about an older swapchain, let it be.
        if (entry.image_index == INVALID_IMAGE_INDEX) {
            ERHE_VERIFY(entry.cleanup_fence != VK_NULL_HANDLE);
            break;
        }

        // Reset the index, so it's not processed in the future.
        entry.image_index = INVALID_IMAGE_INDEX;

        if (entry.cleanup_fence != VK_NULL_HANDLE) {
            // If there is already a fence associated with it, let it be cleaned up once
            // the fence is signaled.
            history_to_keep.push_back(std::move(entry));
        } else {
            ERHE_VERIFY(entry.present_semaphore != VK_NULL_HANDLE);

            // Otherwise accumulate it in cleanup data.
            cleanup_data.semaphores.push_back(entry.present_semaphore);

            // Accumulate any previous swapchains that are pending destruction too.
            for (Swapchain_cleanup_data& swapchain : entry.old_swapchains) {
                m_old_swapchains.emplace_back(swapchain);
            }
            entry.old_swapchains.clear();
        }

        m_present_history.pop_back();
    }
    std::move(history_to_keep.begin(), history_to_keep.end(), std::back_inserter(m_present_history));

    if (cleanup_data.swapchain != VK_NULL_HANDLE || !cleanup_data.semaphores.empty()) {
        m_old_swapchains.emplace_back(std::move(cleanup_data));
    }

}

void Swapchain_impl::init_swapchain_image(const uint32_t index)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;

    ERHE_VERIFY(m_swapchain_objects.views[index] == VK_NULL_HANDLE);

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(m_swapchain_objects.images[index]),
        fmt::format("Swapchain image {}", index).c_str()
    );

    const VkImageViewCreateInfo view_create_info{
        .sType              = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .image              = m_swapchain_objects.images[index],
        .viewType           = VK_IMAGE_VIEW_TYPE_2D,
        .format             = m_swapchain_format,
        .components = {
            .r              = VK_COMPONENT_SWIZZLE_R,
            .g              = VK_COMPONENT_SWIZZLE_G,
            .b              = VK_COMPONENT_SWIZZLE_B,
            .a              = VK_COMPONENT_SWIZZLE_A
        },
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    result = vkCreateImageView(vulkan_device, &view_create_info, nullptr, &m_swapchain_objects.views[index]);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateImageView() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(m_swapchain_objects.views[index]),
        fmt::format("Swapchain image view {}", index).c_str()
    );

    if (m_vulkan_render_pass == VK_NULL_HANDLE) {
        create_placeholder_renderpass();
    }

    const VkFramebufferCreateInfo framebuffer_create_info{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = 0,
        .flags           = 0,
        .renderPass      = m_vulkan_render_pass,
        .attachmentCount = 1,
        .pAttachments    = &m_swapchain_objects.views[index],
        .width           = m_swapchain_extent.width,
        .height          = m_swapchain_extent.height,
        .layers          = 1
    };

    result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &m_swapchain_objects.framebuffers[index]);
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateFramebuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_FRAMEBUFFER,
        reinterpret_cast<uint64_t>(m_swapchain_objects.framebuffers[index]),
        fmt::format("Swapchain image view {}", index).c_str()
    );
}

void Swapchain_impl::init_swapchain()
{
    Vulkan_swapchain_create_info swapchain_create_info{};
    bool recreate_swapchain = m_surface_impl.update_swapchain(swapchain_create_info);
    bool surface_is_valid   = m_surface_impl.is_valid();
    bool surface_is_empty   = m_surface_impl.is_empty();
    if (!surface_is_valid) {
        log_context->debug("Surface is not ready to be used for rendering");
    }
    if (surface_is_empty) {
        log_context->debug("Surface is too small to have any visible pixels");
    }
    m_is_valid = surface_is_valid && !surface_is_empty;
    if (m_is_valid && recreate_swapchain) {
        init_swapchain(swapchain_create_info);
    }
}

void Swapchain_impl::init_swapchain(Vulkan_swapchain_create_info& swapchain_create_info)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;
    VkSwapchainKHR old_swapchain = m_vulkan_swapchain;

    swapchain_create_info.swapchain_create_info.oldSwapchain = old_swapchain;
    result = vkCreateSwapchainKHR(vulkan_device, &swapchain_create_info.swapchain_create_info, nullptr, &m_vulkan_swapchain);
    ++m_swapchain_serial;
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSwapchainKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_SWAPCHAIN_KHR,
        reinterpret_cast<uint64_t>(m_vulkan_swapchain),
        fmt::format("Swapchain {}", m_swapchain_serial)
    );

    // Schedule destruction of the old swapchain resources once this frame's submission is finished.
    m_submit_history[m_submit_history_index].swapchain_garbage.push_back(std::move(m_swapchain_objects));

    // Schedule destruction of the old swapchain itself once its last presentation is finished.
    if (old_swapchain != VK_NULL_HANDLE) {
        schedule_old_swapchain_for_destruction(old_swapchain);
    }

    // Get the swapchain images.
    uint32_t image_count{0};
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, nullptr);
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    

    m_swapchain_objects.images      .resize(image_count, VK_NULL_HANDLE);
    m_swapchain_objects.views       .resize(image_count, VK_NULL_HANDLE);
    m_swapchain_objects.framebuffers.resize(image_count, VK_NULL_HANDLE);
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, m_swapchain_objects.images.data());
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    

    if (!has_maintenance1()) {
        // When VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT is used, image views
        // cannot be created until the first time the image is acquired.
        for (uint32_t index = 0; index < image_count; ++index) {
            init_swapchain_image(index);
        }
    }

    m_swapchain_extent = swapchain_create_info.swapchain_create_info.imageExtent;
    m_swapchain_format = swapchain_create_info.swapchain_create_info.imageFormat;
}

auto Swapchain_impl::get_command_buffer() -> VkCommandBuffer
{
    if (!is_valid()) {
        return VK_NULL_HANDLE;
    }

    const Swapchain_frame_in_flight& frame = m_submit_history[m_submit_history_index];

    return frame.command_buffer;
}

void Swapchain_impl::setup_frame()
{
    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult result        = VK_SUCCESS;

    // For the frame we need:
    // - A fence for the submission
    // - A semaphore for image acquire
    // - A semaphore for image present

    // But first, pace the CPU. Wait for frame N-2 to finish before starting recording of frame N.
    m_submit_history_index = (m_submit_history_index + 1) % m_submit_history.size();
    Swapchain_frame_in_flight& frame = m_submit_history[m_submit_history_index];
    if (frame.submit_fence != VK_NULL_HANDLE) {
        result = vkWaitForFences(vulkan_device, 1, &frame.submit_fence, true, UINT64_MAX);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        // Reset/recycle resources, they are no longer in use.
        recycle_fence(frame.submit_fence);
        recycle_semaphore(frame.acquire_semaphore);
        vkResetCommandPool(vulkan_device, frame.command_pool, 0);

        // Destroy any garbage that's associated with this submission.
        for (Swapchain_objects& garbage : frame.swapchain_garbage) {
            cleanup_swapchain_objects(garbage);
        }
        frame.swapchain_garbage.clear();

        // Note that while the submission fence, the semaphore it waited on and the command
        // pool its command was allocated from are guaranteed to have finished execution,
        // there is no guarantee that the present semaphore is not in use.
        //
        // This is because the fence wait above ensures that the submission _before_ present
        // is finished, but makes no guarantees as to the state of the present operation
        // that follows.  The present semaphore is queued for garbage collection when
        // possible after present, and is not kept as part of the submit history.
        ERHE_VERIFY(frame.present_semaphore == VK_NULL_HANDLE);
    }

    frame.submit_fence      = get_fence();
    frame.acquire_semaphore = get_semaphore();
    frame.present_semaphore = get_semaphore();

    if (frame.command_pool == VK_NULL_HANDLE) {
        const VkCommandPoolCreateInfo command_pool_create_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = m_device_impl.get_present_queue_family_index()
        };
        result = vkCreateCommandPool(vulkan_device, &command_pool_create_info, nullptr, &frame.command_pool);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkCreateCommandPool() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        const VkCommandBufferAllocateInfo command_buffer_allocate_info{
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = frame.command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        result = vkAllocateCommandBuffers(vulkan_device, &command_buffer_allocate_info, &frame.command_buffer);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkAllocateCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        m_device_impl.set_debug_label(
            VK_OBJECT_TYPE_COMMAND_BUFFER,
            reinterpret_cast<uint64_t>(frame.command_buffer),
            fmt::format("Swapchain frame in flight command buffer {}", m_submit_history_index).c_str()
        );
    }
}

auto Swapchain_impl::acquire_next_image(uint32_t* index) -> VkResult
{
    VkDevice                   vulkan_device = m_device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame         = m_submit_history[m_submit_history_index];

    // Use a fence to know when acquire is done.  Without VK_EXT_swapchain_maintenance1, this
    // fence is used to infer when the _previous_ present to this image index has finished.
    // There is no need for this with VK_EXT_swapchain_maintenance1.
    VkFence acquire_fence = has_maintenance1() ? VK_NULL_HANDLE : get_fence();

    VkResult result = vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame.acquire_semaphore, acquire_fence, index);

    if (has_maintenance1() && ((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
        // When VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT is specified, image
        // views must be created after the first time the image is acquired.
        assert(*index < m_swapchain_objects.views.size());
        if (m_swapchain_objects.views[*index] == VK_NULL_HANDLE) {
            init_swapchain_image(*index);
        }
    }

    if (!has_maintenance1()) {
        if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
            // If failed, fence is untouched, recycle it.
            //
            // The semaphore is also untouched, but it may be used in the retry of
            // vkAcquireNextImageKHR. It is nevertheless cleaned up after cpu
            // throttling automatically.
            recycle_fence(acquire_fence);
            return result;
        }

        associate_fence_with_present_history(*index, acquire_fence);
    }

    // TODO Add handling for pretransform suboptimal
    return result;
}

auto Swapchain_impl::present_image(uint32_t index) -> VkResult
{
    VkQueue                    vulkan_present_queue = m_device_impl.get_present_queue();
    Swapchain_frame_in_flight& frame                = m_submit_history[m_submit_history_index];

    VkPresentInfoKHR present_info{
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &frame.present_semaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &m_vulkan_swapchain,
        .pImageIndices      = &index,
        .pResults           = nullptr
    };

    // When VK_EXT_swapchain_maintenance1 is enabled, add a fence to the present operation,
    // which is signaled when the resources associated with present operation can be freed.
    VkFence                        present_fence = VK_NULL_HANDLE;
    VkSwapchainPresentFenceInfoEXT fence_info{
        .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .pNext          = nullptr,
        .swapchainCount = 0,
        .pFences        = nullptr
    };
    VkSwapchainPresentModeInfoEXT  present_mode{
        .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        .pNext          = nullptr,
        .swapchainCount = 0,
        .pPresentModes  = nullptr
    };
    if (has_maintenance1()){
        present_fence = get_fence();

        fence_info.swapchainCount = 1;
        fence_info.pFences        = &present_fence;

        present_info.pNext = &fence_info;

        // If present mode has changed but the two modes are compatible, change the present
        // mode at present time.
        //if (current_present_mode != desired_present_mode) {
        //    // Can't reach here if the modes are not compatible.
        //    assert(are_present_modes_compatible());
        //
        //    current_present_mode = desired_present_mode;
        //
        //    present_mode.swapchainCount = 1;
        //    present_mode.pPresentModes  = &current_present_mode;
        //
        //    fence_info.pNext = &present_mode;
        //}
    }

    VkResult result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    if ((result != VK_SUCCESS) && (result != VK_ERROR_OUT_OF_DATE_KHR) && (result != VK_SUBOPTIMAL_KHR)) {
        log_swapchain->critical("vkQueuePresentKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    add_present_to_history(index, present_fence);
    cleanup_present_history();

    return result;
}

} // namespace erhe::graphics
