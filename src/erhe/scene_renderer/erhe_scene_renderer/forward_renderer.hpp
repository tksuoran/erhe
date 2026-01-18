#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_renderer/pipeline_renderpass.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Device;
    class Render_command_encoder;
    class Texture;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace erhe::scene_renderer {

class Program_interface;

class Forward_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    Forward_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface);

    // Public API
    class Render_parameters
    {
    public:
        erhe::graphics::Render_command_encoder&                            render_encoder;
        erhe::dataformat::Format                                           index_type       {erhe::dataformat::Format::format_32_scalar_uint};
        erhe::graphics::Buffer*                                            index_buffer     {nullptr};
        erhe::graphics::Buffer*                                            vertex_buffer0   {nullptr};
        erhe::graphics::Buffer*                                            vertex_buffer1   {nullptr};

        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        const Light_projections*                                           light_projections{nullptr};
        const std::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        std::size_t                                                        non_mesh_vertex_count{0};
        const std::vector<erhe::renderer::Pipeline_pass*>                  passes;
        erhe::primitive::Primitive_mode                                    primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                                       primitive_settings{};
        const erhe::math::Viewport&                                        viewport;
        const erhe::Item_filter                                            filter{};
        const erhe::graphics::Shader_stages*                               override_shader_stages{nullptr};
        const erhe::graphics::Shader_stages*                               error_shader_stages{nullptr};
        const glm::uvec4&                                                  debug_joint_indices{0, 0, 0, 0};
        const std::span<glm::vec4>&                                        debug_joint_colors{};
        const std::string_view                                             debug_label;

        const glm::vec4                                                    grid_size      {10.0f,  1.0f,  0.1f,  0.01f};
        const glm::vec4                                                    grid_line_width{ 0.006, 0.02f, 0.02f, 0.02f};

        uint64_t                                                           frame_number{0};
    };

    void render(const Render_parameters& parameters);
    void draw_primitives(const Render_parameters& parameters, const erhe::scene::Light* light);

private:
    erhe::graphics::Device&                  m_graphics_device;
    Program_interface&                       m_program_interface;
    Camera_buffer                            m_camera_buffer;
    erhe::renderer::Draw_indirect_buffer     m_draw_indirect_buffer;
    Joint_buffer                             m_joint_buffer;
    Light_buffer                             m_light_buffer;
    Material_buffer                          m_material_buffer;
    Primitive_buffer                         m_primitive_buffer;
    erhe::graphics::Sampler                  m_shadow_sampler_compare;
    erhe::graphics::Sampler                  m_shadow_sampler_no_compare;
    erhe::graphics::Sampler                  m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture> m_dummy_texture;
};

} // namespace erhe::scene_renderer
