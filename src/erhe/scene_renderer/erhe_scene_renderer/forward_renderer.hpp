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
#include "erhe_scene_renderer/scene_pass_resources.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include <glm/glm.hpp>

#include "erhe_primitive/material.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe {
    class Item_filter;
}
namespace erhe::graphics {
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
        // (0 = depth + receiver-plane bias, 1 = distance map + baked fwidth bias).
        uint32_t                                               shadow_technique{0};
        // Shadow map depth bit count (graphics preset), plumbed as the
        // ERHE_SHADOW_DEPTH_BITS variant axis. The depth receiver snaps its
        // hard-path reference to this UNORM format's quantization grid; 32
        // (D32_SFLOAT -- there is no 32-bit UNORM depth) and 0 mean "float / no
        // snap".
        uint32_t                                               shadow_depth_bits{0};
        // .x: 0xffffffffu = no active joint for the joint_weight_ramp debug
        // mode ("missing data" magenta), anything else = one is active and
        // marked per-slot in the joint buffer (see debug_target_joint).
        // .y: 1 = show zero-weight vertices as black.
        const glm::uvec4&                                      debug_joint_indices{0xffffffffu, 0, 0, 0};
        const std::span<glm::vec4>&                            debug_joint_colors{};
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

    void render(const Render_parameters& parameters);
    void draw_primitives(const Primitive_render_parameters& parameters, const erhe::scene::Light* light);

    // The draw-list colour entry point lives in Draw_list_renderer, which
    // shares this renderer's Scene_pass_resources. Nothing here names a
    // draw-list type.

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
        // Shadow map depth bit count to prewarm (ERHE_SHADOW_DEPTH_BITS axis).
        // Same single-valued, warm-the-active-mode policy as shadow_filter.
        uint32_t                                                    shadow_depth_bits{0};
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
};

} // namespace erhe::scene_renderer
