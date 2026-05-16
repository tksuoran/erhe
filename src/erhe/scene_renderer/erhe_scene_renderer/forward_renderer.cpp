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

#include <functional>

namespace erhe::scene_renderer {

const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> Forward_renderer::empty_mesh_spans{};

Forward_renderer::Forward_renderer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    Mesh_memory&                    mesh_memory,
    Program_interface&              program_interface,
    Shader_variant_cache&           shader_variant_cache
)
    : m_graphics_device     {graphics_device}
    , m_mesh_memory         {mesh_memory}   
    , m_program_interface   {program_interface}
    , m_shader_variant_cache{shader_variant_cache}
    , m_camera_buffer       {graphics_device, program_interface.camera_interface}
    , m_draw_indirect_buffer{graphics_device, program_interface.config.max_draw_count}
    , m_joint_buffer        {graphics_device, program_interface.joint_interface}
    , m_light_buffer        {graphics_device, init_command_buffer, program_interface.light_interface}
    , m_material_buffer     {graphics_device, program_interface.material_interface}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Forward_renderer::m_fallback_sampler"
        }
    }
    , m_dummy_texture{graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_srgb)}
    , m_texture_heap{
        std::make_unique<erhe::graphics::Texture_heap>(
            m_graphics_device,
            *m_dummy_texture.get(),
            m_fallback_sampler,
            m_program_interface.bind_group_layout.get()
        )
    }
{
}

Forward_renderer::~Forward_renderer() noexcept = default;

static constexpr std::string_view c_forward_renderer_render{"Forward_renderer::render()"};

namespace {

const char* safe_str(const char* str)
{
    return str != nullptr ? str : "";
}

}

void Forward_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    const Base_render_parameters& base = parameters.base;

    // Check for early out
    bool all_empty = true;
    for (const auto& meshes : parameters.mesh_spans) {
        if (!meshes.empty()) {
            all_empty = false;
            break;
        }
    }
    if (all_empty) {
        log_render->debug("Forward_renderer::render({}) - empty", base.debug_label);
    }

    log_render->debug("Forward_renderer::render({})", base.debug_label);

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& filter     = parameters.filter;

    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    std::optional<Ring_buffer_range> camera_buffer_range{};

    // TODO Single path ERHE_VERIFY(!parameters.views.empty());
    if (!base.views.empty()) {
        // Multiview path: write one Camera UBO entry per view. The
        // exposure/grid/frame state is shared across views; FOV and
        // pose come from each Camera_view_input. Shaders compiled with
        // enable_multiview() pick the per-eye entry via gl_ViewIndex.
        const float exposure = (base.camera != nullptr) ? base.camera->get_exposure() : 1.0f;
        camera_buffer_range = m_camera_buffer.update_views(
            base.views,
            exposure,
            base.grid_size,
            base.grid_line_width,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_camera_buffer.bind(render_encoder, camera_buffer_range.value());
    } 
    else if (base.camera != nullptr) {
        camera_buffer_range = m_camera_buffer.update(
            *base.camera->projection(),
            *base.camera->get_node(),
            base.viewport,
            base.camera->get_exposure(),
            base.grid_size,
            base.grid_line_width,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_camera_buffer.bind(render_encoder, camera_buffer_range.value());
    }

    m_texture_heap->reset_heap(render_encoder.get_command_buffer());

    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), base.materials);
    m_material_buffer.bind(render_encoder, material_range);

    Ring_buffer_range joint_range = m_joint_buffer.update(parameters.debug_joint_indices, parameters.debug_joint_colors, base.skins);
    m_joint_buffer.bind(render_encoder, joint_range);

    // This must be done even if lights is empty.
    // For example, the number of lights is read from the light buffer.
    Ring_buffer_range light_range = m_light_buffer.update(base.lights, base.light_projections, base.ambient_light);
    m_light_buffer.bind_light_buffer(render_encoder, light_range);
    m_light_buffer.bind_shadow_samplers(render_encoder, base.light_projections);

    m_texture_heap->bind(render_encoder);

    for (auto* render_pipeline_state : parameters.base_render_pipelines) {
        erhe::graphics::Scoped_debug_group pipeline_scope{
            render_encoder.get_command_buffer(),
            render_pipeline_state->data.debug_label
        };

        render_encoder.set_viewport_rect(base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);
        render_encoder.set_scissor_rect (base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);

        const Light_layer_partition partition = compute_light_layer_partition(base.lights);

        Shader_key environment_key{};
        environment_key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED, static_cast<uint32_t>(partition.per_type_nonshadow[0]));
        environment_key.set(Shader_int::LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED,     static_cast<uint32_t>(partition.per_type_shadow   [0]));
        environment_key.set(Shader_int::LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED,        static_cast<uint32_t>(partition.per_type_nonshadow[1]));
        environment_key.set(Shader_int::LIGHT_COUNT_SPOT_SHADOWMAPPED,            static_cast<uint32_t>(partition.per_type_shadow   [1]));
        environment_key.set(Shader_int::LIGHT_COUNT_POINT_NOT_SHADOWMAPPED,       static_cast<uint32_t>(partition.per_type_nonshadow[2]));
        environment_key.set(Shader_int::LIGHT_COUNT_POINT_SHADOWMAPPED,           static_cast<uint32_t>(partition.per_type_shadow   [2]));
        environment_key.set(Shader_int::SHADER_DEBUG,                             static_cast<uint32_t>(parameters.shader_debug)); // TODO proper conversion
        environment_key.set(Shader_int::SHADER_MULTIVIEW_COUNT,                   static_cast<uint16_t>(base.views.size()));

        std::vector<Render_bucket> buckets;
        for (const auto& meshes : mesh_spans) {
            bucket_primitives(
                buckets,
                base.shader_key_boolean_mask_force_enable,
                base.shader_key_boolean_mask_force_disable,
                m_mesh_memory,
                environment_key,
                meshes,
                parameters.primitive_mode,
                Variant_signature_policy::require_exact_variant_signature
            );
        }

        for (std::size_t bucket_index = 0, end = buckets.size(); bucket_index < end; ++bucket_index) {
            const Render_bucket&      bucket       = buckets[bucket_index];
            const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(bucket.buffer_set.vertex_input_key);

            const erhe::graphics::Reloadable_shader_stages* reloadable_shader_stages = m_shader_variant_cache.get(
                bucket.shader_key,
                &vertex_input.vertex_format
            );
            if (reloadable_shader_stages == nullptr) {
                log_draw->warn(
                    "No shader variant for bucket {} ({} primitives, {} vertex streams): {}",
                    bucket_index,
                    bucket.entries.size(),
                    bucket.buffer_set.vertex_buffers.size(),
                    bucket.shader_key.describe()
                );
                continue;
            }
            const erhe::graphics::Shader_stages* shader_stages = &reloadable_shader_stages->shader_stages;

            erhe::graphics::Render_pipeline* render_pipeline = render_pipeline_state->get_pipeline_for(
                base.render_pass->get_descriptor(),
                shader_stages,
                vertex_input.vertex_input.get(),
                &vertex_input.vertex_format
            );
            if (render_pipeline == nullptr) {
                log_draw->warn(
                    "No render pipeline for bucket {} ({} primitives, {} vertex streams): {}",
                    bucket_index,
                    bucket.entries.size(),
                    bucket.buffer_set.vertex_buffers.size(),
                    bucket.shader_key.describe()
                );
                continue;
            }

           erhe::graphics::Scoped_debug_group bucket_scope{
                render_encoder.get_command_buffer(),
                erhe::utility::Debug_label{
                    fmt::format(
                        "bucket {}/{} prims={} streams={} {}",
                        bucket_index,
                        buckets.size(),
                        bucket.entries.size(),
                        bucket.buffer_set.vertex_buffers.size(),
                        bucket.shader_key.describe()
                    )
                }
            };

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

            std::size_t primitive_count{0};
            Ring_buffer_range primitive_range = m_primitive_buffer.update(bucket, parameters.primitive_mode, filter, parameters.primitive_settings, primitive_count);
            if (primitive_count == 0){
                primitive_range.cancel();
                continue;
            }
            Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(bucket, parameters.primitive_mode, filter);
            if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                primitive_range.cancel();
                draw_indirect_buffer_range.range.cancel();
                continue;
            }
            ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
            m_primitive_buffer.bind(render_encoder, primitive_range);
            m_draw_indirect_buffer.bind(render_encoder, draw_indirect_buffer_range.range); // Draw indirect buffer is not indexed, this binds the whole buffer

            render_encoder.multi_draw_indexed_primitives_indirect(
                erhe::graphics::Primitive_type::triangle, // render_pipeline->input_assembly.primitive_topology,
                m_mesh_memory.get_index_format(bucket.buffer_set.index_buffer),
                draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                draw_indirect_buffer_range.draw_indirect_count,
                sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
            );

            primitive_range.release();
            draw_indirect_buffer_range.range.release();
        }
    }

    // These must come after the draw calls have been done
    if (camera_buffer_range.has_value()) {
        camera_buffer_range.value().release();
    }
    material_range.release();
    joint_range.release();
    light_range.release();

    m_texture_heap->unbind(render_encoder.get_command_buffer());
}

void Forward_renderer::draw_primitives(
    const Primitive_render_parameters& parameters,
    const erhe::scene::Light*          light
)
{
    ERHE_PROFILE_FUNCTION();

    const Base_render_parameters& base = parameters.base;
    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    m_texture_heap->reset_heap(render_encoder.get_command_buffer());

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), base.materials);
    if (material_range.get_buffer() != nullptr) {
        m_material_buffer.bind(render_encoder, material_range);
    }

    std::optional<Ring_buffer_range> camera_range;
    if (!base.views.empty()) {
        const float exposure = (base.camera != nullptr) ? base.camera->get_exposure() : 1.0f;
        camera_range = m_camera_buffer.update_views(
            base.views,
            exposure,
            base.grid_size,
            base.grid_line_width,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_camera_buffer.bind(render_encoder, camera_range.value());
    }
    else if (base.camera != nullptr) {
        camera_range = m_camera_buffer.update(
            *base.camera->projection(),
            *base.camera->get_node(),
            base.viewport,
            base.camera->get_exposure(),
            base.grid_size,
            base.grid_line_width,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
        m_camera_buffer.bind(render_encoder, camera_range.value());
    }

    std::optional<Ring_buffer_range> light_control_range{};
    if (light != nullptr) {
        const auto* light_projection_transforms = base.light_projections->get_light_projection_transforms_for_light(light);
        if (light_projection_transforms != nullptr) {
            light_control_range = m_light_buffer.update_control(light_projection_transforms->index);
            m_light_buffer.bind_control_buffer(render_encoder, light_control_range.value());
        } else {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }

    Ring_buffer_range light_range = m_light_buffer.update(base.lights, base.light_projections, base.ambient_light);
    m_light_buffer.bind_light_buffer(render_encoder, light_range);
    m_light_buffer.bind_shadow_samplers(render_encoder, base.light_projections);

    m_texture_heap->bind(render_encoder);

    const erhe::graphics::Base_render_pipeline_create_info& pipeline = parameters.base_render_pipeline.data;
    erhe::graphics::Scoped_debug_group pass_scope{
        render_encoder.get_command_buffer(),
        pipeline.debug_label
    };

    const Vertex_input_entry& empty_vertex_input = m_mesh_memory.get_empty_vertex_input();

    ERHE_VERIFY(base.render_pass != nullptr);
    erhe::graphics::Render_pipeline* render_pipeline = parameters.base_render_pipeline.get_pipeline_for(
        base.render_pass->get_descriptor(),
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

    material_range.release();
    light_range.release();

    if (light_control_range.has_value()) {
        light_control_range.value().release();
    }
    if (camera_range.has_value()) {
        camera_range.value().release();
    }

    m_texture_heap->unbind(render_encoder.get_command_buffer());
}

auto Forward_renderer::prewarm_standard_variants(const Prewarm_parameters& parameters) -> std::size_t
{
    static_cast<void>(parameters);
    return 0;
#if 0
    ERHE_PROFILE_FUNCTION();

    if (parameters.standard_shader_variants == nullptr) {
        return 0;
    }
    if (parameters.multiview_view_counts.empty()) {
        return 0;
    }

    std::size_t warmup_count = 0;

    // Materializes one VkPipeline (on Vulkan) per (lazy pipeline, variant,
    // matching Warmup_target) tuple. Shadows the resolved shader_stages
    // into a Render_pipeline_create_info copy along with the target's
    // format/usage fields, then drives Device::warmup_render_pipeline.
    auto warmup_pipeline_variants =
        [this, &parameters, &warmup_count](
            const erhe::graphics::Render_pipeline_create_info& base_create_info,
            const Standard_variant_key&                        key,
            const erhe::graphics::Shader_stages*               variant_shader_stages
        ) {
            if (parameters.warmup_targets.empty() || (variant_shader_stages == nullptr)) {
                return;
            }
            for (const Warmup_target& target : parameters.warmup_targets) {
                if (target.view_count != key.multiview_view_count) {
                    continue;
                }
                erhe::graphics::Render_pipeline_create_info ci = base_create_info;
                // The runtime forward path swaps shader_stages on the
                // Render_pipeline_create_info via the variant cache;
                // mirror that here by writing the resolved variant into
                // the local copy.
                ci.shader_stages             = variant_shader_stages;
                ci.color_attachment_count    = target.color_attachment_count;
                ci.color_attachment_formats  = target.color_attachment_formats;
                ci.depth_attachment_format   = target.depth_attachment_format;
                ci.stencil_attachment_format = target.stencil_attachment_format;
                ci.color_usage_before        = target.color_usage_before;
                ci.color_usage_after         = target.color_usage_after;
                ci.depth_usage_before        = target.depth_usage_before;
                ci.depth_usage_after         = target.depth_usage_after;
                ci.sample_count              = target.sample_count;
                m_graphics_device.warmup_render_pipeline(ci);
                ++warmup_count;
            }
        };

    // Per-pipeline bucket walk. Mirrors the gate Forward_renderer::render
    // applies before consulting the variant cache (uses_standard_variants
    // && vertex_format != nullptr); pipelines that fail the gate would
    // never call get_or_compile at runtime, so prewarming them would only
    // pollute the cache with keys the renderer never asks for.
    for (erhe::graphics::Lazy_render_pipeline* render_pipeline_state : parameters.render_pipeline_states) {
        if (render_pipeline_state == nullptr) {
            continue;
        }
        const erhe::graphics::Render_pipeline_create_info& pipeline = render_pipeline_state->data;
        if (!pipeline.uses_standard_variants) {
            continue;
        }
        if (pipeline.vertex_format == nullptr) {
            continue;
        }

        for (const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes : parameters.mesh_spans) {
            if (meshes.empty()) {
                continue;
            }
            const std::vector<Bucket> buckets = bucket_primitives_by_buffers_and_variant(
                meshes,
                parameters.primitive_mode,
                pipeline.vertex_format
            );
            for (const Bucket& bucket : buckets) {
                Standard_variant_key key = compute_bucket_variant_key(bucket, parameters.light_counts);
                for (const uint32_t view_count : parameters.multiview_view_counts) {
                    key.multiview_view_count = view_count;
                    const erhe::graphics::Shader_stages* stages =
                        parameters.standard_shader_variants->get_or_compile(key);
                    warmup_pipeline_variants(pipeline, key, stages);
                }
            }
        }
    }

    // Content-library / unattached materials. Each material is keyed
    // against fallback_vertex_format; the editor doesn't know up-front
    // whether the user will apply the material to a static mesh or a
    // skinned mesh, so we prewarm both. When the format carries no joint
    // attributes, compute_standard_variant_key collapses
    // mesh_has_skin=true to the same key (the skinning bit also requires
    // joint_indices+joint_weights), so the second get_or_compile lands
    // on the cache hit path and costs nothing. Skipped when
    // fallback_vertex_format is null.
    //
    // Pipeline warmup for content-library materials needs a representative
    // pipeline_create_info to copy state from. We pick the first pipeline
    // in render_pipeline_states that opts into standard variants; that
    // pipeline's state (rasterization, blend, depth_stencil, vertex_input)
    // is the same the runtime will use because all polygon_fill standard
    // pipelines share that state set in the editor.
    if (!parameters.extra_materials.empty()) {
        const erhe::graphics::Render_pipeline_create_info* representative_pipeline = nullptr;
        if (!parameters.warmup_targets.empty()) {
            for (erhe::graphics::Lazy_render_pipeline* render_pipeline_state : parameters.render_pipeline_states) {
                if (render_pipeline_state == nullptr) {
                    continue;
                }
                if (!render_pipeline_state->data.uses_standard_variants) {
                    continue;
                }
                representative_pipeline = &render_pipeline_state->data;
                break;
            }
        }

        for (const std::shared_ptr<erhe::primitive::Material>& material : parameters.extra_materials) {
            if (!material) {
                continue;
            }
            Standard_variant_key key_unskinned = compute_standard_variant_key(
                material.get(),
                parameters.mesh_memory.get_vertex_format(mesh_flag_none),
                /*mesh_has_skin*/ false,
                parameters.light_counts
            );
            Standard_variant_key key_skinned = compute_standard_variant_key(
                material.get(),
                parameters.mesh_memory.get_vertex_format(mesh_flag_skinned),
                /*mesh_has_skin*/ true,
                parameters.light_counts
            );
            for (const uint32_t view_count : parameters.multiview_view_counts) {
                key_unskinned.multiview_view_count = view_count;
                key_skinned  .multiview_view_count = view_count;
                const erhe::graphics::Shader_stages* unskinned_stages =
                    parameters.standard_shader_variants->get_or_compile(key_unskinned);
                const erhe::graphics::Shader_stages* skinned_stages =
                    parameters.standard_shader_variants->get_or_compile(key_skinned);
                if (representative_pipeline != nullptr) {
                    warmup_pipeline_variants(*representative_pipeline, key_unskinned, unskinned_stages);
                    warmup_pipeline_variants(*representative_pipeline, key_skinned,   skinned_stages);
                }
            }
        }
    }

    return warmup_count;
#endif
}

} // namespace erhe::scene_renderer
