#include "erhe_graphics/render_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_render_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#endif

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Device& device, Render_pass& render_pass)
    : m_impl{std::make_unique<Render_command_encoder_impl>(device, render_pass)}
{
}
Render_command_encoder::~Render_command_encoder() noexcept = default;
void Render_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset,
    std::uintptr_t length, std::uintptr_t index)
{
    m_impl->set_buffer(buffer_target, buffer, offset, length, index);
}
void Render_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    m_impl->set_buffer(buffer_target, buffer);
}
void Render_command_encoder::set_render_pipeline_state(const Render_pipeline_state& pipeline) const
{
    m_impl->set_render_pipeline_state(pipeline);
}
void Render_command_encoder::set_render_pipeline_state(const Render_pipeline_state& pipeline,
    const Shader_stages* override_shader_stages) const
{
    m_impl->set_render_pipeline_state(pipeline, override_shader_stages);
}
void Render_command_encoder::set_viewport_rect(int x, int y, int width, int height) const
{
    m_impl->set_viewport_rect(x, y, width, height);
}
void Render_command_encoder::set_viewport_depth_range(float min_depth, float max_depth) const
{
    m_impl->set_viewport_depth_range(min_depth, max_depth);
}
void Render_command_encoder::set_scissor_rect(int x, int y, int width, int height) const
{
    m_impl->set_scissor_rect(x, y, width, height);
}
void Render_command_encoder::set_index_buffer(const Buffer* buffer) const
{
    m_impl->set_index_buffer(buffer);
}
void Render_command_encoder::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index) const
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

} // namespace erhe::graphics