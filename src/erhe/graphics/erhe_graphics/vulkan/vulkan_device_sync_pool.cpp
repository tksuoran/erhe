#include "erhe_graphics/vulkan/vulkan_device_sync_pool.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Device_sync_pool::Device_sync_pool(Device_impl& device_impl)
    : m_device_impl  {device_impl}
    , m_vulkan_device{device_impl.get_vulkan_device()}
{
}

Device_sync_pool::~Device_sync_pool() noexcept
{
    log_swapchain->debug("Sync pool semaphore count at destruction: {}", m_semaphore_pool.size());
    log_swapchain->debug("Sync pool fence count at destruction: {}",     m_fence_pool.size());

    for (VkSemaphore semaphore : m_semaphore_pool) {
        vkDestroySemaphore(m_vulkan_device, semaphore, nullptr);
    }
    for (VkFence fence : m_fence_pool) {
        vkDestroyFence(m_vulkan_device, fence, nullptr);
    }
}

auto Device_sync_pool::get_semaphore() -> VkSemaphore
{
    std::size_t serial = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_semaphore_pool.empty()) {
            const VkSemaphore semaphore = m_semaphore_pool.back();
            m_semaphore_pool.pop_back();
            return semaphore;
        }
        serial = m_semaphore_serial++;
    }

    const VkSemaphoreCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };
    VkSemaphore vulkan_semaphore{VK_NULL_HANDLE};
    const VkResult result = vkCreateSemaphore(m_vulkan_device, &create_info, nullptr, &vulkan_semaphore);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateSemaphore() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_SEMAPHORE,
        reinterpret_cast<uint64_t>(vulkan_semaphore),
        fmt::format("Sync pool semaphore {}", serial).c_str()
    );

    return vulkan_semaphore;
}

auto Device_sync_pool::get_fence() -> VkFence
{
    std::size_t serial = 0;
    {
        const std::lock_guard<std::mutex> lock{m_mutex};
        if (!m_fence_pool.empty()) {
            const VkFence fence = m_fence_pool.back();
            m_fence_pool.pop_back();
            return fence;
        }
        serial = m_fence_serial++;
    }

    const VkFenceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };
    VkFence vulkan_fence{VK_NULL_HANDLE};
    const VkResult result = vkCreateFence(m_vulkan_device, &create_info, nullptr, &vulkan_fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkCreateFence() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    m_device_impl.set_debug_label(
        VK_OBJECT_TYPE_FENCE,
        reinterpret_cast<uint64_t>(vulkan_fence),
        fmt::format("Sync pool fence {}", serial).c_str()
    );

    return vulkan_fence;
}

void Device_sync_pool::recycle_semaphore(const VkSemaphore semaphore)
{
    ERHE_VERIFY(semaphore != VK_NULL_HANDLE);
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_semaphore_pool.push_back(semaphore);
}

void Device_sync_pool::recycle_fence(const VkFence fence)
{
    ERHE_VERIFY(fence != VK_NULL_HANDLE);
    // Reset before the fence becomes visible to other threads in the list.
    const VkResult result = vkResetFences(m_vulkan_device, 1, &fence);
    if (result != VK_SUCCESS) {
        log_swapchain->critical("vkResetFences() failed with {} {}", static_cast<int32_t>(result), c_str(result));
        abort();
    }
    const std::lock_guard<std::mutex> lock{m_mutex};
    m_fence_pool.push_back(fence);
}

} // namespace erhe::graphics
