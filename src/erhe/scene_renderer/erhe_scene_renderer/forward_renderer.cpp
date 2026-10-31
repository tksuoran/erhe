#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <iterator>

#include <functional>

namespace erhe::scene_renderer {

const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> Forward_renderer::empty_mesh_spans{};

Forward_renderer::Forward_renderer(
    erhe::graphics::Device& graphics_device,
    Mesh_memory&            mesh_memory,
    Program_interface&      program_interface,
    Shader_variant_cache&   shader_variant_cache,
    Scene_pass_resources&   pass_resources
)
    : m_graphics_device     {graphics_device}
    , m_mesh_memory         {mesh_memory}
    , m_program_interface   {program_interface}
    , m_shader_variant_cache{shader_variant_cache}
    , m_pass_resources      {pass_resources}
    , m_draw_indirect_buffer{graphics_device, program_interface.config.max_draw_count}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
{
}

Forward_renderer::~Forward_renderer() noexcept = default;

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};

namespace {

const char* safe_str(const char* str)
{
    return str != nullptr ? str : "";
}

// The light layer partition the shading variant is selected with: the
// resolved light set's partition carried by the Light_projections (which
// lights got a UBO slot and a shadow layer, see Light_set), so the shader's
// shadow-mapped / non-shadow loop bounds match the UBO slot layout
// Light_buffer::update() writes. Without light projections no light is shaded
// (Light_buffer::update() writes no light data then either).
[[nodiscard]] auto get_light_layer_partition(const Forward_renderer::Base_render_parameters& base) -> Light_layer_partition
{
    if (base.light_projections != nullptr) {
        return base.light_projections->light_partition;
    }
    return Light_layer_partition{};
}

}

namespace {

// The shader-key axes the render phase owns (and LIGHT_SHADING_CLUSTERED,
// copied in apply_render_environment()): Forward_renderer::prepare()
// derives the keys with these zero (Shader_key::derive copies them from the
// environment key and sets none of them itself), and drawing a snapshot sets
// them from the draw's own environment, so the key equals the one a single
// derive with the full environment produced.
constexpr Shader_int c_render_environment_int_axes[] = {
    Shader_int::LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED,
    Shader_int::LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED,
    Shader_int::LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED,
    Shader_int::LIGHT_COUNT_SPOT_SHADOWMAPPED,
    Shader_int::LIGHT_COUNT_POINT_NOT_SHADOWMAPPED,
    Shader_int::LIGHT_COUNT_POINT_SHADOWMAPPED,
    Shader_int::SHADOW_FILTER,
    Shader_int::SHADOW_BIAS,
    Shader_int::SHADOW_TECHNIQUE,
    Shader_int::SHADOW_DEPTH_BITS,
    Shader_int::SHADER_MULTIVIEW_COUNT
};

void apply_render_environment(Shader_key& key, const Shader_key& environment, const uint32_t force_disable_mask)
{
    for (const Shader_int axis : c_render_environment_int_axes) {
        key.set(axis, environment.get(axis));
    }
    key.set(Shader_bool::LIGHT_SHADING_CLUSTERED, environment.get(Shader_bool::LIGHT_SHADING_CLUSTERED));
    // Scoped to the keys whose shader calls anisotropic_brdf(), as derive()
    // scopes it.
    set_specular_anti_aliasing_axis(key, get_specular_anti_aliasing(environment));
    // derive() ORs the environment bits in before the pass's forced bits are
    // applied, so a pass that forces USE_DDGI, USE_LIGHTMAP or FLAT_DRAW_COLOR
    // off keeps it off.
    for (const Shader_bool axis : {Shader_bool::USE_DDGI, Shader_bool::USE_LIGHTMAP, Shader_bool::FLAT_DRAW_COLOR}) {
        if (environment.get(axis) && ((force_disable_mask & make_shader_bool_mask(axis)) == 0u)) {
            key.set(axis, true);
        }
    }
}

} // anonymous namespace

void Forward_renderer::prepare(const Prepare_parameters& parameters, Render_bucket_snapshot& out) const
{
    ERHE_PROFILE_FUNCTION();

    out.clear();
    out.force_disable_mask = parameters.shader_key_boolean_mask_force_disable;
    for (const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes : parameters.mesh_spans) {
        if (!meshes.empty()) {
            out.has_meshes = true;
            break;
        }
    }
    if (!out.has_meshes) {
        return;
    }

    // Only SHADER_DEBUG of the environment changes what derive() produces
    // beyond copying the axes; the render-phase axes are applied when the
    // snapshot is drawn (apply_render_environment()).
    Shader_key environment_key{};
    environment_key.set(Shader_int::SHADER_DEBUG, static_cast<uint32_t>(parameters.shader_debug)); // TODO proper conversion

    // Leaf-function scratch (AGENTS.md: function-local thread_local buffers):
    // nothing below re-enters prepare(). The entries hold raw Mesh pointers
    // only while this call runs.
    static thread_local Render_bucket_list s_buckets;
    s_buckets.clear();
    for (const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes : parameters.mesh_spans) {
        bucket_primitives(
            s_buckets,
            parameters.shader_key_boolean_mask_force_enable,
            parameters.shader_key_boolean_mask_force_disable,
            m_mesh_memory,
            environment_key,
            meshes,
            parameters.filter,
            parameters.primitive_mode,
            parameters.blending_mode_policy,
            erhe::primitive::Mesh_variant::optimized,
            parameters.shader_debug_filter
        );
    }

    for (const Render_bucket& bucket : s_buckets.get_buckets()) {
        ERHE_VERIFY(!bucket.entries.empty());
        Prepared_bucket& prepared = out.add_bucket();
        prepared.buffer_set.vertex_input_key = bucket.buffer_set.vertex_input_key;
        prepared.buffer_set.index_buffer     = bucket.buffer_set.index_buffer;
        prepared.buffer_set.vertex_buffers.insert(
            prepared.buffer_set.vertex_buffers.end(),
            bucket.buffer_set.vertex_buffers.begin(),
            bucket.buffer_set.vertex_buffers.end()
        );
        prepared.shader_key           = bucket.shader_key;
        prepared.negative_determinant = bucket.negative_determinant;
        prepared.double_sided         = bucket.double_sided;
        prepared.primitive_mode       = parameters.primitive_mode;
        prepared.first_entry          = out.draw_commands.size();
        prepared.entry_count          = bucket.entries.size();
        m_primitive_buffer.append_records(bucket, parameters.material_source, parameters.primitive_mode, parameters.primitive_settings, out.primitive_records);
        m_draw_indirect_buffer.append_commands(bucket, parameters.primitive_mode, out.draw_commands);
        ERHE_VERIFY(out.draw_commands.size() == prepared.first_entry + prepared.entry_count);
    }
    s_buckets.clear();
}

void Forward_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    prepare(
        Prepare_parameters{
            .mesh_spans                            = parameters.mesh_spans,
            .material_source                       = parameters.base.material_source,
            .shader_key_boolean_mask_force_enable  = parameters.base.shader_key_boolean_mask_force_enable,
            .shader_key_boolean_mask_force_disable = parameters.base.shader_key_boolean_mask_force_disable,
            .blending_mode_policy                  = parameters.blending_mode_policy,
            .primitive_mode                        = parameters.primitive_mode,
            .primitive_settings                    = parameters.primitive_settings,
            .filter                                = parameters.filter,
            .shader_debug                          = parameters.shader_debug,
            .shader_debug_filter                   = parameters.shader_debug_filter
        },
        m_immediate_snapshot
    );
    render(
        Draw_parameters{
            .base                   = parameters.base,
            .base_render_pipelines  = parameters.base_render_pipelines,
            .shadow_filter          = parameters.shadow_filter,
            .shadow_bias            = parameters.shadow_bias,
            .shadow_technique       = parameters.shadow_technique,
            .shadow_depth_bits      = parameters.shadow_depth_bits,
            .specular_anti_aliasing = parameters.specular_anti_aliasing,
            .debug_joint_indices    = parameters.debug_joint_indices,
            .debug_joint_colors     = parameters.debug_joint_colors,
            .debug_target_joint     = parameters.debug_target_joint,
            .shader_stages_override = parameters.shader_stages_override,
            .color_blend_override   = parameters.color_blend_override
        },
        m_immediate_snapshot
    );
}

void Forward_renderer::render(const Draw_parameters& parameters, const Render_bucket_snapshot& snapshot)
{
    ERHE_PROFILE_FUNCTION();

    // Same early out as before the bucketing moved to prepare(): no meshes in
    // any span, no pass.
    if (!snapshot.has_meshes) {
        return;
    }

    const Base_render_parameters& base = parameters.base;

    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    Scene_pass_resources::Pass_state pass_state = m_pass_resources.begin_pass(base, parameters.debug_joint_indices, parameters.debug_joint_colors, parameters.debug_target_joint);

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;

    const Light_layer_partition partition = get_light_layer_partition(base);

    Shader_key environment_key{};
    set_light_partition_axes(environment_key, partition);
    environment_key.set(Shader_int::SHADOW_FILTER,                            parameters.shadow_filter);
    environment_key.set(Shader_int::SHADOW_BIAS,                              parameters.shadow_bias);
    environment_key.set(Shader_int::SHADOW_TECHNIQUE,                         parameters.shadow_technique);
    environment_key.set(Shader_int::SHADOW_DEPTH_BITS,                        parameters.shadow_depth_bits);
    environment_key.set(Shader_int::SPECULAR_ANTI_ALIASING,                   static_cast<uint32_t>(parameters.specular_anti_aliasing));
    // SHADER_MULTIVIEW_COUNT: 0 for single-view (views.size() == 1),
    // N for multiview (views.size() >= 2). This matches the prewarm
    // convention (multiview_view_counts uses 0u for the single-view
    // bucket) so the runtime get() lookup hits the prewarmed entry.
    const uint16_t shader_multiview_count = (base.views.size() >= 2) ? static_cast<uint16_t>(base.views.size()) : uint16_t{0};
    environment_key.set(Shader_int::SHADER_MULTIVIEW_COUNT,                   shader_multiview_count);
    environment_key.set(Shader_bool::USE_DDGI,                                m_pass_resources.get_ddgi().is_valid());
    environment_key.set(Shader_bool::USE_LIGHTMAP,                            m_pass_resources.get_lightmap_texture() != nullptr);
    environment_key.set(Shader_bool::FLAT_DRAW_COLOR,                         parameters.flat_draw_color);

    const std::size_t                           record_size = m_program_interface.primitive_interface.primitive_struct.get_size_bytes();
    const std::span<const Prepared_bucket> buckets     = snapshot.get_buckets();

    for (auto* render_pipeline_state : parameters.base_render_pipelines) {
        erhe::graphics::Scoped_debug_group pipeline_scope{
            render_encoder.get_command_buffer(),
            render_pipeline_state->data.debug_label
        };

        // Buckets draw in Blending_class order (opaque, alpha tested,
        // translucent), so a pass taking several classes (allow_all) draws
        // every discarding bucket after every opaque one; a pass taking one
        // class walks the list once per class and skips the others.
        for (const erhe::primitive::Blending_class blending_class : erhe::primitive::blending_class_draw_order) {
            for (std::size_t bucket_index = 0, end = buckets.size(); bucket_index < end; ++bucket_index) {
                const Prepared_bucket&    bucket       = buckets[bucket_index];
                if (bucket.shader_key.get_blending_class() != blending_class) {
                    continue;
                }
                const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(bucket.buffer_set.vertex_input_key);

                ERHE_VERIFY(bucket.entry_count > 0);

                Shader_key shader_key = bucket.shader_key;
                apply_render_environment(shader_key, environment_key, snapshot.force_disable_mask);

                const erhe::graphics::Shader_stages* shader_stages = parameters.shader_stages_override;
                if (shader_stages == nullptr) {
                    const erhe::graphics::Reloadable_shader_stages* reloadable_shader_stages = m_shader_variant_cache.get(
                        shader_key,
                        &vertex_input.vertex_format
                    );
                    if (reloadable_shader_stages == nullptr) {
                        log_draw->warn(
                            "No shader variant for bucket {} ({} primitives, {} vertex streams): {}",
                            bucket_index,
                            bucket.entry_count,
                            bucket.buffer_set.vertex_buffers.size(),
                            shader_key.describe()
                        );
                        continue;
                    }
                    shader_stages = &reloadable_shader_stages->shader_stages;
                }

                // TODO Implement other blending modes
                // Mirrored (negative determinant) buckets get the front-face-
                // flipped pipeline variant so face culling keeps the same
                // visible faces as for non-mirrored meshes.
                // A pass may override the color-blend state (e.g. the selection
                // stencil-mask pass uses color_writes_disabled to write depth/stencil
                // only); otherwise pick by the bucket's blend mode.
                const erhe::graphics::Color_blend_state* color_blend = (parameters.color_blend_override != nullptr)
                    ? parameters.color_blend_override
                    : (blending_class == erhe::primitive::Blending_class::translucent)
                        ? &erhe::graphics::Color_blend_state::color_blend_premultiplied
                        : &erhe::graphics::Color_blend_state::color_blend_disabled;
                erhe::graphics::Render_pipeline* render_pipeline = render_pipeline_state->get_pipeline_for(
                    base.render_pass->get_descriptor(),
                    color_blend,
                    shader_stages,
                    vertex_input.vertex_input.get(),
                    &vertex_input.vertex_format,
                    bucket.negative_determinant,
                    bucket.double_sided
                );
                if (render_pipeline == nullptr) {
                    log_draw->warn(
                        "No render pipeline for bucket {} ({} primitives, {} vertex streams): {}",
                        bucket_index,
                        bucket.entry_count,
                        bucket.buffer_set.vertex_buffers.size(),
                        shader_key.describe()
                    );
                    continue;
                }

                m_bucket_label.clear();
                fmt::format_to(
                    std::back_inserter(m_bucket_label),
                    "bucket {}/{} prims={} streams={} shader key {:016x}",
                    bucket_index,
                    buckets.size(),
                    bucket.entry_count,
                    bucket.buffer_set.vertex_buffers.size(),
                    shader_key.get_hash()
                );
                erhe::graphics::Scoped_debug_group bucket_scope{render_encoder.get_command_buffer(), std::string_view{m_bucket_label}};

                base.render_encoder.set_render_pipeline(*render_pipeline);

                erhe::graphics::Buffer* index_buffer = m_mesh_memory.get_index_buffer(bucket.buffer_set.index_buffer);
                render_encoder.set_index_buffer(index_buffer);
                for (std::size_t stream_index = 0; stream_index < bucket.buffer_set.vertex_buffers.size(); ++stream_index) {
                    erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(bucket.buffer_set.vertex_buffers[stream_index]);
                    render_encoder.set_vertex_buffer(
                        vertex_buffer,
                        0,
                        static_cast<uint32_t>(stream_index)
                    );
                }

                // The records were written against the set this pass binds (D5).
                Ring_buffer_range          primitive_range            = m_primitive_buffer.update(snapshot.get_records(bucket, record_size));
                Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(snapshot.get_draw_commands(bucket));
                ERHE_VERIFY(draw_indirect_buffer_range.draw_indirect_count == bucket.entry_count);
                m_primitive_buffer.bind(render_encoder, primitive_range);
                m_draw_indirect_buffer.bind(render_encoder, draw_indirect_buffer_range.range); // Draw indirect buffer is not indexed, this binds the whole buffer

                render_encoder.multi_draw_indexed_primitives_indirect(
                    render_pipeline_state->data.input_assembly.primitive_topology,
                    m_mesh_memory.get_index_format(bucket.buffer_set.index_buffer),
                    draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                    draw_indirect_buffer_range.draw_indirect_count,
                    sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                );

                primitive_range.release();
                draw_indirect_buffer_range.range.release();
            }
        }
    }

    m_pass_resources.end_pass(pass_state, render_encoder);
}

void Forward_renderer::draw_primitives(
    const Primitive_render_parameters& parameters,
    const erhe::scene::Light_snapshot* light
)
{
    ERHE_PROFILE_FUNCTION();

    const Base_render_parameters& base = parameters.base;
    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    // Static glyph curve data (grid axis labels); bound unconditionally
    // so the shared bind group is always complete.
    m_pass_resources.get_glyph_buffer().bind(render_encoder);

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;

    // Already updated for this frame (D6); this pass binds it and resets
    // nothing. Bound after the light buffer's sampled-image binds below, as
    // the heap bind has always been.
    Material_set& material_set = m_pass_resources.resolve_material_source(base.material_source);

    // draw_primitives() is used for both scene-camera passes (composer
    // fullscreen passes with a Render_context camera) and bare full-
    // screen passes that do not source any state from the Camera UBO
    // (BRDF slice preview, depth-visualization window). The latter
    // group has no camera at all, so we skip the camera buffer bind
    // when views is empty -- the descriptor that came in from a prior
    // pass remains in place, and the shader does not read it.
    std::optional<Ring_buffer_range> camera_range;
    if (base.views.size() >= 2) {
        camera_range = m_pass_resources.get_camera_buffer().update_views(
            base.views,
            base.exposure,
            base.grid_parameters,
            base.sky_parameters,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_pass_resources.get_camera_buffer().bind(render_encoder, camera_range.value());
    } else if (!base.views.empty()) {
        const Camera_view_input& view = base.views[0];
        ERHE_VERIFY(view.camera != nullptr);
        camera_range = m_pass_resources.get_camera_buffer().update(
            *view.camera,
            view.viewport,
            base.exposure,
            base.grid_parameters,
            base.sky_parameters,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_pass_resources.get_camera_buffer().bind(render_encoder, camera_range.value());
    }

    std::optional<Ring_buffer_range> light_control_range{};
    if (light != nullptr) {
        const auto* light_projection_transforms = base.light_projections->get_light_projection_transforms_for_light_id(light->get_id());
        if (light_projection_transforms != nullptr) {
            light_control_range = m_pass_resources.get_light_buffer().update_control(light_projection_transforms->index);
            m_pass_resources.get_light_buffer().bind_control_buffer(render_encoder, light_control_range.value());
        } else {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }

    Light_buffer_ranges light_range = m_pass_resources.get_light_buffer().update(base.light_projections, base.ambient_light, m_pass_resources.get_lightmap_bicubic() ? 1u : 0u, &m_pass_resources.get_ddgi());
    m_pass_resources.get_light_buffer().bind_light_buffer(render_encoder, light_range);
    m_pass_resources.get_light_buffer().bind_shadow_samplers(render_encoder, base.light_projections);
    m_pass_resources.get_light_buffer().bind_lightmap(render_encoder, m_pass_resources.get_lightmap_texture());
    m_pass_resources.get_light_buffer().bind_ddgi(
        render_encoder,
        m_pass_resources.get_ddgi_irradiance_texture(),
        m_pass_resources.get_ddgi_distance_texture(),
        m_pass_resources.get_ddgi_probe_data_texture()
    );

    material_set.bind(render_encoder);

    const erhe::graphics::Base_render_pipeline_create_info& pipeline = parameters.base_render_pipeline.data;
    erhe::graphics::Scoped_debug_group pass_scope{
        render_encoder.get_command_buffer(),
        pipeline.debug_label
    };

    const Vertex_input_entry& empty_vertex_input = m_mesh_memory.get_empty_vertex_input();

    ERHE_VERIFY(base.render_pass != nullptr);
    erhe::graphics::Render_pipeline* render_pipeline = parameters.base_render_pipeline.get_pipeline_for(
        base.render_pass->get_descriptor(),
        parameters.color_blend,
        parameters.shader_stages,
        empty_vertex_input.vertex_input.get(),
        &empty_vertex_input.vertex_format
    );
    if (render_pipeline != nullptr) {
        render_encoder.set_render_pipeline(*render_pipeline);
        render_encoder.set_viewport_rect  (base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);
        render_encoder.set_scissor_rect   (base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);
        render_encoder.draw_primitives    (pipeline.input_assembly.primitive_topology, 0, parameters.vertex_count);
    }

    light_range.release();

    if (light_control_range.has_value()) {
        light_control_range.value().release();
    }
    if (camera_range.has_value()) {
        camera_range.value().release();
    }

    // R9: bind / unbind stay per pass.
    material_set.unbind(render_encoder.get_command_buffer());
}

auto Forward_renderer::prewarm_standard_variants(const Prewarm_parameters& parameters) -> std::size_t
{
    ERHE_PROFILE_FUNCTION();

    // Empty multiview_view_counts means "single view" (SHADER_MULTIVIEW_COUNT = 0),
    // which matches the runtime views.size() == 1 single-camera path.
    static constexpr uint32_t single_view_default[] = { 0u };
    const std::span<const uint32_t> view_counts = parameters.multiview_view_counts.empty()
        ? std::span<const uint32_t>{single_view_default}
        : parameters.multiview_view_counts;

    std::size_t pipeline_warmup_count = 0;

    for (erhe::graphics::Base_render_pipeline* render_pipeline_state : parameters.render_pipeline_states) {
        if (render_pipeline_state == nullptr) {
            continue;
        }

        for (const uint32_t view_count : view_counts) {
            // Mirrors the environment_key block in render() above so the
            // per-primitive Shader_key::derive sees the same light counts +
            // multiview width the runtime would.
            Shader_key environment_key{};
            set_light_partition_axes(environment_key, parameters.light_partition);
            environment_key.set(Shader_int::SHADER_DEBUG,                             static_cast<uint32_t>(parameters.shader_debug));
            environment_key.set(Shader_int::SHADOW_FILTER,                            parameters.shadow_filter);
            environment_key.set(Shader_int::SHADOW_BIAS,                              parameters.shadow_bias);
            environment_key.set(Shader_int::SHADOW_TECHNIQUE,                         parameters.shadow_technique);
            environment_key.set(Shader_int::SHADOW_DEPTH_BITS,                        parameters.shadow_depth_bits);
            environment_key.set(Shader_int::SPECULAR_ANTI_ALIASING,                   static_cast<uint32_t>(parameters.specular_anti_aliasing));
            environment_key.set(Shader_int::SHADER_MULTIVIEW_COUNT,                   view_count);

            Render_bucket_list buckets;
            for (const auto& meshes : parameters.mesh_spans) {
                bucket_primitives(
                    buckets,
                    parameters.shader_key_force_enable_mask,
                    parameters.shader_key_force_disable_mask,
                    parameters.mesh_memory,
                    environment_key,
                    meshes,
                    {}, // TODO
                    parameters.primitive_mode,
                    parameters.blending_mode_policy,
                    erhe::primitive::Mesh_variant::optimized
                );
            }

            for (const Render_bucket& bucket : buckets.get_buckets()) {
                const Vertex_input_entry& vertex_input = parameters.mesh_memory.get_vertex_input(bucket.buffer_set.vertex_input_key);

                // Phase 1: force shader-module compile (glslang -> SPIR-V ->
                // vkCreateShaderModule) for this variant.
                const erhe::graphics::Reloadable_shader_stages* reloadable_shader_stages = m_shader_variant_cache.get(
                    bucket.shader_key,
                    &vertex_input.vertex_format
                );
                if (reloadable_shader_stages == nullptr) {
                    continue;
                }

                if (parameters.warmup_targets.empty()) {
                    continue;
                }

                // Phase 2: populate the driver-level VkPipelineCache for every
                // (Render_pipeline_create_info, bucket-variant-key) tuple whose
                // view_count matches this iteration. Both winding variants are
                // warmed regardless of the bucket's determinant: mirroring a
                // mesh is an interactive editor operation, and warming the
                // flipped sibling avoids a first-mirrored-mesh pipeline-compile
                // hitch (see Base_render_pipeline::get_pipeline_for
                // front_face_flip).
                for (const Warmup_target& target : parameters.warmup_targets) {
                    if (target.view_count != view_count) {
                        continue;
                    }
                    for (const bool front_face_flip : { false, true }) {
                        erhe::graphics::Render_pipeline_create_info ci{
                            .base          = render_pipeline_state->data,
                            .shader_stages = &reloadable_shader_stages->shader_stages,
                            .vertex_input  = vertex_input.vertex_input.get(),
                            .vertex_format = &vertex_input.vertex_format
                        };
                        if (front_face_flip) {
                            ci.base.rasterization = ci.base.rasterization.with_winding_flip();
                        }
                        ci.color_attachment_count    = target.color_attachment_count;
                        ci.color_attachment_formats  = target.color_attachment_formats;
                        ci.depth_attachment_format   = target.depth_attachment_format;
                        ci.stencil_attachment_format = target.stencil_attachment_format;
                        ci.sample_count              = target.sample_count;
                        {
                            ERHE_PROFILE_SCOPE("warmup_render_pipeline");
                            m_graphics_device.warmup_render_pipeline(ci);
                        }
                        ++pipeline_warmup_count;
                    }
                }
            }

            // Content-library materials whose meshes are not yet attached:
            // key each against the fallback Vertex_formats with both
            // mesh_has_skin = false and mesh_has_skin = true so a later
            // runtime mesh-attach hits the cache. The optimized formats are
            // warmed beside the content ones: they can differ in the position
            // encoding (quantized optimized variant) and in the attribute
            // gates (no facet id), so a fill draw resolved to the optimized
            // variant derives its own key. When the formats collapse to the
            // same key the extra get() is a cache hit.
            for (const std::shared_ptr<erhe::primitive::Material>& material : parameters.extra_materials) {
                if (!material) {
                    continue;
                }
                for (const bool has_skin : { false, true }) {
                    const erhe::dataformat::Vertex_format* const vertex_formats[] = {
                        has_skin ? &parameters.mesh_memory.vertex_format_skinned           : &parameters.mesh_memory.vertex_format_not_skinned,
                        has_skin ? &parameters.mesh_memory.vertex_format_skinned_optimized : &parameters.mesh_memory.vertex_format_not_skinned_optimized
                    };
                    for (const erhe::dataformat::Vertex_format* const vertex_format : vertex_formats) {
                        ERHE_PROFILE_SCOPE("extra material variant");
                        Shader_key derived{};
                        {
                            ERHE_PROFILE_SCOPE("Shader_key::derive");
                            derived = environment_key.derive(material.get(), vertex_format, has_skin);
                        }
                        derived.bool_mask |=  parameters.shader_key_force_enable_mask;
                        derived.bool_mask &= ~parameters.shader_key_force_disable_mask;
                        // After the force masks, as the draw path applies it.
                        set_specular_anti_aliasing_axis(derived, get_specular_anti_aliasing(environment_key));

                        static_cast<void>(m_shader_variant_cache.get(derived, vertex_format));
                    }
                }
            }
        }
    }

    return pipeline_warmup_count;
}

} // namespace erhe::scene_renderer
