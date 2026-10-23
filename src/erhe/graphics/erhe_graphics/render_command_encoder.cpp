#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/indirect_count_draws.hpp"
#include "erhe_verify/verify.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_render_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_render_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_render_command_encoder.hpp"
#endif

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Device& device, Command_buffer& command_buffer)
    : m_impl{device, command_buffer}
{
    static_assert(sizeof(Render_command_encoder_impl) <= 512);
    static_assert(alignof(Render_command_encoder_impl) <= 16);
}

Render_command_encoder::~Render_command_encoder() noexcept = default;
void Render_command_encoder::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    m_impl->set_bind_group_layout(bind_group_layout);
}
void Render_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset,
    std::uintptr_t length, std::uintptr_t index)
{
    m_impl->set_buffer(buffer_target, buffer, offset, length, index);
}
void Render_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    m_impl->set_buffer(buffer_target, buffer);
}
void Render_command_encoder::set_sampled_image(uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    m_impl->set_sampled_image(binding_point, texture, sampler);
}
void Render_command_encoder::set_render_pipeline(const Render_pipeline& pipeline)
{
    m_impl->set_render_pipeline(pipeline);
}
void Render_command_encoder::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    m_impl->set_render_pipeline_state(pipeline);
}
void Render_command_encoder::set_viewport_rect(int x, int y, int width, int height)
{
    m_impl->set_viewport_rect(x, y, width, height);
}
void Render_command_encoder::set_viewport_depth_range(float min_depth, float max_depth)
{
    m_impl->set_viewport_depth_range(min_depth, max_depth);
}
void Render_command_encoder::set_scissor_rect(int x, int y, int width, int height)
{
    m_impl->set_scissor_rect(x, y, width, height);
}
void Render_command_encoder::set_depth_bias(float constant_factor, float slope_factor, float clamp)
{
    m_impl->set_depth_bias(constant_factor, slope_factor, clamp);
}
void Render_command_encoder::set_index_buffer(const Buffer* buffer)
{
    m_impl->set_index_buffer(buffer);
}
void Render_command_encoder::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index)
{
    m_impl->set_vertex_buffer(buffer, offset, index);
}
void Render_command_encoder::draw_primitives(Primitive_type primitive_type, std::uintptr_t vertex_start,
    std::uintptr_t vertex_count, std::uintptr_t instance_count) const
{
    m_impl->draw_primitives(primitive_type, vertex_start, vertex_count, instance_count);
}
void Render_command_encoder::draw_primitives(Primitive_type primitive_type, std::uintptr_t vertex_start,
    std::uintptr_t vertex_count) const
{
    m_impl->draw_primitives(primitive_type, vertex_start, vertex_count);
}
void Render_command_encoder::draw_indexed_primitives(Primitive_type primitive_type, std::uintptr_t index_count,
    erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset, std::uintptr_t instance_count) const
{
    m_impl->draw_indexed_primitives(primitive_type, index_count, index_type, index_buffer_offset, instance_count);
}
void Render_command_encoder::draw_indexed_primitives(Primitive_type primitive_type, std::uintptr_t index_count,
    erhe::dataformat::Format index_type, std::uintptr_t index_buffer_offset) const
{
    m_impl->draw_indexed_primitives(primitive_type, index_count, index_type, index_buffer_offset);
}
void Render_command_encoder::multi_draw_indexed_primitives_indirect(
    Primitive_type           primitive_type,
    erhe::dataformat::Format index_type,
    std::uintptr_t           indirect_offset,
    std::uintptr_t           drawcount,
    std::uintptr_t           stride
) const
{
    m_impl->multi_draw_indexed_primitives_indirect(
        primitive_type,
        index_type,
        indirect_offset,
        drawcount,
        stride
    );
}

void Render_command_encoder::multi_draw_indexed_primitives_indirect_count(
    Primitive_type           primitive_type,
    erhe::dataformat::Format index_type,
    std::uintptr_t           indirect_offset,
    const Buffer&            count_buffer,
    std::uintptr_t           count_offset,
    std::uintptr_t           max_draw_count,
    std::uintptr_t           stride
) const
{
    m_impl->multi_draw_indexed_primitives_indirect_count(
        primitive_type,
        index_type,
        indirect_offset,
        count_buffer,
        count_offset,
        max_draw_count,
        stride
    );
}

void Render_command_encoder::multi_draw_indexed_primitives_indirect_count(const Indirect_count_draw_set& set, const uint32_t job_index)
{
    ERHE_VERIFY(set.draws != nullptr);
    const Indirect_count_draws::Slot& slot = set.draws->get_slot(set.slot);
    // An older set's state was replaced by a later prepare (on any slot).
    ERHE_VERIFY((set.sequence != 0) && (set.sequence == set.draws->get_sequence()) && (slot.sequence == set.sequence));
    ERHE_VERIFY(job_index < slot.jobs.size());
#if defined(ERHE_GRAPHICS_API_METAL)
    m_impl->execute_indirect_count_draws(*set.draws, set.slot, job_index);
#else
    // The native count draw over the job's commands.
    const Indirect_count_draw_job&     job          = slot.jobs[job_index];
    const Indirect_count_index_buffer& index_buffer = slot.index_buffers[job.index_buffer];
    constexpr std::size_t              stride       = sizeof(Draw_indexed_primitives_indirect_command);
    set_index_buffer(index_buffer.buffer);
    set_buffer(Buffer_target::draw_indirect, slot.command_buffer);
    m_impl->multi_draw_indexed_primitives_indirect_count(
        slot.primitive_type,
        index_buffer.index_type,
        slot.command_byte_offset + (static_cast<std::size_t>(job.command_offset) * stride),
        *slot.count_buffer,
        slot.count_byte_offset + (static_cast<std::size_t>(job_index) * slot.count_byte_stride),
        job.max_draw_count,
        stride
    );
#endif
}

void Render_command_encoder::dump_state(const char* label) const
{
    m_impl->dump_state(label);
}

auto Render_command_encoder::get_command_buffer() -> Command_buffer&
{
    return m_impl->get_command_buffer();
}

auto Render_command_encoder::get_impl() -> Render_command_encoder_impl&
{
    return *m_impl;
}

auto Render_command_encoder::get_impl() const -> const Render_command_encoder_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
