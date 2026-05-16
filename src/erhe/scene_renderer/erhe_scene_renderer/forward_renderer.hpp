#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Lazy_render_pipeline;
    class Render_command_encoder;
    class Render_pipeline_state;
    class Texture;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace erhe::scene_renderer {

class Mesh_memory;
class Program_interface;
class Shader_variant_cache;

class Forward_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    Forward_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Mesh_memory&                    mesh_memory,
        Program_interface&              program_interface,
        Shader_variant_cache&           shader_variant_cache
    );
    ~Forward_renderer() noexcept;

    // Public API
    class Base_render_parameters
    {
    public:
        erhe::graphics::Render_command_encoder&                            render_encoder;
        const erhe::graphics::Render_pass*                                 render_pass      {nullptr};
        const erhe::math::Viewport&                                        viewport;
        const erhe::scene::Camera*                                         camera           {nullptr};
        std::span<const Camera_view_input>                                 views            {};
        const glm::vec3                                                    ambient_light    {0.0f};
        const Light_projections*                                           light_projections{nullptr};
        const std::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        uint32_t                                                           shader_key_boolean_mask_force_enable {0};
        uint32_t                                                           shader_key_boolean_mask_force_disable{0};
        uint64_t                                                           frame_number     {0};
        bool                                                               reverse_depth    {true};
        erhe::math::Depth_range                                            depth_range      {erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                                 conventions;
        const glm::vec4                                                    grid_size        {10.0f,  1.0f,  0.1f,  0.01f};
        const glm::vec4                                                    grid_line_width  { 0.006, 0.02f, 0.02f, 0.02f};
        const std::string_view                                             debug_label;
    };

    class Render_parameters
    {
    public:
        Base_render_parameters base;

        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                     mesh_spans;
        const std::span<erhe::graphics::Lazy_render_pipeline*> base_render_pipelines;
        const erhe::primitive::Primitive_mode                  primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                           primitive_settings{};
        const erhe::Item_filter                                filter{};
        Shader_debug                                           shader_debug{Shader_debug::none};
        const glm::uvec4&                                      debug_joint_indices{0, 0, 0, 0};
        const std::span<glm::vec4>&                            debug_joint_colors{};
    };

    class Primitive_render_parameters
    {
    public:
        Base_render_parameters base;

        std::size_t                           vertex_count{0};
        erhe::graphics::Lazy_render_pipeline& base_render_pipeline;
        const erhe::graphics::Shader_stages*  shader_stages{nullptr};
    };

    void render(const Render_parameters& parameters);
    void draw_primitives(const Primitive_render_parameters& parameters, const erhe::scene::Light* light);

    class Warmup_target
    {
    public:
        uint32_t                                view_count{0};
        unsigned int                            color_attachment_count{0};
        std::array<erhe::dataformat::Format, 4> color_attachment_formats{};
        std::array<uint64_t, 4>                 color_usage_before{};
        std::array<uint64_t, 4>                 color_usage_after{};
        erhe::dataformat::Format                depth_attachment_format  {erhe::dataformat::Format::format_undefined};
        erhe::dataformat::Format                stencil_attachment_format{erhe::dataformat::Format::format_undefined};
        uint64_t                                depth_usage_before{0};
        uint64_t                                depth_usage_after {0};
        unsigned int                            sample_count{1};
    };

    class Prewarm_parameters
    {
    public:
        std::span<erhe::graphics::Lazy_render_pipeline*>            render_pipeline_states;
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                          mesh_spans;
        std::span<const std::shared_ptr<erhe::primitive::Material>> extra_materials{};
        std::span<const uint32_t>                                   multiview_view_counts;
        Mesh_memory&                                                mesh_memory;
        erhe::primitive::Primitive_mode                             primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        std::span<const Warmup_target>                              warmup_targets{};
    };

    // Returns the number of Device::warmup_render_pipeline calls issued
    // (0 when warmup_targets is empty); useful for the prewarm log.
    auto prewarm_standard_variants(const Prewarm_parameters& parameters) -> std::size_t;

    static const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> empty_mesh_spans;

private:
    erhe::graphics::Device&                       m_graphics_device;
    Mesh_memory&                                  m_mesh_memory;
    Program_interface&                            m_program_interface;
    Shader_variant_cache&                         m_shader_variant_cache;
    Camera_buffer                                 m_camera_buffer;
    erhe::scene_renderer::Draw_indirect_buffer    m_draw_indirect_buffer;
    Joint_buffer                                  m_joint_buffer;
    Light_buffer                                  m_light_buffer;
    Material_buffer                               m_material_buffer;
    Primitive_buffer                              m_primitive_buffer;
    erhe::graphics::Sampler                       m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture>      m_dummy_texture;
    std::unique_ptr<erhe::graphics::Texture_heap> m_texture_heap;
};

} // namespace erhe::scene_renderer
