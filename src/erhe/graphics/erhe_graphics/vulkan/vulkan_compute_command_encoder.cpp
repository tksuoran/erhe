#include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_immediate_commands.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/vulkan/vulkan_submit_handle.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device)
    : m_device{device}
{
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    if (buffer == nullptr) {
        return;
    }

    // For compute, we use an immediate command buffer
    // Push descriptor binding is done in dispatch_compute context
    // For now, store buffer info for later use
    static_cast<void>(buffer_target);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

void Compute_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    m_bind_group_layout = bind_group_layout;
    if (bind_group_layout != nullptr) {
        m_pipeline_layout = bind_group_layout->get_impl().get_pipeline_layout();
    } else {
        m_pipeline_layout = VK_NULL_HANDLE;
    }
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    const Compute_pipeline_data& data = pipeline.data;
    if (data.shader_stages == nullptr) {
        return;
    }
    if (!data.shader_stages->is_valid()) {
        return;
    }

    const Shader_stages_impl& stages_impl = data.shader_stages->get_impl();
    VkShaderModule compute_module = stages_impl.get_compute_module();
    if (compute_module == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();

    const VkPipelineShaderStageCreateInfo shader_stage_info{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext  = nullptr,
        .flags  = 0,
        .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = compute_module,
        .pName  = "main",
        .pSpecializationInfo = nullptr
    };

    VkPipelineLayout layout = (m_pipeline_layout != VK_NULL_HANDLE)
        ? m_pipeline_layout
        : device_impl.get_pipeline_layout();

    const VkComputePipelineCreateInfo pipeline_create_info{
        .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext  = nullptr,
        .flags  = 0,
        .stage  = shader_stage_info,
        .layout = layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex  = -1
    };

    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(
        device_impl.get_vulkan_device(),
        device_impl.get_pipeline_cache(),
        1,
        &pipeline_create_info,
        nullptr,
        &vk_pipeline
    );

    if (result != VK_SUCCESS) {
        log_program->error("vkCreateComputePipelines() failed with {}", static_cast<int32_t>(result));
        return;
    }

    // Bind the pipeline on the active command buffer if there is one,
    // otherwise use an immediate command buffer
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline);
    }

    // Schedule pipeline for cleanup
    m_device.add_completion_handler([device = device_impl.get_vulkan_device(), vk_pipeline]() {
        vkDestroyPipeline(device, vk_pipeline, nullptr);
    });
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdDispatch(
        command_buffer,
        static_cast<uint32_t>(x_size),
        static_cast<uint32_t>(y_size),
        static_cast<uint32_t>(z_size)
    );
}

} // namespace erhe::graphics
