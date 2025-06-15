#pragma once

#include <memory>

namespace erhe::graphics {

class Device;
class Framebuffer;

class Render_command_encoder final
{
public:
    Render_command_encoder(Device& device, const std::shared_ptr<Framebuffer>& framebuffer);
    ~Render_command_encoder();

    void set_render_pipeline_state();
    //void set_color_store_action        ();
    //void set_color_store_action_options();
    //void set_depth_store_action();

    void wait_for_fence();
    void update_fence  ();
    void memory_barrier();

    void set_vertex_buffer       ();
    void set_vertex_buffer_offset();
    void set_vertex_buffers      ();
    void set_vertex_bytes        ();

private:
    Device&                      m_device;
    std::shared_ptr<Framebuffer> m_framebuffer;
};

} // namespace erhe::graphics
