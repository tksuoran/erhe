#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

// Pipeline-related helpers are now in vulkan_helpers.cpp

Render_command_encoder_impl::Render_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : m_device        {device}
    , m_command_buffer{command_buffer}
{
}

Render_command_encoder_impl::~Render_command_encoder_impl() noexcept
{
}

void Render_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    m_bind_group_layout = bind_group_layout;
    if (bind_group_layout != nullptr) {
        m_pipeline_layout = bind_group_layout->get_impl().get_pipeline_layout();
    } else {
        m_pipeline_layout = VK_NULL_HANDLE;
    }
}

void Render_command_encoder_impl::set_render_pipeline(const Render_pipeline& pipeline)
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    VkPipeline vk_pipeline = pipeline.get_impl().get_vk_pipeline();
    if (vk_pipeline == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

    // Set initial draw ID push constant to 0 (only needed for emulated multi-draw)
    if (m_device.get_info().emulate_multi_draw_indirect) {
        int32_t draw_id = 0;
        vkCmdPushConstants(
            command_buffer,
            m_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(int32_t),
            &draw_id
        );
    }
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    set_render_pipeline_state(pipeline, nullptr);
}

void Render_command_encoder_impl::set_render_pipeline_state(
    const Render_pipeline_state& pipeline,
    const Shader_stages* const   override_shader_stages
)
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    const Render_pipeline_data& data = pipeline.data;
    const Shader_stages* shader_stages = (override_shader_stages != nullptr) ? override_shader_stages : data.shader_stages;
    if (shader_stages == nullptr) {
        return;
    }

    if (!shader_stages->is_valid()) {
        return;
    }

    const Shader_stages_impl& stages_impl = shader_stages->get_impl();
    VkShaderModule vertex_module   = stages_impl.get_vertex_module();
    VkShaderModule fragment_module = stages_impl.get_fragment_module();

    if (vertex_module == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();

#if !defined(NDEBUG)
    ERHE_VERIFY(m_bind_group_layout != nullptr); // set_bind_group_layout() must be called before set_render_pipeline_state()
    const Bind_group_layout* shader_bind_group_layout = shader_stages->get_bind_group_layout();
    ERHE_VERIFY(shader_bind_group_layout == nullptr || shader_bind_group_layout == m_bind_group_layout); // shader bind_group_layout must match the active one
#endif

    // Use pipeline layout from the active bind group layout (must be set before pipeline state)
    ERHE_VERIFY(m_pipeline_layout != VK_NULL_HANDLE);

    // Query runtime-dependent state needed for both the hash and (on
    // cache miss) the VkGraphicsPipelineCreateInfo.
    VkSampleCountFlagBits sample_count_flags = VK_SAMPLE_COUNT_1_BIT;
    uint32_t color_attachment_count = 1;
    {
        const Render_pass_impl* active_render_pass = Device_impl::get_device_impl()->get_active_render_pass_impl();
        if (active_render_pass != nullptr) {
            sample_count_flags     = get_vulkan_sample_count(active_render_pass->get_sample_count());
            color_attachment_count = active_render_pass->get_color_attachment_count();
        }
    }

    VkColorComponentFlags color_write_mask = 0;
    if (data.color_blend.write_mask.red)   color_write_mask |= VK_COLOR_COMPONENT_R_BIT;
    if (data.color_blend.write_mask.green) color_write_mask |= VK_COLOR_COMPONENT_G_BIT;
    if (data.color_blend.write_mask.blue)  color_write_mask |= VK_COLOR_COMPONENT_B_BIT;
    if (data.color_blend.write_mask.alpha) color_write_mask |= VK_COLOR_COMPONENT_A_BIT;

    // Compute the pipeline cache hash FIRST so we can skip the full
    // VkGraphicsPipelineCreateInfo construction on cache hits (the
    // common case after warmup). This eliminates 3 heap-allocated
    // vectors and all the Vulkan state struct construction per call.
    auto hash_combine = [](std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };
    auto hash_stencil_op_state = [&hash_combine](std::size_t& hash, const Stencil_op_state& s) {
        hash_combine(hash, static_cast<std::size_t>(s.stencil_fail_op));
        hash_combine(hash, static_cast<std::size_t>(s.z_fail_op));
        hash_combine(hash, static_cast<std::size_t>(s.z_pass_op));
        hash_combine(hash, static_cast<std::size_t>(s.function));
        hash_combine(hash, static_cast<std::size_t>(s.reference));
        hash_combine(hash, static_cast<std::size_t>(s.test_mask));
        hash_combine(hash, static_cast<std::size_t>(s.write_mask));
    };

    std::size_t hash = 0;
    hash_combine(hash, reinterpret_cast<std::size_t>(m_pipeline_layout));
    hash_combine(hash, reinterpret_cast<std::size_t>(vertex_module));
    hash_combine(hash, reinterpret_cast<std::size_t>(fragment_module));
    hash_combine(hash, static_cast<std::size_t>(data.input_assembly.primitive_topology));
    hash_combine(hash, static_cast<std::size_t>(data.rasterization.cull_face_mode));
    hash_combine(hash, static_cast<std::size_t>(data.rasterization.front_face_direction));
    hash_combine(hash, static_cast<std::size_t>(data.rasterization.polygon_mode));
    hash_combine(hash, static_cast<std::size_t>(data.rasterization.face_cull_enable));
    hash_combine(hash, static_cast<std::size_t>(data.rasterization.depth_clamp_enable));
    hash_combine(hash, static_cast<std::size_t>(data.depth_stencil.depth_test_enable));
    hash_combine(hash, static_cast<std::size_t>(data.depth_stencil.depth_write_enable));
    hash_combine(hash, static_cast<std::size_t>(data.depth_stencil.depth_compare_op));
    hash_combine(hash, static_cast<std::size_t>(data.depth_stencil.stencil_test_enable));
    hash_stencil_op_state(hash, data.depth_stencil.stencil_front);
    hash_stencil_op_state(hash, data.depth_stencil.stencil_back);
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.enabled));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.rgb.equation_mode));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.rgb.source_factor));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.rgb.destination_factor));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.alpha.equation_mode));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.alpha.source_factor));
    hash_combine(hash, static_cast<std::size_t>(data.color_blend.alpha.destination_factor));
    hash_combine(hash, std::hash<float>{}(data.color_blend.constant[0]));
    hash_combine(hash, std::hash<float>{}(data.color_blend.constant[1]));
    hash_combine(hash, std::hash<float>{}(data.color_blend.constant[2]));
    hash_combine(hash, std::hash<float>{}(data.color_blend.constant[3]));
    hash_combine(hash, static_cast<std::size_t>(color_write_mask));
    hash_combine(hash, static_cast<std::size_t>(data.multisample.sample_shading_enable));
    hash_combine(hash, static_cast<std::size_t>(data.multisample.alpha_to_coverage_enable));
    hash_combine(hash, static_cast<std::size_t>(data.multisample.alpha_to_one_enable));
    hash_combine(hash, std::hash<float>{}(data.multisample.min_sample_shading));
    hash_combine(hash, static_cast<std::size_t>(sample_count_flags));
    hash_combine(hash, reinterpret_cast<std::size_t>(Device_impl::get_device_impl()->get_active_render_pass()));
    if (data.vertex_input != nullptr) {
        const Vertex_input_state_data& vis = data.vertex_input->get_data();
        hash_combine(hash, vis.attributes.size());
        for (const Vertex_input_attribute& attr : vis.attributes) {
            hash_combine(hash, attr.layout_location);
            hash_combine(hash, static_cast<std::size_t>(attr.format));
            hash_combine(hash, attr.offset);
            hash_combine(hash, attr.binding);
        }
        hash_combine(hash, vis.bindings.size());
        for (const Vertex_input_binding& binding : vis.bindings) {
            hash_combine(hash, binding.binding);
            hash_combine(hash, binding.stride);
            hash_combine(hash, static_cast<std::size_t>(binding.divisor));
        }
    }

    // Fast path: pipeline already cached. Skip all create-info construction.
    VkPipeline vk_pipeline = device_impl.get_cached_pipeline(hash);
    if (vk_pipeline == VK_NULL_HANDLE) {
        // Cache miss: build the full VkGraphicsPipelineCreateInfo.
        // Use stack arrays instead of heap-allocated vectors.
        VkPipelineShaderStageCreateInfo shader_stage_infos[2];
        uint32_t shader_stage_count = 0;
        shader_stage_infos[shader_stage_count++] = VkPipelineShaderStageCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_module,
            .pName  = "main",
            .pSpecializationInfo = nullptr
        };
        if (fragment_module != VK_NULL_HANDLE) {
            shader_stage_infos[shader_stage_count++] = VkPipelineShaderStageCreateInfo{
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext               = nullptr,
                .flags               = 0,
                .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module              = fragment_module,
                .pName               = "main",
                .pSpecializationInfo = nullptr
            };
        }

        // Vertex input: use stack arrays with reasonable upper bounds
        static constexpr std::size_t max_vertex_attributes = 16;
        static constexpr std::size_t max_vertex_bindings   = 4;
        VkVertexInputAttributeDescription vertex_attributes[max_vertex_attributes];
        VkVertexInputBindingDescription   vertex_bindings[max_vertex_bindings];
        uint32_t vertex_attribute_count = 0;
        uint32_t vertex_binding_count   = 0;
        if (data.vertex_input != nullptr) {
            const Vertex_input_state_data& vis_data = data.vertex_input->get_data();
            for (const Vertex_input_attribute& attr : vis_data.attributes) {
                if (vertex_attribute_count < max_vertex_attributes) {
                    vertex_attributes[vertex_attribute_count++] = VkVertexInputAttributeDescription{
                        .location = attr.layout_location,
                        .binding  = static_cast<uint32_t>(attr.binding),
                        .format   = to_vk_vertex_format(attr.format),
                        .offset   = static_cast<uint32_t>(attr.offset)
                    };
                }
            }
            for (const Vertex_input_binding& binding : vis_data.bindings) {
                if (vertex_binding_count < max_vertex_bindings) {
                    vertex_bindings[vertex_binding_count++] = VkVertexInputBindingDescription{
                        .binding   = static_cast<uint32_t>(binding.binding),
                        .stride    = static_cast<uint32_t>(binding.stride),
                        .inputRate = (binding.divisor == 0) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE
                    };
                }
            }
        }

        const VkPipelineVertexInputStateCreateInfo vertex_input_state{
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext                           = nullptr,
            .flags                           = 0,
            .vertexBindingDescriptionCount   = vertex_binding_count,
            .pVertexBindingDescriptions      = (vertex_binding_count > 0) ? vertex_bindings : nullptr,
            .vertexAttributeDescriptionCount = vertex_attribute_count,
            .pVertexAttributeDescriptions    = (vertex_attribute_count > 0) ? vertex_attributes : nullptr
        };

        const VkPrimitiveTopology vk_topology = to_vk_primitive_topology(data.input_assembly.primitive_topology);
        const bool is_strip_or_fan =
            (vk_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) ||
            (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) ||
            (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) ||
            (vk_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY) ||
            (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY);
        const VkBool32 primitive_restart_enable =
            (data.input_assembly.primitive_restart && is_strip_or_fan) ? VK_TRUE : VK_FALSE;

        const VkPipelineInputAssemblyStateCreateInfo input_assembly_state{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .topology               = vk_topology,
            .primitiveRestartEnable = primitive_restart_enable
        };

        const VkPipelineViewportStateCreateInfo viewport_state{
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext         = nullptr,
            .flags         = 0,
            .viewportCount = 1,
            .pViewports    = nullptr,
            .scissorCount  = 1,
            .pScissors     = nullptr
        };

        const VkPipelineRasterizationStateCreateInfo rasterization_state{
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext                   = nullptr,
            .flags                   = 0,
            .depthClampEnable        = data.rasterization.depth_clamp_enable ? VK_TRUE : VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode             = to_vk_polygon_mode(data.rasterization.polygon_mode),
            .cullMode                = to_vk_cull_mode(data.rasterization.face_cull_enable, data.rasterization.cull_face_mode),
            .frontFace               = to_vk_front_face(data.rasterization.front_face_direction),
            .depthBiasEnable         = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp          = 0.0f,
            .depthBiasSlopeFactor    = 0.0f,
            .lineWidth               = 1.0f
        };

        const VkPipelineMultisampleStateCreateInfo multisample_state{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = 0,
            .rasterizationSamples  = sample_count_flags,
            .sampleShadingEnable   = data.multisample.sample_shading_enable ? VK_TRUE : VK_FALSE,
            .minSampleShading      = data.multisample.min_sample_shading,
            .pSampleMask           = nullptr,
            .alphaToCoverageEnable = data.multisample.alpha_to_coverage_enable ? VK_TRUE : VK_FALSE,
            .alphaToOneEnable      = data.multisample.alpha_to_one_enable ? VK_TRUE : VK_FALSE
        };

        const VkPipelineDepthStencilStateCreateInfo depth_stencil_state{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = 0,
            .depthTestEnable       = data.depth_stencil.depth_test_enable ? VK_TRUE : VK_FALSE,
            .depthWriteEnable      = data.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE,
            .depthCompareOp        = to_vk_compare_op(data.depth_stencil.depth_compare_op),
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable     = data.depth_stencil.stencil_test_enable ? VK_TRUE : VK_FALSE,
            .front                 = to_vk_stencil_op_state(data.depth_stencil.stencil_front),
            .back                  = to_vk_stencil_op_state(data.depth_stencil.stencil_back),
            .minDepthBounds        = 0.0f,
            .maxDepthBounds        = 1.0f
        };

        const VkPipelineColorBlendAttachmentState color_blend_attachment{
            .blendEnable         = data.color_blend.enabled ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = to_vk_blend_factor(data.color_blend.rgb.source_factor),
            .dstColorBlendFactor = to_vk_blend_factor(data.color_blend.rgb.destination_factor),
            .colorBlendOp        = to_vk_blend_op(data.color_blend.rgb.equation_mode),
            .srcAlphaBlendFactor = to_vk_blend_factor(data.color_blend.alpha.source_factor),
            .dstAlphaBlendFactor = to_vk_blend_factor(data.color_blend.alpha.destination_factor),
            .alphaBlendOp        = to_vk_blend_op(data.color_blend.alpha.equation_mode),
            .colorWriteMask      = color_write_mask
        };

        const VkPipelineColorBlendStateCreateInfo color_blend_state{
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext           = nullptr,
            .flags           = 0,
            .logicOpEnable   = VK_FALSE,
            .logicOp         = VK_LOGIC_OP_COPY,
            .attachmentCount = color_attachment_count,
            .pAttachments    = (color_attachment_count > 0) ? &color_blend_attachment : nullptr,
            .blendConstants  = {
                data.color_blend.constant[0],
                data.color_blend.constant[1],
                data.color_blend.constant[2],
                data.color_blend.constant[3]
            }
        };

        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        const VkPipelineDynamicStateCreateInfo dynamic_state{
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .pNext             = nullptr,
            .flags             = 0,
            .dynamicStateCount = 2,
            .pDynamicStates    = dynamic_states
        };

        const VkGraphicsPipelineCreateInfo pipeline_create_info{
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stageCount          = shader_stage_count,
            .pStages             = shader_stage_infos,
            .pVertexInputState   = &vertex_input_state,
            .pInputAssemblyState = &input_assembly_state,
            .pTessellationState  = nullptr,
            .pViewportState      = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState   = &multisample_state,
            .pDepthStencilState  = &depth_stencil_state,
            .pColorBlendState    = &color_blend_state,
            .pDynamicState       = &dynamic_state,
            .layout              = m_pipeline_layout,
            .renderPass          = Device_impl::get_device_impl()->get_active_render_pass(),
            .subpass             = 0,
            .basePipelineHandle  = VK_NULL_HANDLE,
            .basePipelineIndex   = -1
        };

        vk_pipeline = device_impl.create_graphics_pipeline(pipeline_create_info, hash);
        if (vk_pipeline == VK_NULL_HANDLE) {
            std::string error_msg = fmt::format(
                "Pipeline creation failed for '{}'", shader_stages->name()
            );
            log_program->error("{}", error_msg);
            m_device.shader_error(error_msg, "");
            return;
        }
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

    // Set initial draw ID push constant to 0 (only needed for emulated multi-draw)
    if (m_device.get_info().emulate_multi_draw_indirect) {
        int32_t draw_id = 0;
        vkCmdPushConstants(
            command_buffer,
            m_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(int32_t),
            &draw_id
        );
    }
}

void Render_command_encoder_impl::set_viewport_rect(const int x, const int y, const int width, const int height)
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    // Negative height flips Y axis to match OpenGL convention (VK_KHR_maintenance1)
    // Y offset is shifted to y + height to compensate
    const VkViewport viewport{
        .x        = static_cast<float>(x),
        .y        = static_cast<float>(y + height),
        .width    = static_cast<float>(width),
        .height   = static_cast<float>(-height),
        .minDepth = m_viewport_znear,
        .maxDepth = m_viewport_zfar
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
}

void Render_command_encoder_impl::set_viewport_depth_range(const float min_depth, const float max_depth)
{
    m_viewport_znear = min_depth;
    m_viewport_zfar  = max_depth;
    // Viewport depth range is part of VkViewport; it will be applied at the next set_viewport_rect call.
}

void Render_command_encoder_impl::set_scissor_rect(const int x, const int y, const int width, const int height)
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    const VkRect2D scissor{
        .offset = {
            .x = std::max(0, x),
            .y = std::max(0, y)
        },
        .extent = {
            .width  = static_cast<uint32_t>(std::max(0, width)),
            .height = static_cast<uint32_t>(std::max(0, height))
        }
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

void Render_command_encoder_impl::set_index_buffer(const Buffer* const buffer)
{
    if (buffer != nullptr) {
        m_index_buffer = buffer->get_impl().get_vk_buffer();
    } else {
        m_index_buffer = VK_NULL_HANDLE;
    }
}

void Render_command_encoder_impl::set_vertex_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t index
)
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if ((command_buffer == VK_NULL_HANDLE) || (buffer == nullptr)) {
        return;
    }
    VkBuffer     vk_buffer = buffer->get_impl().get_vk_buffer();
    VkDeviceSize vk_offset = static_cast<VkDeviceSize>(offset);
    uint32_t     binding   = static_cast<uint32_t>(index);
    vkCmdBindVertexBuffers(command_buffer, binding, 1, &vk_buffer, &vk_offset);
}

void Render_command_encoder_impl::set_buffer(
    Buffer_target      buffer_target,
    const Buffer*      buffer,
    std::uintptr_t     offset,
    std::uintptr_t     length,
    std::uintptr_t     index
)
{
    if (buffer == nullptr) {
        return;
    }

    if (buffer_target == Buffer_target::draw_indirect) {
        m_indirect_buffer = buffer->get_impl().get_vk_buffer();
        return;
    }

#if !defined(NDEBUG)
    ERHE_VERIFY(m_bind_group_layout != nullptr); // set_bind_group_layout() must be called before set_buffer()
#endif

    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    Device_impl& device_impl = m_device.get_impl();

    if (device_impl.has_push_descriptor()) {
        VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if ((buffer_target == Buffer_target::storage) || (buffer_target == Buffer_target::uniform_texel) || (buffer_target == Buffer_target::storage_texel)) {
            descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        const VkDescriptorBufferInfo buffer_info{
            .buffer = buffer->get_impl().get_vk_buffer(),
            .offset = static_cast<VkDeviceSize>(offset),
            .range  = static_cast<VkDeviceSize>(length)
        };

        const VkWriteDescriptorSet write{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = VK_NULL_HANDLE, // ignored for push descriptors
            .dstBinding       = static_cast<uint32_t>(index),
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = descriptor_type,
            .pImageInfo       = nullptr,
            .pBufferInfo      = &buffer_info,
            .pTexelBufferView = nullptr
        };

        vkCmdPushDescriptorSetKHR(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline_layout,
            0, // set index
            1,
            &write
        );
        ++device_impl.m_desc_push_buf_count;
        ERHE_VULKAN_DESC_TRACE("[PUSH_BUF] binding={} offset={} size={}", index, offset, length);
    } else {
        // Traditional descriptor set fallback
        VkDescriptorType descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        if ((buffer_target == Buffer_target::storage) || (buffer_target == Buffer_target::uniform_texel) || (buffer_target == Buffer_target::storage_texel)) {
            descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        VkDescriptorSet descriptor_set = device_impl.allocate_descriptor_set();
        if (descriptor_set == VK_NULL_HANDLE) {
            return;
        }

        const VkDescriptorBufferInfo buffer_info{
            .buffer = buffer->get_impl().get_vk_buffer(),
            .offset = static_cast<VkDeviceSize>(offset),
            .range  = static_cast<VkDeviceSize>(length)
        };

        const VkWriteDescriptorSet write{
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = nullptr,
            .dstSet           = descriptor_set,
            .dstBinding       = static_cast<uint32_t>(index),
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = descriptor_type,
            .pImageInfo       = nullptr,
            .pBufferInfo      = &buffer_info,
            .pTexelBufferView = nullptr
        };

        VkDevice vulkan_device = device_impl.get_vulkan_device();
        vkUpdateDescriptorSets(vulkan_device, 1, &write, 0, nullptr);

        vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipeline_layout,
            0, // set index
            1,
            &descriptor_set,
            0,
            nullptr
        );
    }
}

void Render_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    if (buffer == nullptr) {
        return;
    }
    set_buffer(buffer_target, buffer, 0, VK_WHOLE_SIZE, 0);
}

void Render_command_encoder_impl::set_sampled_image(
    const uint32_t binding_point,
    const Texture& texture,
    const Sampler& sampler
)
{
    ERHE_VERIFY(m_bind_group_layout != nullptr);
    Device_impl& device_impl = m_device.get_impl();
    ERHE_VERIFY(device_impl.has_push_descriptor()); // todo: descriptor-set fallback

    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

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

    // For immutable bindings the descriptor's sampler field is ignored by
    // the driver; writing VK_NULL_HANDLE prevents validation tripping
    // VUID-VkDescriptorImageInfo-mutableComparisonSamplers-04450 when the
    // pre-baked sampler has compareEnable=true on devices that report
    // VkPhysicalDevicePortabilitySubsetFeaturesKHR::mutableComparisonSamplers
    // as VK_FALSE (e.g. MoltenVK).
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
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_pipeline_layout,
        0, // set index
        1,
        &write
    );
    ++device_impl.m_desc_push_img_count;
    ERHE_VULKAN_DESC_TRACE("[PUSH_IMG] binding={} image=0x{:x}", vk_binding, reinterpret_cast<std::uintptr_t>(image_info.imageView));
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type /*primitive_type*/,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count,
    const std::uintptr_t instance_count
) const
{
    // primitive_type is baked into the pipeline via input assembly state
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    vkCmdDraw(
        command_buffer,
        static_cast<uint32_t>(vertex_count),
        static_cast<uint32_t>(instance_count),
        static_cast<uint32_t>(vertex_start),
        0 // firstInstance
    );
    ++Device_impl::get_device_impl()->m_desc_draw_count;
    ERHE_VULKAN_TRACE("[DRAW] type=direct vertices={} instances={}", vertex_count, instance_count);
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type primitive_type,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count
) const
{
    draw_primitives(primitive_type, vertex_start, vertex_count, 1);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    const Primitive_type           /*primitive_type*/,
    const std::uintptr_t           index_count,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           index_buffer_offset,
    const std::uintptr_t           instance_count
) const
{
    // primitive_type is baked into the pipeline via input assembly state
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if ((command_buffer == VK_NULL_HANDLE) || (m_index_buffer == VK_NULL_HANDLE)) {
        return;
    }

    vkCmdBindIndexBuffer(command_buffer, m_index_buffer, 0, to_vk_index_type(index_type));

    const std::size_t index_size  = erhe::dataformat::get_format_size_bytes(index_type);
    const uint32_t    first_index = (index_size > 0) ? static_cast<uint32_t>(index_buffer_offset / index_size) : 0;

    vkCmdDrawIndexed(
        command_buffer,
        static_cast<uint32_t>(index_count),
        static_cast<uint32_t>(instance_count),
        first_index,
        0, // vertexOffset
        0  // firstInstance
    );
    ++Device_impl::get_device_impl()->m_desc_draw_count;
    ERHE_VULKAN_TRACE("[DRAW] type=indexed indices={} instances={}", index_count, instance_count);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    const Primitive_type           primitive_type,
    const std::uintptr_t           index_count,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           index_buffer_offset
) const
{
    draw_indexed_primitives(primitive_type, index_count, index_type, index_buffer_offset, 1);
}

void Render_command_encoder_impl::multi_draw_indexed_primitives_indirect(
    const Primitive_type           /*primitive_type*/,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           indirect_offset,
    const std::uintptr_t           drawcount,
    const std::uintptr_t           stride
) const
{
    VkCommandBuffer command_buffer = m_command_buffer.get_impl().get_vulkan_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    if (m_index_buffer == VK_NULL_HANDLE) {
        return;
    }
    if (m_indirect_buffer == VK_NULL_HANDLE) {
        return;
    }

    // Bind the index buffer
    vkCmdBindIndexBuffer(command_buffer, m_index_buffer, 0, to_vk_index_type(index_type));

    const uint32_t actual_stride = (stride != 0)
        ? static_cast<uint32_t>(stride)
        : static_cast<uint32_t>(sizeof(Draw_indexed_primitives_indirect_command));

    const Device_info& info = m_device.get_info();
    if (info.use_multi_draw_indirect_core) {
        // Native multi-draw: single call, gl_DrawID built-in provides draw index
        vkCmdDrawIndexedIndirect(
            command_buffer,
            m_indirect_buffer,
            static_cast<VkDeviceSize>(indirect_offset),
            static_cast<uint32_t>(drawcount),
            actual_stride
        );
        ++Device_impl::get_device_impl()->m_desc_draw_count;
        ERHE_VULKAN_TRACE("[DRAW] type=mdi drawcount={}", drawcount);
    } else {
        // Emulated multi-draw: loop with per-draw push constant
        VkPipelineLayout layout = m_pipeline_layout;
        for (std::uintptr_t i = 0; i < drawcount; ++i) {
            int32_t draw_id = static_cast<int32_t>(i);
            vkCmdPushConstants(
                command_buffer,
                layout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(int32_t),
                &draw_id
            );

            VkDeviceSize offset = static_cast<VkDeviceSize>(indirect_offset) + i * actual_stride;
            vkCmdDrawIndexedIndirect(command_buffer, m_indirect_buffer, offset, 1, actual_stride);
        }
        Device_impl::get_device_impl()->m_desc_draw_count += static_cast<uint32_t>(drawcount);
        ERHE_VULKAN_TRACE("[DRAW] type=emulated_mdi drawcount={}", drawcount);
    }
}

void Render_command_encoder_impl::dump_state(const char* label) const
{
    static_cast<void>(label);
}

} // namespace erhe::graphics
