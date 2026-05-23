#include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
    , m_command_buffer{VK_NULL_HANDLE}
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

    const VkCommandBuffer vk_cb = command_buffer.get_impl().get_vulkan_command_buffer();
    if (vk_cb == VK_NULL_HANDLE) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {0.1f, 0.2f, 0.3f, 1.0f}
    };

    log_debug->debug("begin debug group: {}", m_debug_label.string_view());

    m_command_buffer = vk_cb;
    vkCmdBeginDebugUtilsLabelEXT(m_command_buffer, &label_info);
}

Scoped_debug_group_impl::~Scoped_debug_group_impl() noexcept
{
    if (m_command_buffer == VK_NULL_HANDLE) {
        // Either s_enabled was false, no device impl, or the cb itself
        // was null. Nothing was opened, so nothing to close.
        return;
    }
    log_debug->debug("end debug group: {}", m_debug_label.string_view());
    vkCmdEndDebugUtilsLabelEXT(m_command_buffer);
}

} // namespace erhe::graphics
