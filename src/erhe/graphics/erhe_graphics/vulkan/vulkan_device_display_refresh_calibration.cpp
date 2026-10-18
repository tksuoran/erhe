// Display refresh init calibration (Display_refresh_resolver): a dedicated
// swapchain on the window surface, created and destroyed inside device init
// before the application's own swapchain exists, presents clear-only frames
// and measures the display from the GPU timestamps of their completions.
//
// Two phases, each in its own present mode:
//  - free run (IMMEDIATE, else MAILBOX): no backpressure, so consecutive
//    frame ends are one production interval apart - the cost of a clear and
//    a present on this device;
//  - locked (plain FIFO, two images): once the queue is full the frame's
//    clear waits for the swapchain image the display releases at each
//    vblank, so consecutive frame ends are one refresh period apart.
// The resolver accepts the locked cadence as the display's only when the
// production interval was at most half of it. The present mode switches per
// present through VkSwapchainPresentModeInfoKHR when the surface reports the
// modes compatible (VK_KHR/EXT_swapchain_maintenance1), else the swapchain
// is recreated between the phases.
//
// Runs synchronously on the init thread; the queue mutex is taken for every
// submit and present like the rest of the device.

#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/display_refresh_resolver.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <vector>

namespace erhe::graphics {

namespace {

constexpr uint32_t s_frames_in_flight     = 2;
constexpr uint32_t s_query_ring           = 256;
constexpr uint64_t s_fence_timeout_ns     = 1'000'000'000;
constexpr uint64_t s_acquire_timeout_ns   = 500'000'000;
constexpr double   s_wall_clock_bound_s   = 3.0;

class Calibration_swapchain
{
public:
    VkSwapchainKHR           swapchain{VK_NULL_HANDLE};
    VkPresentModeKHR         present_mode{VK_PRESENT_MODE_FIFO_KHR};
    std::vector<VkImage>     images;
    std::vector<VkSemaphore> present_semaphores; // one per image
    std::vector<VkPresentModeKHR> switchable_modes;
};

auto present_kind(const VkPresentModeKHR mode) -> Display_refresh_present_kind
{
    switch (mode) {
        case VK_PRESENT_MODE_FIFO_KHR:      return Display_refresh_present_kind::fifo;
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
        case VK_PRESENT_MODE_MAILBOX_KHR:   return Display_refresh_present_kind::free_running;
        default:                            return Display_refresh_present_kind::other;
    }
}

} // namespace

void Device_impl::run_display_refresh_calibration(Surface_impl& surface_impl)
{
    Display_refresh_resolver& resolver = m_device.get_display_refresh_resolver();
    if (!resolver.is_calibrating()) {
        return;
    }
    const VkSurfaceKHR surface = surface_impl.get_vulkan_surface();
    if ((surface == VK_NULL_HANDLE) || !m_gpu_timers_supported || (m_vulkan_present_queue == VK_NULL_HANDLE)) {
        resolver.abort_calibration("no surface, present queue or GPU timestamps");
        return;
    }

    // Supported present modes: plain FIFO locks to the display; IMMEDIATE
    // (else MAILBOX) runs free.
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkan_physical_device, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> supported_modes(mode_count);
    if (mode_count > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkan_physical_device, surface, &mode_count, supported_modes.data());
    }
    const auto supported = [&supported_modes](const VkPresentModeKHR mode) -> bool {
        return std::find(supported_modes.begin(), supported_modes.end(), mode) != supported_modes.end();
    };
    const bool can_lock = supported(VK_PRESENT_MODE_FIFO_KHR);
    VkPresentModeKHR free_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    if (supported(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
        free_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    } else if (supported(VK_PRESENT_MODE_MAILBOX_KHR)) {
        free_mode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    const bool can_free_run = free_mode != VK_PRESENT_MODE_MAX_ENUM_KHR;
    resolver.set_calibration_modes(can_free_run, can_lock);
    if (!resolver.is_calibrating()) {
        return;
    }

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkan_physical_device, surface, &surface_capabilities);
    if (result != VK_SUCCESS) {
        resolver.abort_calibration("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return;
    }
    VkExtent2D extent = surface_capabilities.currentExtent;
    if ((extent.width == 0xFFFFFFFFu) || (extent.width == 0) || (extent.height == 0)) {
        extent = surface_capabilities.minImageExtent;
        extent.width  = std::max(extent.width,  1u);
        extent.height = std::max(extent.height, 1u);
    }
    if ((surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        resolver.abort_calibration("surface images cannot be cleared (no TRANSFER_DST usage)");
        return;
    }
    const VkSurfaceFormatKHR surface_format = surface_impl.get_surface_format();
    const uint32_t           image_count    = std::max(2u, surface_capabilities.minImageCount);
    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0) {
        composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }

    // Per-present mode switching: the modes compatible with the first mode,
    // when the surface and swapchain maintenance extensions are there.
    std::vector<VkPresentModeKHR> switchable_modes;
    if (m_capabilities.m_swapchain_maintenance1 && m_capabilities.m_surface_maintenance1 && m_capabilities.m_surface_capabilities2 && can_free_run && can_lock) {
        VkSurfacePresentModeCompatibilityKHR compatibility{
            .sType            = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_KHR,
            .pNext            = nullptr,
            .presentModeCount = 0,
            .pPresentModes    = nullptr
        };
        VkSurfaceCapabilities2KHR capabilities2{
            .sType               = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
            .pNext               = &compatibility,
            .surfaceCapabilities = {}
        };
        VkSurfacePresentModeKHR first_mode{
            .sType       = VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR,
            .pNext       = nullptr,
            .presentMode = free_mode
        };
        const VkPhysicalDeviceSurfaceInfo2KHR surface_info{
            .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
            .pNext   = &first_mode,
            .surface = surface
        };
        std::vector<VkPresentModeKHR> compatible;
        if ((vkGetPhysicalDeviceSurfaceCapabilities2KHR(m_vulkan_physical_device, &surface_info, &capabilities2) == VK_SUCCESS) && (compatibility.presentModeCount > 0)) {
            compatible.resize(compatibility.presentModeCount);
            compatibility.pPresentModes = compatible.data();
            if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(m_vulkan_physical_device, &surface_info, &capabilities2) == VK_SUCCESS) {
                compatible.resize(compatibility.presentModeCount);
            } else {
                compatible.clear();
            }
        }
        if (std::find(compatible.begin(), compatible.end(), VK_PRESENT_MODE_FIFO_KHR) != compatible.end()) {
            switchable_modes = {free_mode, VK_PRESENT_MODE_FIFO_KHR};
        }
    }
    log_display_refresh->info(
        "display refresh calibration: swapchain {}x{} {} images, free-running mode {}, plain FIFO {}, mode switch per present {}",
        extent.width, extent.height, image_count,
        can_free_run ? c_str(free_mode) : "none",
        can_lock ? "available" : "unavailable",
        switchable_modes.empty() ? "unavailable (swapchain recreated between the phases)" : "available"
    );

    const VkDevice vulkan_device = m_vulkan_device;

    // Per frame-in-flight: acquire semaphore, fence, command buffer.
    VkCommandPool command_pool = VK_NULL_HANDLE;
    {
        const VkCommandPoolCreateInfo pool_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_graphics_queue_family_index
        };
        result = vkCreateCommandPool(vulkan_device, &pool_info, nullptr, &command_pool);
        if (result != VK_SUCCESS) {
            resolver.abort_calibration("vkCreateCommandPool failed");
            return;
        }
    }
    std::array<VkCommandBuffer, s_frames_in_flight> command_buffers{};
    {
        const VkCommandBufferAllocateInfo allocate_info{
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = s_frames_in_flight
        };
        result = vkAllocateCommandBuffers(vulkan_device, &allocate_info, command_buffers.data());
        if (result != VK_SUCCESS) {
            vkDestroyCommandPool(vulkan_device, command_pool, nullptr);
            resolver.abort_calibration("vkAllocateCommandBuffers failed");
            return;
        }
    }
    std::array<VkSemaphore, s_frames_in_flight> acquire_semaphores{};
    std::array<VkFence,     s_frames_in_flight> fences{};
    for (uint32_t i = 0; i < s_frames_in_flight; ++i) {
        const VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0 };
        vkCreateSemaphore(vulkan_device, &semaphore_info, nullptr, &acquire_semaphores[i]);
        const VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        vkCreateFence(vulkan_device, &fence_info, nullptr, &fences[i]);
    }
    VkQueryPool query_pool = VK_NULL_HANDLE;
    {
        const VkQueryPoolCreateInfo query_pool_info{
            .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext              = nullptr,
            .flags              = 0,
            .queryType          = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount         = s_query_ring,
            .pipelineStatistics = 0
        };
        vkCreateQueryPool(vulkan_device, &query_pool_info, nullptr, &query_pool);
    }

    Calibration_swapchain current{};
    std::vector<Calibration_swapchain> retired; // destroyed at the end, after the queue idles

    const uint32_t queue_family_indices[2] = {m_graphics_queue_family_index, m_present_queue_family_index};
    const bool     concurrent_families     = m_graphics_queue_family_index != m_present_queue_family_index;

    const auto create_swapchain = [&](const VkPresentModeKHR mode, const VkSwapchainKHR old_swapchain) -> bool {
        Calibration_swapchain created{};
        created.present_mode     = mode;
        created.switchable_modes = switchable_modes;
        VkSwapchainPresentModesCreateInfoKHR modes_info{
            .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_KHR,
            .pNext            = nullptr,
            .presentModeCount = static_cast<uint32_t>(created.switchable_modes.size()),
            .pPresentModes    = created.switchable_modes.data()
        };
        const VkSwapchainCreateInfoKHR create_info{
            .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext                 = created.switchable_modes.empty() ? nullptr : &modes_info,
            .flags                 = 0,
            .surface               = surface,
            .minImageCount         = image_count,
            .imageFormat           = surface_format.format,
            .imageColorSpace       = surface_format.colorSpace,
            .imageExtent           = extent,
            .imageArrayLayers      = 1,
            .imageUsage            = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode      = concurrent_families ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = concurrent_families ? 2u : 0u,
            .pQueueFamilyIndices   = concurrent_families ? queue_family_indices : nullptr,
            .preTransform          = (surface_capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0
                                        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                        : surface_capabilities.currentTransform,
            .compositeAlpha        = composite_alpha,
            .presentMode           = mode,
            .clipped               = VK_TRUE,
            .oldSwapchain          = old_swapchain
        };
        const VkResult create_result = vkCreateSwapchainKHR(vulkan_device, &create_info, nullptr, &created.swapchain);
        if (create_result != VK_SUCCESS) {
            log_display_refresh->warn("display refresh calibration: vkCreateSwapchainKHR({}) failed with {} {}", c_str(mode), static_cast<int32_t>(create_result), c_str(create_result));
            return false;
        }
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(vulkan_device, created.swapchain, &count, nullptr);
        created.images.resize(count);
        vkGetSwapchainImagesKHR(vulkan_device, created.swapchain, &count, created.images.data());
        created.present_semaphores.resize(count, VK_NULL_HANDLE);
        for (VkSemaphore& semaphore : created.present_semaphores) {
            const VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0 };
            vkCreateSemaphore(vulkan_device, &semaphore_info, nullptr, &semaphore);
        }
        if (current.swapchain != VK_NULL_HANDLE) {
            retired.push_back(current);
        }
        current = created;
        return true;
    };

    const auto mode_for_phase = [&](const Display_refresh_calibration_phase phase) -> VkPresentModeKHR {
        return (phase == Display_refresh_calibration_phase::free_run) ? free_mode : VK_PRESENT_MODE_FIFO_KHR;
    };

    const double timestamp_period_seconds = m_gpu_timer_timestamp_period * 1e-9;
    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    std::int64_t frame = 0;
    std::array<std::int64_t, s_frames_in_flight> frame_in_slot{};
    frame_in_slot.fill(-1);
    bool ok = create_swapchain(mode_for_phase(resolver.get_calibration_phase()), VK_NULL_HANDLE);
    const char* abort_reason = nullptr;

    while (ok && resolver.is_calibrating()) {
        const double elapsed_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
        if (elapsed_s > s_wall_clock_bound_s) {
            abort_reason = "time bound reached";
            break;
        }
        // The phase's present mode: switch per present, or recreate.
        const VkPresentModeKHR wanted_mode = mode_for_phase(resolver.get_calibration_phase());
        bool switch_at_present = false;
        if (wanted_mode != current.present_mode) {
            const bool switchable = std::find(current.switchable_modes.begin(), current.switchable_modes.end(), wanted_mode) != current.switchable_modes.end();
            if (switchable) {
                switch_at_present = true;
            } else {
                {
                    const std::lock_guard<std::mutex> queue_lock{m_queue_mutex};
                    vkQueueWaitIdle(m_vulkan_graphics_queue);
                    vkQueueWaitIdle(m_vulkan_present_queue);
                }
                if (!create_swapchain(wanted_mode, current.swapchain)) {
                    abort_reason = "swapchain recreation for the next phase failed";
                    break;
                }
            }
        }

        const uint32_t slot = static_cast<uint32_t>(frame % s_frames_in_flight);
        result = vkWaitForFences(vulkan_device, 1, &fences[slot], VK_TRUE, s_fence_timeout_ns);
        if (result != VK_SUCCESS) {
            abort_reason = "frame fence timed out";
            break;
        }
        vkResetFences(vulkan_device, 1, &fences[slot]);
        // The frame that used this slot has completed: its end timestamp.
        if (frame_in_slot[slot] >= 0) {
            uint64_t ticks = 0;
            const VkResult query_result = vkGetQueryPoolResults(
                vulkan_device, query_pool, static_cast<uint32_t>(frame_in_slot[slot] % s_query_ring), 1,
                sizeof(ticks), &ticks, sizeof(ticks), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );
            if (query_result == VK_SUCCESS) {
                resolver.add_gpu_frame_end(frame_in_slot[slot], static_cast<double>(ticks & m_gpu_timer_valid_mask) * timestamp_period_seconds);
            }
            frame_in_slot[slot] = -1;
        }
        if (!resolver.is_calibrating()) {
            break;
        }

        uint32_t image_index = 0;
        result = vkAcquireNextImageKHR(vulkan_device, current.swapchain, s_acquire_timeout_ns, acquire_semaphores[slot], VK_NULL_HANDLE, &image_index);
        if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
            abort_reason = (result == VK_TIMEOUT) ? "vkAcquireNextImageKHR timed out (is the window shown?)" : "vkAcquireNextImageKHR failed";
            break;
        }

        // Record: clear the image, timestamp at the end.
        const VkCommandBuffer cb = command_buffers[slot];
        vkResetCommandBuffer(cb, 0);
        const VkCommandBufferBeginInfo begin_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(cb, &begin_info);
        const uint32_t query_index = static_cast<uint32_t>(frame % s_query_ring);
        vkCmdResetQueryPool(cb, query_pool, query_index, 1);
        const VkImageSubresourceRange whole_image{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        };
        VkImageMemoryBarrier to_transfer{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = 0,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = current.images[image_index],
            .subresourceRange    = whole_image
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);
        const VkClearColorValue black{ .float32 = {0.0f, 0.0f, 0.0f, 1.0f} };
        vkCmdClearColorImage(cb, current.images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole_image);
        VkImageMemoryBarrier to_present{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = current.images[image_index],
            .subresourceRange    = whole_image
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_present);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, query_index);
        vkEndCommandBuffer(cb);

        // Submit: the clear waits for the acquired image; the present waits
        // for the clear.
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkSubmitInfo submit_info{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &acquire_semaphores[slot],
            .pWaitDstStageMask    = &wait_stage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &current.present_semaphores[image_index]
        };
        VkPresentModeKHR present_mode_storage = wanted_mode;
        VkSwapchainPresentModeInfoKHR present_mode_info{
            .sType          = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_KHR,
            .pNext          = nullptr,
            .swapchainCount = 1,
            .pPresentModes  = &present_mode_storage
        };
        const VkPresentInfoKHR present_info{
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = switch_at_present ? &present_mode_info : nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &current.present_semaphores[image_index],
            .swapchainCount     = 1,
            .pSwapchains        = &current.swapchain,
            .pImageIndices      = &image_index,
            .pResults           = nullptr
        };
        {
            const std::lock_guard<std::mutex> queue_lock{m_queue_mutex};
            result = vkQueueSubmit(m_vulkan_graphics_queue, 1, &submit_info, fences[slot]);
            if (result != VK_SUCCESS) {
                abort_reason = "vkQueueSubmit failed";
                break;
            }
            result = vkQueuePresentKHR(m_vulkan_present_queue, &present_info);
        }
        if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
            abort_reason = "vkQueuePresentKHR failed";
            break;
        }
        if (switch_at_present) {
            current.present_mode = wanted_mode;
        }
        frame_in_slot[slot] = frame;
        resolver.note_present(frame, present_kind(current.present_mode));
        ++frame;
    }

    // Drain: the last frames' timestamps, then everything goes.
    {
        const std::lock_guard<std::mutex> queue_lock{m_queue_mutex};
        vkQueueWaitIdle(m_vulkan_graphics_queue);
        vkQueueWaitIdle(m_vulkan_present_queue);
    }
    for (uint32_t slot = 0; slot < s_frames_in_flight; ++slot) {
        // In frame order: the older slot first.
        const uint32_t ordered_slot = static_cast<uint32_t>((frame + slot) % s_frames_in_flight);
        if ((frame_in_slot[ordered_slot] >= 0) && resolver.is_calibrating()) {
            uint64_t ticks = 0;
            const VkResult query_result = vkGetQueryPoolResults(
                vulkan_device, query_pool, static_cast<uint32_t>(frame_in_slot[ordered_slot] % s_query_ring), 1,
                sizeof(ticks), &ticks, sizeof(ticks), VK_QUERY_RESULT_64_BIT
            );
            if (query_result == VK_SUCCESS) {
                resolver.add_gpu_frame_end(frame_in_slot[ordered_slot], static_cast<double>(ticks & m_gpu_timer_valid_mask) * timestamp_period_seconds);
            }
        }
    }
    if (resolver.is_calibrating()) {
        resolver.abort_calibration((abort_reason != nullptr) ? abort_reason : (ok ? "ended before the phases completed" : "calibration swapchain could not be created"));
    }
    log_display_refresh->info("display refresh calibration: {} frames presented", frame);

    const auto destroy_swapchain = [&](Calibration_swapchain& entry) {
        for (VkSemaphore semaphore : entry.present_semaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(vulkan_device, semaphore, nullptr);
            }
        }
        if (entry.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(vulkan_device, entry.swapchain, nullptr);
        }
        entry = Calibration_swapchain{};
    };
    for (Calibration_swapchain& entry : retired) {
        destroy_swapchain(entry);
    }
    destroy_swapchain(current);
    for (uint32_t i = 0; i < s_frames_in_flight; ++i) {
        vkDestroySemaphore(vulkan_device, acquire_semaphores[i], nullptr);
        vkDestroyFence    (vulkan_device, fences[i],             nullptr);
    }
    vkDestroyQueryPool  (vulkan_device, query_pool,   nullptr);
    vkDestroyCommandPool(vulkan_device, command_pool, nullptr);
}

} // namespace erhe::graphics
