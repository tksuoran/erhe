#pragma once

#include <cstdint>

namespace erhe::graphics {

class Buffer;
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

    void set_index_buffer (const Buffer* buffer);
    void set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index);

private:
    void start_render_pass();
    void end_render_pass();

    Device&      m_device;
    Render_pass& m_render_pass;
};

} // namespace erhe::graphics
