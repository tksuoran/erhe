#include "erhe_graphics/null/null_render_command_encoder.hpp"

namespace erhe::graphics {

Render_command_encoder_impl::Render_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Render_command_encoder_impl::~Render_command_encoder_impl() noexcept = default;

void Render_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    static_cast<void>(bind_group_layout);
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    static_cast<void>(pipeline);
}

void Render_command_encoder_impl::set_render_pipeline_state(
    const Render_pipeline_state& pipeline,
    const Shader_stages* const   override_shader_stages
)
{
    static_cast<void>(pipeline);
    static_cast<void>(override_shader_stages);
}

void Render_command_encoder_impl::set_viewport_rect(const int x, const int y, const int width, const int height)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(width);
    static_cast<void>(height);
}

void Render_command_encoder_impl::set_viewport_depth_range(const float min_depth, const float max_depth)
{
    static_cast<void>(min_depth);
    static_cast<void>(max_depth);
}

void Render_command_encoder_impl::set_scissor_rect(const int x, const int y, const int width, const int height)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(width);
    static_cast<void>(height);
}

void Render_command_encoder_impl::set_index_buffer(const Buffer* const buffer)
{
    static_cast<void>(buffer);
}

void Render_command_encoder_impl::set_vertex_buffer(
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t index
)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(index);
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type primitive_type,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count,
    const std::uintptr_t instance_count
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(vertex_start);
    static_cast<void>(vertex_count);
    static_cast<void>(instance_count);
}

void Render_command_encoder_impl::draw_primitives(
    const Primitive_type primitive_type,
    const std::uintptr_t vertex_start,
    const std::uintptr_t vertex_count
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(vertex_start);
    static_cast<void>(vertex_count);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    const Primitive_type           primitive_type,
    const std::uintptr_t           index_count,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           index_buffer_offset,
    const std::uintptr_t           instance_count
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_count);
    static_cast<void>(index_type);
    static_cast<void>(index_buffer_offset);
    static_cast<void>(instance_count);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    const Primitive_type           primitive_type,
    const std::uintptr_t           index_count,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           index_buffer_offset
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_count);
    static_cast<void>(index_type);
    static_cast<void>(index_buffer_offset);
}

void Render_command_encoder_impl::multi_draw_indexed_primitives_indirect(
    const Primitive_type           primitive_type,
    const erhe::dataformat::Format index_type,
    const std::uintptr_t           indirect_offset,
    const std::uintptr_t           drawcount,
    const std::uintptr_t           stride
) const
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_type);
    static_cast<void>(indirect_offset);
    static_cast<void>(drawcount);
    static_cast<void>(stride);
}

} // namespace erhe::graphics
