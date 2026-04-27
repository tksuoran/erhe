#include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

Scoped_debug_group_impl::Scoped_debug_group_impl(erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
    m_command_buffer = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;

    Device_impl* device_impl = Device_impl::get_device_impl();
    if (device_impl == nullptr) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {0.1f, 0.2f, 0.3f, 1.0f}
    };

    log_debug->debug("begin debug group: {}", m_debug_label.string_view());

    // The cb-targeted debug label needs an explicit Command_buffer& to
    // record into; there's no "active cb" singleton anymore. Until the
    // public Scoped_debug_group takes a Command_buffer, fall back to the
    // queue-level label only.
    m_queue = device_impl->get_graphics_queue();
    if (m_queue != VK_NULL_HANDLE) {
        vkQueueBeginDebugUtilsLabelEXT(m_queue, &label_info);
    }
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    log_debug->debug("end debug group: {}", m_debug_label.string_view());

    if (m_command_buffer != VK_NULL_HANDLE) {
        vkCmdEndDebugUtilsLabelEXT(m_command_buffer);
        return;
    }
    if (m_queue != VK_NULL_HANDLE) {
        vkQueueEndDebugUtilsLabelEXT(m_queue);
    }
}

} // namespace erhe::graphics
