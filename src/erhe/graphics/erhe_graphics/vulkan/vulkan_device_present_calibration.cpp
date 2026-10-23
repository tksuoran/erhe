// Present calibration at device init, Vulkan part (the algorithm and its
// rationale: present_calibration_runner.cpp): tier W through
// VK_GOOGLE_display_timing (MoltenVK), on a dedicated clear-only plain-FIFO
// swapchain on the window surface, created after the display refresh
// calibration and destroyed before the application's swapchain exists.
// Each probe presents with VkPresentTimesInfoGOOGLE (desiredPresentTime =
// the request time; MoltenVK: presentDrawable:atTime:); the displayed time
// comes back through vkGetPastPresentationTimingGOOGLE (actualPresentTime;
// MoltenVK: the drawable's presentedTime, 0 when not displayed, every
// present recorded once), the GPU end from a timestamp after the clear,
// converted with the calibrated timestamps. MoltenVK times are
// mach_absolute_time nanoseconds, the Frame_time_recorder clock on Apple;
// the display timing capability is Apple-only for that reason
// (vulkan_device_init.cpp), and so is this calibration.
//
// Runs synchronously on the init thread; the queue mutex is taken for every
// submit and present like the rest of the device.

#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_surface.hpp"
#if defined(ERHE_OS_MACOS)
#   include "erhe_graphics/vulkan/vulkan_metal_layer_mac.hpp"
#endif
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/present_calibration.hpp"
#include "erhe_graphics/present_calibration_cache.hpp"
#include "erhe_graphics/present_calibration_runner.hpp"
#include "erhe_frame_pacing/frame_time_recorder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace erhe::graphics {

namespace {

constexpr uint32_t s_query_ring         = 256;
constexpr uint64_t s_fence_timeout_ns   = 1'000'000'000;
constexpr uint64_t s_acquire_timeout_ns = 1'000'000'000;

class Vulkan_calibration_presenter : public IPresent_calibration_presenter
{
public:
    class Create_info
    {
    public:
        VkDevice                         device{VK_NULL_HANDLE};
        VkQueue                          graphics_queue{VK_NULL_HANDLE};
        VkQueue                          present_queue{VK_NULL_HANDLE};
        std::mutex*                      queue_mutex{nullptr};
        VkSwapchainKHR                   swapchain{VK_NULL_HANDLE};
        std::vector<VkImage>             images;
        VkCommandPool                    command_pool{VK_NULL_HANDLE};
        VkQueryPool                      query_pool{VK_NULL_HANDLE};
        std::function<double(uint64_t)>  gpu_ticks_to_seconds;
    };

    explicit Vulkan_calibration_presenter(Create_info&& create_info)
        : m_create_info{std::move(create_info)}
    {
        const uint32_t image_count = static_cast<uint32_t>(m_create_info.images.size());
        m_ring = image_count + 1;
        m_command_buffers.resize(m_ring, VK_NULL_HANDLE);
        const VkCommandBufferAllocateInfo allocate_info{
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = nullptr,
            .commandPool        = m_create_info.command_pool,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = m_ring
        };
        m_ok = vkAllocateCommandBuffers(m_create_info.device, &allocate_info, m_command_buffers.data()) == VK_SUCCESS;
        m_acquire_semaphores.resize(m_ring, VK_NULL_HANDLE);
        m_fences.resize(m_ring, VK_NULL_HANDLE);
        for (uint32_t i = 0; i < m_ring; ++i) {
            const VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0 };
            m_ok = m_ok && (vkCreateSemaphore(m_create_info.device, &semaphore_info, nullptr, &m_acquire_semaphores[i]) == VK_SUCCESS);
            const VkFenceCreateInfo fence_info{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
            m_ok = m_ok && (vkCreateFence(m_create_info.device, &fence_info, nullptr, &m_fences[i]) == VK_SUCCESS);
        }
        m_present_semaphores.resize(image_count, VK_NULL_HANDLE);
        for (VkSemaphore& semaphore : m_present_semaphores) {
            const VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0 };
            m_ok = m_ok && (vkCreateSemaphore(m_create_info.device, &semaphore_info, nullptr, &semaphore) == VK_SUCCESS);
        }
    }

    ~Vulkan_calibration_presenter() noexcept override
    {
        for (VkSemaphore semaphore : m_acquire_semaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_create_info.device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : m_present_semaphores) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_create_info.device, semaphore, nullptr);
            }
        }
        for (VkFence fence : m_fences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_create_info.device, fence, nullptr);
            }
        }
        // The command buffers go with the caller's command pool.
    }

    [[nodiscard]] auto is_ok() const -> bool { return m_ok; }

    auto present(const double request_time, const double commit_time) -> std::ptrdiff_t override
    {
        const std::size_t index = m_entries.size();
        const uint32_t slot = static_cast<uint32_t>(index % m_ring);
        if (vkWaitForFences(m_create_info.device, 1, &m_fences[slot], VK_TRUE, s_fence_timeout_ns) != VK_SUCCESS) {
            return -1;
        }
        uint32_t image_index = 0;
        const VkResult acquire_result = vkAcquireNextImageKHR(m_create_info.device, m_create_info.swapchain, s_acquire_timeout_ns, m_acquire_semaphores[slot], VK_NULL_HANDLE, &image_index);
        if ((acquire_result != VK_SUCCESS) && (acquire_result != VK_SUBOPTIMAL_KHR)) {
            log_display_refresh->warn("present calibration: vkAcquireNextImageKHR failed with {} {}", static_cast<int32_t>(acquire_result), c_str(acquire_result));
            return -1;
        }
        vkResetFences(m_create_info.device, 1, &m_fences[slot]);

        const VkCommandBuffer cb = m_command_buffers[slot];
        vkResetCommandBuffer(cb, 0);
        const VkCommandBufferBeginInfo begin_info{
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr
        };
        vkBeginCommandBuffer(cb, &begin_info);
        // A query is read only in its probe's own wait(), at most a batch
        // (tens of probes) later, so the ring does not wrap onto an unread one.
        const uint32_t query_index = static_cast<uint32_t>(index % s_query_ring);
        vkCmdResetQueryPool(cb, m_create_info.query_pool, query_index, 1);
        const VkImageSubresourceRange whole_image{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        };
        const VkImageMemoryBarrier to_transfer{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = 0,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = m_create_info.images[image_index],
            .subresourceRange    = whole_image
        };
        // Source stage = the acquire semaphore's wait stage (below).
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_transfer);
        const VkClearColorValue black{ .float32 = {0.0f, 0.0f, 0.0f, 1.0f} };
        vkCmdClearColorImage(cb, m_create_info.images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole_image);
        const VkImageMemoryBarrier to_present{
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = nullptr,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = 0,
            .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = m_create_info.images[image_index],
            .subresourceRange    = whole_image
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &to_present);
        // On MoltenVK the Apple GPU samples timestamps at stage / encoder
        // boundaries only, so this one may land at the command buffer's end
        // and gpu_end read slightly late (a margin slightly small: the
        // deadline errs toward more latency, not toward late frames).
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_create_info.query_pool, query_index);
        vkEndCommandBuffer(cb);

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        const VkSubmitInfo submit_info{
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &m_acquire_semaphores[slot],
            .pWaitDstStageMask    = &wait_stage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &m_present_semaphores[image_index]
        };
        // The present id ties the feedback to the probe (0 is "no id").
        const VkPresentTimeGOOGLE present_time{
            .presentID          = static_cast<uint32_t>(index + 1),
            .desiredPresentTime = (request_time > 0.0) ? static_cast<uint64_t>(request_time * 1e9) : 0
        };
        const VkPresentTimesInfoGOOGLE present_times_info{
            .sType          = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE,
            .pNext          = nullptr,
            .swapchainCount = 1,
            .pTimes         = &present_time
        };
        const VkPresentInfoKHR present_info{
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext              = &present_times_info,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &m_present_semaphores[image_index],
            .swapchainCount     = 1,
            .pSwapchains        = &m_create_info.swapchain,
            .pImageIndices      = &image_index,
            .pResults           = nullptr
        };
        m_entries.emplace_back();
        if (commit_time > 0.0) {
            present_calibration_wait_until(commit_time);
        }
        VkResult result = VK_SUCCESS;
        {
            const std::lock_guard<std::mutex> queue_lock{*m_create_info.queue_mutex};
            result = vkQueueSubmit(m_create_info.graphics_queue, 1, &submit_info, m_fences[slot]);
            if (result == VK_SUCCESS) {
                result = vkQueuePresentKHR(m_create_info.present_queue, &present_info);
            }
        }
        if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
            log_display_refresh->warn("present calibration: submit or present failed with {} {}", static_cast<int32_t>(result), c_str(result));
            return -1;
        }
        return static_cast<std::ptrdiff_t>(index);
    }

    auto wait(const std::size_t index, const double timeout_seconds) -> Present_calibration_probe_feedback override
    {
        const double end_time = erhe::frame_pacing::Frame_time_recorder::now() + std::max(0.0, timeout_seconds);
        for (;;) {
            poll();
            if (m_entries[index].presented || (erhe::frame_pacing::Frame_time_recorder::now() >= end_time)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds{500});
        }
        Present_calibration_probe_feedback& entry = m_entries[index];
        if (entry.presented && (entry.gpu_end == 0.0)) {
            // The clear completed before its image could be presented.
            uint64_t ticks = 0;
            const VkResult query_result = vkGetQueryPoolResults(
                m_create_info.device, m_create_info.query_pool, static_cast<uint32_t>(index % s_query_ring), 1,
                sizeof(ticks), &ticks, sizeof(ticks), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );
            if (query_result == VK_SUCCESS) {
                entry.gpu_end = m_create_info.gpu_ticks_to_seconds(ticks);
            }
        }
        return entry;
    }

    void service_window() override
    {
#if defined(ERHE_OS_MACOS)
        service_main_run_loop();
#endif
    }

    auto get_queue_depth() const -> std::size_t override
    {
        return m_create_info.images.size();
    }

private:
    // Drains the presentation history (MoltenVK returns each entry once).
    void poll()
    {
        for (;;) {
            uint32_t count = static_cast<uint32_t>(m_timings.size());
            const VkResult result = vkGetPastPresentationTimingGOOGLE(m_create_info.device, m_create_info.swapchain, &count, m_timings.data());
            if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
                return;
            }
            for (uint32_t i = 0; i < count; ++i) {
                const VkPastPresentationTimingGOOGLE& timing = m_timings[i];
                if ((timing.presentID == 0) || (timing.presentID > m_entries.size())) {
                    continue;
                }
                Present_calibration_probe_feedback& entry = m_entries[timing.presentID - 1];
                entry.presented      = true;
                entry.presented_time = static_cast<double>(timing.actualPresentTime) * 1e-9;
            }
            if (result == VK_SUCCESS) {
                return;
            }
        }
    }

    Create_info                                     m_create_info;
    bool                                            m_ok{false};
    uint32_t                                        m_ring{0};
    std::vector<VkCommandBuffer>                    m_command_buffers;
    std::vector<VkSemaphore>                        m_acquire_semaphores; // per ring slot
    std::vector<VkFence>                            m_fences;             // per ring slot
    std::vector<VkSemaphore>                        m_present_semaphores; // per image
    std::vector<Present_calibration_probe_feedback> m_entries;            // per probe
    std::array<VkPastPresentationTimingGOOGLE, 64>  m_timings{};
};

} // namespace

auto Device_impl::get_present_request_offset() const -> std::optional<double>
{
    return m_device.get_present_request_offset();
}

void Device_impl::run_present_calibration(Surface_impl& surface_impl)
{
    Present_calibration result{};
    result.done = true;
    const auto finish = [this, &result](const char* const failure) {
        if (failure != nullptr) {
            result.valid  = false;
            result.status = failure;
            log_display_refresh->warn("present calibration: {}; tier W requests at T/4 before the target and estimates the deadline from 2.5 ms", failure);
        } else {
            log_display_refresh->info("present calibration: {}", result.status);
        }
        m_device.finish_present_calibration(result);
    };

    // A cached calibration for this display, mode, OS build, GPU and path
    // (doc/erhe/graphics.md "Present calibration cache") replaces the probes.
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_vulkan_physical_device, &properties);
        const Present_path present_path = (m_driver_properties.driverID == VK_DRIVER_ID_MOLTENVK)
            ? Present_path::moltenvk_display_timing
            : Present_path::vulkan_display_timing;
        if (m_device.begin_present_calibration(m_context_window, properties.deviceName, present_path, result)) {
            return;
        }
    }

    const VkSurfaceKHR surface = surface_impl.get_vulkan_surface();
    if ((surface == VK_NULL_HANDLE) || !m_gpu_timers_supported || (m_vulkan_present_queue == VK_NULL_HANDLE) || (vkGetPastPresentationTimingGOOGLE == nullptr)) {
        finish("no surface, present queue, GPU timestamps or vkGetPastPresentationTimingGOOGLE");
        return;
    }
    // The probes' GPU ends in the reference clock.
    update_gpu_calibration(0);
    {
        const std::lock_guard<std::mutex> lock{m_time_calibration_mutex};
        if (!m_gpu_calibration_valid) {
            finish("no calibrated GPU timestamps");
            return;
        }
    }

    VkSurfaceCapabilitiesKHR surface_capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkan_physical_device, surface, &surface_capabilities) != VK_SUCCESS) {
        finish("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
        return;
    }
    if ((surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0) {
        finish("surface images cannot be cleared (no TRANSFER_DST usage)");
        return;
    }
    VkExtent2D extent = surface_capabilities.currentExtent;
    if ((extent.width == 0xFFFFFFFFu) || (extent.width == 0) || (extent.height == 0)) {
        extent = surface_capabilities.minImageExtent;
        extent.width  = std::max(extent.width,  1u);
        extent.height = std::max(extent.height, 1u);
    }
    // The application swapchain's image count and present mode (tier W on
    // this path: plain FIFO), so the probes see the queue the frames see.
    // A change to this probe swapchain configuration bumps
    // c_present_calibration_version (present_calibration_cache.hpp).
    uint32_t image_count = std::max(surface_impl.get_image_count(), std::max(3u, surface_capabilities.minImageCount));
    if (surface_capabilities.maxImageCount != 0) {
        image_count = std::min(image_count, surface_capabilities.maxImageCount);
    }
    const VkSurfaceFormatKHR surface_format = surface_impl.get_surface_format();
    const uint32_t queue_family_indices[2] = {m_graphics_queue_family_index, m_present_queue_family_index};
    const bool     concurrent_families     = m_graphics_queue_family_index != m_present_queue_family_index;
    const VkSwapchainCreateInfoKHR swapchain_create_info{
        .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext                 = nullptr,
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
        .preTransform          = ((surface_capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0)
                                    ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                    : surface_capabilities.currentTransform,
        .compositeAlpha        = ((surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0)
                                    ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                    : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode           = VK_PRESENT_MODE_FIFO_KHR,
        .clipped               = VK_TRUE,
        .oldSwapchain          = VK_NULL_HANDLE
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    const VkResult swapchain_result = vkCreateSwapchainKHR(m_vulkan_device, &swapchain_create_info, nullptr, &swapchain);
    if (swapchain_result != VK_SUCCESS) {
        log_display_refresh->warn("present calibration: vkCreateSwapchainKHR failed with {} {}", static_cast<int32_t>(swapchain_result), c_str(swapchain_result));
        finish("calibration swapchain could not be created");
        return;
    }
    uint32_t created_image_count = 0;
    VkResult images_result = vkGetSwapchainImagesKHR(m_vulkan_device, swapchain, &created_image_count, nullptr);
    std::vector<VkImage> images(created_image_count, VK_NULL_HANDLE);
    if (images_result == VK_SUCCESS) {
        images_result = vkGetSwapchainImagesKHR(m_vulkan_device, swapchain, &created_image_count, images.data());
        images.resize(created_image_count);
    }
    if ((images_result != VK_SUCCESS) || images.empty()) {
        log_display_refresh->warn("present calibration: vkGetSwapchainImagesKHR failed with {} {}", static_cast<int32_t>(images_result), c_str(images_result));
        vkDestroySwapchainKHR(m_vulkan_device, swapchain, nullptr);
        finish("calibration swapchain images could not be queried");
        return;
    }

    VkCommandPool command_pool = VK_NULL_HANDLE;
    const VkCommandPoolCreateInfo pool_info{
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = nullptr,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_graphics_queue_family_index
    };
    VkQueryPool query_pool = VK_NULL_HANDLE;
    const VkQueryPoolCreateInfo query_pool_info{
        .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .queryType          = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount         = s_query_ring,
        .pipelineStatistics = 0
    };
    const bool pools_ok =
        (vkCreateCommandPool(m_vulkan_device, &pool_info, nullptr, &command_pool) == VK_SUCCESS) &&
        (vkCreateQueryPool(m_vulkan_device, &query_pool_info, nullptr, &query_pool) == VK_SUCCESS);

    log_display_refresh->info(
        "present calibration: swapchain {}x{} {} images, plain FIFO, VK_GOOGLE_display_timing",
        extent.width, extent.height, created_image_count
    );
    const char* failure = "command or query pool creation failed";
    if (pools_ok) {
        const uint64_t valid_mask = m_gpu_timer_valid_mask;
        Vulkan_calibration_presenter presenter{Vulkan_calibration_presenter::Create_info{
            .device               = m_vulkan_device,
            .graphics_queue       = m_vulkan_graphics_queue,
            .present_queue        = m_vulkan_present_queue,
            .queue_mutex          = &m_queue_mutex,
            .swapchain            = swapchain,
            .images               = images,
            .command_pool         = command_pool,
            .query_pool           = query_pool,
            .gpu_ticks_to_seconds = [this, valid_mask](const uint64_t ticks) -> double {
                return gpu_ticks_to_reference_seconds(ticks & valid_mask);
            }
        }};
        failure = presenter.is_ok()
            ? erhe::graphics::run_present_calibration(presenter, m_device.get_display_refresh_duration_seconds(), result)
            : "semaphore, fence or command buffer creation failed";
        {
            const std::lock_guard<std::mutex> queue_lock{m_queue_mutex};
            vkQueueWaitIdle(m_vulkan_graphics_queue);
            vkQueueWaitIdle(m_vulkan_present_queue);
        }
        // presenter destroys its semaphores and fences here
    }
    if (query_pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_vulkan_device, query_pool, nullptr);
    }
    if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_vulkan_device, command_pool, nullptr);
    }
    vkDestroySwapchainKHR(m_vulkan_device, swapchain, nullptr);
    finish(failure);
}

} // namespace erhe::graphics
