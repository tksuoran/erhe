#pragma once

namespace erhe::graphics {

class Framebuffer;

class Render_command_encoder;
class Render_pass_descriptor
{
public:
    std::shared_ptr<Framebuffer> framebuffer;
};

class Command_buffer final
{
public:
    void make_render_command_encoder(Render_pass_descriptor& descriptor);
};

} // namespace erhe::graphics
