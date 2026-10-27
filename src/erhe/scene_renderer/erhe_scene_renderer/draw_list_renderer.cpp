#include "erhe_scene_renderer/draw_list_renderer.hpp"

#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

namespace {

// The light layer partition the shading variant is selected with: the
// resolved light set's partition carried by the Light_projections (which
// lights got a UBO slot and a shadow layer, see Light_set), so the shader's
// shadow-mapped / non-shadow loop bounds match the UBO slot layout
// Light_buffer::update() writes. Without light projections no light is shaded
// (Light_buffer::update() writes no light data then either).
[[nodiscard]] auto get_light_layer_partition(const Base_render_parameters& base) -> Light_layer_partition
{
    if (base.light_projections != nullptr) {
        return base.light_projections->light_partition;
    }
    return Light_layer_partition{};
}

}

Draw_list_renderer::Draw_list_renderer(
    erhe::graphics::Device& graphics_device,
    Program_interface&      program_interface,
    Scene_pass_resources&   pass_resources
)
    : m_graphics_device     {graphics_device}
    , m_pass_resources      {pass_resources}
    , m_draw_indirect_buffer{graphics_device, program_interface.config.max_draw_count}
    , m_primitive_buffer    {graphics_device, program_interface.primitive_interface}
{
}

Draw_list_renderer::~Draw_list_renderer() noexcept = default;

auto Draw_list_renderer::render(const Render_parameters& parameters) -> Draw_statistics
{
    ERHE_PROFILE_FUNCTION();

    const Base_render_parameters& base = parameters.base;

    // The set this pass binds and the set its cached records name have to be
    // the same one (D5). The caller picks both, so pin them together here
    // rather than trusting the two fields to stay in step.
    ERHE_VERIFY(base.material_source == &parameters.draw_list_scene.get_material_set());
    // The draws copy the lists' cached records (Primitive_buffer::update):
    // no per-mesh color or size source.
    ERHE_VERIFY(parameters.primitive_settings.color_source != Primitive_color_source::id_offset);
    ERHE_VERIFY(parameters.primitive_settings.size_source  == Primitive_size_source::constant_size);

    // The render phase draws the lists published for its frame
    // (Draw_list_scene::publish); the device's frame index on the render
    // thread is the frame being rendered.
    const uint64_t frame_index = m_graphics_device.get_frame_index();

    // Early out before any upload / bind, mirroring the bucket path's empty
    // mesh-span check: nothing passes the filter for these layers.
    if (!parameters.draw_list_scene.has_drawable_entries(frame_index, Draw_purpose::color, parameters.layers, parameters.blending, parameters.filter)) {
        return Draw_statistics{};
    }

    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    Scene_pass_resources::Pass_state pass_state = m_pass_resources.begin_pass(base, parameters.debug_joint_indices, parameters.debug_joint_colors, parameters.debug_target_joint);

    // Environment (R18): recomputed per pass, compared inside draw_color.
    Color_environment environment{};
    environment.light_partition   = get_light_layer_partition(base);
    environment.shadow_filter     = parameters.shadow_filter;
    environment.shadow_bias       = parameters.shadow_bias;
    environment.shadow_technique  = parameters.shadow_technique;
    environment.shadow_depth_bits = parameters.shadow_depth_bits;
    environment.specular_anti_aliasing = parameters.specular_anti_aliasing;
    environment.ddgi_enabled      = m_pass_resources.get_ddgi().is_valid();
    environment.lightmap_enabled  = (m_pass_resources.get_lightmap_texture() != nullptr);
    // Same convention as the bucket path: 0 for single view, N for multiview.
    const uint16_t multiview_count = (base.views.size() >= 2) ? static_cast<uint16_t>(base.views.size()) : uint16_t{0};

    // Frustum culling of the entries (doc/erhe/draw_list_renderer.md Q6):
    // the view the caller added before the frame's classification
    // (add_color_cull_view()).
    const Cull_view cull_view = parameters.cull_view;

    Draw_statistics statistics{};
    for (erhe::graphics::Base_render_pipeline* base_render_pipeline : parameters.base_render_pipelines) {
        erhe::graphics::Scoped_debug_group pipeline_scope{
            render_encoder.get_command_buffer(),
            base_render_pipeline->data.debug_label
        };
        const Draw_statistics pass_statistics = parameters.draw_list_scene.draw_color(
            Draw_color_parameters{
                .render_encoder       = render_encoder,
                .render_pass          = base.render_pass,
                .base_render_pipeline = *base_render_pipeline,
                .primitive_buffer     = m_primitive_buffer,
                .draw_indirect_buffer = m_draw_indirect_buffer,
                .primitive_settings   = parameters.primitive_settings,
                .filter               = parameters.filter,
                .layers               = parameters.layers,
                .blending             = parameters.blending,
                .multiview_count      = multiview_count,
                .environment          = environment,
                .color_blend_override = parameters.color_blend_override,
                .cull_view            = cull_view,
                .command_selection    = parameters.command_selection,
                .debug_label          = base.debug_label,
                .frame_index          = frame_index
            }
        );
        statistics.draw_list_count += pass_statistics.draw_list_count;
        statistics.entry_count     += pass_statistics.entry_count;
        statistics.draw_call_count += pass_statistics.draw_call_count;
        statistics.culled_count    += pass_statistics.culled_count;
        statistics.gpu_generated    = statistics.gpu_generated || pass_statistics.gpu_generated;
    }

    m_pass_resources.end_pass(pass_state, render_encoder);
    return statistics;
}

auto Draw_list_renderer::add_color_command_selection(
    Draw_list_scene&                             draw_list_scene,
    const uint64_t                               frame_index,
    const Cull_view                              cull_view,
    const erhe::Item_filter&                     filter,
    const std::span<const erhe::scene::Layer_id> layers,
    const Draw_blending_selection                blending,
    const Gpu_command_key&                       key
) -> Command_selection_id
{
    if (cull_view == Cull_view::none) {
        return Command_selection_id::none;
    }
    if (m_draw_indirect_buffer.is_index_count_clamped()) {
        if (!m_index_count_clamp_refusal_logged) {
            log_draw_list->warn("Draw_list_renderer: GPU color commands refused, the Draw_indirect_buffer index count clamp is enabled; the color passes write their commands on the CPU");
            m_index_count_clamp_refusal_logged = true;
        }
        return Command_selection_id::none;
    }
    return draw_list_scene.add_color_command_selection(
        frame_index,
        Color_command_selection{
            .cull_view        = cull_view,
            .filter           = filter,
            .layers           = layers,
            .blending         = blending,
            .primitive_buffer = &m_primitive_buffer,
            .key              = key
        }
    );
}

auto Draw_list_renderer::add_color_cull_view(
    Draw_list_scene&                  draw_list_scene,
    const uint64_t                    frame_index,
    const Color_cull_view_parameters& parameters
) -> Cull_view
{
    const std::optional<glm::mat4> clip_from_world = get_color_clip_from_world(parameters);
    if (!clip_from_world.has_value()) {
        return Cull_view::none;
    }
    const float                    clip_z_min      = (parameters.depth_range == erhe::math::Depth_range::zero_to_one) ? 0.0f : -1.0f;
    const std::array<glm::vec4, 6> view_frustum_planes = erhe::math::extract_frustum_planes(clip_from_world.value(), clip_z_min, 1.0f);
    return draw_list_scene.add_cull_view(frame_index, Draw_purpose::color, std::span<const glm::vec4>{view_frustum_planes});
}

auto Draw_list_renderer::get_color_clip_from_world(const Color_cull_view_parameters& parameters) -> std::optional<glm::mat4>
{
    if ((parameters.views.size() != 1) || (parameters.views.front().camera == nullptr)) {
        return {};
    }
    const Camera_view_input& view = parameters.views.front();
    return view.camera->projection().clip_from_node_transform(view.viewport, parameters.reverse_depth, parameters.depth_range, parameters.conventions).get_matrix() * view.camera->node_from_world();
}

auto Draw_list_renderer::get_color_view_eye(const Color_cull_view_parameters& parameters) -> std::optional<glm::vec4>
{
    if ((parameters.views.size() != 1) || (parameters.views.front().camera == nullptr)) {
        return {};
    }
    const erhe::scene::Camera_snapshot& camera = *parameters.views.front().camera;
    if (camera.projection().is_orthographic()) {
        return camera.world_from_node() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
    }
    return camera.position_in_world();
}

auto Draw_list_renderer::add_color_occlusion_view(
    Draw_list_scene&                  draw_list_scene,
    const uint64_t                    frame_index,
    const Cull_view                   cull_view,
    const Color_cull_view_parameters& parameters,
    const Gpu_command_key&            key
) -> Cull_view
{
    if (cull_view == Cull_view::none) {
        return Cull_view::none;
    }
    const std::optional<glm::mat4> clip_from_world = get_color_clip_from_world(parameters);
    if (!clip_from_world.has_value()) {
        return Cull_view::none;
    }
    // extract_frustum_planes() returns the clip_z_min plane at 4 and the
    // clip z = 1 plane at 5: the near plane is 4, or 5 with reverse depth.
    const float                    clip_z_min = (parameters.depth_range == erhe::math::Depth_range::zero_to_one) ? 0.0f : -1.0f;
    const std::array<glm::vec4, 6> planes     = erhe::math::extract_frustum_planes(clip_from_world.value(), clip_z_min, 1.0f);
    const glm::vec4&               near_plane = parameters.reverse_depth ? planes[5] : planes[4];
    return draw_list_scene.add_occlusion_view(frame_index, cull_view, near_plane, key, 1);
}

} // namespace erhe::scene_renderer
