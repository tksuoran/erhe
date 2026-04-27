#pragma once

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Lazy_render_pipeline;
    class Render_command_encoder;
    class Render_pass;
}
namespace erhe::scene    { class Camera; }
namespace erhe::math     { class Viewport; }

namespace erhe::scene_renderer {

class Program_interface;

class Cube_renderer
{
public:
    Cube_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Program_interface&              program_interface
    );

    [[nodiscard]] auto make_buffer(const std::vector<uint32_t>& cubes) -> std::shared_ptr<Cube_instance_buffer>;

    // Public API
    class Render_parameters
    {
    public:
        Cube_instance_buffer&                   cube_instance_buffer;
        erhe::graphics::Render_command_encoder& render_encoder;
        erhe::graphics::Lazy_render_pipeline&   render_pipeline_state;
        const erhe::graphics::Render_pass&      render_pass;
        const erhe::scene::Camera*              camera{nullptr};
        std::shared_ptr<erhe::scene::Node>      node{};
        Primitive_interface_settings            primitive_settings{};
        erhe::math::Viewport                    viewport;
        glm::vec4                               cube_size  {0.4f, 0.4f, 0.4f, 1.0f};
        glm::vec4                               color_bias {0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4                               color_scale{1.0f, 1.0f, 1.0f, 0.0f};
        glm::vec4                               color_start{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec4                               color_end  {1.0f, 1.0f, 1.0f, 0.0f};
        uint64_t                                frame_number{0};
    };

    void render(const Render_parameters& parameters);

private:
    erhe::graphics::Device& m_graphics_device;
    Program_interface&      m_program_interface;
    Camera_buffer           m_camera_buffer;
    Light_buffer            m_light_buffer;
    Primitive_buffer        m_primitive_buffer;
    Cube_control_buffer     m_cube_control_buffer;
};

} // erhe::scene_renderer
