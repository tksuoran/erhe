#pragma once

#include <memory>

namespace erhe::graphics {

class Device;
class Render_pass;
class Render_pipeline_state;

class Render_command_encoder final
{
public:
    Render_command_encoder(Device& device, Render_pass& render_pass);
    Render_command_encoder(const Render_command_encoder&) = delete;
    Render_command_encoder& operator=(const Render_command_encoder&) = delete;
    Render_command_encoder(Render_command_encoder&&) = delete;
    Render_command_encoder& operator=(Render_command_encoder&&) = delete;
    ~Render_command_encoder();

    void set_render_pipeline_state(const Render_pipeline_state& pipeline);

    void wait_for_fence();
    void update_fence  ();
    void memory_barrier();

    void set_vertex_buffer       ();
    void set_vertex_buffer_offset();
    void set_vertex_buffers      ();
    void set_vertex_bytes        ();

private:
    void start_render_pass();
    void end_render_pass();

    Device&      m_device;
    Render_pass& m_render_pass;
};

} // namespace erhe::graphics
