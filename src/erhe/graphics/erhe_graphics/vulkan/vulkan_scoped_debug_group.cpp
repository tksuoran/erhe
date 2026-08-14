#include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
    , m_command_buffer_impl{nullptr}
{
    // vkCmdBeginDebugUtilsLabelEXT belongs to VK_EXT_debug_utils. If
    // the extension is not loaded the function pointer is null and a
    // call would segfault. Gate on s_enabled, which the device init
    // sets to true only after the extension is actually present and
    // the function pointers are resolved (matches the GL backend's
    // s_enabled gate).
    if (!s_enabled) {
        return;
    }

    if (Device_impl::get_device_impl() == nullptr) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {0.1f, 0.2f, 0.3f, 1.0f}
    };

    // log_debug->debug("begin debug group: {}", m_debug_label.string_view());

    // Route the label region through the impl so it can track open
    // regions: the cb may be ended (XR fan-out ends + submits the
    // rendergraph cb mid-scope) before this scope's destructor runs,
    // in which case end() auto-closes the region and the destructor
    // must not record another end label.
    Command_buffer_impl& command_buffer_impl = command_buffer.get_impl();
    if (command_buffer_impl.begin_debug_label(label_info)) {
        m_command_buffer_impl = &command_buffer_impl;
    }
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    if (m_command_buffer_impl == nullptr) {
        // Either s_enabled was false, no device impl, or the cb was
        // not recording. Nothing was opened, so nothing to close.
        return;
    }
    // log_debug->debug("end debug group: {}", m_debug_label.string_view());
    m_command_buffer_impl->end_debug_label();
}

Scoped_queue_debug_group_impl::Scoped_queue_debug_group_impl(Device& device, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
    , m_queue{VK_NULL_HANDLE}
{
    // Same VK_EXT_debug_utils gate as the cb-level scope: the queue
    // label entry points are null unless the extension is loaded.
    if (!Scoped_debug_group_impl::s_enabled) {
        return;
    }

    if (Device_impl::get_device_impl() == nullptr) {
        return;
    }

    const VkQueue vk_queue = device.get_impl().get_graphics_queue();
    if (vk_queue == VK_NULL_HANDLE) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {0.1f, 0.2f, 0.3f, 1.0f}
    };

    m_queue = vk_queue;
    vkQueueBeginDebugUtilsLabelEXT(m_queue, &label_info);
}

Scoped_queue_debug_group_impl::~Scoped_queue_debug_group_impl() noexcept
{
    if (m_queue == VK_NULL_HANDLE) {
        // Debug utils disabled or no device; nothing was opened.
        return;
    }
    vkQueueEndDebugUtilsLabelEXT(m_queue);
}

} // namespace erhe::graphics
