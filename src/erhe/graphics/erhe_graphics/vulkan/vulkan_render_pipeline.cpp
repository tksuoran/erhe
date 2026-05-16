#include "erhe_graphics/vulkan/vulkan_render_pipeline.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

Render_pipeline_impl::Render_pipeline_impl(Device& device, const Render_pipeline_create_info& create_info)
    : m_device_impl{device.get_impl()}
{
    const Shader_stages* shader_stages = create_info.shader_stages;
    if (shader_stages == nullptr) {
        device.device_message(Message_severity::error,"Render_pipeline_impl: shader_stages is nullptr");
        return;
    }
    if (!shader_stages->is_valid()) {
        device.device_message(Message_severity::error,fmt::format("Render_pipeline_impl: shader_stages '{}' is not valid", shader_stages->name()));
        return;
    }

    Device_impl& device_impl = device.get_impl();
    VkDevice vk_device = device_impl.get_vulkan_device();

    // Get shader modules
    const Shader_stages_impl& stages_impl = shader_stages->get_impl();
    VkShaderModule vertex_module   = stages_impl.get_vertex_module();
    VkShaderModule fragment_module = stages_impl.get_fragment_module();
    if (vertex_module == VK_NULL_HANDLE) {
        device.device_message(Message_severity::error,fmt::format("Render_pipeline_impl: vertex module is VK_NULL_HANDLE for '{}'", shader_stages->name()));
        return;
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos;
    shader_stage_infos.push_back(VkPipelineShaderStageCreateInfo{
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stage               = VK_SHADER_STAGE_VERTEX_BIT,
        .module              = vertex_module,
        .pName               = "main",
        .pSpecializationInfo = nullptr
    });
    if (fragment_module != VK_NULL_HANDLE) {
        shader_stage_infos.push_back(VkPipelineShaderStageCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module              = fragment_module,
            .pName               = "main",
            .pSpecializationInfo = nullptr
        });
    }

    // Vertex input state
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    if (create_info.vertex_input != nullptr) {
        const Vertex_input_state_data& vis_data = create_info.vertex_input->get_data();
        for (const Vertex_input_attribute& attr : vis_data.attributes) {
            vertex_attributes.push_back(VkVertexInputAttributeDescription{
                .location = attr.layout_location,
                .binding  = static_cast<uint32_t>(attr.binding),
                .format   = to_vk_vertex_format(attr.format),
                .offset   = static_cast<uint32_t>(attr.offset)
            });
        }
        for (const Vertex_input_binding& binding : vis_data.bindings) {
            vertex_bindings.push_back(VkVertexInputBindingDescription{
                .binding   = static_cast<uint32_t>(binding.binding),
                .stride    = static_cast<uint32_t>(binding.stride),
                .inputRate = (binding.divisor == 0) ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE
            });
        }
    }

    const VkPipelineVertexInputStateCreateInfo vertex_input_state{
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = static_cast<uint32_t>(vertex_bindings.size()),
        .pVertexBindingDescriptions      = vertex_bindings.empty() ? nullptr : vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size()),
        .pVertexAttributeDescriptions    = vertex_attributes.empty() ? nullptr : vertex_attributes.data()
    };

    // Input assembly
    const Base_render_pipeline_create_info& base = create_info.base;
    const VkPrimitiveTopology vk_topology = to_vk_primitive_topology(base.input_assembly.primitive_topology);
    const bool is_strip_or_fan =
        (vk_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) ||
        (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) ||
        (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) ||
        (vk_topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY) ||
        (vk_topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY);
    const VkBool32 primitive_restart_enable =
        (create_info.base.input_assembly.primitive_restart && is_strip_or_fan) ? VK_TRUE : VK_FALSE;

    const VkPipelineInputAssemblyStateCreateInfo input_assembly_state{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = vk_topology,
        .primitiveRestartEnable = primitive_restart_enable
    };

    // Viewport and scissor are dynamic state
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
        .depthClampEnable        = base.rasterization.depth_clamp_enable ? VK_TRUE : VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = to_vk_polygon_mode(base.rasterization.polygon_mode),
        .cullMode                = to_vk_cull_mode   (base.rasterization.face_cull_enable, base.rasterization.cull_face_mode),
        .frontFace               = to_vk_front_face  (base.rasterization.front_face_direction),
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f
    };

    VkSampleCountFlagBits sample_count_flags = get_vulkan_sample_count(create_info.sample_count);

    const VkPipelineMultisampleStateCreateInfo multisample_state{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = sample_count_flags,
        .sampleShadingEnable   = base.multisample.sample_shading_enable ? VK_TRUE : VK_FALSE,
        .minSampleShading      = base.multisample.min_sample_shading,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = base.multisample.alpha_to_coverage_enable ? VK_TRUE : VK_FALSE,
        .alphaToOneEnable      = base.multisample.alpha_to_one_enable ? VK_TRUE : VK_FALSE
    };

    const VkPipelineDepthStencilStateCreateInfo depth_stencil_state{
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .depthTestEnable       = base.depth_stencil.depth_test_enable ? VK_TRUE : VK_FALSE,
        .depthWriteEnable      = base.depth_stencil.depth_write_enable ? VK_TRUE : VK_FALSE,
        .depthCompareOp        = to_vk_compare_op(base.depth_stencil.depth_compare_op),
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = base.depth_stencil.stencil_test_enable ? VK_TRUE : VK_FALSE,
        .front                 = to_vk_stencil_op_state(base.depth_stencil.stencil_front),
        .back                  = to_vk_stencil_op_state(base.depth_stencil.stencil_back),
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f
    };

    VkColorComponentFlags color_write_mask = 0;
    if (base.color_blend.write_mask.red)   color_write_mask |= VK_COLOR_COMPONENT_R_BIT;
    if (base.color_blend.write_mask.green) color_write_mask |= VK_COLOR_COMPONENT_G_BIT;
    if (base.color_blend.write_mask.blue)  color_write_mask |= VK_COLOR_COMPONENT_B_BIT;
    if (base.color_blend.write_mask.alpha) color_write_mask |= VK_COLOR_COMPONENT_A_BIT;

    const VkPipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable         = base.color_blend.enabled ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = to_vk_blend_factor(base.color_blend.rgb.source_factor),
        .dstColorBlendFactor = to_vk_blend_factor(base.color_blend.rgb.destination_factor),
        .colorBlendOp        = to_vk_blend_op    (base.color_blend.rgb.equation_mode),
        .srcAlphaBlendFactor = to_vk_blend_factor(base.color_blend.alpha.source_factor),
        .dstAlphaBlendFactor = to_vk_blend_factor(base.color_blend.alpha.destination_factor),
        .alphaBlendOp        = to_vk_blend_op    (base.color_blend.alpha.equation_mode),
        .colorWriteMask      = color_write_mask
    };

    const VkPipelineColorBlendStateCreateInfo color_blend_state{
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = (create_info.color_attachment_count > 0) ? create_info.color_attachment_count : 0u,
        .pAttachments    = (create_info.color_attachment_count > 0) ? &color_blend_attachment : nullptr,
        .blendConstants  = {
            base.color_blend.constant[0],
            base.color_blend.constant[1],
            base.color_blend.constant[2],
            base.color_blend.constant[3]
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

    // Get pipeline layout from bind group layout (fall back to shader stages' layout)
    const Bind_group_layout* bind_group_layout = base.bind_group_layout;
    if (bind_group_layout == nullptr) {
        bind_group_layout = shader_stages->get_bind_group_layout();
    }
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    if (bind_group_layout != nullptr) {
        pipeline_layout = bind_group_layout->get_impl().get_pipeline_layout();
    }
    if (pipeline_layout == VK_NULL_HANDLE) {
        device.device_message(Message_severity::error,fmt::format("Render_pipeline_impl: no pipeline layout for '{}'", shader_stages->name()));
        return;
    }

    // Compute dependency masks from attachment usage flags using shared mapping
    VkPipelineStageFlags incoming_src_stage  = 0;
    VkAccessFlags        incoming_src_access = 0;
    VkPipelineStageFlags incoming_dst_stage  = 0;
    VkAccessFlags        incoming_dst_access = 0;
    VkPipelineStageFlags outgoing_src_stage  = 0;
    VkAccessFlags        outgoing_src_access = 0;
    VkPipelineStageFlags outgoing_dst_stage  = 0;
    VkAccessFlags        outgoing_dst_access = 0;

    for (unsigned int i = 0; i < create_info.color_attachment_count; ++i) {
        const Vk_stage_access before = usage_to_vk_stage_access(create_info.color_usage_before[i], false);
        const Vk_stage_access after  = usage_to_vk_stage_access(create_info.color_usage_after[i],  false);
        incoming_src_stage  |= before.stage;
        incoming_src_access |= before.access;
        incoming_dst_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        incoming_dst_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        outgoing_src_stage  |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        outgoing_src_access |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        outgoing_dst_stage  |= after.stage;
        outgoing_dst_access |= after.access;
    }
    if (create_info.depth_attachment_format != erhe::dataformat::Format::format_undefined) {
        const Vk_stage_access before = usage_to_vk_stage_access(create_info.depth_usage_before, true);
        const Vk_stage_access after  = usage_to_vk_stage_access(create_info.depth_usage_after,  true);
        incoming_src_stage  |= before.stage;
        incoming_src_access |= before.access;
        incoming_dst_stage  |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        incoming_dst_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        outgoing_src_stage  |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        outgoing_src_access |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        outgoing_dst_stage  |= after.stage;
        outgoing_dst_access |= after.access;
    }

    // Create a compatible VkRenderPass from format info
    VkRenderPass compatible_render_pass = device_impl.get_or_create_compatible_render_pass(
        create_info.color_attachment_count,
        create_info.color_attachment_formats,
        create_info.depth_attachment_format,
        create_info.stencil_attachment_format,
        create_info.sample_count,
        incoming_src_stage,
        incoming_src_access,
        incoming_dst_stage,
        incoming_dst_access,
        outgoing_src_stage,
        outgoing_src_access,
        outgoing_dst_stage,
        outgoing_dst_access
    );
    if (compatible_render_pass == VK_NULL_HANDLE) {
        device.device_message(
            Message_severity::error,
            fmt::format("Render_pipeline_impl: failed to create compatible render pass for '{}'", shader_stages->name())
        );
        return;
    }

    const VkGraphicsPipelineCreateInfo pipeline_create_info{
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shader_stage_infos.size()),
        .pStages             = shader_stage_infos.data(),
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline_layout,
        .renderPass          = compatible_render_pass,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1
    };

    VkResult result = vkCreateGraphicsPipelines(
        vk_device,
        device_impl.get_pipeline_cache(),
        1,
        &pipeline_create_info,
        nullptr,
        &m_vk_pipeline
    );
    if (result != VK_SUCCESS) {
        device.device_message(Message_severity::error,fmt::format("vkCreateGraphicsPipelines() failed with {} for '{}'", static_cast<int32_t>(result), shader_stages->name()));
        m_vk_pipeline = VK_NULL_HANDLE;
        return;
    }

    // Debug label
    if (!base.debug_label.empty()) {
        device_impl.set_debug_label(
            VK_OBJECT_TYPE_PIPELINE,
            reinterpret_cast<uint64_t>(m_vk_pipeline),
            fmt::format("Pipeline {}", base.debug_label.data()).c_str()
        );
    }
}

Render_pipeline_impl::~Render_pipeline_impl() noexcept
{
    if (m_vk_pipeline != VK_NULL_HANDLE) {
        VkPipeline pipeline = m_vk_pipeline;
        m_vk_pipeline = VK_NULL_HANDLE;
        m_device_impl.add_completion_handler(
            [pipeline](Device_impl& device_impl) {
                vkDestroyPipeline(device_impl.get_vulkan_device(), pipeline, nullptr);
            }
        );
    }
}

auto Render_pipeline_impl::get_vk_pipeline() const -> VkPipeline
{
    return m_vk_pipeline;
}

auto Render_pipeline_impl::is_valid() const -> bool
{
    return m_vk_pipeline != VK_NULL_HANDLE;
}

} // namespace erhe::graphics
