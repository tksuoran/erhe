#pragma once

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include "erhe_renderer/pipeline_renderpass.hpp"

namespace erhe::graphics { class Instance; }
namespace erhe::scene    { class Camera; }
namespace erhe::math     { class Viewport; }

namespace erhe::scene_renderer {

class Program_interface;

class Cube_renderer
{
public:
    Cube_renderer(erhe::graphics::Instance& graphics_instance, Program_interface& program_interface);

    [[nodiscard]] auto make_buffer(const std::vector<uint32_t>& cubes) -> std::shared_ptr<Cube_instance_buffer>;

    // Public API
    class Render_parameters
    {
    public:
        Cube_instance_buffer&               cube_instance_buffer;
        erhe::graphics::Pipeline&           pipeline;
        const erhe::scene::Camera*          camera{nullptr};
        std::shared_ptr<erhe::scene::Node>  node{};
        //std::shared_ptr<erhe::scene::Light> light{};
        Primitive_interface_settings        primitive_settings{};
        erhe::math::Viewport                viewport;
    };

    void render(const Render_parameters& parameters);

private:
    erhe::graphics::Instance& m_graphics_instance;
    Program_interface&        m_program_interface;
    Camera_buffer             m_camera_buffer;
    Light_buffer              m_light_buffer;
    Primitive_buffer          m_primitive_buffer;
};

} // erhe::scene_renderer
