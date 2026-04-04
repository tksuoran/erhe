#include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <algorithm>

namespace erhe::graphics {

static std::vector<Gpu_timer_impl*> s_all_gpu_timers;

void Gpu_timer_impl::on_thread_enter()
{
}

void Gpu_timer_impl::on_thread_exit()
{
}

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_device{&device}
    , m_label {label}
{
    s_all_gpu_timers.push_back(this);

    Device_impl& device_impl = device.get_impl();
    VkDevice vulkan_device = device_impl.get_vulkan_device();

    const VkQueryPoolCreateInfo query_pool_create_info{
        .sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .queryType          = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount         = 2, // begin + end
        .pipelineStatistics = 0
    };

    VkResult result = vkCreateQueryPool(vulkan_device, &query_pool_create_info, nullptr, &m_query_pool);
    if (result != VK_SUCCESS) {
        log_context->warn("vkCreateQueryPool() failed for timer '{}' with {}", label, static_cast<int32_t>(result));
        m_query_pool = VK_NULL_HANDLE;
    }
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
    if ((m_device != nullptr) && (m_query_pool != VK_NULL_HANDLE)) {
        VkDevice vulkan_device = m_device->get_impl().get_vulkan_device();
        vkDestroyQueryPool(vulkan_device, m_query_pool, nullptr);
    }

    s_all_gpu_timers.erase(
        std::remove(s_all_gpu_timers.begin(), s_all_gpu_timers.end(), this),
        s_all_gpu_timers.end()
    );
}

void Gpu_timer_impl::create()
{
}

void Gpu_timer_impl::reset()
{
    m_last_result   = 0;
    m_query_started = false;
    m_query_ended   = false;
}

void Gpu_timer_impl::begin()
{
    if (m_query_pool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdResetQueryPool(command_buffer, m_query_pool, 0, 2);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_query_pool, 0);
    m_query_started = true;
    m_query_ended   = false;
}

void Gpu_timer_impl::end()
{
    if (m_query_pool == VK_NULL_HANDLE) {
        return;
    }
    if (!m_query_started) {
        return;
    }

    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_query_pool, 1);
    m_query_ended = true;
}

void Gpu_timer_impl::read()
{
    if (m_query_pool == VK_NULL_HANDLE) {
        return;
    }
    if (!m_query_started || !m_query_ended) {
        return;
    }

    VkDevice vulkan_device = m_device->get_impl().get_vulkan_device();

    uint64_t timestamps[2] = {0, 0};
    VkResult result = vkGetQueryPoolResults(
        vulkan_device,
        m_query_pool,
        0, 2,
        sizeof(timestamps),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (result == VK_SUCCESS) {
        // Convert to nanoseconds using timestamp period
        // Note: timestamps are in device-specific ticks
        m_last_result = timestamps[1] - timestamps[0];
    }

    m_query_started = false;
    m_query_ended   = false;
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    return m_last_result;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return m_label;
}

void Gpu_timer_impl::end_frame()
{
}

auto Gpu_timer_impl::all_gpu_timers() -> std::vector<Gpu_timer_impl*>
{
    return s_all_gpu_timers;
}

} // namespace erhe::graphics
