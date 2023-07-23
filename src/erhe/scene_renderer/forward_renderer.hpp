#pragma once

#include "erhe/graphics/sampler.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/renderer/draw_indirect_buffer.hpp"
#include "erhe/renderer/pipeline_renderpass.hpp"
#include "erhe/scene_renderer/camera_buffer.hpp"
#include "erhe/scene_renderer/joint_buffer.hpp"
#include "erhe/scene_renderer/light_buffer.hpp"
#include "erhe/scene_renderer/material_buffer.hpp"
#include "erhe/scene_renderer/primitive_buffer.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Instance;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace erhe::scene_renderer
{

class Program_interface;

class Forward_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    Forward_renderer(
        erhe::graphics::Instance& graphics_instance,
        Program_interface&        program_interface
    );

    // Public API
    class Render_parameters
    {
    public:
        gl::Draw_elements_type                                             index_type       {gl::Draw_elements_type::unsigned_int};

        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        const Light_projections*                                           light_projections{nullptr};
        const gsl::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const gsl::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
        const gsl::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::vector<
            gsl::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        const std::vector<erhe::renderer::Pipeline_renderpass*>            passes;
        erhe::primitive::Primitive_mode                                    primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                                       primitive_settings{};
        const erhe::graphics::Texture*                                     shadow_texture{nullptr};
        const erhe::toolkit::Viewport&                                     viewport;
        const erhe::Item_filter                                     filter{};
        const erhe::graphics::Shader_stages*                               override_shader_stages{nullptr};
        const erhe::graphics::Shader_stages*                               error_shader_stages{nullptr};
        const glm::uvec4&                                                  debug_joint_indices{0, 0, 0, 0};
        const gsl::span<glm::vec4>&                                        debug_joint_colors{};
    };

    void render(const Render_parameters& parameters);

    void render_fullscreen(
        const Render_parameters&  parameters,
        const erhe::scene::Light* light
    );

    void next_frame();

private:
    erhe::graphics::Instance& m_graphics_instance;

    int                                      m_base_texture_unit{0};
    Camera_buffer                            m_camera_buffers;
    erhe::renderer::Draw_indirect_buffer     m_draw_indirect_buffers;
    Joint_buffer                             m_joint_buffers;
    Light_buffer                             m_light_buffers;
    Material_buffer                          m_material_buffers;
    Primitive_buffer                         m_primitive_buffers;
    erhe::graphics::Sampler                  m_nearest_sampler;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
};

} // namespace erhe::scene_renderer
