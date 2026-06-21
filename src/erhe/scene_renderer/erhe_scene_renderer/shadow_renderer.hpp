#pragma once

#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
//#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <array>
#include <cstddef>
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

// Face culling used while rasterizing shadow casters into the shadow map.
// cull_front (the default) keeps only back faces -- reduces peter-panning on
// closed meshes; cull_back keeps front faces -- lets single-sided geometry
// cast shadows from the side facing the light; cull_none rasterizes both
// sides. The values index Shadow_renderer's per-cull-mode caster pipelines, so
// keep them contiguous from 0 and in sync with the editor's Shadow_cull_mode
// codegen enum (src/editor/config/definitions/shadow_cull_mode.py).
enum class Shadow_cull_mode : unsigned int
{
    cull_front = 0,
    cull_back  = 1,
    cull_none  = 2
};
inline constexpr std::size_t shadow_cull_mode_count = 3;

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

        // Optional tight directional shadow frustum fit settings;
        // nullptr gives the legacy stable fit. Must outlive the render call.
        const erhe::scene::Shadow_frustum_fit_settings*                    fit_settings{nullptr};

        // Rasterizer (hardware) depth bias applied while rendering the shadow
        // map -- a caster-side acne / peter-panning control, orthogonal to the
        // receiver-side bias in the forward shader. Both default to 0 (no
        // bias). Set per pass via Render_command_encoder::set_depth_bias().
        float                                                              depth_bias_constant{0.0f};
        float                                                              depth_bias_slope{0.0f};

        // Face culling for the shadow caster pass; selects one of the
        // per-cull-mode pipelines. Defaults to cull_front (back faces only),
        // the historical behavior.
        Shadow_cull_mode                                                   cull_mode{Shadow_cull_mode::cull_front};

        // Shadow_technique_mode::distance support. When use_distance is true the
        // caster runs a fragment shader (VARIANT_SHADOW_DISTANCE) that writes the
        // fwidth-biased light-space depth into distance_texture (color attachment
        // of the render passes), and the receiver compares against it with no
        // bias. distance_bias_coeff is cdd*(1+pcfRadius), the per-pass coefficient
        // the caster multiplies fwidth() by. Defaults keep the depth technique.
        std::shared_ptr<erhe::graphics::Texture>                           distance_texture{};
        bool                                                               use_distance{false};
        float                                                              distance_bias_coeff{0.0f};

        // Omnidirectional point-light shadows. point_cube_texture is the R32F
        // texture_cube_map_array (one cube / 6 faces per shadow-casting point
        // light); point_cube_render_passes holds 6 * point_shadow_light_count
        // render passes, the cube face for dense point index p / face f being
        // pass [6*p + f]. point_shadow_viewport is the per-face viewport (the
        // point cube resolution). Empty / null disables the point cube pass.
        std::shared_ptr<erhe::graphics::Texture>                           point_cube_texture{};
        const std::vector<std::unique_ptr<erhe::graphics::Render_pass>>*   point_cube_render_passes{nullptr};
        erhe::math::Viewport                                               point_shadow_viewport{};
    };

    auto render(const Render_parameters& parameters) -> bool;

    // Init-time prewarm. Drives both:
    //   Phase 1: shader-module compile (glslang -> SPIR-V ->
    //            vkCreateShaderModule) for each unique depth-only variant
    //            the scene's meshes would produce at runtime. Uses
    //            bucket_primitives with accept_all_variant_signatures (same
    //            as render()), so the variant set matches exactly what the
    //            runtime shadow path will request -- including custom
    //            vertex formats from imported meshes.
    //   Phase 2: per-render-pass VkPipeline construction
    //            (vkCreateGraphicsPipelines). Skipped when render_passes
    //            is empty (e.g. orchestrator runs before any
    //            Shadow_render_node has been created).
    //
    // mesh_spans is the same shape Forward_renderer takes; orchestrator
    // typically passes the scene_root's content + controller layers.
    void prewarm_pipelines(
        std::span<const std::unique_ptr<erhe::graphics::Render_pass>>            render_passes,
        const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>>&  mesh_spans,
        Shadow_cull_mode                                                         cull_mode
    );

private:
    // Shared shadow-caster bucket walk + draw used by both the 2D (directional /
    // spot) per-light pass and the per-face point-light cube pass. The caller
    // sets up the encoder (bind group, viewport / scissor, depth bias, material
    // / joint / light / camera / control buffers, texture heap) and opens the
    // render pass; this fills it with the shadow casters, selecting the variant
    // via boolean_mask_force_enable (VARIANT_DEPTH_ONLY [+ VARIANT_SHADOW_DISTANCE],
    // or VARIANT_SHADOW_CUBE) and the color attachment policy via color_blend.
    void draw_shadow_casters(
        erhe::graphics::Command_buffer&                                                          command_buffer,
        erhe::graphics::Render_command_encoder&                                                  encoder,
        erhe::graphics::Base_render_pipeline&                                                     base_pipeline,
        const erhe::graphics::Render_pass&                                                       render_pass,
        const erhe::graphics::Color_blend_state*                                                  color_blend,
        const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>&  mesh_spans,
        const erhe::Item_filter&                                                                 shadow_filter,
        uint32_t                                                                                 boolean_mask_force_enable
    );

    erhe::graphics::Device&                       m_graphics_device;
    Mesh_memory&                                  m_mesh_memory;
    Shader_variant_cache&                         m_shader_variant_cache;
    erhe::graphics::Fragment_outputs              m_empty_fragment_outputs;
    erhe::graphics::Bind_group_layout*            m_bind_group_layout{nullptr};
    // Declared before the pipelines: used in their rasterization state init.
    bool                                          m_y_flip{false};
    // Shadow caster pipelines, one per Shadow_cull_mode (indexed by its value).
    // m_pipelines_depth_clamp[] are the depth-clamp siblings, selected by
    // Shadow_frustum_fit_settings::depth_clamp. Both built in the constructor;
    // only the pipeline for the active cull mode ever builds GPU pipelines.
    std::array<erhe::graphics::Base_render_pipeline, shadow_cull_mode_count> m_pipelines;
    std::array<erhe::graphics::Base_render_pipeline, shadow_cull_mode_count> m_pipelines_depth_clamp;
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
    // Per-render() caster / receiver world AABB gather for the tight frustum
    // fit; members (cleared each call) so the vectors keep their capacity.
    std::vector<erhe::math::Aabb>                 m_caster_world_aabbs;
    std::vector<erhe::math::Aabb>                 m_receiver_world_aabbs;
};


} // namespace erhe::scene_renderer
