#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_helpers.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::graphics {

namespace {

auto to_vk_primitive_topology(const Primitive_type type) -> VkPrimitiveTopology
{
    switch (type) {
        case Primitive_type::point:          return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Primitive_type::line:           return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Primitive_type::line_strip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Primitive_type::triangle:       return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Primitive_type::triangle_strip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        default:                             return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

auto to_vk_cull_mode(const bool face_cull_enable, const Cull_face_mode mode) -> VkCullModeFlags
{
    if (!face_cull_enable) {
        return VK_CULL_MODE_NONE;
    }
    switch (mode) {
        case Cull_face_mode::front:          return VK_CULL_MODE_FRONT_BIT;
        case Cull_face_mode::back:           return VK_CULL_MODE_BACK_BIT;
        case Cull_face_mode::front_and_back: return VK_CULL_MODE_FRONT_AND_BACK;
        default:                             return VK_CULL_MODE_NONE;
    }
}

auto to_vk_front_face(const Front_face_direction direction) -> VkFrontFace
{
    switch (direction) {
        case Front_face_direction::ccw: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case Front_face_direction::cw:  return VK_FRONT_FACE_CLOCKWISE;
        default:                        return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    }
}

auto to_vk_polygon_mode(const Polygon_mode mode) -> VkPolygonMode
{
    switch (mode) {
        case Polygon_mode::fill:  return VK_POLYGON_MODE_FILL;
        case Polygon_mode::line:  return VK_POLYGON_MODE_LINE;
        case Polygon_mode::point: return VK_POLYGON_MODE_POINT;
        default:                  return VK_POLYGON_MODE_FILL;
    }
}

auto to_vk_compare_op(const Compare_operation op) -> VkCompareOp
{
    switch (op) {
        case Compare_operation::never:            return VK_COMPARE_OP_NEVER;
        case Compare_operation::less:             return VK_COMPARE_OP_LESS;
        case Compare_operation::equal:            return VK_COMPARE_OP_EQUAL;
        case Compare_operation::less_or_equal:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case Compare_operation::greater:          return VK_COMPARE_OP_GREATER;
        case Compare_operation::not_equal:        return VK_COMPARE_OP_NOT_EQUAL;
        case Compare_operation::greater_or_equal: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case Compare_operation::always:           return VK_COMPARE_OP_ALWAYS;
        default:                                  return VK_COMPARE_OP_ALWAYS;
    }
}

auto to_vk_stencil_op(const Stencil_op op) -> VkStencilOp
{
    switch (op) {
        case Stencil_op::zero:      return VK_STENCIL_OP_ZERO;
        case Stencil_op::keep:      return VK_STENCIL_OP_KEEP;
        case Stencil_op::replace:   return VK_STENCIL_OP_REPLACE;
        case Stencil_op::incr:      return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case Stencil_op::incr_wrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case Stencil_op::decr:      return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case Stencil_op::decr_wrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        case Stencil_op::invert:    return VK_STENCIL_OP_INVERT;
        default:                    return VK_STENCIL_OP_KEEP;
    }
}

auto to_vk_blend_factor(const Blending_factor factor) -> VkBlendFactor
{
    switch (factor) {
        case Blending_factor::zero:                     return VK_BLEND_FACTOR_ZERO;
        case Blending_factor::one:                      return VK_BLEND_FACTOR_ONE;
        case Blending_factor::src_color:                return VK_BLEND_FACTOR_SRC_COLOR;
        case Blending_factor::one_minus_src_color:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case Blending_factor::dst_color:                return VK_BLEND_FACTOR_DST_COLOR;
        case Blending_factor::one_minus_dst_color:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case Blending_factor::src_alpha:                return VK_BLEND_FACTOR_SRC_ALPHA;
        case Blending_factor::one_minus_src_alpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case Blending_factor::dst_alpha:                return VK_BLEND_FACTOR_DST_ALPHA;
        case Blending_factor::one_minus_dst_alpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case Blending_factor::constant_color:           return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case Blending_factor::one_minus_constant_color: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case Blending_factor::constant_alpha:           return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case Blending_factor::one_minus_constant_alpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case Blending_factor::src_alpha_saturate:       return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case Blending_factor::src1_color:               return VK_BLEND_FACTOR_SRC1_COLOR;
        case Blending_factor::one_minus_src1_color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case Blending_factor::src1_alpha:               return VK_BLEND_FACTOR_SRC1_ALPHA;
        case Blending_factor::one_minus_src1_alpha:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default:                                        return VK_BLEND_FACTOR_ONE;
    }
}

auto to_vk_blend_op(const Blend_equation_mode mode) -> VkBlendOp
{
    switch (mode) {
        case Blend_equation_mode::func_add:              return VK_BLEND_OP_ADD;
        case Blend_equation_mode::func_subtract:         return VK_BLEND_OP_SUBTRACT;
        case Blend_equation_mode::func_reverse_subtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case Blend_equation_mode::min_:                  return VK_BLEND_OP_MIN;
        case Blend_equation_mode::max_:                  return VK_BLEND_OP_MAX;
        default:                                         return VK_BLEND_OP_ADD;
    }
}

auto to_vk_vertex_format(const erhe::dataformat::Format format) -> VkFormat
{
    using F = erhe::dataformat::Format;
    switch (format) {
        case F::format_8_scalar_uint:   return VK_FORMAT_R8_UINT;
        case F::format_8_vec2_uint:     return VK_FORMAT_R8G8_UINT;
        case F::format_8_vec3_uint:     return VK_FORMAT_R8G8B8_UINT;
        case F::format_8_vec4_uint:     return VK_FORMAT_R8G8B8A8_UINT;
        case F::format_8_scalar_sint:   return VK_FORMAT_R8_SINT;
        case F::format_8_vec2_sint:     return VK_FORMAT_R8G8_SINT;
        case F::format_8_vec3_sint:     return VK_FORMAT_R8G8B8_SINT;
        case F::format_8_vec4_sint:     return VK_FORMAT_R8G8B8A8_SINT;
        case F::format_8_vec2_unorm:    return VK_FORMAT_R8G8_UNORM;
        case F::format_8_vec4_unorm:    return VK_FORMAT_R8G8B8A8_UNORM;
        case F::format_8_vec2_snorm:    return VK_FORMAT_R8G8_SNORM;
        case F::format_8_vec4_snorm:    return VK_FORMAT_R8G8B8A8_SNORM;
        case F::format_16_scalar_uint:  return VK_FORMAT_R16_UINT;
        case F::format_16_vec2_uint:    return VK_FORMAT_R16G16_UINT;
        case F::format_16_vec3_uint:    return VK_FORMAT_R16G16B16_UINT;
        case F::format_16_vec4_uint:    return VK_FORMAT_R16G16B16A16_UINT;
        case F::format_16_scalar_sint:  return VK_FORMAT_R16_SINT;
        case F::format_16_vec2_sint:    return VK_FORMAT_R16G16_SINT;
        case F::format_16_vec3_sint:    return VK_FORMAT_R16G16B16_SINT;
        case F::format_16_vec4_sint:    return VK_FORMAT_R16G16B16A16_SINT;
        case F::format_16_scalar_float: return VK_FORMAT_R16_SFLOAT;
        case F::format_16_vec2_float:   return VK_FORMAT_R16G16_SFLOAT;
        case F::format_16_vec4_float:   return VK_FORMAT_R16G16B16A16_SFLOAT;
        case F::format_16_scalar_unorm: return VK_FORMAT_R16_UNORM;
        case F::format_16_vec2_unorm:   return VK_FORMAT_R16G16_UNORM;
        case F::format_16_vec4_unorm:   return VK_FORMAT_R16G16B16A16_UNORM;
        case F::format_16_scalar_snorm: return VK_FORMAT_R16_SNORM;
        case F::format_16_vec2_snorm:   return VK_FORMAT_R16G16_SNORM;
        case F::format_16_vec4_snorm:   return VK_FORMAT_R16G16B16A16_SNORM;
        case F::format_32_scalar_float: return VK_FORMAT_R32_SFLOAT;
        case F::format_32_vec2_float:   return VK_FORMAT_R32G32_SFLOAT;
        case F::format_32_vec3_float:   return VK_FORMAT_R32G32B32_SFLOAT;
        case F::format_32_vec4_float:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case F::format_32_scalar_sint:  return VK_FORMAT_R32_SINT;
        case F::format_32_vec2_sint:    return VK_FORMAT_R32G32_SINT;
        case F::format_32_vec3_sint:    return VK_FORMAT_R32G32B32_SINT;
        case F::format_32_vec4_sint:    return VK_FORMAT_R32G32B32A32_SINT;
        case F::format_32_scalar_uint:  return VK_FORMAT_R32_UINT;
        case F::format_32_vec2_uint:    return VK_FORMAT_R32G32_UINT;
        case F::format_32_vec3_uint:    return VK_FORMAT_R32G32B32_UINT;
        case F::format_32_vec4_uint:    return VK_FORMAT_R32G32B32A32_UINT;
        case F::format_packed1010102_vec4_snorm: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        case F::format_packed1010102_vec4_unorm: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        default:
            ERHE_FATAL("Unsupported vertex format %u", static_cast<unsigned int>(format));
    }
}

auto to_vk_index_type(const erhe::dataformat::Format format) -> VkIndexType
{
    switch (format) {
        case erhe::dataformat::Format::format_16_scalar_uint: return VK_INDEX_TYPE_UINT16;
        case erhe::dataformat::Format::format_32_scalar_uint: return VK_INDEX_TYPE_UINT32;
        default: return VK_INDEX_TYPE_UINT32;
    }
}

auto to_vk_stencil_op_state(const Stencil_op_state& state) -> VkStencilOpState
{
    return VkStencilOpState{
        .failOp      = to_vk_stencil_op(state.stencil_fail_op),
        .passOp      = to_vk_stencil_op(state.z_pass_op),
        .depthFailOp = to_vk_stencil_op(state.z_fail_op),
        .compareOp   = to_vk_compare_op(state.function),
        .compareMask = state.test_mask,
        .writeMask   = state.write_mask,
        .reference   = state.reference
    };
}

} // anonymous namespace

Render_command_encoder_impl::Render_command_encoder_impl(Device& device)
    : m_device{device}
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

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    set_render_pipeline_state(pipeline, nullptr);
}

void Render_command_encoder_impl::set_render_pipeline_state(
    const Render_pipeline_state& pipeline,
    const Shader_stages* const   override_shader_stages
)
{
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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

    // Determine pipeline layout: prefer bind_group_layout from shader, fall back to global
    const Bind_group_layout* bind_group_layout = shader_stages->get_bind_group_layout();
    if (bind_group_layout != nullptr) {
        m_pipeline_layout = bind_group_layout->get_impl().get_pipeline_layout();
    } else {
        m_pipeline_layout = device_impl.get_pipeline_layout();
    }

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos;
    shader_stage_infos.push_back(VkPipelineShaderStageCreateInfo{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext  = nullptr,
        .flags  = 0,
        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertex_module,
        .pName  = "main",
        .pSpecializationInfo = nullptr
    });

    if (fragment_module != VK_NULL_HANDLE) {
        shader_stage_infos.push_back(VkPipelineShaderStageCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_module,
            .pName  = "main",
            .pSpecializationInfo = nullptr
        });
    }

    // Vertex input state
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
    std::vector<VkVertexInputBindingDescription>   vertex_bindings;
    if (data.vertex_input != nullptr) {
        const Vertex_input_state_data& vis_data = data.vertex_input->get_data();
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

    // Primitive restart is only valid for strip/fan topologies in Vulkan
    // unless the primitiveTopologyListRestart feature is enabled
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

    // Viewport and scissor are dynamic state
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = nullptr, // dynamic
        .scissorCount  = 1,
        .pScissors     = nullptr  // dynamic
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
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = data.multisample.sample_shading_enable ? VK_TRUE : VK_FALSE,
        .minSampleShading      = data.multisample.min_sample_shading,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE
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

    VkColorComponentFlags color_write_mask = 0;
    if (data.color_blend.write_mask.red)   color_write_mask |= VK_COLOR_COMPONENT_R_BIT;
    if (data.color_blend.write_mask.green) color_write_mask |= VK_COLOR_COMPONENT_G_BIT;
    if (data.color_blend.write_mask.blue)  color_write_mask |= VK_COLOR_COMPONENT_B_BIT;
    if (data.color_blend.write_mask.alpha) color_write_mask |= VK_COLOR_COMPONENT_A_BIT;

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
        .attachmentCount = 1,
        .pAttachments    = &color_blend_attachment,
        .blendConstants  = {0.0f, 0.0f, 0.0f, 0.0f}
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
        .layout              = m_pipeline_layout,
        .renderPass          = Render_pass_impl::get_active_render_pass(),
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = -1
    };

    // Compute pipeline cache hash from all state that affects the pipeline object
    std::size_t hash = 0;
    {
        auto hash_combine = [](std::size_t& seed, std::size_t value) {
            seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        };
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
        hash_combine(hash, static_cast<std::size_t>(data.color_blend.enabled));
        hash_combine(hash, static_cast<std::size_t>(data.color_blend.rgb.source_factor));
        hash_combine(hash, static_cast<std::size_t>(data.color_blend.rgb.destination_factor));
        hash_combine(hash, static_cast<std::size_t>(data.color_blend.alpha.source_factor));
        hash_combine(hash, static_cast<std::size_t>(data.color_blend.alpha.destination_factor));
        hash_combine(hash, static_cast<std::size_t>(color_write_mask));
        hash_combine(hash, static_cast<std::size_t>(data.multisample.sample_shading_enable));
        hash_combine(hash, static_cast<std::size_t>(data.multisample.alpha_to_coverage_enable));
        hash_combine(hash, reinterpret_cast<std::size_t>(Render_pass_impl::get_active_render_pass()));
        // Include vertex input in hash
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
    }

    VkPipeline vk_pipeline = device_impl.get_or_create_graphics_pipeline(pipeline_create_info, hash);
    if (vk_pipeline == VK_NULL_HANDLE) {
        std::string error_msg = fmt::format(
            "Pipeline creation failed for '{}'", shader_stages->name()
        );
        log_program->error("{}", error_msg);
        m_device.shader_error(error_msg, "");
        return;
    }

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

    // Set initial draw ID push constant to 0
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

void Render_command_encoder_impl::set_viewport_rect(const int x, const int y, const int width, const int height)
{
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }
    const VkViewport viewport{
        .x        = static_cast<float>(x),
        .y        = static_cast<float>(y),
        .width    = static_cast<float>(width),
        .height   = static_cast<float>(height),
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
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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

    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = VK_NULL_HANDLE, // ignored for push descriptors
            .dstBinding      = static_cast<uint32_t>(index),
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = descriptor_type,
            .pImageInfo      = nullptr,
            .pBufferInfo     = &buffer_info,
            .pTexelBufferView = nullptr
        };

        vkCmdPushDescriptorSetKHR(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            (m_pipeline_layout != VK_NULL_HANDLE) ? m_pipeline_layout : device_impl.get_pipeline_layout(),
            0, // set index
            1,
            &write
        );
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
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext           = nullptr,
            .dstSet          = descriptor_set,
            .dstBinding      = static_cast<uint32_t>(index),
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = descriptor_type,
            .pImageInfo      = nullptr,
            .pBufferInfo     = &buffer_info,
            .pTexelBufferView = nullptr
        };

        VkDevice vulkan_device = device_impl.get_vulkan_device();
        vkUpdateDescriptorSets(vulkan_device, 1, &write, 0, nullptr);

        vkCmdBindDescriptorSets(
            command_buffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            (m_pipeline_layout != VK_NULL_HANDLE) ? m_pipeline_layout : device_impl.get_pipeline_layout(),
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

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type /*primitive_type*/,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count,
    const std::uintptr_t instance_count
) const
{
    // primitive_type is baked into the pipeline via input assembly state
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
    if ((command_buffer == VK_NULL_HANDLE) || (m_index_buffer == VK_NULL_HANDLE)) {
        return;
    }

    vkCmdBindIndexBuffer(command_buffer, m_index_buffer, 0, to_vk_index_type(index_type));

    const std::size_t index_size = erhe::dataformat::get_format_size_bytes(index_type);
    const uint32_t    first_index = (index_size > 0) ? static_cast<uint32_t>(index_buffer_offset / index_size) : 0;

    vkCmdDrawIndexed(
        command_buffer,
        static_cast<uint32_t>(index_count),
        static_cast<uint32_t>(instance_count),
        first_index,
        0, // vertexOffset
        0  // firstInstance
    );
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
    VkCommandBuffer command_buffer = Render_pass_impl::get_active_command_buffer();
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

    Device_impl& device_impl = m_device.get_impl();
    const uint32_t actual_stride = (stride != 0)
        ? static_cast<uint32_t>(stride)
        : static_cast<uint32_t>(sizeof(Draw_indexed_primitives_indirect_command));

    // Loop over draws, updating draw ID push constant per draw (matching Metal pattern)
    for (std::uintptr_t i = 0; i < drawcount; ++i) {
        int32_t draw_id = static_cast<int32_t>(i);
        vkCmdPushConstants(
            command_buffer,
            (m_pipeline_layout != VK_NULL_HANDLE) ? m_pipeline_layout : device_impl.get_pipeline_layout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(int32_t),
            &draw_id
        );

        VkDeviceSize offset = static_cast<VkDeviceSize>(indirect_offset) + i * actual_stride;
        vkCmdDrawIndexedIndirect(command_buffer, m_indirect_buffer, offset, 1, actual_stride);
    }
}

void Render_command_encoder_impl::dump_state(const char* label) const
{
    static_cast<void>(label);
}

} // namespace erhe::graphics
