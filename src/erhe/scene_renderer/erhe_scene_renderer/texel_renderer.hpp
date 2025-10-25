#pragma once

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"

namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
}
namespace erhe::scene { class Camera; }
namespace erhe::math  { class Viewport; }

namespace erhe::scene_renderer {

class Program_interface;
class Light_projections;

class Texel_renderer
{
public:
    Texel_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface);

    // Public API
    class Render_parameters
    {
    public:
        erhe::graphics::Render_command_encoder&                     render_encoder;
        erhe::graphics::Render_pipeline_state&                      pipeline;
        const erhe::scene::Camera*                                  camera           {nullptr};
        const Light_projections*                                    light_projections{nullptr};
        const std::span<const std::shared_ptr<erhe::scene::Light>>& lights           {};
        erhe::math::Viewport                                        viewport;
    };

    void render(const Render_parameters& parameters);

private:
    erhe::graphics::Device&                  m_graphics_device;
    Program_interface&                       m_program_interface;
    Camera_buffer                            m_camera_buffer;
    Light_buffer                             m_light_buffer;
    erhe::graphics::Sampler                  m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
};

} // erhe::scene_renderer
