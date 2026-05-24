#include "erhe_graphics/metal/metal_render_pipeline.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_helpers.hpp"
#include "erhe_graphics/metal/metal_render_command_encoder.hpp"
#include "erhe_graphics/metal/metal_shader_stages.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <Metal/Metal.hpp>

namespace erhe::graphics {

Render_pipeline_impl::Render_pipeline_impl(Device& device, const Render_pipeline_create_info& create_info)
    : m_device_impl{device.get_impl()}
{
    const Shader_stages* shader_stages = create_info.shader_stages;
    if (shader_stages == nullptr) {
        return;
    }
    if (!shader_stages->is_valid()) {
        return;
    }

    const Shader_stages_impl& stages_impl = shader_stages->get_impl();
    MTL::Function* vertex_function   = stages_impl.get_vertex_function();
    MTL::Function* fragment_function = stages_impl.get_fragment_function();
    if (vertex_function == nullptr) {
        return;
    }

    MTL::Device* mtl_device = m_device_impl.get_mtl_device();
    ERHE_VERIFY(mtl_device != nullptr);

    const Base_render_pipeline_create_info& base = create_info.base;

    // --- Render pipeline state ---
    MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    desc->setVertexFunction(vertex_function);
    desc->setFragmentFunction(fragment_function);

    // Color attachment pixel formats from create info
    for (unsigned int i = 0; i < create_info.color_attachment_count; ++i) {
        MTL::PixelFormat pixel_format = to_mtl_pixel_format(create_info.color_attachment_formats[i]);
        desc->colorAttachments()->object(static_cast<NS::UInteger>(i))->setPixelFormat(pixel_format);
    }
    // Set remaining to Invalid (depth-only passes have 0 color attachments)
    if (create_info.color_attachment_count == 0) {
        desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatInvalid);
    }

    // Sample count
    if (create_info.sample_count > 1) {
        desc->setSampleCount(static_cast<NS::UInteger>(create_info.sample_count));
    }

    // Multisample coverage/one state (pipeline-descriptor state, applies
    // even when sample_count == 1 - Metal ignores the flags in that case).
    desc->setAlphaToCoverageEnabled(base.multisample.alpha_to_coverage_enable);
    desc->setAlphaToOneEnabled(base.multisample.alpha_to_one_enable);

    // Depth/stencil pixel formats
    if (create_info.depth_attachment_format != erhe::dataformat::Format::format_undefined) {
        desc->setDepthAttachmentPixelFormat(to_mtl_pixel_format(create_info.depth_attachment_format));
    }
    if (create_info.stencil_attachment_format != erhe::dataformat::Format::format_undefined) {
        desc->setStencilAttachmentPixelFormat(to_mtl_pixel_format(create_info.stencil_attachment_format));
    }

    // Vertex descriptor
    if (create_info.vertex_input != nullptr) {
        const Vertex_input_state_data& vis_data = create_info.vertex_input->get_data();
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
    const Color_blend_state& color_blend = (base.color_blend != nullptr) ? *base.color_blend : erhe::graphics::Color_blend_state::color_blend_disabled;
    for (unsigned int i = 0; i < std::max(1u, create_info.color_attachment_count); ++i) {
        MTL::RenderPipelineColorAttachmentDescriptor* color_att = desc->colorAttachments()->object(static_cast<NS::UInteger>(i));
        if (color_blend.enabled) {
            color_att->setBlendingEnabled(true);
            color_att->setSourceRGBBlendFactor(to_mtl_blend_factor(color_blend.rgb.source_factor));
            color_att->setDestinationRGBBlendFactor(to_mtl_blend_factor(color_blend.rgb.destination_factor));
            color_att->setRgbBlendOperation(to_mtl_blend_operation(color_blend.rgb.equation_mode));
            color_att->setSourceAlphaBlendFactor(to_mtl_blend_factor(color_blend.alpha.source_factor));
            color_att->setDestinationAlphaBlendFactor(to_mtl_blend_factor(color_blend.alpha.destination_factor));
            color_att->setAlphaBlendOperation(to_mtl_blend_operation(color_blend.alpha.equation_mode));
        }

        MTL::ColorWriteMask mask = MTL::ColorWriteMaskNone;
        if (color_blend.write_mask.red)   mask |= MTL::ColorWriteMaskRed;
        if (color_blend.write_mask.green) mask |= MTL::ColorWriteMaskGreen;
        if (color_blend.write_mask.blue)  mask |= MTL::ColorWriteMaskBlue;
        if (color_blend.write_mask.alpha) mask |= MTL::ColorWriteMaskAlpha;
        color_att->setWriteMask(mask);
    }

    NS::Error* error = nullptr;
    m_pso = mtl_device->newRenderPipelineState(desc, &error);
    desc->release();

    if (m_pso == nullptr) {
        const char* error_str = (error != nullptr) ? error->localizedDescription()->utf8String() : "unknown error";
        std::string error_msg = fmt::format("Pipeline state creation failed for '{}': {}", shader_stages->name(), error_str);
        device.shader_error(error_msg, "");
        return;
    }

    // --- Depth stencil state ---
    const bool has_depth   = (create_info.depth_attachment_format   != erhe::dataformat::Format::format_undefined);
    const bool has_stencil = (create_info.stencil_attachment_format != erhe::dataformat::Format::format_undefined);

    MTL::DepthStencilDescriptor* ds_desc = MTL::DepthStencilDescriptor::alloc()->init();

    if (has_depth && base.depth_stencil.depth_write_enable) {
        ds_desc->setDepthWriteEnabled(true);
    }
    if (has_depth && base.depth_stencil.depth_test_enable) {
        ds_desc->setDepthCompareFunction(to_mtl_compare_function(base.depth_stencil.depth_compare_op));
    } else {
        ds_desc->setDepthCompareFunction(MTL::CompareFunctionAlways);
    }

    if (has_stencil && base.depth_stencil.stencil_test_enable) {
        auto configure_stencil = [](MTL::StencilDescriptor* stencil_desc, const Stencil_op_state& state) {
            stencil_desc->setStencilCompareFunction(to_mtl_compare_function(state.function));
            stencil_desc->setStencilFailureOperation(to_mtl_stencil_operation(state.stencil_fail_op));
            stencil_desc->setDepthFailureOperation(to_mtl_stencil_operation(state.z_fail_op));
            stencil_desc->setDepthStencilPassOperation(to_mtl_stencil_operation(state.z_pass_op));
            stencil_desc->setReadMask(state.test_mask);
            stencil_desc->setWriteMask(state.write_mask);
        };
        configure_stencil(ds_desc->frontFaceStencil(), base.depth_stencil.stencil_front);
        configure_stencil(ds_desc->backFaceStencil(),  base.depth_stencil.stencil_back);
    }

    m_depth_stencil = mtl_device->newDepthStencilState(ds_desc);
    ds_desc->release();
}

Render_pipeline_impl::~Render_pipeline_impl() noexcept
{
    MTL::RenderPipelineState* pso           = m_pso;
    MTL::DepthStencilState*   depth_stencil = m_depth_stencil;
    m_pso           = nullptr;
    m_depth_stencil = nullptr;
    if ((pso != nullptr) || (depth_stencil != nullptr)) {
        m_device_impl.add_completion_handler(
            [pso, depth_stencil](Device_impl&) {
                if (pso != nullptr) {
                    pso->release();
                }
                if (depth_stencil != nullptr) {
                    depth_stencil->release();
                }
            }
        );
    }
}

auto Render_pipeline_impl::get_pso() const -> MTL::RenderPipelineState*
{
    return m_pso;
}

auto Render_pipeline_impl::get_depth_stencil() const -> MTL::DepthStencilState*
{
    return m_depth_stencil;
}

auto Render_pipeline_impl::is_valid() const -> bool
{
    return m_pso != nullptr;
}

} // namespace erhe::graphics
