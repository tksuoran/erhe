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
#include "erhe_graphics/scoped_gpu_zone.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/math_util.hpp"
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
    , m_y_flip{graphics_device.get_info().coordinate_conventions.clip_space_y_flip == erhe::math::Clip_space_y_flip::enabled}
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
    // Build one shadow caster pipeline per Shadow_cull_mode, plus a depth-clamp
    // sibling for each. Cull front (only back faces write depth) reduces
    // peter-panning on closed meshes; cull back lets single-sided geometry cast
    // shadows from the lit side; cull none rasterizes both sides. Light
    // projections use the same device coordinate conventions as viewport
    // passes, so the same y-flip winding compensation applies. Depth bias is
    // enabled so the magnitudes (App_settings preset) can be applied per pass
    // via encoder.set_depth_bias(). The depth-clamp variants differ ONLY in
    // depth clamping (Shadow_frustum_fit_settings::depth_clamp), not culling.
    const bool reverse_depth = graphics_device.get_reverse_depth();
    auto build_pipeline = [&](const char* label, const Rasterization_state& rasterization) -> erhe::graphics::Base_render_pipeline {
        return erhe::graphics::Base_render_pipeline{
            m_graphics_device,
            erhe::graphics::Base_render_pipeline_create_info{
                .debug_label       = erhe::utility::Debug_label{label},
                .input_assembly    = Input_assembly_state::triangle,
                .rasterization     = rasterization.with_depth_bias().with_winding_flip_if(m_y_flip),
                .depth_stencil     = Depth_stencil_state::depth_test_enabled_stencil_test_disabled(reverse_depth),
                .bind_group_layout = m_bind_group_layout
            }
        };
    };

    using Cull = Shadow_cull_mode;
    m_pipelines[static_cast<std::size_t>(Cull::cull_front)] = build_pipeline("Shadow Renderer Cull Front", Rasterization_state::cull_mode_front_ccw);
    m_pipelines[static_cast<std::size_t>(Cull::cull_back )] = build_pipeline("Shadow Renderer Cull Back",  Rasterization_state::cull_mode_back_ccw );
    m_pipelines[static_cast<std::size_t>(Cull::cull_none )] = build_pipeline("Shadow Renderer Cull None",  Rasterization_state::cull_mode_none     );
    m_pipelines_depth_clamp[static_cast<std::size_t>(Cull::cull_front)] = build_pipeline("Shadow Renderer Cull Front Depth Clamp", Rasterization_state::cull_mode_front_ccw_depth_clamp);
    m_pipelines_depth_clamp[static_cast<std::size_t>(Cull::cull_back )] = build_pipeline("Shadow Renderer Cull Back Depth Clamp",  Rasterization_state::cull_mode_back_ccw_depth_clamp );
    m_pipelines_depth_clamp[static_cast<std::size_t>(Cull::cull_none )] = build_pipeline("Shadow Renderer Cull None Depth Clamp",  Rasterization_state::cull_mode_none_depth_clamp     );
}

Shadow_renderer::~Shadow_renderer() noexcept = default;

void Shadow_renderer::draw_shadow_casters(
    erhe::graphics::Command_buffer&                                                         command_buffer,
    erhe::graphics::Render_command_encoder&                                                 encoder,
    erhe::graphics::Base_render_pipeline&                                                    base_pipeline,
    const erhe::graphics::Render_pass&                                                       render_pass,
    const erhe::graphics::Color_blend_state*                                                 color_blend,
    const std::initializer_list<const std::span<const std::shared_ptr<erhe::scene::Mesh>>>& mesh_spans,
    const erhe::Item_filter&                                                                 shadow_filter,
    const uint32_t                                                                           boolean_mask_force_enable
)
{
    using Ring_buffer_range          = erhe::graphics::Ring_buffer_range;
    using Draw_indirect_buffer_range = erhe::scene_renderer::Draw_indirect_buffer_range;

    const erhe::primitive::Primitive_mode primitive_mode{erhe::primitive::Primitive_mode::polygon_fill};

    Shader_key                 environment_key{};
    std::vector<Render_bucket> buckets;
    const uint32_t             boolean_mask_force_disable = 0; // TODO

    for (const auto& meshes : mesh_spans) {
        bucket_primitives(
            buckets,
            boolean_mask_force_enable,
            boolean_mask_force_disable,
            m_mesh_memory,
            environment_key,
            meshes,
            shadow_filter,
            primitive_mode,
            Blending_mode_policy::opaque_primitives_only // TODO Think about what would be correct
        );
    }

    for (std::size_t bucket_index = 0, end = buckets.size(); bucket_index < end; ++bucket_index) {
        const Render_bucket& bucket = buckets[bucket_index];
        erhe::graphics::Scoped_debug_group bucket_scope{
            command_buffer,
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
        // Mirrored (negative determinant) buckets get the front-face-flipped
        // variant so front-face culling removes the side facing the light, same
        // as for non-mirrored casters.
        erhe::graphics::Render_pipeline* render_pipeline = base_pipeline.get_pipeline_for(
            render_pass.get_descriptor(),
            color_blend,
            &reloadable_shader_stages->shader_stages,
            vertex_input.vertex_input.get(),
            &vertex_input.vertex_format,
            bucket.negative_determinant
        );
        if (render_pipeline == nullptr) {
            log_draw->warn("No render pipeline");
            continue;
        }
        // Bind the pipeline before the index/vertex buffers: switching the
        // pipeline switches the GL VAO, and on GL 4.1 (no DSA) the element
        // array buffer binding is VAO-local.
        encoder.set_render_pipeline(*render_pipeline);

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

        Ring_buffer_range primitive_range = m_primitive_buffer.update(
            bucket,
            primitive_mode,
            Primitive_interface_settings{}
        );
        ERHE_VERIFY(!bucket.entries.empty());
        Draw_indirect_buffer_range draw_indirect_buffer_range = m_draw_indirect_buffer.update(bucket, primitive_mode);
        ERHE_VERIFY(bucket.entries.size() == draw_indirect_buffer_range.draw_indirect_count);

        m_primitive_buffer.bind(encoder, primitive_range);
        m_draw_indirect_buffer.bind(encoder, draw_indirect_buffer_range.range);

        encoder.multi_draw_indexed_primitives_indirect(
            base_pipeline.data.input_assembly.primitive_topology,
            m_mesh_memory.get_index_format(bucket.buffer_set.index_buffer),
            draw_indirect_buffer_range.range.get_byte_start_offset_in_buffer(),
            draw_indirect_buffer_range.draw_indirect_count,
            sizeof(erhe::graphics::Draw_indexed_primitives_indirect_command)
        );

        primitive_range.release();
        draw_indirect_buffer_range.range.release();
    }
}

auto Shadow_renderer::render(const Render_parameters& parameters) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(parameters.view_camera != nullptr);
    ERHE_VERIFY(parameters.texture);

    const auto& mesh_spans = parameters.mesh_spans;
    const auto& lights     = parameters.lights;

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    // Gather one world-space AABB per shadow caster for the tight directional
    // shadow frustum fit. The per-light contribution filter - culling casters
    // whose bounds cannot shadow a visible receiver - needs each caster's
    // bounds individually and depends on the light direction, so it runs per
    // light inside the fit (Light::tight_directional_light_projection_transforms);
    // here we only collect the candidates. Light_projections::apply() borrows
    // these vectors as spans for the duration of the call; no copy is made.
    std::vector<erhe::math::Aabb>& caster_world_aabbs   = m_caster_world_aabbs;
    std::vector<erhe::math::Aabb>& receiver_world_aabbs = m_receiver_world_aabbs;
    caster_world_aabbs.clear();
    receiver_world_aabbs.clear();
    const bool gather_casters = (parameters.fit_settings != nullptr) && parameters.fit_settings->fit_to_casters;
    // Receiver bounds refine the caster cull (Shadow_frustum_fit_settings::
    // fit_to_receivers) and are only consumed when casters are also fitted.
    const bool gather_receivers = gather_casters && parameters.fit_settings->fit_to_receivers;
    if (gather_casters || gather_receivers) {
        ERHE_PROFILE_SCOPE("shadow: gather caster/receiver bounds");
        for (const auto& meshes : mesh_spans) {
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
                if (!mesh) {
                    continue;
                }
                // Receivers are all visible content meshes; casters additionally
                // require the shadow_cast flag (shadow_filter). Every caster is a
                // receiver, so compute the world AABB once and bucket it into both.
                const uint64_t flag_bits = mesh->get_flag_bits();
                if ((flag_bits & erhe::Item_flags::visible) == 0) {
                    continue;
                }
                const bool is_caster = gather_casters && shadow_filter(flag_bits);
                if (!gather_receivers && !is_caster) {
                    continue; // nothing to gather for this mesh; skip the AABB compute
                }
                const erhe::math::Aabb aabb = mesh->get_aabb_world();
                if (!aabb.is_valid()) {
                    continue;
                }
                if (gather_receivers) {
                    receiver_world_aabbs.push_back(aabb);
                }
                if (is_caster) {
                    caster_world_aabbs.push_back(aabb);
                }
            }
        }
    }

    // Also assigns lights slot in uniform block shader resource
    parameters.light_projections.apply(
        parameters.lights,
        parameters.view_camera,
        parameters.view_camera_viewport,
        parameters.light_camera_viewport,
        parameters.texture,
        parameters.reverse_depth,
        parameters.depth_range,
        parameters.conventions,
        caster_world_aabbs,
        receiver_world_aabbs,
        parameters.fit_settings
    );

    // Make the distance map (if any) reachable to the forward pass'
    // bind_shadow_samplers via the shared Light_projections. Null for the depth
    // technique, in which case the receiver uses the depth samplers instead.
    parameters.light_projections.shadow_distance_texture = parameters.distance_texture;

    // Likewise expose the point-light shadow cube array (if any) so the forward
    // pass binds it to s_shadow_cube. Null when no point shadows are configured;
    // bind_shadow_samplers then binds the fallback cube.
    parameters.light_projections.shadow_cube_texture = parameters.point_cube_texture;

    erhe::graphics::Scoped_debug_group debug_group{
        parameters.command_buffer,
        erhe::utility::Debug_label{"Shadow_renderer::render()"}
    };
    erhe::graphics::Scoped_gpu_zone gpu_zone{parameters.command_buffer, "Shadow_renderer::render()"};

    using Ring_buffer_range          = erhe::graphics::Ring_buffer_range;
    using Draw_indirect_buffer_range = erhe::scene_renderer::Draw_indirect_buffer_range;

    m_texture_heap->reset_heap(parameters.command_buffer);
    Ring_buffer_range material_range = m_material_buffer.update(*m_texture_heap.get(), parameters.materials);
    Ring_buffer_range joint_range    = m_joint_buffer.update(glm::uvec4{0, 0, 0, 0}, {}, parameters.skins);
    Ring_buffer_range light_range    = m_light_buffer.update(lights, &parameters.light_projections, glm::vec3{0.0f});

    // log_shadow_renderer->trace("Rendering shadow map to '{}'", parameters.texture->get_debug_label().string_view());

    // Depth clamping preserves casters outside the (tightly fitted) light
    // space near plane by clamping their depth instead of clipping them.
    // Face culling is the active graphics preset's Shadow_cull_mode; both
    // dimensions index the pre-built pipeline set.
    const bool        use_depth_clamp = (parameters.fit_settings != nullptr) && parameters.fit_settings->depth_clamp;
    const std::size_t cull_index      = static_cast<std::size_t>(parameters.cull_mode);
    erhe::graphics::Base_render_pipeline& base_pipeline = use_depth_clamp
        ? m_pipelines_depth_clamp[cull_index]
        : m_pipelines[cull_index];

    erhe::graphics::Render_pass* previous_render_pass = nullptr;
    for (const auto& light : lights) {
        if (!light->casts_shadow()) {
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
        // Hardware depth bias for the shadow (caster) pass. Both shadow
        // pipelines enable depth bias, so this must be set before their draws;
        // 0/0 means no bias. The sign that reduces acne depends on the depth
        // convention -- negative values are valid for experimentation.
        encoder.set_depth_bias(parameters.depth_bias_constant, parameters.depth_bias_slope, 0.0f);
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

        // For directional lights the light "camera" must be the FITTED pose
        // and projection computed by Light::projection_transforms()
        // (view-camera-centred along the light direction, snapped to
        // shadow-map texels), not the raw light node. light_buffer.cpp
        // writes texture_from_world from the same fitted clip_from_world,
        // and the shading pass samples shadows through that matrix --
        // rasterizing the shadow map from a different pose or projection
        // would make every shadow lookup miss.
        Ring_buffer_range camera_range = m_camera_buffer.update(
            light_projection_transform->projection,
            light_projection_transform->world_from_light_camera,
            parameters.light_camera_viewport,
            1.0f,                      // exposure
            Grid_parameters{},         // unused by shadow pass shaders
            Sky_parameters{},          // unused by shadow pass shaders
            0,                         // frame number
            parameters.reverse_depth,
            parameters.depth_range
        );
        m_camera_buffer.bind(encoder, camera_range);

        Ring_buffer_range control_range = m_light_buffer.update_control(light_index, parameters.distance_bias_coeff);
        m_light_buffer.bind_control_buffer(encoder, control_range);

        m_texture_heap->bind(encoder);

        // Directional / spot caster variant: depth-only, plus the distance
        // fragment shader (and its R32F color attachment) when the distance
        // technique is active.
        uint32_t boolean_mask_force_enable = erhe::scene_renderer::make_shader_bool_mask(
            erhe::scene_renderer::Shader_bool::VARIANT_DEPTH_ONLY
        );
        if (parameters.use_distance) {
            boolean_mask_force_enable |= erhe::scene_renderer::make_shader_bool_mask(
                erhe::scene_renderer::Shader_bool::VARIANT_SHADOW_DISTANCE
            );
        }
        const erhe::graphics::Color_blend_state* color_blend = parameters.use_distance
            ? &erhe::graphics::Color_blend_state::color_blend_disabled   // write biased distance to the color attachment
            : &erhe::graphics::Color_blend_state::color_writes_disabled; // depth-only

        draw_shadow_casters(
            parameters.command_buffer,
            encoder,
            base_pipeline,
            *parameters.render_passes[shadow_index].get(),
            color_blend,
            mesh_spans,
            shadow_filter,
            boolean_mask_force_enable
        );

        control_range.release();
        camera_range.release();
    }

    // Omnidirectional point-light shadow cube pass. Each shadow-casting point
    // light renders the scene into the six faces of its R32F cube (radial
    // distance from the light), one render pass per face. cull_none (per the
    // forge SKILL.md: safe for non-watertight glTF) with color writes enabled.
    if (
        (parameters.point_cube_texture != nullptr) &&
        (parameters.point_cube_render_passes != nullptr) &&
        !parameters.point_cube_render_passes->empty() &&
        (parameters.point_shadow_viewport.width > 0) &&
        (parameters.point_shadow_viewport.height > 0)
    ) {
        const std::vector<std::unique_ptr<erhe::graphics::Render_pass>>& cube_passes = *parameters.point_cube_render_passes;
        const std::size_t cube_count = cube_passes.size() / 6;

        // Cube face look / up vectors in Vulkan cube-face order (+X, -X, +Y, -Y,
        // +Z, -Z), matching the layer order written here and the direction-based
        // samplerCubeArray lookup in the forward pass. These match the GL/forge
        // cube up-vectors.
        static const glm::vec3 cube_look[6] = {
            { 1.0f,  0.0f,  0.0f}, {-1.0f,  0.0f,  0.0f},
            { 0.0f,  1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f}
        };
        static const glm::vec3 cube_up[6] = {
            { 0.0f, -1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f},
            { 0.0f,  0.0f,  1.0f}, { 0.0f,  0.0f, -1.0f},
            { 0.0f, -1.0f,  0.0f}, { 0.0f, -1.0f,  0.0f}
        };

        // Cube-shadow caster coordinate conventions. Unlike a 2D color target or
        // the 2D shadow map (which is sampled through a texture_from_world matrix
        // that absorbs the flip via Light::get_texture_from_clip), a cube face is
        // sampled by raw direction through the API's FIXED cube-map (s,t)
        // convention, which is vertically inverted relative to the framebuffer
        // row order. So the stored face must be physically oriented to match it:
        // the cube caster needs its clip-space Y flipped OPPOSITE to the screen
        // pass. erhe's framebuffer y-flip is the framebuffer_origin compensation
        // (a negative-height viewport on top_left backends, none on bottom_left
        // GL). We express the cube's counter-flip the same convention-driven way
        // get_texture_from_clip does -- derived from framebuffer_origin, via
        // clip_space_y_flip: on a top_left framebuffer the projection y-negate
        // cancels the viewport flip (net GL-native orientation == the cube
        // convention); on bottom_left GL nothing is flipped. This is the one
        // legitimate use of clip_space_y_flip (disabled device-wide otherwise).
        erhe::math::Coordinate_conventions cube_conventions = parameters.conventions;
        cube_conventions.clip_space_y_flip =
            (parameters.conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left)
                ? erhe::math::Clip_space_y_flip::enabled
                : erhe::math::Clip_space_y_flip::disabled;
        const uint32_t cube_force_enable = erhe::scene_renderer::make_shader_bool_mask(
            erhe::scene_renderer::Shader_bool::VARIANT_SHADOW_CUBE
        );
        erhe::graphics::Base_render_pipeline& cube_base_pipeline =
            m_pipelines[static_cast<std::size_t>(Shadow_cull_mode::cull_none)];

        erhe::graphics::Scoped_debug_group cube_debug_group{
            parameters.command_buffer,
            erhe::utility::Debug_label{"Shadow_renderer point cubes"}
        };

        erhe::graphics::Render_pass* previous_cube_render_pass = nullptr;
        for (const auto& light : lights) {
            if (!light->casts_shadow() || (light->type != erhe::scene::Light_type::point)) {
                continue;
            }
            auto* lpt = parameters.light_projections.get_light_projection_transforms_for_light(light.get());
            if (lpt == nullptr) {
                continue;
            }
            const std::size_t point_shadow_index = lpt->point_shadow_index;
            if (point_shadow_index >= cube_count) {
                continue;
            }
            const std::size_t light_index = lpt->index;
            const glm::vec3   light_pos   = glm::vec3{lpt->world_from_light_camera.get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
            const float       far_plane   = light->range;

            for (int face = 0; face < 6; ++face) {
                const std::size_t pass_index = (point_shadow_index * 6) + static_cast<std::size_t>(face);

                erhe::graphics::Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(parameters.command_buffer);
                erhe::graphics::Scoped_render_pass scoped_render_pass{
                    *cube_passes[pass_index].get(),
                    parameters.command_buffer,
                    previous_cube_render_pass,
                    nullptr
                };
                previous_cube_render_pass = cube_passes[pass_index].get();

                encoder.set_bind_group_layout(m_bind_group_layout);
                encoder.set_viewport_rect(
                    parameters.point_shadow_viewport.x,
                    parameters.point_shadow_viewport.y,
                    parameters.point_shadow_viewport.width,
                    parameters.point_shadow_viewport.height
                );
                // Full-face scissor: unlike the 2D map there is no empty border,
                // so adjacent cube faces meet without a clear-value seam.
                encoder.set_scissor_rect(
                    parameters.point_shadow_viewport.x,
                    parameters.point_shadow_viewport.y,
                    parameters.point_shadow_viewport.width,
                    parameters.point_shadow_viewport.height
                );
                encoder.set_depth_bias(parameters.depth_bias_constant, parameters.depth_bias_slope, 0.0f);
                m_material_buffer.bind(encoder, material_range);
                m_joint_buffer.bind(encoder, joint_range);
                m_light_buffer.bind_light_buffer(encoder, light_range);

                const glm::mat4 world_from_face = erhe::math::create_look_at(
                    light_pos,                 // eye
                    light_pos + cube_look[face], // center
                    cube_up[face]              // up
                );
                const erhe::scene::Trs_transform face_transform{world_from_face, glm::inverse(world_from_face)};
                Ring_buffer_range camera_range = m_camera_buffer.update(
                    lpt->projection,
                    face_transform,
                    parameters.point_shadow_viewport,
                    1.0f,                      // exposure
                    Grid_parameters{},
                    Sky_parameters{},
                    0,                         // frame number
                    parameters.reverse_depth,
                    parameters.depth_range,
                    cube_conventions           // clip_space_y_flip per framebuffer_origin (see above)
                );
                m_camera_buffer.bind(encoder, camera_range);

                // The caster fragment shader reads the light world position (and
                // far) from the light control block to store radial distance.
                Ring_buffer_range control_range = m_light_buffer.update_control(
                    light_index,
                    0.0f,
                    glm::vec4{light_pos, far_plane}
                );
                m_light_buffer.bind_control_buffer(encoder, control_range);

                m_texture_heap->bind(encoder);

                draw_shadow_casters(
                    parameters.command_buffer,
                    encoder,
                    cube_base_pipeline,
                    *cube_passes[pass_index].get(),
                    &erhe::graphics::Color_blend_state::color_blend_disabled,
                    mesh_spans,
                    shadow_filter,
                    cube_force_enable
                );

                control_range.release();
                camera_range.release();
            }
        }
    }

    m_texture_heap->unbind(parameters.command_buffer);

    material_range.release();
    joint_range.release();
    light_range.release();

    return true;
}

void Shadow_renderer::prewarm_pipelines(
    std::span<const std::unique_ptr<erhe::graphics::Render_pass>>           render_passes,
    const std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>>& mesh_spans,
    const Shadow_cull_mode                                                  cull_mode
)
{
    ERHE_PROFILE_FUNCTION();

    // Mirror the runtime bucket walk in render() above: empty environment
    // key, accept_all_variant_signatures policy (material -> nullptr),
    // VARIANT_DEPTH_ONLY forced on.
    const Shader_key environment_key{};
    const uint32_t boolean_mask_force_enable  = make_shader_bool_mask(Shader_bool::VARIANT_DEPTH_ONLY);
    const uint32_t boolean_mask_force_disable = 0;

    erhe::Item_filter shadow_filter{
        .require_all_bits_set           = erhe::Item_flags::visible | erhe::Item_flags::shadow_cast,
        .require_at_least_one_bit_set   = 0u,
        .require_all_bits_clear         = 0u,
        .require_at_least_one_bit_clear = 0u
    };

    std::vector<Render_bucket> buckets;
    for (const std::span<const std::shared_ptr<erhe::scene::Mesh>>& meshes : mesh_spans) {
        bucket_primitives(
            buckets,
            boolean_mask_force_enable,
            boolean_mask_force_disable,
            m_mesh_memory,
            environment_key,
            meshes,
            shadow_filter,
            erhe::primitive::Primitive_mode::polygon_fill,
            Blending_mode_policy::opaque_primitives_only // TODO
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
        //
        // Both winding variants are warmed regardless of the bucket's
        // determinant: mirroring a mesh is an interactive editor
        // operation, and warming the flipped sibling here avoids a
        // first-mirrored-mesh pipeline-compile hitch. Both the regular
        // and the depth-clamp base pipelines are warmed so toggling
        // Shadow_frustum_fit_settings::depth_clamp does not hitch. Only the
        // active cull mode is warmed -- changing it is a rare settings action
        // that compiles the other mode's pipelines once, on demand.
        const std::size_t cull_index = static_cast<std::size_t>(cull_mode);
        for (const std::unique_ptr<erhe::graphics::Render_pass>& render_pass : render_passes) {
            if (!render_pass) {
                continue;
            }
            for (erhe::graphics::Base_render_pipeline* base_pipeline : { &m_pipelines[cull_index], &m_pipelines_depth_clamp[cull_index] }) {
                for (const bool front_face_flip : { false, true }) {
                    static_cast<void>(
                        base_pipeline->get_pipeline_for(
                            render_pass->get_descriptor(),
                            &erhe::graphics::Color_blend_state::color_writes_disabled,
                            &reloadable_shader_stages->shader_stages,
                            vertex_input.vertex_input.get(),
                            &vertex_input.vertex_format,
                            front_face_flip
                        )
                    );
                }
            }
        }
    }
}

} // namespace erhe::scene_renderer
