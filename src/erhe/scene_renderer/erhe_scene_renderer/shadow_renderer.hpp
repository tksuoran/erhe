#pragma once

#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
//#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <initializer_list>
#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Device;
    class Sampler;
    class Texture;
    class Vertex_input_state;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Mesh_primitive;
}
namespace erhe::primitive {
    class Buffer_mesh;
}

namespace erhe::scene_renderer {

class Draw_indirect_buffer;
class Mesh_memory;
class Program_interface;
class Render_bucket;
class Scene_root;
class Scene_view;
class Settings;
class Shader_variant_cache;

class Shadow_renderer
{
public:
    static const int shadow_texture_unit_compare{0};
    static const int shadow_texture_unit_no_compare{1};

    Shadow_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Mesh_memory&                    mesh_memory,
        Program_interface&              program_interface,
        Shader_variant_cache&           shader_variant_cache
    );
    ~Shadow_renderer() noexcept;

    // Public API
    class Render_parameters
    {
    public:
        // The cb is in recording state. Shadow_renderer creates a
        // Render_command_encoder + Scoped_render_pass against this cb
        // for each light, drawing shadow casters into the shadow map
        // attachments.
        erhe::graphics::Command_buffer&          command_buffer;
        const erhe::scene::Camera*               view_camera{nullptr};
        const erhe::math::Viewport               view_camera_viewport;
        const erhe::math::Viewport               light_camera_viewport;
        std::shared_ptr<erhe::graphics::Texture> texture{};
        const std::vector<
            std::unique_ptr<erhe::graphics::Render_pass>
        >&                                       render_passes;
        const std::initializer_list<
            const std::span<
                const std::shared_ptr<erhe::scene::Mesh>
            >
        >&                                                                 mesh_spans;
        const std::span<const std::shared_ptr<erhe::scene::Light>>         lights;
        const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins{};
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials{};
        Light_projections&                                                 light_projections;
        bool                                                               reverse_depth{true};
        erhe::math::Depth_range                                            depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                                 conventions;
    };

    auto render(const Render_parameters& parameters) -> bool;

    // Init-time prewarm. Drives the pipeline-cache path render() takes so
    // the per-render-pass VkPipeline (vkCreateGraphicsPipelines on Vulkan)
    // exists before the first frame. Calls get_pipeline(vertex_input_state,
    // reverse_depth) to ensure the matching Lazy_render_pipeline entry is
    // registered, then invokes Lazy_render_pipeline::get_pipeline_for() on
    // each supplied render-pass descriptor so its format-hashed VkPipeline
    // variant is created.
    //
    // The depth-only shader stages are owned eagerly by m_shader_stages
    // (Reloadable_shader_stages compiles at construction), so there is no
    // Phase 1 shader-module compile to drive here -- only the lazy
    // VkPipeline construction.
    void prewarm_pipelines(std::span<const std::unique_ptr<erhe::graphics::Render_pass>> render_passes);

private:
    erhe::graphics::Device&                       m_graphics_device;
    Mesh_memory&                                  m_mesh_memory;
    Shader_variant_cache&                         m_shader_variant_cache;
    erhe::graphics::Fragment_outputs              m_empty_fragment_outputs;
    erhe::graphics::Bind_group_layout*            m_bind_group_layout{nullptr};
    erhe::graphics::Lazy_render_pipeline          m_pipeline;
    std::shared_ptr<erhe::graphics::Texture>      m_dummy_texture;
    erhe::graphics::Sampler                       m_fallback_sampler;

    erhe::graphics::Vertex_input_state            m_vertex_input;
    erhe::scene_renderer::Draw_indirect_buffer    m_draw_indirect_buffer;
    Joint_buffer                                  m_joint_buffer;
    Light_buffer                                  m_light_buffer;
    Camera_buffer                                 m_camera_buffer;
    Primitive_buffer                              m_primitive_buffer;
    Material_buffer                               m_material_buffer;
    // TODO Re-add per-cascade GPU timers; the cascade Render_pass objects
    // are owned by Shadow_render_node, so the timers should live there.
    std::unique_ptr<erhe::graphics::Texture_heap> m_texture_heap;
};


} // namespace erhe::scene_renderer
