#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/render_pass.hpp"

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Device& device, Render_pass& render_pass)
    : m_device     {device}
    , m_render_pass{render_pass}
{
    start_render_pass();
}

Render_command_encoder::~Render_command_encoder()
{
    end_render_pass();
}

void Render_command_encoder::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    m_device.opengl_state_tracker.execute_(pipeline, false);
}

void Render_command_encoder::start_render_pass()
{
    m_render_pass.start_render_pass();
}

void Render_command_encoder::end_render_pass()
{
    m_render_pass.end_render_pass();
}

void Render_command_encoder::set_index_buffer(const Buffer* buffer)
{
    m_device.opengl_state_tracker.vertex_input.set_index_buffer(buffer);
}
void Render_command_encoder::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index)
{
    m_device.opengl_state_tracker.vertex_input.set_vertex_buffer(index, buffer, offset);
}

} // namespace erhe::graphics
