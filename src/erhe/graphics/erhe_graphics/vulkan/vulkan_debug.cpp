#include "erhe_graphics/device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"

#include "volk.h"

namespace erhe::graphics {

bool Scoped_debug_group::s_enabled{false};

void Scoped_debug_group::begin()
{
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    const VkDebugUtilsLabelEXT label_info{
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext      = nullptr,
        .pLabelName = m_debug_label.data(),
        .color      = {1.0f, 1.0f, 1.0f, 1.0f}
    };
    vkCmdBeginDebugUtilsLabelEXT(command_buffer, &label_info);
}

void Scoped_debug_group::end()
{
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdEndDebugUtilsLabelEXT(command_buffer);
}

} // namespace erhe::graphics
