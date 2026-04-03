#include "erhe_graphics/metal/metal_render_command_encoder.hpp"
#include "erhe_graphics/metal/metal_render_pass.hpp"
#include "erhe_graphics/metal/metal_buffer.hpp"
#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/metal/metal_helpers.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <Metal/Metal.hpp>

namespace erhe::graphics {

namespace {

auto to_mtl_primitive_type(const Primitive_type type) -> MTL::PrimitiveType
{
    switch (type) {
        case Primitive_type::point:          return MTL::PrimitiveTypePoint;
        case Primitive_type::line:           return MTL::PrimitiveTypeLine;
        case Primitive_type::line_strip:     return MTL::PrimitiveTypeLineStrip;
        case Primitive_type::triangle:       return MTL::PrimitiveTypeTriangle;
        case Primitive_type::triangle_strip: return MTL::PrimitiveTypeTriangleStrip;
        default:                             return MTL::PrimitiveTypeTriangle;
    }
}

auto to_mtl_index_type(const erhe::dataformat::Format format) -> MTL::IndexType
{
    switch (format) {
        case erhe::dataformat::Format::format_16_scalar_uint: return MTL::IndexTypeUInt16;
        case erhe::dataformat::Format::format_32_scalar_uint: return MTL::IndexTypeUInt32;
        default: return MTL::IndexTypeUInt32;
    }
}

auto to_mtl_vertex_format(const erhe::dataformat::Format format) -> MTL::VertexFormat
{
    using F = erhe::dataformat::Format;
    switch (format) {
        case F::format_8_scalar_uint:    return MTL::VertexFormatUChar;
        case F::format_8_vec2_uint:      return MTL::VertexFormatUChar2;
        case F::format_8_vec3_uint:      return MTL::VertexFormatUChar3;
        case F::format_8_vec4_uint:      return MTL::VertexFormatUChar4;
        case F::format_8_scalar_sint:    return MTL::VertexFormatChar;
        case F::format_8_vec2_sint:      return MTL::VertexFormatChar2;
        case F::format_8_vec3_sint:      return MTL::VertexFormatChar3;
        case F::format_8_vec4_sint:      return MTL::VertexFormatChar4;
        case F::format_8_vec2_unorm:     return MTL::VertexFormatUChar2Normalized;
        case F::format_8_vec4_unorm:     return MTL::VertexFormatUChar4Normalized;
        case F::format_8_vec2_snorm:     return MTL::VertexFormatChar2Normalized;
        case F::format_8_vec4_snorm:     return MTL::VertexFormatChar4Normalized;
        case F::format_16_scalar_uint:   return MTL::VertexFormatUShort;
        case F::format_16_vec2_uint:     return MTL::VertexFormatUShort2;
        case F::format_16_vec3_uint:     return MTL::VertexFormatUShort3;
        case F::format_16_vec4_uint:     return MTL::VertexFormatUShort4;
        case F::format_16_scalar_sint:   return MTL::VertexFormatShort;
        case F::format_16_vec2_sint:     return MTL::VertexFormatShort2;
        case F::format_16_vec3_sint:     return MTL::VertexFormatShort3;
        case F::format_16_vec4_sint:     return MTL::VertexFormatShort4;
        case F::format_16_scalar_float:  return MTL::VertexFormatHalf;
        case F::format_16_vec2_float:    return MTL::VertexFormatHalf2;
        case F::format_16_vec4_float:    return MTL::VertexFormatHalf4;
        case F::format_16_scalar_unorm:  return MTL::VertexFormatUShortNormalized;
        case F::format_16_vec2_unorm:    return MTL::VertexFormatUShort2Normalized;
        case F::format_16_vec4_unorm:    return MTL::VertexFormatUShort4Normalized;
        case F::format_16_scalar_snorm:  return MTL::VertexFormatShortNormalized;
        case F::format_16_vec2_snorm:    return MTL::VertexFormatShort2Normalized;
        case F::format_16_vec4_snorm:    return MTL::VertexFormatShort4Normalized;
        case F::format_32_scalar_float:  return MTL::VertexFormatFloat;
        case F::format_32_vec2_float:    return MTL::VertexFormatFloat2;
        case F::format_32_vec3_float:    return MTL::VertexFormatFloat3;
        case F::format_32_vec4_float:    return MTL::VertexFormatFloat4;
        case F::format_32_scalar_sint:   return MTL::VertexFormatInt;
        case F::format_32_vec2_sint:     return MTL::VertexFormatInt2;
        case F::format_32_vec3_sint:     return MTL::VertexFormatInt3;
        case F::format_32_vec4_sint:     return MTL::VertexFormatInt4;
        case F::format_32_scalar_uint:   return MTL::VertexFormatUInt;
        case F::format_32_vec2_uint:     return MTL::VertexFormatUInt2;
        case F::format_32_vec3_uint:     return MTL::VertexFormatUInt3;
        case F::format_32_vec4_uint:     return MTL::VertexFormatUInt4;
        case F::format_packed1010102_vec4_snorm: return MTL::VertexFormatInt1010102Normalized;
        case F::format_packed1010102_vec4_unorm: return MTL::VertexFormatUInt1010102Normalized;
        default:
            ERHE_FATAL("Unsupported vertex format %u", static_cast<unsigned int>(format));
    }
}

auto to_mtl_winding(const Front_face_direction direction) -> MTL::Winding
{
    switch (direction) {
        case Front_face_direction::ccw: return MTL::WindingCounterClockwise;
        case Front_face_direction::cw:  return MTL::WindingClockwise;
        default:                        return MTL::WindingCounterClockwise;
    }
}

auto to_mtl_cull_mode(const bool face_cull_enable, const Cull_face_mode mode) -> MTL::CullMode
{
    if (!face_cull_enable) {
        return MTL::CullModeNone;
    }
    switch (mode) {
        case Cull_face_mode::front:          return MTL::CullModeFront;
        case Cull_face_mode::back:           return MTL::CullModeBack;
        case Cull_face_mode::front_and_back: return MTL::CullModeNone;
        default:                             return MTL::CullModeNone;
    }
}

auto to_mtl_stencil_operation(const Stencil_op op) -> MTL::StencilOperation
{
    switch (op) {
        case Stencil_op::zero:      return MTL::StencilOperationZero;
        case Stencil_op::keep:      return MTL::StencilOperationKeep;
        case Stencil_op::replace:   return MTL::StencilOperationReplace;
        case Stencil_op::incr:      return MTL::StencilOperationIncrementClamp;
        case Stencil_op::incr_wrap: return MTL::StencilOperationIncrementWrap;
        case Stencil_op::decr:      return MTL::StencilOperationDecrementClamp;
        case Stencil_op::decr_wrap: return MTL::StencilOperationDecrementWrap;
        case Stencil_op::invert:    return MTL::StencilOperationInvert;
        default:                    return MTL::StencilOperationKeep;
    }
}

auto to_mtl_compare_function(const Compare_operation op) -> MTL::CompareFunction
{
    switch (op) {
        case Compare_operation::never:            return MTL::CompareFunctionNever;
        case Compare_operation::less:             return MTL::CompareFunctionLess;
        case Compare_operation::equal:            return MTL::CompareFunctionEqual;
        case Compare_operation::less_or_equal:    return MTL::CompareFunctionLessEqual;
        case Compare_operation::greater:          return MTL::CompareFunctionGreater;
        case Compare_operation::not_equal:        return MTL::CompareFunctionNotEqual;
        case Compare_operation::greater_or_equal: return MTL::CompareFunctionGreaterEqual;
        case Compare_operation::always:           return MTL::CompareFunctionAlways;
        default:                                  return MTL::CompareFunctionAlways;
    }
}

auto to_mtl_blend_factor(const Blending_factor factor) -> MTL::BlendFactor
{
    switch (factor) {
        case Blending_factor::zero:                     return MTL::BlendFactorZero;
        case Blending_factor::one:                      return MTL::BlendFactorOne;
        case Blending_factor::src_color:                return MTL::BlendFactorSourceColor;
        case Blending_factor::one_minus_src_color:      return MTL::BlendFactorOneMinusSourceColor;
        case Blending_factor::dst_color:                return MTL::BlendFactorDestinationColor;
        case Blending_factor::one_minus_dst_color:      return MTL::BlendFactorOneMinusDestinationColor;
        case Blending_factor::src_alpha:                return MTL::BlendFactorSourceAlpha;
        case Blending_factor::one_minus_src_alpha:      return MTL::BlendFactorOneMinusSourceAlpha;
        case Blending_factor::dst_alpha:                return MTL::BlendFactorDestinationAlpha;
        case Blending_factor::one_minus_dst_alpha:      return MTL::BlendFactorOneMinusDestinationAlpha;
        case Blending_factor::constant_color:           return MTL::BlendFactorBlendColor;
        case Blending_factor::one_minus_constant_color: return MTL::BlendFactorOneMinusBlendColor;
        case Blending_factor::constant_alpha:           return MTL::BlendFactorBlendAlpha;
        case Blending_factor::one_minus_constant_alpha: return MTL::BlendFactorOneMinusBlendAlpha;
        case Blending_factor::src_alpha_saturate:       return MTL::BlendFactorSourceAlphaSaturated;
        case Blending_factor::src1_color:               return MTL::BlendFactorSource1Color;
        case Blending_factor::one_minus_src1_color:     return MTL::BlendFactorOneMinusSource1Color;
        case Blending_factor::src1_alpha:               return MTL::BlendFactorSource1Alpha;
        case Blending_factor::one_minus_src1_alpha:     return MTL::BlendFactorOneMinusSource1Alpha;
        default:                                        return MTL::BlendFactorOne;
    }
}

auto to_mtl_blend_operation(const Blend_equation_mode mode) -> MTL::BlendOperation
{
    switch (mode) {
        case Blend_equation_mode::func_add:              return MTL::BlendOperationAdd;
        case Blend_equation_mode::func_subtract:         return MTL::BlendOperationSubtract;
        case Blend_equation_mode::func_reverse_subtract: return MTL::BlendOperationReverseSubtract;
        case Blend_equation_mode::min_:                  return MTL::BlendOperationMin;
        case Blend_equation_mode::max_:                  return MTL::BlendOperationMax;
        default:                                         return MTL::BlendOperationAdd;
    }
}

// Vertex buffer indices are offset to avoid collision with SPIRV-Cross
// assigned argument buffer indices (uniform/storage buffers).
static constexpr NS::UInteger vertex_buffer_index_offset = 16;

} // anonymous namespace

Render_command_encoder_impl::Render_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Render_command_encoder_impl::~Render_command_encoder_impl() noexcept = default;

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    set_render_pipeline_state(pipeline, nullptr);
}

void Render_command_encoder_impl::set_render_pipeline_state(
    const Render_pipeline_state& pipeline,
    const Shader_stages* const   override_shader_stages
)
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        // No active encoder (e.g. tick called from SDL event before begin_frame)
        return;
    }

    const Render_pipeline_data& data = pipeline.data;
    const Shader_stages* shader_stages = (override_shader_stages != nullptr) ? override_shader_stages : data.shader_stages;
    if (shader_stages == nullptr) {
        return; // No shader stages, skip
    }

    if (!shader_stages->is_valid()) {
        return; // Invalid shader (e.g. uses geometry shaders), skip
    }

    const Shader_stages_impl& stages_impl = shader_stages->get_impl();
    MTL::Function* vertex_function   = stages_impl.get_vertex_function();
    MTL::Function* fragment_function = stages_impl.get_fragment_function();

    if (vertex_function == nullptr) {
        return; // No vertex function, skip
    }

    Device_impl& device_impl = m_device.get_impl();
    MTL::Device* mtl_device = device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vertex_function);
    desc->setFragmentFunction(fragment_function);

    // Color attachment pixel format - must match the render pass
    // When no color attachment is set (e.g. shadow depth-only pass),
    // the pixel format must be MTLPixelFormatInvalid.
    {
        unsigned long color_fmt = Render_pass_impl::get_active_color_pixel_format(0);
        desc->colorAttachments()->object(0)->setPixelFormat(static_cast<MTL::PixelFormat>(color_fmt));
    }

    // Sample count
    {
        unsigned long sample_count = Render_pass_impl::get_active_sample_count();
        if (sample_count > 1) {
            desc->setSampleCount(static_cast<NS::UInteger>(sample_count));
        }
    }

    // Depth/stencil pixel formats
    {
        unsigned long depth_fmt = Render_pass_impl::get_active_depth_pixel_format();
        if (depth_fmt != 0) {
            desc->setDepthAttachmentPixelFormat(static_cast<MTL::PixelFormat>(depth_fmt));
        }
        unsigned long stencil_fmt = Render_pass_impl::get_active_stencil_pixel_format();
        if (stencil_fmt != 0) {
            desc->setStencilAttachmentPixelFormat(static_cast<MTL::PixelFormat>(stencil_fmt));
        }
    }

    // Vertex descriptor from vertex input state
    if (data.vertex_input != nullptr) {
        const Vertex_input_state_data& vis_data = data.vertex_input->get_data();
        MTL::VertexDescriptor* vertex_desc = MTL::VertexDescriptor::alloc()->init();

        for (const Vertex_input_attribute& attr : vis_data.attributes) {
            MTL::VertexAttributeDescriptor* attr_desc = vertex_desc->attributes()->object(
                static_cast<NS::UInteger>(attr.layout_location)
            );
            attr_desc->setFormat(to_mtl_vertex_format(attr.format));
            attr_desc->setOffset(static_cast<NS::UInteger>(attr.offset));
            attr_desc->setBufferIndex(static_cast<NS::UInteger>(attr.binding) + vertex_buffer_index_offset);
        }

        for (const Vertex_input_binding& binding : vis_data.bindings) {
            MTL::VertexBufferLayoutDescriptor* layout_desc = vertex_desc->layouts()->object(
                static_cast<NS::UInteger>(binding.binding) + vertex_buffer_index_offset
            );
            layout_desc->setStride(static_cast<NS::UInteger>(binding.stride));
            if (binding.divisor == 0) {
                layout_desc->setStepFunction(MTL::VertexStepFunctionPerVertex);
            } else {
                layout_desc->setStepFunction(MTL::VertexStepFunctionPerInstance);
                layout_desc->setStepRate(static_cast<NS::UInteger>(binding.divisor));
            }
        }

        desc->setVertexDescriptor(vertex_desc);
        vertex_desc->release();
    }

    // Color blend state
    {
        MTL::RenderPipelineColorAttachmentDescriptor* color_att = desc->colorAttachments()->object(0);
        if (data.color_blend.enabled) {
            color_att->setBlendingEnabled(true);
            color_att->setSourceRGBBlendFactor(to_mtl_blend_factor(data.color_blend.rgb.source_factor));
            color_att->setDestinationRGBBlendFactor(to_mtl_blend_factor(data.color_blend.rgb.destination_factor));
            color_att->setRgbBlendOperation(to_mtl_blend_operation(data.color_blend.rgb.equation_mode));
            color_att->setSourceAlphaBlendFactor(to_mtl_blend_factor(data.color_blend.alpha.source_factor));
            color_att->setDestinationAlphaBlendFactor(to_mtl_blend_factor(data.color_blend.alpha.destination_factor));
            color_att->setAlphaBlendOperation(to_mtl_blend_operation(data.color_blend.alpha.equation_mode));
        }

        MTL::ColorWriteMask mask = MTL::ColorWriteMaskNone;
        if (data.color_blend.write_mask.red)   mask |= MTL::ColorWriteMaskRed;
        if (data.color_blend.write_mask.green) mask |= MTL::ColorWriteMaskGreen;
        if (data.color_blend.write_mask.blue)  mask |= MTL::ColorWriteMaskBlue;
        if (data.color_blend.write_mask.alpha) mask |= MTL::ColorWriteMaskAlpha;
        color_att->setWriteMask(mask);
    }

    NS::Error* error = nullptr;
    MTL::RenderPipelineState* pso = mtl_device->newRenderPipelineState(desc, &error);
    desc->release();

    if (pso == nullptr) {
        const char* error_str = (error != nullptr) ? error->localizedDescription()->utf8String() : "unknown error";
        std::string error_msg = fmt::format("Pipeline state creation failed for '{}': {}", shader_stages->name(), error_str);
        m_device.shader_error(error_msg, "");
        return;
    }

    encoder->setRenderPipelineState(pso);
    pso->release();

    // Bind push constant (ERHE_DRAW_ID = 0) for vertex stage.
    // Push constant is always at fixed index 15 (metal_push_constant_index).
    // Only vertex/compute shaders declare ERHE_DRAW_ID; fragment shaders do not.
    {
        static constexpr NS::UInteger metal_push_constant_index = 15;
        int32_t draw_id = 0;
        encoder->setVertexBytes(&draw_id, sizeof(draw_id), metal_push_constant_index);
    }

    // Depth stencil state
    {
        const bool has_depth   = (Render_pass_impl::get_active_depth_pixel_format()   != 0);
        const bool has_stencil = (Render_pass_impl::get_active_stencil_pixel_format() != 0);
        MTL::DepthStencilDescriptor* ds_desc = MTL::DepthStencilDescriptor::alloc()->init();

        // Depth
        if (has_depth && data.depth_stencil.depth_write_enable) {
            ds_desc->setDepthWriteEnabled(true);
        }
        if (has_depth && data.depth_stencil.depth_test_enable) {
            ds_desc->setDepthCompareFunction(to_mtl_compare_function(data.depth_stencil.depth_compare_op));
        } else {
            ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
        }

        // Stencil
        if (has_stencil && data.depth_stencil.stencil_test_enable) {
            auto configure_stencil = [](MTL::StencilDescriptor* stencil_desc, const Stencil_op_state& state) {
                stencil_desc->setStencilCompareFunction(to_mtl_compare_function(state.function));
                stencil_desc->setStencilFailureOperation(to_mtl_stencil_operation(state.stencil_fail_op));
                stencil_desc->setDepthFailureOperation(to_mtl_stencil_operation(state.z_fail_op));
                stencil_desc->setDepthStencilPassOperation(to_mtl_stencil_operation(state.z_pass_op));
                stencil_desc->setReadMask(state.test_mask);
                stencil_desc->setWriteMask(state.write_mask);
            };
            configure_stencil(ds_desc->frontFaceStencil(), data.depth_stencil.stencil_front);
            configure_stencil(ds_desc->backFaceStencil(),  data.depth_stencil.stencil_back);
        }

        MTL::DepthStencilState* ds_state = mtl_device->newDepthStencilState(ds_desc);
        ds_desc->release();
        if (ds_state != nullptr) {
            encoder->setDepthStencilState(ds_state);
            ds_state->release();
        }

        // Stencil reference value (set on encoder, not descriptor)
        if (has_stencil && data.depth_stencil.stencil_test_enable) {
            encoder->setStencilReferenceValues(
                data.depth_stencil.stencil_front.reference,
                data.depth_stencil.stencil_back.reference
            );
        }
    }

    // Rasterization state
    encoder->setFrontFacingWinding(to_mtl_winding(data.rasterization.front_face_direction));
    encoder->setCullMode(to_mtl_cull_mode(data.rasterization.face_cull_enable, data.rasterization.cull_face_mode));

    // Samplers are now provided via the texture argument buffer, not individual slots
}

void Render_command_encoder_impl::set_viewport_rect(const int x, const int y, const int width, const int height)
{
    m_viewport_x      = x;
    m_viewport_y      = y;
    m_viewport_width  = width;
    m_viewport_height = height;
    apply_viewport();
}

void Render_command_encoder_impl::set_viewport_depth_range(const float min_depth, const float max_depth)
{
    m_viewport_znear = static_cast<double>(min_depth);
    m_viewport_zfar  = static_cast<double>(max_depth);
    apply_viewport();
}

void Render_command_encoder_impl::apply_viewport()
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }
    MTL::Viewport viewport{
        .originX = static_cast<double>(m_viewport_x),
        .originY = static_cast<double>(m_viewport_y),
        .width   = static_cast<double>(m_viewport_width),
        .height  = static_cast<double>(m_viewport_height),
        .znear   = m_viewport_znear,
        .zfar    = m_viewport_zfar
    };
    encoder->setViewport(viewport);
}

void Render_command_encoder_impl::set_scissor_rect(const int x, const int y, const int width, const int height)
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }
    MTL::ScissorRect rect{
        .x      = static_cast<NS::UInteger>(std::max(0, x)),
        .y      = static_cast<NS::UInteger>(std::max(0, y)),
        .width  = static_cast<NS::UInteger>(std::max(0, width)),
        .height = static_cast<NS::UInteger>(std::max(0, height))
    };
    encoder->setScissorRect(rect);
}

void Render_command_encoder_impl::set_index_buffer(const Buffer* const buffer)
{
    if (buffer != nullptr) {
        m_index_buffer = buffer->get_impl().get_mtl_buffer();
    } else {
        m_index_buffer = nullptr;
    }
}

void Render_command_encoder_impl::set_vertex_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t index
)
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if ((encoder == nullptr) || (buffer == nullptr)) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer != nullptr) {
        encoder->setVertexBuffer(mtl_buffer, static_cast<NS::UInteger>(offset), static_cast<NS::UInteger>(index) + vertex_buffer_index_offset);
    }
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type primitive_type,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count,
    const std::uintptr_t instance_count
) const
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }
    encoder->drawPrimitives(
        to_mtl_primitive_type(primitive_type),
        static_cast<NS::UInteger>(vertex_start),
        static_cast<NS::UInteger>(vertex_count),
        static_cast<NS::UInteger>(instance_count)
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
    const Primitive_type           primitive_type,
    const std::uintptr_t           index_count,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           index_buffer_offset,
    const std::uintptr_t           instance_count
) const
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if ((encoder == nullptr) || (m_index_buffer == nullptr)) {
        return;
    }
    encoder->drawIndexedPrimitives(
        to_mtl_primitive_type(primitive_type),
        static_cast<NS::UInteger>(index_count),
        to_mtl_index_type(index_type),
        m_index_buffer,
        static_cast<NS::UInteger>(index_buffer_offset),
        static_cast<NS::UInteger>(instance_count)
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
    const Primitive_type           primitive_type,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           indirect_offset,
    const std::uintptr_t           drawcount,
    const std::uintptr_t           stride
) const
{
    MTL::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    MTL::Buffer* indirect_buffer = get_draw_indirect_buffer();
    if (encoder == nullptr) {
        ERHE_FATAL("multi_draw_indexed_primitives_indirect: no active encoder");
    }
    if (m_index_buffer == nullptr) {
        ERHE_FATAL("multi_draw_indexed_primitives_indirect: no index buffer");
    }
    if (indirect_buffer == nullptr) {
        ERHE_FATAL("multi_draw_indexed_primitives_indirect: no indirect buffer");
    }

    const MTL::PrimitiveType mtl_primitive_type = to_mtl_primitive_type(primitive_type);
    const MTL::IndexType     mtl_index_type     = to_mtl_index_type(index_type);
    const NS::UInteger       actual_stride      = (stride != 0)
        ? static_cast<NS::UInteger>(stride)
        : static_cast<NS::UInteger>(sizeof(Draw_indexed_primitives_indirect_command));

    // Update ERHE_DRAW_ID per draw call at fixed push constant index
    static constexpr NS::UInteger metal_push_constant_index = 15;

    for (std::uintptr_t i = 0; i < drawcount; ++i) {
        int32_t draw_id = static_cast<int32_t>(i);
        encoder->setVertexBytes(&draw_id, sizeof(draw_id), metal_push_constant_index);

        const NS::UInteger offset = static_cast<NS::UInteger>(indirect_offset) + i * actual_stride;
        encoder->drawIndexedPrimitives(
            mtl_primitive_type,
            mtl_index_type,
            m_index_buffer,
            0, // index buffer offset is in the indirect command
            indirect_buffer,
            offset
        );
    }
}

void Render_command_encoder_impl::dump_state(const char* label) const
{
    static_cast<void>(label);
}

} // namespace erhe::graphics
