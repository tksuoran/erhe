// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/forward_renderer.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/lazy_shader_handle.hpp"
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
#include "erhe_scene_renderer/standard_shader_variant.hpp"
#include "erhe_scene_renderer/standard_shader_variants.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <functional>

namespace erhe::scene_renderer {

const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> Forward_renderer::empty_mesh_spans{};

Forward_renderer::Forward_renderer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    Program_interface&              program_interface
)
    : m_graphics_device     {graphics_device}
    , m_program_interface   {program_interface}
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

// Forward declaration; helper defined below.
[[nodiscard]] auto resolve_pipeline_shader_stages(
    const erhe::graphics::Render_pipeline_create_info& pipeline
) -> const erhe::graphics::Shader_stages*;

auto make_temp_pipeline_state(const erhe::graphics::Render_pipeline_create_info& ci) -> erhe::graphics::Render_pipeline_state
{
    // Resolve the lazy handle here so callers that pass temp_state to
    // set_render_pipeline_state without an explicit override still get
    // the right shader bound. The const_cast strips off the
    // const-qualifier the resolver returns to match
    // Render_pipeline_data's existing non-const Shader_stages* field.
    const erhe::graphics::Shader_stages* resolved = resolve_pipeline_shader_stages(ci);
    return erhe::graphics::Render_pipeline_state{erhe::graphics::Render_pipeline_data{
        .debug_label          = ci.debug_label,
        .shader_stages        = const_cast<erhe::graphics::Shader_stages*>(resolved),
        .vertex_input         = ci.vertex_input,
        .input_assembly       = ci.input_assembly,
        .multisample          = ci.multisample,
        .viewport_depth_range = ci.viewport_depth_range,
        .rasterization        = ci.rasterization,
        .depth_stencil        = ci.depth_stencil,
        .color_blend          = ci.color_blend
    }};
}

// Pipelines may bind a Lazy_shader_handle instead of (or in addition to)
// a raw Shader_stages*. The lazy handle takes priority: it lets a
// pipeline reference a shader whose actual compile is deferred until
// the renderer asks for it. Returns nullptr only when the handle's
// underlying compile fails AND no fallback was wired through; the
// renderer's existing error_shader_stages path then steps in.
[[nodiscard]] auto resolve_pipeline_shader_stages(
    const erhe::graphics::Render_pipeline_create_info& pipeline
) -> const erhe::graphics::Shader_stages*
{
    if (pipeline.lazy_shader_stages != nullptr) {
        return pipeline.lazy_shader_stages->shader_stages();
    }
    return pipeline.shader_stages;
}

// Same idea, but for the multiview-compiled sibling. The handle
// returns nullptr when the pipeline didn't configure a multiview
// view count -- caller treats nullptr as "no multiview sibling for
// this pipeline" exactly as it does for the raw multiview_shader_stages
// field.
[[nodiscard]] auto resolve_pipeline_multiview_shader_stages(
    const erhe::graphics::Render_pipeline_create_info& pipeline
) -> const erhe::graphics::Shader_stages*
{
    if (pipeline.lazy_shader_stages != nullptr) {
        return pipeline.lazy_shader_stages->multiview_shader_stages();
    }
    return pipeline.multiview_shader_stages;
}

class Buffer_set
{
public:
    erhe::graphics::Buffer*              index_buffer{nullptr};
    std::vector<erhe::graphics::Buffer*> vertex_buffers;

    [[nodiscard]] auto valid() const -> bool { return index_buffer != nullptr; }

    [[nodiscard]] auto operator==(const Buffer_set& other) const -> bool
    {
        return index_buffer == other.index_buffer && vertex_buffers == other.vertex_buffers;
    }
};

// Returns the buffer set the first non-empty primitive of `mesh` lives in.
// Asserts that all subsequent primitives of the same mesh share the set --
// today every primitive of a mesh is allocated in one batch from the same
// pools, so this should always hold. If a mesh's primitives ever spill
// across pool blocks the assertion fires loudly and we know a primitive-
// level bucketing pass is needed.
auto peek_mesh_buffers(
    const std::shared_ptr<erhe::scene::Mesh>& mesh,
    const erhe::primitive::Primitive_mode     primitive_mode
) -> Buffer_set
{
    Buffer_set result;
    if (!mesh) {
        return result;
    }
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
        const erhe::primitive::Primitive* primitive = mesh_primitive.primitive.get();
        if (primitive == nullptr) {
            continue;
        }
        const erhe::primitive::Buffer_mesh* buffer_mesh = primitive->get_renderable_mesh();
        if (buffer_mesh == nullptr) {
            continue;
        }
        if (buffer_mesh->index_range(primitive_mode).index_count == 0) {
            continue;
        }
        if (!result.valid()) {
            result.index_buffer = buffer_mesh->index_buffer_range.buffer;
            result.vertex_buffers.reserve(buffer_mesh->vertex_buffer_ranges.size());
            for (const erhe::primitive::Buffer_range& vr : buffer_mesh->vertex_buffer_ranges) {
                result.vertex_buffers.push_back(vr.buffer);
            }
        } else {
            ERHE_VERIFY(buffer_mesh->index_buffer_range.buffer == result.index_buffer);
            ERHE_VERIFY(buffer_mesh->vertex_buffer_ranges.size() == result.vertex_buffers.size());
            for (std::size_t i = 0; i < buffer_mesh->vertex_buffer_ranges.size(); ++i) {
                ERHE_VERIFY(buffer_mesh->vertex_buffer_ranges[i].buffer == result.vertex_buffers[i]);
            }
        }
    }
    return result;
}

class Bucket
{
public:
    Buffer_set                                      key;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
};

auto bucket_meshes_by_buffers(
    const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes,
    const erhe::primitive::Primitive_mode                      primitive_mode
) -> std::vector<Bucket>
{
    std::vector<Bucket> buckets;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        const Buffer_set bs = peek_mesh_buffers(mesh, primitive_mode);
        if (!bs.valid()) {
            continue;
        }
        Bucket* found = nullptr;
        for (Bucket& b : buckets) {
            if (b.key == bs) {
                found = &b;
                break;
            }
        }
        if (found == nullptr) {
            buckets.push_back(Bucket{.key = bs, .meshes = {}});
            found = &buckets.back();
        }
        found->meshes.push_back(mesh);
    }
    return buckets;
}

// Build a Standard_variant_key for an entire bucket: OR every primitive's
// material caps across the bucket, OR the per-mesh has-skin flag, and pair
// with the pipeline's vertex_format and the scene's per-frame light counts.
//
// One bucket emits one MDI, so it must compile a single shader. ORing the
// caps gives a permissive variant -- a primitive whose material doesn't
// actually use, e.g., a normal map runs through the same shader but the
// runtime `if (normal_texture.x != max_u32)` check skips the sampling.
// The optimization vs. today is that variants with all-flags-off omit the
// dead code paths entirely.
[[nodiscard]] auto compute_bucket_variant_key(
    const Bucket&                                                  bucket,
    const erhe::dataformat::Vertex_format&                         vertex_format,
    const erhe::scene_renderer::Standard_variant_light_counts&     light_counts
) -> erhe::scene_renderer::Standard_variant_key
{
    erhe::scene_renderer::Standard_variant_key key{};
    bool                                       has_any_skin = false;
    bool                                       seeded       = false;

    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : bucket.meshes) {
        if (!mesh) {
            continue;
        }
        if (mesh->skin) {
            has_any_skin = true;
        }
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            if (!mesh_primitive.material) {
                continue;
            }
            const erhe::scene_renderer::Standard_variant_key per_primitive =
                erhe::scene_renderer::compute_standard_variant_key(
                    mesh_primitive.material.get(),
                    vertex_format,
                    /*mesh_has_skin*/ false, // skinning OR'd separately below
                    light_counts
                );
            if (!seeded) {
                // Take counts from the first computed key (light_counts
                // are constant across the bucket, but copying once here
                // avoids re-seeding manually).
                key    = per_primitive;
                seeded = true;
            } else {
                key.boolean_mask |= per_primitive.boolean_mask;
            }
        }
    }

    if (!seeded) {
        // No primitive carries a material; build a base key from just the
        // format + light counts so the cache still keys consistently.
        key = erhe::scene_renderer::compute_standard_variant_key(
            /*material*/ nullptr,
            vertex_format,
            /*mesh_has_skin*/ false,
            light_counts
        );
    }

    if (has_any_skin) {
        key.set_boolean(erhe::scene_renderer::Standard_variant_key::Boolean_axis::use_skinning, true);
    }
    return key;
}

}

void Forward_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    // Check for early out
    bool all_empty = true;
    for (const auto& meshes : parameters.mesh_spans) {
        if (!meshes.empty()) {
            all_empty = false;
            break;
        }
    }
    if (all_empty) {
        log_render->debug("Forward_renderer::render({}) - empty", parameters.debug_label);
    }

    log_render->debug("Forward_renderer::render({})", parameters.debug_label);

    const auto& viewport               = parameters.viewport;
    const auto* camera                 = parameters.camera;
    const auto& mesh_spans             = parameters.mesh_spans;
    const auto& lights                 = parameters.lights;
    const auto& skins                  = parameters.skins;
    const auto& materials              = parameters.materials;
    const auto& render_pipeline_states = parameters.render_pipeline_states;
    const auto& filter                 = parameters.filter;
    const auto  primitive_mode         = parameters.primitive_mode;

    parameters.render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    std::optional<Ring_buffer_range> camera_buffer_range{};
    if (!parameters.multiview_views.empty()) {
        // Multiview path: write one Camera UBO entry per view. The
        // exposure/grid/frame state is shared across views; FOV and
        // pose come from each Camera_view_input. Shaders compiled with
        // enable_multiview() pick the per-eye entry via gl_ViewIndex.
        const float exposure = (camera != nullptr) ? camera->get_exposure() : 1.0f;
        camera_buffer_range = m_camera_buffer.update_views(
            parameters.multiview_views,
            exposure,
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number,
            parameters.reverse_depth,
            parameters.depth_range,
            parameters.conventions
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_buffer_range.value());
    } else if (camera != nullptr) {
        camera_buffer_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number,
            parameters.reverse_depth,
            parameters.depth_range,
            parameters.conventions
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_buffer_range.value());
    }

    m_texture_heap->reset_heap(parameters.render_encoder.get_command_buffer());

    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), materials);
    m_material_buffer.bind(parameters.render_encoder, material_range);

    Ring_buffer_range joint_range = m_joint_buffer.update(parameters.debug_joint_indices, parameters.debug_joint_colors, skins);
    m_joint_buffer.bind(parameters.render_encoder, joint_range);

    // This must be done even if lights is empty.
    // For example, the number of lights is read from the light buffer.
    Ring_buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light);
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);
    m_light_buffer.bind_shadow_samplers(parameters.render_encoder, parameters.light_projections);

    m_texture_heap->bind(parameters.render_encoder);

    // On the multiview path each pipeline picks its multiview-compiled
    // sibling shader stages so gl_ViewIndex resolves and the shaders
    // read camera.cameras[gl_ViewIndex] instead of cameras[0]. The
    // selection happens through the override-shader-stages path so the
    // Vulkan pipeline cache compiles a separate VkPipeline keyed on
    // the multiview shader modules; the Lazy_render_pipeline cache is
    // not used because its baked shader_stages is the single-view
    // variant.
    const bool multiview_path = !parameters.multiview_views.empty();

    for (auto* render_pipeline_state : render_pipeline_states) {
        const erhe::graphics::Render_pipeline_create_info& pipeline = render_pipeline_state->data;
        bool use_override_shader_stages = (parameters.override_shader_stages != nullptr);
        const erhe::graphics::Shader_stages* multiview_stages =
            multiview_path ? resolve_pipeline_multiview_shader_stages(pipeline) : nullptr;
        if (multiview_stages != nullptr) {
            use_override_shader_stages = true;
        }
        const erhe::graphics::Shader_stages* pipeline_shader_stages = resolve_pipeline_shader_stages(pipeline);
        if ((pipeline_shader_stages == nullptr) && !use_override_shader_stages) {
            continue;
        }

        auto* used_shader_stages =
            (parameters.override_shader_stages != nullptr) ? parameters.override_shader_stages :
            (multiview_stages != nullptr)                  ? multiview_stages :
                                                             pipeline_shader_stages;
        if (!used_shader_stages->is_valid()) {
            use_override_shader_stages = true;
            used_shader_stages = parameters.error_shader_stages;
        }

        //erhe::graphics::Scoped_debug_group pass_scope{"Forward_renderer::render() pass"};
        // Pipeline-level debug group recorded into the same cb the encoder
        // is writing to so RenderDoc nests draw calls under it (the
        // single-arg ctor on Vulkan only emits a queue-level label which
        // captures show separated from the cb's commands).
        erhe::graphics::Scoped_debug_group pipeline_scope{
            parameters.render_encoder.get_command_buffer(),
            pipeline.debug_label
        };

        if (use_override_shader_stages) {
            erhe::graphics::Render_pipeline_state temp_state = make_temp_pipeline_state(pipeline);
            parameters.render_encoder.set_render_pipeline_state(temp_state, used_shader_stages);
        } else if (parameters.render_pass != nullptr) {
            erhe::graphics::Render_pipeline* p = render_pipeline_state->get_pipeline_for(parameters.render_pass->get_descriptor());
            if (p == nullptr) { continue; }
            parameters.render_encoder.set_render_pipeline(*p);
        } else {
            erhe::graphics::Render_pipeline_state temp_state = make_temp_pipeline_state(pipeline);
            parameters.render_encoder.set_render_pipeline_state(temp_state);
        }
        parameters.render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        parameters.render_encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);

        // Track what's bound after the per-pipeline state so per-bucket
        // variant overrides only rebind when the chosen shader differs.
        const erhe::graphics::Shader_stages* currently_bound_shader = used_shader_stages;

        // Per-bucket variant lookup is gated on:
        //   - no per-call override_shader_stages (debug viz wins)
        //   - not on the multiview path (multiview uses pre-built stages)
        //   - this pipeline opted in via uses_standard_variants
        //   - the cache, vertex_format, and light_projections are supplied
        // !use_override_shader_stages already covers multiview transitively
        // (multiview_stages != nullptr forced that flag above), but the
        // explicit !multiview_path keeps the gate self-evident.
        // When inactive, the bucket loop runs identically to before this
        // change.
        const bool variant_lookup_active =
            !use_override_shader_stages &&
            !multiview_path &&
            pipeline.uses_standard_variants &&
            (parameters.standard_shader_variants != nullptr) &&
            (pipeline.vertex_format != nullptr) &&
            (parameters.light_projections != nullptr);

        for (const auto& meshes : mesh_spans) {
            ERHE_PROFILE_SCOPE("mesh span");
            //ERHE_PROFILE_GPU_SCOPE(c_forward_renderer_render);
            if (meshes.empty()) {
                continue;
            }

            // Group meshes by the GPU buffer set their primitives live in;
            // multi-draw-indirect needs identical vertex/index bindings for
            // every draw it covers, so each buffer set becomes its own MDI.
            // Single-format scenes with one block per pool produce exactly
            // one bucket and the loop below collapses to today's behavior.
            const std::vector<Bucket> buckets = bucket_meshes_by_buffers(meshes, primitive_mode);
            std::size_t bucket_index = 0;
            for (const Bucket& bucket : buckets) {
                // Annotate each MDI with a RenderDoc / Vulkan debug-utils
                // scope so captures show one labeled marker per bucket
                // (e.g. "bucket 1/3 meshes=2 streams=3"). The cb-targeted
                // overload records the begin/end label calls into the
                // same cb the encoder writes to so RenderDoc nests the
                // bucket's draw under it.
                erhe::graphics::Scoped_debug_group bucket_scope{
                    parameters.render_encoder.get_command_buffer(),
                    erhe::utility::Debug_label{
                        fmt::format(
                            "bucket {}/{} meshes={} streams={}",
                            bucket_index + 1,
                            buckets.size(),
                            bucket.meshes.size(),
                            bucket.key.vertex_buffers.size()
                        )
                    }
                };
                ++bucket_index;

                // Per-bucket variant override. Falls through to the
                // pipeline-default binding when the cache returns null /
                // invalid (mid-compile fallback) or when variant lookup
                // is gated off.
                if (variant_lookup_active) {
                    const erhe::scene_renderer::Standard_variant_key key = compute_bucket_variant_key(
                        bucket,
                        *pipeline.vertex_format,
                        parameters.light_projections->light_counts
                    );
                    const erhe::graphics::Shader_stages* variant = parameters.standard_shader_variants->get_or_compile(key);
                    if ((variant != nullptr) && variant->is_valid() && (variant != currently_bound_shader)) {
                        erhe::graphics::Render_pipeline_state temp_state = make_temp_pipeline_state(pipeline);
                        parameters.render_encoder.set_render_pipeline_state(temp_state, variant);
                        currently_bound_shader = variant;
                    }
                }

                parameters.render_encoder.set_index_buffer(bucket.key.index_buffer);
                for (std::size_t stream_index = 0; stream_index < bucket.key.vertex_buffers.size(); ++stream_index) {
                    parameters.render_encoder.set_vertex_buffer(
                        bucket.key.vertex_buffers[stream_index],
                        0,
                        static_cast<uint32_t>(stream_index)
                    );
                }

                const std::span<const std::shared_ptr<erhe::scene::Mesh>> bucket_meshes{bucket.meshes};
                std::size_t primitive_count{0};
                Ring_buffer_range primitive_range = m_primitive_buffer.update(bucket_meshes, primitive_mode, filter, parameters.primitive_settings, primitive_count);
                if (primitive_count == 0){
                    primitive_range.cancel();
                    continue;
                }
                erhe::renderer::Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(bucket_meshes, primitive_mode, filter);
                if (draw_indirect_buffer_range.draw_indirect_count == 0) {
                    primitive_range.cancel();
                    draw_indirect_buffer_range.range.cancel();
                    continue;
                }
                ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
                m_primitive_buffer.bind(parameters.render_encoder, primitive_range);
                m_draw_indirect_buffer.bind(parameters.render_encoder, draw_indirect_buffer_range.range); // Draw indirect buffer is not indexed, this binds the whole buffer

                parameters.render_encoder.multi_draw_indexed_primitives_indirect(
                    pipeline.input_assembly.primitive_topology,
                    parameters.index_type,
                    draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                    draw_indirect_buffer_range.draw_indirect_count,
                    sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                );

                primitive_range.release();
                draw_indirect_buffer_range.range.release();
            }
        }
    }

    // These must come after the draw calls have been done
    if (camera_buffer_range.has_value()) {
        camera_buffer_range.value().release();
    }
    material_range.release();
    joint_range.release();
    light_range.release();

    m_texture_heap->unbind(parameters.render_encoder.get_command_buffer());
}

void Forward_renderer::draw_primitives(const Render_parameters& parameters, const erhe::scene::Light* light)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport               = parameters.viewport;
    const auto* camera                 = parameters.camera;
    const auto& lights                 = parameters.lights;
    const auto& render_pipeline_states = parameters.render_pipeline_states;

    parameters.render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    m_texture_heap->reset_heap(parameters.render_encoder.get_command_buffer());

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), parameters.materials);
    if (material_range.get_buffer() != nullptr) {
        m_material_buffer.bind(parameters.render_encoder, material_range);
    }

    std::optional<Ring_buffer_range> camera_range;
    if (!parameters.multiview_views.empty()) {
        const float exposure = (camera != nullptr) ? camera->get_exposure() : 1.0f;
        camera_range = m_camera_buffer.update_views(
            parameters.multiview_views,
            exposure,
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number,
            parameters.reverse_depth,
            parameters.depth_range,
            parameters.conventions
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_range.value());
    } else if (camera != nullptr) {
        camera_range = m_camera_buffer.update(
            *camera->projection(),
            *camera->get_node(),
            viewport,
            camera->get_exposure(),
            parameters.grid_size,
            parameters.grid_line_width,
            parameters.frame_number,
            parameters.reverse_depth,
            parameters.depth_range,
            parameters.conventions
        );
        m_camera_buffer.bind(parameters.render_encoder, camera_range.value());
    }

    std::optional<Ring_buffer_range> light_control_range{};
    if (light != nullptr) {
        const auto* light_projection_transforms = parameters.light_projections->get_light_projection_transforms_for_light(light);
        if (light_projection_transforms != nullptr) {
            light_control_range = m_light_buffer.update_control(light_projection_transforms->index);
            m_light_buffer.bind_control_buffer(parameters.render_encoder, light_control_range.value());
        } else {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
        }
    }

    Ring_buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, parameters.ambient_light);
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);
    m_light_buffer.bind_shadow_samplers(parameters.render_encoder, parameters.light_projections);

    m_texture_heap->bind(parameters.render_encoder);

    // See render() above for the multiview-pipeline selection rationale.
    const bool multiview_path_dp = !parameters.multiview_views.empty();

    for (auto* render_pipeline_state : render_pipeline_states) {
        const erhe::graphics::Render_pipeline_create_info& pipeline = render_pipeline_state->data;
        const erhe::graphics::Shader_stages* multiview_stages =
            multiview_path_dp ? resolve_pipeline_multiview_shader_stages(pipeline) : nullptr;
        const erhe::graphics::Shader_stages* pipeline_shader_stages = resolve_pipeline_shader_stages(pipeline);
        if ((pipeline_shader_stages == nullptr) && (multiview_stages == nullptr)) {
            continue;
        }
        erhe::graphics::Scoped_debug_group pass_scope{
            parameters.render_encoder.get_command_buffer(),
            pipeline.debug_label
        };

        if (multiview_stages != nullptr) {
            erhe::graphics::Render_pipeline_state temp_state = make_temp_pipeline_state(pipeline);
            parameters.render_encoder.set_render_pipeline_state(temp_state, multiview_stages);
        } else if (parameters.render_pass != nullptr) {
            erhe::graphics::Render_pipeline* p = render_pipeline_state->get_pipeline_for(parameters.render_pass->get_descriptor());
            if (p == nullptr) { continue; }
            parameters.render_encoder.set_render_pipeline(*p);
        } else {
            erhe::graphics::Render_pipeline_state temp_state = make_temp_pipeline_state(pipeline);
            parameters.render_encoder.set_render_pipeline_state(temp_state);
        }
        parameters.render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        parameters.render_encoder.set_scissor_rect(viewport.x, viewport.y, viewport.width, viewport.height);
        parameters.render_encoder.draw_primitives(pipeline.input_assembly.primitive_topology, 0, parameters.non_mesh_vertex_count);
    }

    material_range.release();
    light_range.release();

    if (light_control_range.has_value()) {
        light_control_range.value().release();
    }
    if (camera_range.has_value()) {
        camera_range.value().release();
    }

    m_texture_heap->unbind(parameters.render_encoder.get_command_buffer());
}

} // namespace erhe::scene_renderer
