#pragma once

#include "erhe_scene_renderer/cube_instance_buffer.hpp"

#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"

namespace erhe::graphics {
    class Instance;
}
namespace erhe::scene {
    class Camera;
}
namespace erhe::math {
    class Viewport;
}

namespace erhe::scene_renderer {

class Program_interface;

class Cube_renderer
{
public:
    Cube_renderer(erhe::graphics::Instance& graphics_instance, Program_interface& program_interface);

    [[nodiscard]] auto get_buffer() -> Cube_instance_buffer&;

    // Public API
    class Render_parameters
    {
    public:
        std::size_t                 frame;
        erhe::graphics::Pipeline&   pipeline;
        const erhe::scene::Camera*  camera{nullptr};
        const erhe::math::Viewport& viewport;
    };

    void render(const Render_parameters& parameters);

private:
    erhe::graphics::Instance&           m_graphics_instance;
    erhe::graphics::Buffer              m_index_buffer;
    erhe::scene_renderer::Camera_buffer m_camera_buffer;
    Cube_instance_buffer                m_cube_instance_buffer;

};

} // erhe::scene_renderer
