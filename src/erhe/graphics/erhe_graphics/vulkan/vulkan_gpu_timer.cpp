#include "erhe_graphics/vulkan/vulkan_gpu_timer.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gpu_timer.hpp"

namespace erhe::graphics {

Gpu_timer_impl::Gpu_timer_impl(Device& device, const char* label)
    : m_device_impl{&device.get_impl()}
    , m_label      {label}
{
    if (m_device_impl != nullptr) {
        m_slot = m_device_impl->allocate_gpu_timer_slot(this);
    }
}

Gpu_timer_impl::~Gpu_timer_impl() noexcept
{
    if ((m_device_impl != nullptr) && (m_slot >= 0)) {
        m_device_impl->release_gpu_timer_slot(m_slot);
        m_slot = -1;
    }
}

void Gpu_timer_impl::write_begin_timestamp(Command_buffer& command_buffer)
{
    if ((m_slot < 0) || (m_device_impl == nullptr)) {
        return;
    }
    const VkCommandBuffer vk_cb = command_buffer.get_impl().get_vulkan_command_buffer();
    if (vk_cb == VK_NULL_HANDLE) {
        return;
    }
    m_device_impl->record_gpu_timer_begin_query(vk_cb, m_slot);
}

void Gpu_timer_impl::write_end_timestamp(Command_buffer& command_buffer)
{
    if ((m_slot < 0) || (m_device_impl == nullptr)) {
        return;
    }
    const VkCommandBuffer vk_cb = command_buffer.get_impl().get_vulkan_command_buffer();
    if (vk_cb == VK_NULL_HANDLE) {
        return;
    }
    m_device_impl->record_gpu_timer_end_query(vk_cb, m_slot);
}

void Gpu_timer_impl::store_last_result_ns(uint64_t nanoseconds)
{
    m_last_result = nanoseconds;
}

auto Gpu_timer_impl::last_result() -> uint64_t
{
    return m_last_result;
}

auto Gpu_timer_impl::label() const -> const char*
{
    return (m_label != nullptr) ? m_label : "(unnamed)";
}

} // namespace erhe::graphics
