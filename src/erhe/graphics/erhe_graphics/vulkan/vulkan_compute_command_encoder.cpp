#include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_compute_pipeline.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/texture.hpp"

#include <algorithm>
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : m_device        {device}
    , m_command_buffer{command_buffer}
{
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept = default;

auto Compute_command_encoder_impl::get_active_vk_command_buffer() -> VkCommandBuffer
{
    return m_command_buffer.get_impl().get_vulkan_command_buffer();
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    if (buffer == nullptr) {
        return;
    }

    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();

    VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    if ((buffer_target == Buffer_target::storage) || (buffer_target == Buffer_target::uniform_texel) || (buffer_target == Buffer_target::storage_texel)) {
        descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    if (device_impl.has_push_descriptor()) {
        const VkDescriptorBufferInfo buffer_info{
            .buffer = buffer->get_impl().get_vk_buffer(),
            .offset = offset,
            .range  = (length > 0) ? length : VK_WHOLE_SIZE
        };

        const VkWriteDescriptorSet write{
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = VK_NULL_HANDLE,
            .dstBinding      = static_cast<uint32_t>(index),
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = descriptor_type,
            .pImageInfo      = nullptr,
            .pBufferInfo     = &buffer_info,
            .pTexelBufferView = nullptr
        };

        ERHE_VERIFY(m_pipeline_layout != VK_NULL_HANDLE);
        VkPipelineLayout layout = m_pipeline_layout;

        vkCmdPushDescriptorSetKHR(
            command_buffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            layout,
            0,
            1,
            &write
        );
    }
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    if (buffer != nullptr) {
        set_buffer(buffer_target, buffer, 0, VK_WHOLE_SIZE, 0);
    }
}

void Compute_command_encoder_impl::set_storage_image(const uint32_t binding_point, const Texture& texture)
{
    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();
    ERHE_VERIFY(device_impl.has_push_descriptor()); // todo: descriptor-set fallback
    ERHE_VERIFY(m_pipeline_layout != VK_NULL_HANDLE);

    // Storage images are bound at their raw binding point (no sampler-binding
    // offset) and must already be in VK_IMAGE_LAYOUT_GENERAL (the caller
    // transitions them around the dispatch). A single-layer 2D color view.
    Texture_impl& texture_impl = const_cast<Texture_impl&>(texture.get_impl());
    VkImageView   image_view   = texture_impl.get_vk_image_view(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
    if (image_view == VK_NULL_HANDLE) {
        return;
    }

    const VkDescriptorImageInfo image_info{
        .sampler     = VK_NULL_HANDLE,
        .imageView   = image_view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    };

    const VkWriteDescriptorSet write{
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = VK_NULL_HANDLE, // ignored for push descriptors
        .dstBinding       = binding_point,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .pImageInfo       = &image_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };

    vkCmdPushDescriptorSetKHR(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout,
        0, // set index
        1,
        &write
    );
}

void Compute_command_encoder_impl::set_sampled_image(const uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();
    ERHE_VERIFY(device_impl.has_push_descriptor()); // todo: descriptor-set fallback
    ERHE_VERIFY(m_pipeline_layout != VK_NULL_HANDLE);
    ERHE_VERIFY(m_bind_group_layout != nullptr);

    // Combined image samplers are bound past the buffer bindings; resolve the
    // actual Vulkan binding (and aspect / immutability) from the layout, exactly
    // as Render_command_encoder_impl::set_sampled_image does.
    const Bind_group_layout_impl& layout_impl  = m_bind_group_layout->get_impl();
    const Sampler_aspect          aspect       = layout_impl.get_sampler_aspect_for_binding(binding_point);
    const uint32_t                vk_binding   = layout_impl.get_vulkan_binding_for_sampler(binding_point);
    const bool                    is_immutable = layout_impl.is_sampler_binding_immutable(binding_point);

    VkImageAspectFlags vk_aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageLayout      image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    switch (aspect) {
        case Sampler_aspect::color:
            vk_aspect    = VK_IMAGE_ASPECT_COLOR_BIT;
            image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            break;
        case Sampler_aspect::depth:
            vk_aspect    = VK_IMAGE_ASPECT_DEPTH_BIT;
            image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            break;
        case Sampler_aspect::stencil:
            vk_aspect    = VK_IMAGE_ASPECT_STENCIL_BIT;
            image_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            break;
    }

    Texture_impl& texture_impl = const_cast<Texture_impl&>(texture.get_impl());
    VkImageView image_view = texture_impl.get_vk_image_view(
        vk_aspect,
        0,
        std::max(1, texture_impl.get_array_layer_count())
    );
    VkSampler vk_sampler = sampler.get_impl().get_vulkan_sampler();
    if (image_view == VK_NULL_HANDLE) {
        return;
    }
    if (!is_immutable && (vk_sampler == VK_NULL_HANDLE)) {
        return;
    }

    const VkDescriptorImageInfo image_info{
        .sampler     = is_immutable ? VK_NULL_HANDLE : vk_sampler,
        .imageView   = image_view,
        .imageLayout = image_layout
    };

    const VkWriteDescriptorSet write{
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext            = nullptr,
        .dstSet           = VK_NULL_HANDLE, // ignored for push descriptors
        .dstBinding       = vk_binding,
        .dstArrayElement  = 0,
        .descriptorCount  = 1,
        .descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo       = &image_info,
        .pBufferInfo      = nullptr,
        .pTexelBufferView = nullptr
    };

    vkCmdPushDescriptorSetKHR(
        command_buffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipeline_layout,
        0, // set index
        1,
        &write
    );
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

    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
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

    ERHE_VERIFY(m_pipeline_layout != VK_NULL_HANDLE);
    VkPipelineLayout layout = m_pipeline_layout;

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

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline);

    // Schedule pipeline for cleanup
    m_device.add_completion_handler([device = device_impl.get_vulkan_device(), vk_pipeline]() {
        vkDestroyPipeline(device, vk_pipeline, nullptr);
    });
}

void Compute_command_encoder_impl::set_compute_pipeline(const Compute_pipeline& pipeline)
{
    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    VkPipeline vk_pipeline = pipeline.get_impl().get_vk_pipeline();
    if (vk_pipeline == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, vk_pipeline);
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    VkCommandBuffer command_buffer = get_active_vk_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdDispatch(
        command_buffer,
        static_cast<uint32_t>(x_size),
        static_cast<uint32_t>(y_size),
        static_cast<uint32_t>(z_size)
    );
    ERHE_VULKAN_TRACE("[DISPATCH] x={} y={} z={}", x_size, y_size, z_size);
}

} // namespace erhe::graphics
