#include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

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
    return vulkan_fence;
}

void Swapchain_impl::recycle_semaphore(const VkSemaphore semaphore)
{
    ERHE_VERIFY(semaphore != VK_NULL_HANDLE);
    m_semaphore_pool.push_back(semaphore);
}

void Swapchain_impl::recycle_fence(VkFence fence)
{
	m_fence_pool.push_back(fence);
    ERHE_VERIFY(semaphore != VK_NULL_HANDLE);

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

    for (VkImageView view : garbage.views) {
        vkDestroyImageView(vulkan_device, view, nullptr);
    }
    for (VkFramebuffer framebuffer : garbage.framebuffers) {
        vkDestroyFramebuffer(vulkan_device, framebuffer, nullptr);
    }

    garbage.images      .clear();
    garbage.views       .clear();
    garbage.framebuffers.clear();
}

void Swapchain_impl::add_present_to_history(uint32_t index, VkFence present_fence)
{
    Swapchain_frame_in_flight& frame = m_submit_history[m_submit_history_index];

    m_present_history.emplace_back();
    m_present_history.back().present_semaphore = frame.present_semaphore;
    m_present_history.back().old_swapchains    = std::move(old_swapchains);

    frame.present_semaphore = VK_NULL_HANDLE;

    const bool has_maintenance1 = m_device_impl.get_capabilities().m_swapchain_maintenance1;

    if (has_maintenance1) {
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
        VkResult result = vkGetFenceStatus(vulkan_device, edntry.cleanup_fence);
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
    if (present_history.size() > swapchain_objects.images.size() * 2 && present_history.front().cleanup_fence == VK_NULL_HANDLE)
    {
        PresentOperationInfo present_info = std::move(present_history.front());
        present_history.pop_front();

        // We can't be stuck on a presentation to an old swapchain without a fence.
        assert(present_info.image_index != INVALID_IMAGE_INDEX);

        // Move clean up data to the next (now first) present operation, if any.  Note that
        // there cannot be any clean up data on the rest of the present operations, because
        // the first present already gathers every old swapchain to clean up.
        assert(std::ranges::all_of(present_history, [](const PresentOperationInfo &op) {
            return op.old_swapchains.empty();
        }));
        present_history.front().old_swapchains = std::move(present_info.old_swapchains);

        // Put the present operation at the end of the queue, so it's revisited after the
        // rest of the present operations are cleaned up.
        present_history.push_back(std::move(present_info));
    }
}

void Swapchain_impl::cleanup_present_info(Present_history_entry& entry)
{
    // Called when it's safe to destroy resources associated with a present operation.
    if (entry.cleanup_fence != VK_NULL_HANDLE) {
        recycle_fence(present_info.cleanup_fence);
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

void Swapchain_impl::associate_fence_with_present_history(uint32_t index, VkFence acquire_fence)
{
    // The history looks like this:
    //
    // <entries for old swapchains, imageIndex == UINT32_MAX> <entries for this swapchain>
    //
    // Walk the list backwards and find the entry for the given image index. That's the last
    // present with that image. Associate the fence with that present operation.
    for (size_t history_index = 0; history_index < present_history.size(); ++history_index)
    {
        Present_history_entry& entry = m_present_history[present_history.size() - history_index - 1];
        if (entry.image_index == INVALID_IMAGE_INDEX) {
            // No previous presentation with this index.
            break;
        }

        if (entry.image_index == index) {
            assert(present_info.cleanup_fence == VK_NULL_HANDLE);
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
    if (!m_present_history.empty() && m_present_history.back().image_index == INVALID_IMAGE_INDEX) {
        vkDestroySwapchainKHR(vulkan_device, old_swapchain, nullptr);
        return;
    }

    Swapchain_cleanup_data cleanup_data{
        .swapchain = old_swapchain;
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
        old_swapchains.emplace_back(std::move(cleanup_data));
    }

}

//////////////////////////////////////////////////////////////////////////////////////////////////

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
    //create_frames_in_flight_resources();
    create_image_entry_resources();

    // TODO Once we have renderpass implemented properly, this may go away
    create_placeholder_renderpass_and_framebuffers();
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

    for (VkSemaphore semaphore : semaphore_pool) {
        vkDestroySemaphore(vulkan_device, semaphore, nullptr);
    }

    for (VkFence fence : fence_pool) {
        vkDestroyFence(vulkan_device, fence, nullptr);
    }

    cleanup_swapchain_objects(m_swapchain_objects);
    if (m_vulkan_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan_device, m_swapchain_objects, nullptr);
    }

    if (m_render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkan_device, m_render_pass, nullptr);
    }
}

//void Swapchain_impl::create_frames_in_flight_resources()
//{
//    if (!m_is_valid) {
//        return;
//    }
//
//    VkDevice vulkan_device = m_device_impl.get_vulkan_device();
//    VkResult result        = VK_SUCCESS;
//
//    // Fences are created initially signaled as swapchain frames in flight
//    // are not initially in use and thus we want vkWaitForFences() not to
//    // wait the first time.
//    const VkFenceCreateInfo fence_create_info{
//        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
//        .pNext = nullptr,
//        .flags = VK_FENCE_CREATE_SIGNALED_BIT
//    };
//    const VkSemaphoreCreateInfo semaphore_create_info{
//        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType
//        .pNext = nullptr,                                 // const void*
//        .flags = 0                                        // VkSemaphoreCreateFlags
//    };
//
//    ERHE_VERIFY(m_frames_in_flight.empty());
//    const size_t number_of_frames_in_flight = m_device_impl.get_number_of_frames_in_flight();
//    m_frames_in_flight.resize(number_of_frames_in_flight);
//    for (size_t i = 0; i < number_of_frames_in_flight; ++i) {
//        Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight[i];
//
//        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_submit_fence);
//        if (result != VK_SUCCESS) {
//            log_swapchain->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//            abort();
//        }
//        m_device_impl.set_debug_label(
//            VK_OBJECT_TYPE_FENCE,
//            reinterpret_cast<uint64_t>(frame_in_flight.m_submit_fence),
//            fmt::format("Swapchain frame in flight submit fence {}", i).c_str()
//        );
//
//        result = vkCreateFence(vulkan_device, &fence_create_info, nullptr, &frame_in_flight.m_if (result != VK_SUCCESS);
//        if (result != VK_SUCCESS) {
//            log_swapchain->critical("vkCreateFence() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//            abort();
//        }
//        m_device_impl.set_debug_label(
//            VK_OBJECT_TYPE_FENCE,
//            reinterpret_cast<uint64_t>(frame_in_flight.m_present_fence),
//            fmt::format("Swapchain frame in flight present fence {}", i).c_str()
//        );
//
//        result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &frame_in_flight.m_acquire_semaphore);
//        if (result != VK_SUCCESS) {
//            log_swapchain->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//            abort();
//        }
//        m_device_impl.set_debug_label(
//            VK_OBJECT_TYPE_SEMAPHORE,
//            reinterpret_cast<uint64_t>(frame_in_flight.m_acquire_semaphore),
//            fmt::format("Swapchain frame in flight acquire semaphore {}", i).c_str()
//        );
//        frame_in_flight.m_command_buffer = m_device_impl.allocate_command_buffer();
//        if (frame_in_flight.m_command_buffer == VK_NULL_HANDLE) {
//            log_swapchain->critical("Allocating command buffer failed");
//            abort();
//        }
//        m_device_impl.set_debug_label(
//            VK_OBJECT_TYPE_COMMAND_BUFFER,
//            reinterpret_cast<uint64_t>(frame_in_flight.m_command_buffer),
//            fmt::format("Swapchain frame in flight command buffer {}", i).c_str()
//        );
//    }
//}

// void Swapchain_impl::create_image_entry_resources()
// {
//     if (!m_is_valid) {
//         return;
//     }
// 
//     VkDevice vulkan_device = m_device_impl.get_vulkan_device();
//     VkResult result        = VK_SUCCESS;
// 
//     uint32_t image_count = 0;
//     result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, nullptr);
//     if (result != VK_SUCCESS) {
//         log_swapchain->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//         abort();
//     }
//     std::vector<VkImage> images(image_count);
//     result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, images.data());
//     if (result != VK_SUCCESS) {
//         log_swapchain->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//         abort();
//     }
//     log_swapchain->debug("Got {} swaphain images", image_count);
// 
//     const VkSemaphoreCreateInfo semaphore_create_info{
//         .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType
//         .pNext = nullptr,                                 // const void*
//         .flags = 0                                        // VkSemaphoreCreateFlags
//     };
// 
//     m_image_entries.resize(image_count);
//     const VkSurfaceFormatKHR surface_format = m_surface_impl.get_surface_format();
//     for (uint32_t image_index = 0; image_index < image_count; ++image_index) {
//         Swapchain_image_entry& image_entry = m_image_entries[image_index];
//         image_entry.m_image = images[image_index];
//         m_device_impl.set_debug_label(
//             VK_OBJECT_TYPE_IMAGE,
//             reinterpret_cast<uint64_t>(image_entry.m_image),
//             fmt::format("Swapchain image {}", image_index).c_str()
//         );
//         VkImageViewCreateInfo image_view_create_info{
//             .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // VkStructureType
//             .pNext      = nullptr,                                  // const void*
//             .flags      = 0,                                        // VkImageViewCreateFlags
//             .image      = images[image_index],                      // VkImage
//             .viewType   = VK_IMAGE_VIEW_TYPE_2D,                    // VkImageViewType
//             .format     = surface_format.format,                    // VkFormat
//             .components = {                                         // VkComponentMapping
//                 .r = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
//                 .g = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
//                 .b = VK_COMPONENT_SWIZZLE_IDENTITY,                 // VkComponentSwizzle
//                 .a = VK_COMPONENT_SWIZZLE_IDENTITY                  // VkComponentSwizzle
//             },                                                      
//             .subresourceRange = {                                   // VkImageSubresourceRange
//                 .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,        // VkImageAspectFlags
//                 .baseMipLevel   = 0,                                // uint32_t
//                 .levelCount     = 1,                                // uint32_t
//                 .baseArrayLayer = 0,                                // uint32_t
//                 .layerCount     = 1                                 // uint32_t
//             }
//         };
//         result = vkCreateImageView(vulkan_device, &image_view_create_info, nullptr, &image_entry.m_image_view);
//         if (result != VK_SUCCESS) {
//             log_swapchain->critical("vkCreateImageView() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//             abort();
//         }
//         m_device_impl.set_debug_label(
//             VK_OBJECT_TYPE_IMAGE_VIEW,
//             reinterpret_cast<uint64_t>(image_entry.m_image_view),
//             fmt::format("Swapchain image view {}", image_index).c_str()
//         );
// 
//         result = vkCreateSemaphore(vulkan_device, &semaphore_create_info, nullptr, &image_entry.m_submit_semaphore);
//         if (result != VK_SUCCESS) {
//             log_swapchain->critical("vkCreateSemaphore() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//             abort();
//         }
//         m_device_impl.set_debug_label(
//             VK_OBJECT_TYPE_SEMAPHORE,
//             reinterpret_cast<uint64_t>(image_entry.m_submit_semaphore),
//             fmt::format("Swapchain image submit semaphore {}", image_index).c_str()
//         );
//     }
// }

// void Swapchain_impl::create_placeholder_renderpass_and_framebuffers()
// {
//     if (!m_is_valid) {
//         return;
//     }
// 
//     VkDevice vulkan_device = m_device_impl.get_vulkan_device();
//     VkResult result        = VK_SUCCESS;
// 
//     // temp stuff
//     const VkAttachmentDescription color_attachment_description{
//         .flags          = 0,                                            // VkAttachmentDescriptionFlags
//         .format         = m_surface_impl.get_surface_format().format,   // VkFormat
//         .samples        = VK_SAMPLE_COUNT_1_BIT,                        // VkSampleCountFlagBits
//         .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,                  // VkAttachmentLoadOp
//         .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,                 // VkAttachmentStoreOp
//         .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,              // VkAttachmentLoadOp
//         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,             // VkAttachmentStoreOp
//         .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,                    // VkImageLayout
//         .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR               // VkImageLayout
//     };
// 
//     const VkAttachmentReference color_attachment_reference{
//         .attachment = 0,                                       // uint32_t
//         .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // VkImageLayout
//     };
// 
//     const VkSubpassDescription subpass_description{
//         .flags                   = 0,                               // VkSubpassDescriptionFlag
//         .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS, // VkPipelineBindPoint
//         .inputAttachmentCount    = 0,                               // uint32_t
//         .pInputAttachments       = nullptr,                         // const VkAttachmentReference*
//         .colorAttachmentCount    = 1,                               // uint32_t
//         .pColorAttachments       = &color_attachment_reference,     // const VkAttachmentReference*
//         .pResolveAttachments     = nullptr,                         // const VkAttachmentReference*
//         .pDepthStencilAttachment = nullptr,                         // const VkAttachmentReference*
//         .preserveAttachmentCount = 0,                               // uint32_t
//         .pPreserveAttachments    = nullptr                          // const uint32_t*
//     };
//     // Make the render pass wait for the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT stage
//     // - wait for the implicit subpass before or after the render pass
//     // - wait for the swap chain to finish reading from the image
//     // - wait before doing writes to color attachment
//     const VkSubpassDependency subpass_dependency{
//         .srcSubpass      = VK_SUBPASS_EXTERNAL,                           // uint32_t
//         .dstSubpass      = 0,                                             // uint32_t
//         .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
//         .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags
//         .srcAccessMask   = 0,                                             // VkAccessFlags
//         .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // VkAccessFlags
//         .dependencyFlags = 0,                                             // VkDependencyFlags
//     };
// 
//     const VkRenderPassCreateInfo render_pass_create_info{
//         .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, // VkStructureType
//         .pNext           = nullptr,                                   // const void*
//         .flags           = 0,                                         // VkRenderPassCreateFlags
//         .attachmentCount = 1,                                         // uint32_t
//         .pAttachments    = &color_attachment_description,             // const VkAttachmentDescription*
//         .subpassCount    = 1,                                         // uint32_t
//         .pSubpasses      = &subpass_description,                      // const VkSubpassDescription*
//         .dependencyCount = 1,                                         // uint32_t
//         .pDependencies   = &subpass_dependency,                       // const VkSubpassDependency*
//     };
// 
//     log_swapchain->debug("Creating renderpass");
//     result = vkCreateRenderPass(vulkan_device, &render_pass_create_info, nullptr, &m_vulkan_renderpass);
//     if (result != VK_SUCCESS) {
//         log_swapchain->critical("vkCreateRenderPass() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//         abort();
//     }
//     m_device_impl.set_debug_label(
//         VK_OBJECT_TYPE_RENDER_PASS,
//         reinterpret_cast<uint64_t>(m_vulkan_renderpass),
//         "Swapchain renderpass"
//     );
// 
//     log_swapchain->debug("Creating image entry framebuffers");
//     for (
//         uint32_t image_index = 0, image_count = static_cast<uint32_t>(m_image_entries.size());
//         image_index < image_count;
//         ++image_index
//     ) {
//         Swapchain_image_entry& image_entry = m_image_entries[image_index];
// 
//         VkFramebufferCreateInfo framebuffer_create_info{
//             .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
//             .pNext           = nullptr,
//             .flags           = 0,
//             .renderPass      = m_vulkan_renderpass,
//             .attachmentCount = 1,
//             .pAttachments    = &image_entry.m_image_view,
//             .width           = m_extent.width,
//             .height          = m_extent.height,
//             .layers          = 1
//         };
// 
//         result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &image_entry.m_framebuffer);
//         if (result != VK_SUCCESS) {
//             log_swapchain->critical("vkCreateFramebuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
//             abort();
//         }
//         m_device_impl.set_debug_label(
//             VK_OBJECT_TYPE_FRAMEBUFFER,
//             reinterpret_cast<uint64_t>(image_entry.m_framebuffer),
//             fmt::format("Swapchain framebuffer {}", image_index)
//         );
//     }
// }

void Swapchain_impl::init_swapchain_image(const uint32_t index)
{
    const VkDevice vulkan_device = m_device_impl.get_vulkan_device();
    VkResult       result        = VK_SUCCESS;

    ERHE_VERIFY(m_swapchain_objects.views[index] == VK_NULL_HANDLE);

    const VkImageViewCreateInfo view_create_info{
        .sType              = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .image              = m_swapchain_objects.images[index],
        .viewType           = VK_IMAGE_VIEW_TYPE_2D,
        .format             = m_surface_format.format,
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

    const VkFramebufferCreateInfo framebuffer_create_info{
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = 0,
        .flags           = 0,
        .renderPass      = m_render_pass,
        .attachmentCount = 1,
        .pAttachments    = &swapchain_objects.views[index],
        .width           = m_extent.width,
        .height          = m_extent.height,
        .layers          = 1
    }

    result = vkCreateFramebuffer(vulkan_device, &framebuffer_create_info, nullptr, &swapchain_objects.framebuffers[index]));
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateFramebuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    
}

void Swapchain_impl::update_vulkan_swapchain(VkSwapchainCreateInfoKHR& swapchain_create_info)
{
    const VkDevice vulkan_device    = m_device_impl.get_vulkan_device();
    const bool     has_maintenance1 = m_device_impl.get_capabilities().m_swapchain_maintenance1;
    VkResult       result           = VK_SUCCESS;

    VkSwapchainKHR old_swapchain = m_vulkan_swapchain;

    swapchain_create_info.oldSwapchain = old_swapchain;
    result = vkCreateSwapchainKHR(vulkan_device, &swapchain_create_info, nullptr, &vulkan_swapchain);
    ++m_swapchain_creation_count;
    if (result != VK_SUCCESS) {
        log_context->critical("vkCreateSwapchainKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    
    
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
    result = vkGetSwapchainImagesKHR(vulkan_device, m_vulkan_swapchain, &image_count, swapchain_objects.images.data()));
    if (result != VK_SUCCESS) {
        log_context->critical("vkGetSwapchainImagesKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }    

    if (!has_maintenance1) {
        // When VK_SWAPCHAIN_CREATE_DEFERRED_MEMORY_ALLOCATION_BIT_EXT is used, image views
        // cannot be created until the first time the image is acquired.
        for (uint32_t index = 0; index < image_count; ++index) {
            init_swapchain_image(index);
        }
    }

}

auto Swapchain_impl::get_command_buffer() -> VkCommandBuffer
{
    if (!m_is_valid) {
        return VK_NULL_HANDLE;
    }

    const uint64_t                   fif_index       = m_device_impl.get_frame_in_flight_index();
    const Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight.at(fif_index);

    return frame_in_flight.m_command_buffer;
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

    log_swapchain->debug("submit_placeholder_renderpass() fif_index = {} image_index = {} frame_index = {}", fif_index, frame_in_flight.m_image_index, frame_index);

    const VkCommandBuffer vulkan_command_buffer = get_command_buffer();
    const VkCommandBufferBeginInfo begin_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = nullptr,
        .flags            = 0,
        .pInheritanceInfo = nullptr
    };

    log_swapchain->debug("vkResetCommandBuffer()");
    result = vkResetCommandBuffer(vulkan_command_buffer, 0);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkResetCommandBuffer() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

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
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType
        .pNext           = nullptr,                                  // const void*
        .renderPass      = m_vulkan_renderpass,                      // VkRenderPass
        .framebuffer     = image_entry.m_framebuffer,                // VkFramebuffer
        .renderArea      = VkRect2D{{0,0}, m_extent},                // VkRect2D
        .clearValueCount = 1,                                        // uint32_t
        .pClearValues    = &clear_values[clear_color_index]          // const VkClearValue*
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
    log_swapchain->debug("vkQueueSubmit()");
    result = vkQueueSubmit(vulkan_graphics_queue, 1, &submit_info, frame_in_flight.m_submit_fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkQueueSubmit() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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

    log_swapchain->debug("Swapchain_impl::start_of_frame() fif_index = {} frame_index = {}", fif_index, frame_index);

    VkDevice                   vulkan_device   = m_device_impl.get_vulkan_device();
    Swapchain_frame_in_flight& frame_in_flight = m_frames_in_flight.at(fif_index);
    VkResult                   result          = VK_SUCCESS;

    log_swapchain->debug("vkWaitForFences() frame_in_flight.m_submit_fence");
    result = vkWaitForFences(vulkan_device, 1, &frame_in_flight.m_submit_fence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_swapchain->debug("vkResetFences() frame_in_flight.m_submit_fence");
    result = vkResetFences(vulkan_device, 1, &frame_in_flight.m_submit_fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkResetFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_swapchain->debug("vkAcquireNextImageKHR()");
    result = vkAcquireNextImageKHR(vulkan_device, m_vulkan_swapchain, UINT64_MAX, frame_in_flight.m_acquire_semaphore, VK_NULL_HANDLE, &frame_in_flight.m_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    }
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkAcquireNextImageKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
        abort();
    }

    log_swapchain->debug("Received image index = {}", frame_in_flight.m_image_index);
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

    log_swapchain->debug("Swapchain_impl::end_of_frame() fif_index = {} image_index = {} frame_index = {}", fif_index, frame_in_flight.m_image_index, frame_index);

    // TODO Once we have renderpass implemented properly, this may go away
    submit_placeholder_renderpass();

    if (use_present_fence) {
        log_swapchain->debug("vkWaitForFences() frame_in_flight.m_present_fence");
        result = vkWaitForFences(vulkan_device, 1, &frame_in_flight.m_present_fence, VK_TRUE, 0);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkWaitForFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
            abort();
        }

        log_swapchain->debug("vkResetFences() frame_in_flight.m_present_fence");
        result = vkResetFences(vulkan_device, 1, &frame_in_flight.m_present_fence);
        if (result != VK_SUCCESS) {
            log_swapchain->critical("vkResetFences() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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
    log_swapchain->debug("vkQueuePresentKHR()");
    result = vkQueuePresentKHR(vulkan_present_queue, &present_info);
    switch (result) {
        case VK_SUCCESS:
            break;
        case VK_SUBOPTIMAL_KHR:
            log_swapchain->warn("  VK_SUBOPTIMAL_KHR");
            break;
        case VK_ERROR_OUT_OF_DATE_KHR: {
            log_swapchain->warn("  VK_ERROR_OUT_OF_DATE_KHR");
            m_is_valid = false;
            break;
        }
        default: {
            log_swapchain->critical("vkAcquireNextImageKHR() failed with {} {}", static_cast<uint32_t>(result), c_str(result));
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
