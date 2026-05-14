#pragma once

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"

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

class Program_interface;
class Standard_shader_variants;

class Forward_renderer
{
public:
    using Mesh_layer_collection = std::vector<const erhe::scene::Mesh_layer*>;

    Forward_renderer(
        erhe::graphics::Device&         graphics_device,
        erhe::graphics::Command_buffer& init_command_buffer,
        Program_interface&              program_interface
    );
    ~Forward_renderer() noexcept;

    // Public API
    class Render_parameters
    {
    public:
        erhe::graphics::Render_command_encoder&                            render_encoder;
        erhe::dataformat::Format                                           index_type       {erhe::dataformat::Format::format_32_scalar_uint};

        const glm::vec3                                                    ambient_light    {0.0f};
        const erhe::scene::Camera*                                         camera           {nullptr};
        // Multiview cameras. When non-empty, the renderer writes one
        // Camera UBO entry per view via Camera_buffer::update_views()
        // and binds the resulting block; shaders compiled with
        // enable_multiview() then index camera.cameras[gl_ViewIndex].
        // The single `camera` field above is ignored on this path. The
        // span size must match Program_interface_config::max_view_count
        // (i.e. the cameras[N] array size declared in the UBO).
        std::span<const Camera_view_input>                                 multiview_views{};
        const Light_projections*                                           light_projections{nullptr};
        const std::span<const std::shared_ptr<erhe::scene::Light>>&        lights           {};
        const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins            {};
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials        {};
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                                 mesh_spans;
        std::size_t                                                        non_mesh_vertex_count{0};
        const std::span<erhe::graphics::Lazy_render_pipeline*>             render_pipeline_states;
        const erhe::graphics::Render_pass*                                 render_pass{nullptr};
        erhe::primitive::Primitive_mode                                    primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        Primitive_interface_settings                                       primitive_settings{};
        const erhe::math::Viewport&                                        viewport;
        const erhe::Item_filter                                            filter{};
        const erhe::graphics::Shader_stages*                               override_shader_stages{nullptr};
        const erhe::graphics::Shader_stages*                               error_shader_stages{nullptr};
        // Optional. When non-null and a pipeline opts in via
        // Render_pipeline_create_info::uses_standard_variants, the bucket
        // loop computes a per-bucket Standard_variant_key (material caps
        // OR'd across the bucket's primitives, the pipeline's
        // vertex_format, mesh.skin presence OR'd across the bucket, and
        // light_projections->light_counts) and overrides the bucket's
        // shader_stages with the cache lookup. Skipped when
        // override_shader_stages is non-null (debug-viz takes priority).
        Standard_shader_variants*                                          standard_shader_variants{nullptr};
        // Per-viewport debug visualization override. When non-none, the
        // per-bucket variant key is rebuilt with this Shader_debug value
        // so each pipeline picks the matching ERHE_SHADER_DEBUG variant
        // of the standard shader. Default (Shader_debug::none) keeps
        // the production rendering path.
        Shader_debug                                                       shader_debug{Shader_debug::none};
        const glm::uvec4&                                                  debug_joint_indices{0, 0, 0, 0};
        const std::span<glm::vec4>&                                        debug_joint_colors{};
        const std::string_view                                             debug_label;

        const glm::vec4                                                    grid_size      {10.0f,  1.0f,  0.1f,  0.01f};
        const glm::vec4                                                    grid_line_width{ 0.006, 0.02f, 0.02f, 0.02f};

        uint64_t                                                           frame_number{0};
        bool                                                               reverse_depth{true};
        erhe::math::Depth_range                                            depth_range{erhe::math::Depth_range::zero_to_one};
        erhe::math::Coordinate_conventions                                 conventions;
    };

    void render(const Render_parameters& parameters);
    void draw_primitives(const Render_parameters& parameters, const erhe::scene::Light* light);

    // Init-time prewarm. Walks the same buckets render() would build for
    // each (pipeline, mesh_span) pair, computes the per-bucket
    // Standard_variant_key with the supplied light_counts, and calls
    // standard_shader_variants->get_or_compile(key, view_count) for every
    // requested view count. Pipelines that do not opt into standard
    // variants (uses_standard_variants=false or vertex_format=null) are
    // skipped, exactly mirroring the runtime gate.
    //
    // extra_materials is a content-library style list whose meshes are
    // not yet attached to the scene -- each material is converted to a
    // single Standard_variant_key against fallback_vertex_format (no
    // skin, no aniso_control beyond what the material/format combination
    // would imply at draw time) and prewarmed once. extra_materials is
    // ignored when fallback_vertex_format is null.
    //
    // Performs no GPU draws and does not require a Render_command_encoder.
    // The compile work is glslang -> SPIR-V -> vkCreateShaderModule for
    // each cache miss; populating the per-pipeline VkPipeline cache is
    // optional and driven by Warmup_target below.
    //
    // Optional VkPipeline warmup. When set, prewarm_standard_variants not
    // only compiles shader stages but also drives Device::warmup_render_pipeline
    // for each (Lazy_render_pipeline, variant_key, view_count) tuple it
    // visits whose view_count matches Warmup_target::view_count. On Vulkan
    // this lands the compiled pipeline binary in the driver-level
    // VkPipelineCache (Device_impl::m_pipeline_cache), so the first
    // runtime vkCreateGraphicsPipelines call against the same shader
    // modules + state tuple skips IR-optimization work even though the
    // application-level VkRenderPass-keyed cache still misses on the
    // first bind. The format/usage fields must match a render pass the
    // runtime will actually use; mismatches do not produce validation
    // errors (they pollute the cache with un-reused binaries instead).
    // OpenGL / Null / Metal backends are no-ops on the device side.
    class Warmup_target
    {
    public:
        // Single-view: 0. Multiview: the view count the matching render
        // pass uses (typically 2 for Quest stereo). Only variants
        // compiled with this view_count are warmed; mixing
        // multiview/single-view in one Warmup_target is not supported.
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
        std::span<erhe::graphics::Lazy_render_pipeline*>             render_pipeline_states;
        const std::vector<
            std::span<const std::shared_ptr<erhe::scene::Mesh>>
        >&                                                           mesh_spans;
        std::span<const std::shared_ptr<erhe::primitive::Material>>  extra_materials{};
        Standard_variant_light_counts                                light_counts{};
        std::span<const uint32_t>                                    multiview_view_counts;
        Standard_shader_variants*                                    standard_shader_variants{nullptr};
        const erhe::dataformat::Vertex_format*                       fallback_vertex_format{nullptr};
        erhe::primitive::Primitive_mode                              primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};
        // Optional. When non-null and a Warmup_target's view_count
        // matches an entry in multiview_view_counts, the prewarm also
        // calls Device::warmup_render_pipeline for every variant compiled
        // at that view_count (see Warmup_target). Multiple targets can
        // be supplied (e.g. one for the headset color pass, one for the
        // mirror window) -- each is matched independently against the
        // visited view_counts.
        std::span<const Warmup_target>                               warmup_targets{};
    };

    // Returns the number of Device::warmup_render_pipeline calls issued
    // (0 when warmup_targets is empty); useful for the prewarm log.
    auto prewarm_standard_variants(const Prewarm_parameters& parameters) -> std::size_t;

    static const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> empty_mesh_spans;

private:
    erhe::graphics::Device&                       m_graphics_device;
    Program_interface&                            m_program_interface;
    Camera_buffer                                 m_camera_buffer;
    erhe::renderer::Draw_indirect_buffer          m_draw_indirect_buffer;
    Joint_buffer                                  m_joint_buffer;
    Light_buffer                                  m_light_buffer;
    Material_buffer                               m_material_buffer;
    Primitive_buffer                              m_primitive_buffer;
    erhe::graphics::Sampler                       m_fallback_sampler;
    std::shared_ptr<erhe::graphics::Texture>      m_dummy_texture;
    std::unique_ptr<erhe::graphics::Texture_heap> m_texture_heap;
};

} // namespace erhe::scene_renderer
