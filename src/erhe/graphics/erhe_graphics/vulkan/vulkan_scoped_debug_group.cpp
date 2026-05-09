#include "erhe_graphics/vulkan/vulkan_scoped_debug_group.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"

namespace erhe::graphics {

bool Scoped_debug_group_impl::s_enabled{false};

namespace {

[[nodiscard]] auto make_label_info(const erhe::utility::Debug_label& debug_label) -> VkDebugUtilsLabelEXT
{
    return VkDebugUtilsLabelEXT{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = debug_label.data(),
        .color      = {0.1f, 0.2f, 0.3f, 1.0f}
    };
}

} // anonymous namespace

Scoped_debug_group_impl::Scoped_debug_group_impl(erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
    m_command_buffer = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;

    Device_impl* device_impl = Device_impl::get_device_impl();
    if (device_impl == nullptr) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info = make_label_info(m_debug_label);

    log_debug->debug("begin debug group: {}", m_debug_label.string_view());

    // No command buffer was supplied; record the label at queue level so
    // it still shows up at submission time. Note: queue-level labels do
    // NOT bracket the cb's draw commands in RenderDoc captures -- callers
    // that want the label to bracket their draws must use the
    // Command_buffer-targeted overload.
    m_queue = device_impl->get_graphics_queue();
    if (m_queue != VK_NULL_HANDLE) {
        vkQueueBeginDebugUtilsLabelEXT(m_queue, &label_info);
    }
}

Scoped_debug_group_impl::Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label)
    : m_debug_label{std::move(debug_label)}
{
    m_command_buffer = VK_NULL_HANDLE;
    m_queue          = VK_NULL_HANDLE;

    Device_impl* device_impl = Device_impl::get_device_impl();
    if (device_impl == nullptr) {
        return;
    }

    const VkCommandBuffer vk_cb = command_buffer.get_impl().get_vulkan_command_buffer();
    if (vk_cb == VK_NULL_HANDLE) {
        return;
    }

    const VkDebugUtilsLabelEXT label_info = make_label_info(m_debug_label);

    log_debug->debug("begin debug group (cb): {}", m_debug_label.string_view());

    m_command_buffer = vk_cb;
    vkCmdBeginDebugUtilsLabelEXT(m_command_buffer, &label_info);
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
