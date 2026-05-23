#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"

#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/draw_indirect.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
//#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::scene_renderer {

using erhe::graphics::Render_pass;
using erhe::graphics::Texture;

using erhe::graphics::Vertex_input_state;
using erhe::graphics::Input_assembly_state;
using erhe::graphics::Rasterization_state;
using erhe::graphics::Depth_stencil_state;
using erhe::graphics::Color_blend_state;

static constexpr std::string_view c_shadow_renderer_initialize_component{"Shadow_renderer::initialize_component()"};

Shadow_renderer::Shadow_renderer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    Mesh_memory&                    mesh_memory,
    Program_interface&              program_interface,
    Shader_variant_cache&           shader_variant_cache
)
    : m_graphics_device{graphics_device}
    , m_mesh_memory{mesh_memory}
    , m_shader_variant_cache{shader_variant_cache}
    , m_empty_fragment_outputs{}
    , m_bind_group_layout{program_interface.bind_group_layout.get()}
    , m_pipeline{
        m_graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label       = erhe::utility::Debug_label{"Shadow Renderer"},
            //.shader_stages     = &m_shader_stages.shader_stages,
            .input_assembly    = Input_assembly_state::triangle,
            .rasterization     = Rasterization_state::cull_mode_none,
            .depth_stencil     = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(graphics_device.get_graphics_config().reverse_depth),
            .color_blend       = Color_blend_state::color_writes_disabled,
            .bind_group_layout = m_bind_group_layout
        }
    }
    , m_dummy_texture{graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_srgb)}
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Shadow_renderer::m_fallback_sampler"
        }
    }
    , m_vertex_input        {graphics_device}
    , m_camera_buffer       {graphics_device, program_interface.camera_interface}
    , m_draw_indirect_buffer{graphics_device, program_interface.config.max_draw_count}
    , m_joint_buffer        {graphics_device, program_interface.joint_interface}
    , m_light_buffer        {graphics_device, init_command_buffer, program_interface.light_interface}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
    , m_material_buffer     {graphics_device, program_interface.material_interface}
    // NOTE m_dummy_texture is NOT used for shadow map texture
    , m_texture_heap{
        std::make_unique<erhe::graphics::Texture_heap>(
            m_graphics_device,
            *m_dummy_texture.get(),
            m_fallback_sampler,
            program_interface.bind_group_layout.get()
        )
    }
{
}

Shadow_renderer::~Shadow_renderer() noexcept = default;

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(parameters.view_camera != nullptr);
    ERHE_VERIFY(parameters.texture);

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections.apply(
        parameters.lights,
        parameters.view_camera,
        parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture,
        parameters.reverse_depth,
        parameters.depth_range,
        parameters.conventions
    );

    erhe::graphics::Scoped_debug_group debug_group{
        parameters.command_buffer,
        erhe::utility::Debug_label{"Shadow_renderer::render()"}
    };

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    using Ring_buffer_range          = erhe::graphics::Ring_buffer_range;
    using Draw_indirect_buffer_range = erhe::scene_renderer::Draw_indirect_buffer_range;

    m_texture_heap->reset_heap(parameters.command_buffer);
    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), parameters.materials);
    Ring_buffer_range joint_range    = m_joint_buffer.update(glm::uvec4{0, 0, 0, 0}, {}, parameters.skins);
    Ring_buffer_range light_range    = m_light_buffer.update(lights, &parameters.light_projections, glm::vec3{0.0f});

    log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->get_debug_label().string_view());

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};

    erhe::graphics::Render_pass* previous_render_pass = nullptr;
    for (const auto& light : lights) {
        if (!light->cast_shadow) {
            continue;
        }

        auto* light_projection_transform = parameters.light_projections.get_light_projection_transforms_for_light(light.get());
        if (light_projection_transform == nullptr) {
            //// log_render->warn("Light {} has no light projection transforms", light->name());
            continue;
        }
        const std::size_t light_index  = light_projection_transform->index;
        const std::size_t shadow_index = light_projection_transform->shadow_index;
        if (shadow_index >= parameters.render_passes.size()) {
            continue;
        }

        erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(parameters.command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{
            *parameters.render_passes[shadow_index].get(),
            parameters.command_buffer,
            previous_render_pass,
            nullptr // TODO
        };
        previous_render_pass = parameters.render_passes[shadow_index].get();

        encoder.set_bind_group_layout(m_bind_group_layout);
        encoder.set_viewport_rect(
            parameters.light_camera_viewport.x,
            parameters.light_camera_viewport.y,
            parameters.light_camera_viewport.width,
            parameters.light_camera_viewport.height
        );
        m_material_buffer.bind(encoder, material_range);
        m_joint_buffer.bind(encoder, joint_range);
        m_light_buffer.bind_light_buffer(encoder, light_range);

        if ((parameters.light_camera_viewport.width > 2) && (parameters.light_camera_viewport.height > 2)) {
            encoder.set_scissor_rect(
                parameters.light_camera_viewport.x + 1,
                parameters.light_camera_viewport.y + 1,
                parameters.light_camera_viewport.width - 2,
                parameters.light_camera_viewport.height - 2
            );
        }

        // For directional lights the light "camera" must be the SNAPPED
        // pose computed by Light::stable_directional_light_projection_transforms
        // (view-camera-centred along the light direction, snapped to
        // shadow-map texels), not the raw light node. light_buffer.cpp
        // writes texture_from_world from the same snapped clip_from_world,
        // and the shading pass samples shadows through that matrix --
        // rasterizing the shadow map from a different pose would make
        // every shadow lookup miss.
        Ring_buffer_range camera_range = m_camera_buffer.update(
            light->projection(parameters.light_projections.parameters),
            light_projection_transform->world_from_light_camera,
            parameters.light_camera_viewport,
            1.0f,                      // exposure
            glm::vec4{0.0f},           // grid size
            glm::vec4{0.0f},           // grid line width
            0,                         // frame number
            parameters.reverse_depth,
            parameters.depth_range
        );
        m_camera_buffer.bind(encoder, camera_range);

        Ring_buffer_range control_range = m_light_buffer.update_control(light_index);
        m_light_buffer.bind_control_buffer(encoder, control_range);

        m_texture_heap->bind(encoder);

        const Light_layer_partition partition = compute_light_layer_partition(parameters.lights);

        Shader_key environment_key{};
        std::vector<Render_bucket> buckets;
        const uint32_t boolean_mask_force_enable = erhe::scene_renderer::make_shader_bool_mask(
            erhe::scene_renderer::Shader_bool::VARIANT_DEPTH_ONLY
        );
        const uint32_t boolean_mask_force_disable = 0; // TODO

        for (const auto& meshes : mesh_spans) {
            bucket_primitives(
                buckets,
                boolean_mask_force_enable,
                boolean_mask_force_disable,
                m_mesh_memory,
                environment_key,
                meshes,
                primitive_mode,
                Variant_signature_policy::accept_all_variant_signatures
            );
        }

        for (std::size_t bucket_index = 0, end = buckets.size(); bucket_index < end; ++bucket_index) {
            const Render_bucket& bucket = buckets[bucket_index];
            erhe::graphics::Scoped_debug_group bucket_scope{
                parameters.command_buffer,
                erhe::utility::Debug_label{
                    fmt::format(
                        "shadow bucket {}/{} entries={} streams={}",
                        bucket_index + 1,
                        buckets.size(),
                        bucket.entries.size(),
                        bucket.buffer_set.vertex_buffers.size()
                    )
                }
            };

            erhe::graphics::Buffer* index_buffer = m_mesh_memory.get_index_buffer(bucket.buffer_set.index_buffer);
            encoder.set_index_buffer(index_buffer);
            for (std::size_t stream_index = 0; stream_index < bucket.buffer_set.vertex_buffers.size(); ++stream_index) {
                erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(bucket.buffer_set.vertex_buffers[stream_index]);
                encoder.set_vertex_buffer(
                    vertex_buffer,
                    0,
                    static_cast<uint32_t>(stream_index)
                );
            }

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
            erhe::graphics::Render_pipeline* render_pipeline = m_pipeline.get_pipeline_for(
                parameters.render_passes[shadow_index]->get_descriptor(),
                &reloadable_shader_stages->shader_stages,
                vertex_input.vertex_input.get(),
                &vertex_input.vertex_format
            );
            if (render_pipeline == nullptr) {
                log_draw->warn("No render pipeline");
                continue;
            }
            encoder.set_render_pipeline(*render_pipeline);

            //const std::span<const std::shared_ptr<erhe::scene::Mesh>> bucket_meshes{bucket.meshes};
            std::size_t primitive_count{0};
            Ring_buffer_range primitive_range = m_primitive_buffer.update(
                bucket,
                primitive_mode,
                shadow_filter,
                Primitive_interface_settings{},
                primitive_count
            );
            if (primitive_count == 0) {
                primitive_range.cancel();
                continue;
            }
            Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(bucket, primitive_mode, shadow_filter);
            ERHE_VERIFY(primitive_count == draw_indirect_buffer_range.draw_indirect_count);
            if (primitive_count == 0) {
                primitive_range.cancel();
                draw_indirect_buffer_range.range.cancel();
                continue;
            }

            log_draw->warn("MDI - shader key = {}", bucket.shader_key.describe());

            m_primitive_buffer.bind(encoder, primitive_range);
            m_draw_indirect_buffer.bind(encoder, draw_indirect_buffer_range.range);

            {
                static constexpr std::string_view c_id_mdi{"mdi"};

                ERHE_PROFILE_SCOPE("mdi");
                //ERHE_PROFILE_GPU_SCOPE(c_id_mdi);
                encoder.multi_draw_indexed_primitives_indirect(
                    m_pipeline.data.input_assembly.primitive_topology,
                    m_mesh_memory.get_index_format(bucket.buffer_set.index_buffer),
                    draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
                    draw_indirect_buffer_range.draw_indirect_count,
                    sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
                );
            }
            primitive_range.release();
            draw_indirect_buffer_range.range.release();
        }

        control_range.release();
        camera_range.release();
    }

    m_texture_heap->unbind(parameters.command_buffer);

    material_range.release();
    joint_range.release();
    light_range.release();

    return true;
}

void Shadow_renderer::prewarm_pipelines(
    std::span<const std::unique_ptr<erhe::graphics::Render_pass>>           render_passes,
    const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>>& mesh_spans
)
{
    ERHE_PROFILE_FUNCTION();

    // Mirror the runtime bucket walk in render() above: empty environment
    // key, accept_all_variant_signatures policy (material -> nullptr),
    // VARIANT_DEPTH_ONLY forced on.
    const Shader_key environment_key{};
    const uint32_t boolean_mask_force_enable  = make_shader_bool_mask(Shader_bool::VARIANT_DEPTH_ONLY);
    const uint32_t boolean_mask_force_disable = 0;

    std::vector<Render_bucket> buckets;
    for (const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes : mesh_spans) {
        bucket_primitives(
            buckets,
            boolean_mask_force_enable,
            boolean_mask_force_disable,
            m_mesh_memory,
            environment_key,
            meshes,
            erhe::primitive::Primitive_mode::polygon_fill,
            Variant_signature_policy::accept_all_variant_signatures
        );
    }

    for (const Render_bucket& bucket : buckets) {
        const Vertex_input_entry& vertex_input = m_mesh_memory.get_vertex_input(bucket.buffer_set.vertex_input_key);

        // Phase 1: shader-module compile (no-op on cache hit).
        const erhe::graphics::Reloadable_shader_stages* reloadable_shader_stages = m_shader_variant_cache.get(
            bucket.shader_key,
            &vertex_input.vertex_format
        );
        if (reloadable_shader_stages == nullptr) {
            continue;
        }

        // Phase 2: force the format-hashed Render_pipeline (and its
        // underlying VkPipeline on Vulkan) to be constructed before
        // the first draw. The ignored return is intentional -- the
        // side effect (cache population) is the goal. Skipped when no
        // render passes are available yet (e.g. orchestrator ran before
        // any Shadow_render_node was created); the runtime will still
        // hit a warm Shader_variant_cache.
        for (const std::unique_ptr<erhe::graphics::Render_pass>& render_pass : render_passes) {
            if (!render_pass) {
                continue;
            }
            static_cast<void>(
                m_pipeline.get_pipeline_for(
                    render_pass->get_descriptor(),
                    &reloadable_shader_stages->shader_stages,
                    vertex_input.vertex_input.get(),
                    &vertex_input.vertex_format
                )
            );
        }
    }
}

} // namespace erhe::scene_renderer
