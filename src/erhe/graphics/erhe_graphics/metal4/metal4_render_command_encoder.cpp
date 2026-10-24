#include "erhe_graphics/metal4/metal4_render_command_encoder.hpp"
#include "erhe_graphics/scoped_transient_object_pool.hpp"
#include "erhe_graphics/metal4/metal4_pipeline_state_cache.hpp"
#include "erhe_graphics/metal4/metal4_render_pass.hpp"
#include "erhe_graphics/metal4/metal4_command_buffer.hpp"
#include "erhe_graphics/metal4/metal4_submission.hpp"
#include "erhe_graphics/metal4/metal4_buffer.hpp"
#include "erhe_graphics/metal4/metal4_sampler.hpp"
#include "erhe_graphics/metal4/metal4_shader_stages.hpp"
#include "erhe_graphics/metal4/metal4_texture.hpp"
#include "erhe_graphics/metal4/metal4_helpers.hpp"
#include "erhe_graphics/metal4/metal4_device.hpp"
#include "erhe_graphics/metal4/metal4_indirect_count_draws.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/metal4/metal4_bind_group_layout.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/metal4/metal4_render_pipeline.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
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

} // anonymous namespace

Render_command_encoder_impl::Render_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
}

Render_command_encoder_impl::~Render_command_encoder_impl() noexcept = default;

void Render_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    m_bind_group_layout = bind_group_layout;
}

void Render_command_encoder_impl::set_sampled_image(uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    MTL4::ArgumentTable* argument_table = Render_pass_impl::get_active_argument_table();
    if (argument_table == nullptr) {
        return;
    }
    ERHE_VERIFY(m_bind_group_layout != nullptr);

    // Dedicated samplers of vertex functions are direct [[texture(N)]] /
    // [[sampler(M)]] bindings, those of fragment functions entries of the
    // fragment argument buffer (see metal4_shader_stages_prototype.cpp).
    //
    // The texture slot is the GLSL binding number, computed the same way
    // SPIRV-Cross sees it: the user-facing binding_point plus the bind group
    // layout's sampler binding offset (= one past the highest buffer binding).
    // The shader emitter's ERHE_SAMPLER_BINDING_OFFSET macro applies the same
    // offset.
    //
    // The sampler slot is *not* that number: Metal caps [[sampler(M)]] at
    // M <= 15, so dedicated samplers are allocated a separate compact index by
    // the bind group layout, which the shader-compile path pins to match.
    const uint32_t glsl_binding = binding_point + m_bind_group_layout->get_sampler_binding_offset();
    const uint32_t sampler_slot = m_bind_group_layout->get_impl().get_metal_sampler_slot(glsl_binding);

    MTL::Texture*      mtl_texture = texture.get_impl().get_mtl_texture();
    MTL::SamplerState* mtl_sampler = sampler.get_impl().get_mtl_sampler();
    if (mtl_texture == nullptr || mtl_sampler == nullptr) {
        return;
    }
    // Vertex functions read their dedicated samplers from the argument
    // table at [[texture(N)]] / [[sampler(M)]]; fragment functions from the
    // layout's fragment argument buffer, encoded before the next draw
    // (Render_pass_impl::flush_fragment_arguments()).
    const uint32_t stage_flags = m_bind_group_layout->get_impl().get_sampler_stage_flags(glsl_binding);
    ERHE_VERIFY((stage_flags & (Shader_stage_flags::vertex | Shader_stage_flags::fragment)) != 0);
    if ((stage_flags & Shader_stage_flags::vertex) != 0) {
        argument_table->setTexture     (mtl_texture->gpuResourceID(), static_cast<NS::UInteger>(glsl_binding));
        argument_table->setSamplerState(mtl_sampler->gpuResourceID(), static_cast<NS::UInteger>(sampler_slot));
    }
    if ((stage_flags & Shader_stage_flags::fragment) != 0) {
        const std::optional<std::size_t> argument_index = m_bind_group_layout->get_impl().get_fragment_argument_index(glsl_binding);
        ERHE_VERIFY(argument_index.has_value());
        Render_pass_impl::set_fragment_argument(m_bind_group_layout->get_impl(), argument_index.value(), mtl_texture, mtl_sampler);
    }
}

void Render_command_encoder_impl::set_render_pipeline(const Render_pipeline& pipeline)
{
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }

    const Render_pipeline_impl& impl = pipeline.get_impl();
    if (!impl.is_valid()) {
        return;
    }

    encoder->setRenderPipelineState(impl.get_pso());
    m_pipeline_supports_indirect_count_draws =
        (pipeline.get_create_info().base.indirect_count_draws == Indirect_count_draw_support::enabled);

    if (impl.get_depth_stencil() != nullptr) {
        encoder->setDepthStencilState(impl.get_depth_stencil());
    }

    // Rasterization state from create info
    const Render_pipeline_create_info&      create_info = pipeline.get_create_info();
    const Base_render_pipeline_create_info& base        = create_info.base;
    encoder->setFrontFacingWinding(to_mtl_winding(base.rasterization.front_face_direction));
    encoder->setCullMode(to_mtl_cull_mode(base.rasterization.face_cull_enable, base.rasterization.cull_face_mode));
    encoder->setTriangleFillMode(to_mtl_triangle_fill_mode(base.rasterization.polygon_mode));
    encoder->setDepthClipMode(to_mtl_depth_clip_mode(base.rasterization.depth_clamp_enable));

    // Blend constant color (used when blend factors reference constant_color/alpha)
    const Color_blend_state& color_blend = (base.color_blend != nullptr) ? *base.color_blend : erhe::graphics::Color_blend_state::color_blend_disabled;
    encoder->setBlendColor(
        color_blend.constant[0],
        color_blend.constant[1],
        color_blend.constant[2],
        color_blend.constant[3]
    );

    // Stencil reference values
    if (base.depth_stencil.stencil_test_enable) {
        encoder->setStencilReferenceValues(
            base.depth_stencil.stencil_front.reference,
            base.depth_stencil.stencil_back.reference
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
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    Command_buffer_impl* const  cb_impl = Render_pass_impl::get_active_command_buffer_impl();
    if ((encoder == nullptr) || (cb_impl == nullptr)) {
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

    // One pipeline state per distinct key for the device's lifetime
    // (Pipeline_state_cache): a bind every frame creates nothing.
    std::array<unsigned long, Pipeline_state_cache::max_color_attachments> color_pixel_formats{};
    for (unsigned int i = 0; i < Pipeline_state_cache::max_color_attachments; ++i) {
        color_pixel_formats[i] = Render_pass_impl::get_active_color_pixel_format(i);
    }
    Pipeline_state_cache& cache = m_device.get_impl().get_pipeline_state_cache();
    MTL::RenderPipelineState* const pso = cache.get_render_pipeline(
        *shader_stages,
        data,
        color_pixel_formats,
        Render_pass_impl::get_active_sample_count()
    );
    if (pso == nullptr) {
        return; // No vertex function, or the pipeline failed (reported)
    }

    encoder->setRenderPipelineState(pso);
    // This path never sets supportIndirectCommandBuffers: only a
    // Render_pipeline created with Indirect_count_draw_support::enabled
    // draws prepared count draws.
    m_pipeline_supports_indirect_count_draws = false;

    // Depth stencil state
    {
        const unsigned long depth_format   = Render_pass_impl::get_active_depth_pixel_format();
        const unsigned long stencil_format = Render_pass_impl::get_active_stencil_pixel_format();
        const bool          has_stencil    = (stencil_format != 0);
        MTL::DepthStencilState* const ds_state = cache.get_depth_stencil_state(data.depth_stencil, depth_format, stencil_format);
        if (ds_state != nullptr) {
            encoder->setDepthStencilState(ds_state);
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
    encoder->setTriangleFillMode(to_mtl_triangle_fill_mode(data.rasterization.polygon_mode));
    encoder->setDepthClipMode(to_mtl_depth_clip_mode(data.rasterization.depth_clamp_enable));

    // Blend constant color (used when blend factors reference constant_color/alpha)
    encoder->setBlendColor(
        data.color_blend.constant[0],
        data.color_blend.constant[1],
        data.color_blend.constant[2],
        data.color_blend.constant[3]
    );

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
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
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
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
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

void Render_command_encoder_impl::set_depth_bias(const float constant_factor, const float slope_factor, const float clamp)
{
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }
    // Metal applies depth bias as render-encoder state (no pipeline flag):
    // setDepthBias:slopeScale:clamp:.
    encoder->setDepthBias(constant_factor, slope_factor, clamp);
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
    MTL4::ArgumentTable* argument_table = Render_pass_impl::get_active_argument_table();
    if ((argument_table == nullptr) || (buffer == nullptr)) {
        return;
    }
    MTL::Buffer* mtl_buffer = buffer->get_impl().get_mtl_buffer();
    if (mtl_buffer != nullptr) {
        argument_table->setAddress(
            mtl_buffer->gpuAddress() + static_cast<uint64_t>(offset),
            static_cast<NS::UInteger>(index) + vertex_buffer_index_offset
        );
    }
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type primitive_type,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count,
    const std::uintptr_t instance_count
) const
{
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        return;
    }
    Render_pass_impl::flush_fragment_arguments();
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
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
    if ((encoder == nullptr) || (m_index_buffer == nullptr)) {
        return;
    }
    Render_pass_impl::flush_fragment_arguments();
    encoder->drawIndexedPrimitives(
        to_mtl_primitive_type(primitive_type),
        static_cast<NS::UInteger>(index_count),
        to_mtl_index_type(index_type),
        m_index_buffer->gpuAddress() + static_cast<uint64_t>(index_buffer_offset),
        m_index_buffer->length() - static_cast<NS::UInteger>(index_buffer_offset),
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
    MTL4::RenderCommandEncoder* encoder = Render_pass_impl::get_active_mtl_encoder();
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

    const uint64_t     index_address    = m_index_buffer->gpuAddress();
    const NS::UInteger index_length     = m_index_buffer->length();
    const uint64_t     indirect_address = indirect_buffer->gpuAddress();

    Render_pass_impl::flush_fragment_arguments();
    // One indirect draw per command; each names its record by its
    // first_instance (Record_indexing::first_instance), so the argument
    // table does not change between the draws.
    for (std::uintptr_t i = 0; i < drawcount; ++i) {
        const uint64_t offset = static_cast<uint64_t>(indirect_offset) + (i * actual_stride);
        encoder->drawIndexedPrimitives(
            mtl_primitive_type,
            mtl_index_type,
            index_address, // index buffer offset is in the indirect command
            index_length,
            indirect_address + offset
        );
    }
}

void Render_command_encoder_impl::multi_draw_indexed_primitives_indirect_count(
    const Primitive_type           primitive_type,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           indirect_offset,
    const Buffer&                  count_buffer,
    const std::uintptr_t           count_offset,
    const std::uintptr_t           max_draw_count,
    const std::uintptr_t           stride
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_type);
    static_cast<void>(indirect_offset);
    static_cast<void>(count_buffer);
    static_cast<void>(count_offset);
    static_cast<void>(max_draw_count);
    static_cast<void>(stride);
    // Metal cannot draw with a GPU-read count from the command buffer
    // directly: its count draws are prepared into an indirect command buffer
    // in a compute pass (Compute_command_encoder::
    // prepare_indexed_indirect_count_draws()) and drawn by the
    // Indirect_count_draw_set overload.
    ERHE_FATAL("multi_draw_indexed_primitives_indirect_count: Metal draws GPU-counted multi-draws only from a prepared Indirect_count_draw_set");
}

void Render_command_encoder_impl::execute_indirect_count_draws(
    const Indirect_count_draws& draws,
    const std::size_t           slot,
    const uint32_t              job_index
)
{
    MTL4::RenderCommandEncoder* const encoder = Render_pass_impl::get_active_mtl_encoder();
    if (encoder == nullptr) {
        ERHE_FATAL("multi_draw_indexed_primitives_indirect_count: no active encoder");
    }
    const Indirect_count_draws::Slot&                         slot_data = draws.get_slot(slot);
    const Indirect_count_draws_impl::Slot_resources&          resources = draws.get_impl().get_slot_resources(slot);
    // The ICB and range buffer hold the slot's most recent prepare, which the
    // set is (Render_command_encoder verified the sequence).
    ERHE_VERIFY(resources.encoded_sequence == slot_data.sequence);
    ERHE_VERIFY((resources.icb != nullptr) && (resources.range_buffer != nullptr));
    const Indirect_count_draw_job&     job          = slot_data.jobs[job_index];
    const Indirect_count_index_buffer& index_buffer = slot_data.index_buffers[job.index_buffer];
    MTL::Buffer* const mtl_index_buffer = index_buffer.buffer->get_impl().get_mtl_buffer();
    ERHE_VERIFY(mtl_index_buffer != nullptr);
    // The commands inherit the bound pipeline, which must have been created
    // with supportIndirectCommandBuffers (Indirect_count_draw_support).
    ERHE_VERIFY(m_pipeline_supports_indirect_count_draws);

    // The commands reach the index buffer by address only, the ICB is not
    // bound: both are resident through the device's residency set. The
    // commands inherit the encoder's argument table bindings; each names its
    // record by its first_instance.
    static_cast<void>(mtl_index_buffer);
    Render_pass_impl::flush_fragment_arguments();
    encoder->executeCommandsInBuffer(
        resources.icb,
        resources.range_buffer->gpuAddress() + (static_cast<uint64_t>(job_index) * sizeof(MTL::IndirectCommandBufferExecutionRange))
    );
}

void Render_command_encoder_impl::dump_state(const char* label) const
{
    static_cast<void>(label);
}

} // namespace erhe::graphics
