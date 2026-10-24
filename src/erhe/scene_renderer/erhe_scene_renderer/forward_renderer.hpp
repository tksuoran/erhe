#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_scene_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/glyph_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/render_bucket_snapshot.hpp"
#include "erhe_scene_renderer/scene_pass_resources.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include "erhe_primitive/material.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
    class Shader_stages;
    class Color_blend_state;
    class Command_buffer;
    class Device;
    class Base_render_pipeline;
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
    class Light_snapshot;
    class Mesh;
    class Mesh_layer;
    class Xformable; using Node = Xformable;
}
namespace erhe::ui {
    class Glyph_outline_set;
}

namespace erhe::scene_renderer {

class Mesh_memory;
class Program_interface;
class Shader_variant_cache;

class Forward_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    // Lightmap / DDGI environment state lives with the pass resources
    // (Scene_pass_resources), which both binds it and exposes it to the
    // shader-variant key derivation here. These forward for the callers that
    // reach the renderer rather than the resources.
    void set_lightmap_texture(const std::shared_ptr<erhe::graphics::Texture>& texture) { m_pass_resources.set_lightmap_texture(texture); }
    void set_lightmap_bicubic(const bool enabled) { m_pass_resources.set_lightmap_bicubic(enabled); }
    void set_ddgi(
        const Ddgi_parameters&                          parameters,
        const std::shared_ptr<erhe::graphics::Texture>& irradiance_texture,
        const std::shared_ptr<erhe::graphics::Texture>& distance_texture,
        const std::shared_ptr<erhe::graphics::Texture>& probe_data_texture
    )
    {
        m_pass_resources.set_ddgi(parameters, irradiance_texture, distance_texture, probe_data_texture);
    }
    void clear_ddgi() { m_pass_resources.clear_ddgi(); }

    [[nodiscard]] auto get_pass_resources() -> Scene_pass_resources& { return m_pass_resources; }

    // pass_resources is shared with Draw_list_renderer and owned by neither:
    // it holds no per-frame state of its own (begin_pass returns a
    // Pass_state), and both entry points run the same prologue through it.
    Forward_renderer(
        erhe::graphics::Device& graphics_device,
        Mesh_memory&            mesh_memory,
        Program_interface&      program_interface,
        Shader_variant_cache&   shader_variant_cache,
        Scene_pass_resources&   pass_resources
    );
    ~Forward_renderer() noexcept;

    // Moved to namespace scope (scene_pass_resources.hpp) so the pass
    // prologue can be described without naming a renderer. The alias
    // keeps every Forward_renderer::Base_render_parameters{...} call site.
    using Base_render_parameters = erhe::scene_renderer::Base_render_parameters;

    class Render_parameters
    {
    public:
        Base_render_parameters base;

        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                     mesh_spans;
        const std::span<erhe::graphics::Base_render_pipeline*> base_render_pipelines;
        Blending_mode_policy                                   blending_mode_policy{Blending_mode_policy::not_set};
        const erhe::primitive::Primitive_mode                  primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                           primitive_settings{};
        const erhe::Item_filter                                filter{};
        Shader_debug                                           shader_debug{Shader_debug::none};
        // The SHADER_DEBUG override visualization applies only to meshes that
        // pass this filter; meshes it rejects render with SHADER_DEBUG = none.
        // Default (match-all) preserves the prior "applies to everything" behavior.
        const erhe::Item_filter                                shader_debug_filter{};
        // Shadow map filtering method, plumbed to the shader as the
        // ERHE_SHADOW_FILTER compile-time variant axis. The value is the PCF
        // kernel width in texels (0 = hard, 2 = 2x2, 4 = 4x4, 6 = 6x6).
        uint32_t                                               shadow_filter{0};
        // Depth-bias method for the wide-kernel PCF path, plumbed as the
        // ERHE_SHADOW_BIAS variant axis (0 = slope-scaled, 1 = receiver-plane).
        uint32_t                                               shadow_bias{1};
        // Shadow technique, plumbed as the ERHE_SHADOW_TECHNIQUE variant axis
        // (0 = depth map, 1 = distance map of caster plane light distances).
        uint32_t                                               shadow_technique{0};
        // ERHE_SHADOW_DEPTH_BITS variant axis: get_shadow_depth_bits_axis() of
        // the format of the shadow map texture sampled by this pass (16 / 24
        // = UNORM, the hard path snaps its reference to that grid; 32 = float
        // and 0 = no shadow map, no snap).
        uint32_t                                               shadow_depth_bits{0};
        // ERHE_SPECULAR_ANTI_ALIASING variant axis of the variants that call
        // anisotropic_brdf() (set_specular_anti_aliasing_axis()).
        Specular_anti_aliasing                                 specular_anti_aliasing{Specular_anti_aliasing::normal_kernel};
        // .x: 0xffffffffu = no active joint for the joint_weight_ramp debug
        // mode ("missing data" magenta), anything else = one is active and
        // marked per-slot in the joint buffer (see debug_target_joint).
        // .y: 1 = show zero-weight vertices as black.
        const glm::uvec4&                                      debug_joint_indices{0xffffffffu, 0, 0, 0};
        const std::span<const glm::vec4>&                      debug_joint_colors{};
        // Active joint for the joint_weight_ramp debug mode, matched per
        // joint slot by Joint_buffer::update(). nullptr = none.
        const erhe::scene::Node*                               debug_target_joint{nullptr};
        // When non-null, bypass the Shader_variant_cache lookup and use
        // these stages for every bucket. Used by non-standard primitive
        // passes that draw with a dedicated program instead of the
        // standard mesh shader (e.g. an edge_lines pass using the
        // geometry-shader wide_lines program).
        const erhe::graphics::Shader_stages*                   shader_stages_override{nullptr};
        // When non-null, overrides the per-bucket color-blend state (otherwise
        // color_blend_disabled / color_blend_premultiplied chosen by blend mode).
        // Used by depth/stencil-only passes that must not write color, e.g. the
        // selection stencil-mask pass (color_writes_disabled).
        const erhe::graphics::Color_blend_state*               color_blend_override{nullptr};
    };

    class Primitive_render_parameters
    {
    public:
        Base_render_parameters base;

        std::size_t                              vertex_count{0};
        erhe::graphics::Base_render_pipeline&    base_render_pipeline;
        const erhe::graphics::Color_blend_state* color_blend{nullptr};
        const erhe::graphics::Shader_stages*     shader_stages{nullptr};
    };

    // The inputs of the bucketing, which reads the meshes: the prepare-phase
    // half of render() (prepare()).
    class Prepare_parameters
    {
    public:
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                       mesh_spans;
        // The set the records' material slots are resolved in; the draw binds
        // the same set (Base_render_parameters::material_source).
        const Material_set*                      material_source{nullptr};
        uint32_t                                 shader_key_boolean_mask_force_enable {0};
        uint32_t                                 shader_key_boolean_mask_force_disable{0};
        Blending_mode_policy                     blending_mode_policy{Blending_mode_policy::not_set};
        erhe::primitive::Primitive_mode          primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings             primitive_settings{};
        erhe::Item_filter                        filter{};
        Shader_debug                             shader_debug{Shader_debug::none};
        // See Render_parameters::shader_debug_filter.
        erhe::Item_filter                        shader_debug_filter{};
    };

    // The inputs of drawing a prepared snapshot: the render-phase half of
    // render(). Reads no Mesh; the environment shader-key axes come from
    // here (light partition of base.light_projections, the shadow axes, the
    // view count of base.views, the pass resources' DDGI state).
    class Draw_parameters
    {
    public:
        Base_render_parameters                   base;
        std::span<erhe::graphics::Base_render_pipeline*> base_render_pipelines;
        uint32_t                                 shadow_filter{0};
        uint32_t                                 shadow_bias{1};
        uint32_t                                 shadow_technique{0};
        uint32_t                                 shadow_depth_bits{0};
        Specular_anti_aliasing                   specular_anti_aliasing{Specular_anti_aliasing::normal_kernel};
        glm::uvec4                               debug_joint_indices{0xffffffffu, 0, 0, 0};
        std::span<const glm::vec4>               debug_joint_colors{};
        const erhe::scene::Node*                 debug_target_joint{nullptr};
        const erhe::graphics::Shader_stages*     shader_stages_override{nullptr};
        const erhe::graphics::Color_blend_state* color_blend_override{nullptr};
    };

    // Prepare phase (main thread): buckets the meshes and writes each
    // bucket's complete primitive records and draw commands into out
    // (cleared first). Reads the meshes, their primitives and materials and
    // the material set; mutates nothing of this renderer, so it may run
    // while the render phase draws another snapshot.
    void prepare(const Prepare_parameters& parameters, Render_bucket_snapshot& out) const;

    // Render phase: draws a snapshot prepare() filled. The records and draw
    // commands are copied as prepared; only the environment axes of the
    // shader keys are applied here.
    void render(const Draw_parameters& parameters, const Render_bucket_snapshot& snapshot);

    // prepare() and render(snapshot) in one call, on one thread, through a
    // snapshot this renderer keeps: for callers that prepare and draw at the
    // same point (examples, tests).
    void render(const Render_parameters& parameters);
    void draw_primitives(const Primitive_render_parameters& parameters, const erhe::scene::Light_snapshot* light);

    // The draw-list colour entry point lives in Draw_list_renderer, which
    // shares this renderer's Scene_pass_resources. Nothing here names a
    // draw-list type.

    class Warmup_target
    {
    public:
        uint32_t                                view_count{0};
        unsigned int                            color_attachment_count{0};
        std::array<erhe::dataformat::Format, 4> color_attachment_formats{};
        erhe::dataformat::Format                depth_attachment_format  {erhe::dataformat::Format::format_undefined};
        erhe::dataformat::Format                stencil_attachment_format{erhe::dataformat::Format::format_undefined};
        unsigned int                            sample_count{1};
    };

    class Prewarm_parameters
    {
    public:
        Blending_mode_policy                                        blending_mode_policy;
        std::span<erhe::graphics::Base_render_pipeline*>            render_pipeline_states;
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                          mesh_spans;
        std::span<const std::shared_ptr<erhe::primitive::Material>> extra_materials{};
        std::span<const uint32_t>                                   multiview_view_counts;
        Mesh_memory&                                                mesh_memory;
        erhe::primitive::Primitive_mode                             primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        std::span<const Warmup_target>                              warmup_targets{};

        Light_layer_partition                                       light_partition{};
        uint32_t                                                    shader_key_force_enable_mask {0};
        uint32_t                                                    shader_key_force_disable_mask{0};
        Shader_debug                                                shader_debug{Shader_debug::none};
        // Shadow filtering method to prewarm (ERHE_SHADOW_FILTER axis).
        // Single-valued like shader_debug: warm the active mode so there is
        // no first-frame compile-on-miss; switching modes at runtime compiles
        // the other variant once, on demand.
        uint32_t                                                    shadow_filter{0};
        // Shadow bias method to prewarm (ERHE_SHADOW_BIAS axis). Same
        // single-valued, warm-the-active-mode policy as shadow_filter.
        uint32_t                                                    shadow_bias{1};
        // Shadow technique to prewarm (ERHE_SHADOW_TECHNIQUE axis). Same
        // single-valued, warm-the-active-mode policy as shadow_filter.
        uint32_t                                                    shadow_technique{0};
        // ERHE_SHADOW_DEPTH_BITS axis to prewarm: get_shadow_depth_bits_axis()
        // of the shadow map format the runtime will sample.
        uint32_t                                                    shadow_depth_bits{0};
        // Specular anti-aliasing mode to prewarm (ERHE_SPECULAR_ANTI_ALIASING
        // axis). Same single-valued, warm-the-active-mode policy.
        Specular_anti_aliasing                                      specular_anti_aliasing{Specular_anti_aliasing::normal_kernel};
    };

    // Returns the number of Device::warmup_render_pipeline calls issued
    // (0 when warmup_targets is empty); useful for the prewarm log.
    auto prewarm_standard_variants(const Prewarm_parameters& parameters) -> std::size_t;

    // The forward renderer owns the per-frame joint UBO/SSBO ring buffer.
    // Other renderers that run in the same frame (e.g. the content
    // wide-line compute pre-pass for skinned edge lines) can update +
    // bind through this same Joint_buffer instead of allocating a
    // duplicate ring buffer.
    [[nodiscard]] auto get_joint_buffer() -> Joint_buffer& { return m_pass_resources.get_joint_buffer(); }

    static const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> empty_mesh_spans;

private:
    erhe::graphics::Device&                       m_graphics_device;
    Mesh_memory&                                  m_mesh_memory;
    Program_interface&                            m_program_interface;
    Shader_variant_cache&                         m_shader_variant_cache;
    Scene_pass_resources&                         m_pass_resources;
    erhe::scene_renderer::Draw_indirect_buffer    m_draw_indirect_buffer;
    Primitive_buffer                              m_primitive_buffer;
    // The snapshot render(const Render_parameters&) prepares and draws, and
    // the debug label of one bucket; cleared at use so the capacity is kept.
    Render_bucket_snapshot                        m_immediate_snapshot;
    std::string                                   m_bucket_label;
};

} // namespace erhe::scene_renderer
