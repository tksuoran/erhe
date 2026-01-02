#include "erhe_graphics/vulkan/vulkan_render_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_render_pass.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Render_command_encoder_impl::Render_command_encoder_impl(Device& device, Render_pass& render_pass)
    : m_device     {device}
    , m_render_pass{render_pass}
{
    start_render_pass();
}

Render_command_encoder_impl::~Render_command_encoder_impl() noexcept
{
    end_render_pass();
}

void Render_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Render_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    static_cast<void>(pipeline);
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline, const Shader_stages* override_shader_stages)
{
    static_cast<void>(pipeline);
    static_cast<void>(override_shader_stages);
}

void Render_command_encoder_impl::set_viewport_rect(int x, int y, int width, int height)
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

void Render_command_encoder_impl::set_scissor_rect(int x, int y, int width, int height)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(width);
    static_cast<void>(height);
}

void Render_command_encoder_impl::start_render_pass()
{
    m_command_buffer = m_render_pass.get_impl().start_render_pass();
}

void Render_command_encoder_impl::end_render_pass()
{
    m_render_pass.get_impl().end_render_pass();
}

void Render_command_encoder_impl::set_index_buffer(const Buffer* buffer)
{
    static_cast<void>(buffer);
}

void Render_command_encoder_impl::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index)
{
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(index);
}

void Render_command_encoder_impl::draw_primitives(
    Primitive_type primitive_type,
    std::uintptr_t vertex_start,
    std::uintptr_t vertex_count,
    std::uintptr_t instance_count
)
{
    static_cast<void>(primitive_type);
    static_cast<void>(vertex_start);
    static_cast<void>(vertex_count);
    static_cast<void>(instance_count);
}

void Render_command_encoder_impl::draw_primitives(
    Primitive_type primitive_type,
    std::uintptr_t vertex_start,
    std::uintptr_t vertex_count
)
{
    static_cast<void>(primitive_type);
    static_cast<void>(vertex_start);
    static_cast<void>(vertex_count);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    Primitive_type           primitive_type,
    std::uintptr_t           index_count,
    erhe::dataformat::Format index_type,
    std::uintptr_t           index_buffer_offset,
    std::uintptr_t           instance_count)
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_count);
    static_cast<void>(index_type);
    static_cast<void>(index_buffer_offset);
    static_cast<void>(instance_count);
}

void Render_command_encoder_impl::draw_indexed_primitives(
    Primitive_type           primitive_type,
    std::uintptr_t           index_count,
    erhe::dataformat::Format index_type,
    std::uintptr_t           index_buffer_offset
)
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_count);
    static_cast<void>(index_type);
    static_cast<void>(index_buffer_offset);
}

void Render_command_encoder_impl::multi_draw_indexed_primitives_indirect(
    Primitive_type           primitive_type,
    erhe::dataformat::Format index_type,
    std::uintptr_t           indirect_offset,
    std::uintptr_t           drawcount,
    std::uintptr_t           stride
)
{
    static_cast<void>(primitive_type);
    static_cast<void>(index_type);
    static_cast<void>(indirect_offset);
    static_cast<void>(drawcount);
    static_cast<void>(stride);
}

} // namespace erhe::graphics
