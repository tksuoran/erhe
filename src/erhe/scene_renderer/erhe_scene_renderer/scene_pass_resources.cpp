#include "erhe_scene_renderer/scene_pass_resources.hpp"

#include "erhe_scene_renderer/program_interface.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Scene_pass_resources::Scene_pass_resources(
    erhe::graphics::Device&            graphics_device,
    erhe::graphics::Command_buffer&    init_command_buffer,
    Program_interface&                 program_interface,
    const erhe::ui::Glyph_outline_set* glyph_outline_set,
    Material_set&                      empty_material_set
)
    : m_graphics_device   {graphics_device}
    , m_program_interface {program_interface}
    , m_camera_buffer     {graphics_device, program_interface.camera_interface}
    , m_glyph_buffer      {graphics_device, program_interface.glyph_interface, glyph_outline_set}
    , m_joint_buffer      {graphics_device, program_interface.joint_interface}
    , m_light_buffer      {graphics_device, init_command_buffer, program_interface.light_interface}
    , m_empty_material_set{empty_material_set}
{
}

Scene_pass_resources::~Scene_pass_resources() noexcept = default;

auto Scene_pass_resources::resolve_material_source(Material_set* material_source) -> Material_set&
{
    return (material_source != nullptr) ? *material_source : m_empty_material_set;
}

auto Scene_pass_resources::begin_pass(
    const Base_render_parameters& base,
    const glm::uvec4&             debug_joint_indices,
    const std::span<glm::vec4>&   debug_joint_colors,
    const erhe::scene::Node*      debug_target_joint
) -> Pass_state
{
    Pass_state state{};
    erhe::graphics::Render_command_encoder& render_encoder = base.render_encoder;
    render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    // The material set is already updated for this frame (D6) and its heap is
    // already populated; the pass binds it and resets nothing.
    state.material_source = &resolve_material_source(base.material_source);

    ERHE_VERIFY(!base.views.empty());
    // Single-view passes (size 1) call update() so the trailing
    // cameras[] entries get zero-filled when the program was built
    // with view_count > 1 (XR build running a non-multiview pass).
    // Multiview passes (size >= 2) call update_views() which writes
    // every entry; its internal verify guards size against the
    // compile-time view_count.
    if (base.views.size() >= 2) {
        state.camera_range = m_camera_buffer.update_views(
            base.views,
            base.exposure,
            base.grid_parameters,
            base.sky_parameters,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
    } else {
        const Camera_view_input& view = base.views[0];
        ERHE_VERIFY(view.projection != nullptr);
        ERHE_VERIFY(view.node       != nullptr);
        state.camera_range = m_camera_buffer.update(
            *view.projection,
            *view.node,
            view.viewport,
            base.exposure,
            base.grid_parameters,
            base.sky_parameters,
            base.frame_number,
            base.reverse_depth,
            base.depth_range,
            base.conventions
        );
    }
    m_camera_buffer.bind(render_encoder, state.camera_range.value());

    // Static glyph curve data (grid axis labels); bound unconditionally
    // so the shared bind group is always complete.
    m_glyph_buffer.bind(render_encoder);

    state.joint_range = m_joint_buffer.update(debug_joint_indices, debug_joint_colors, base.skins, debug_target_joint);
    m_joint_buffer.bind(render_encoder, state.joint_range);

    // This must be done even if lights is empty.
    // For example, the number of lights is read from the light buffer.
    state.light_range = m_light_buffer.update(
        base.light_projections,
        base.ambient_light,
        m_lightmap_bicubic ? 1u : 0u,
        &m_ddgi,
        base.shadow_debug_light_index
    );
    m_light_buffer.bind_light_buffer(render_encoder, state.light_range);
    m_light_buffer.bind_shadow_samplers(render_encoder, base.light_projections);
    m_light_buffer.bind_lightmap(render_encoder, m_lightmap_texture.get());
    m_light_buffer.bind_ddgi(
        render_encoder,
        m_ddgi_irradiance_texture.get(),
        m_ddgi_distance_texture  .get(),
        m_ddgi_probe_data_texture.get()
    );

    // After the light buffer's set_sampled_image() calls, as the heap bind
    // has always been.
    state.material_source->bind(render_encoder);

    render_encoder.set_viewport_rect(base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);
    render_encoder.set_scissor_rect (base.viewport.x, base.viewport.y, base.viewport.width, base.viewport.height);

    return state;
}

void Scene_pass_resources::end_pass(Pass_state& state, erhe::graphics::Render_command_encoder& render_encoder)
{
    // These must come after the draw calls have been done
    if (state.camera_range.has_value()) {
        state.camera_range.value().release();
    }
    state.joint_range.release();
    state.light_range.release();

    // R9: bind / unbind stay per pass even though the storage is persistent.
    ERHE_VERIFY(state.material_source != nullptr);
    state.material_source->unbind(render_encoder.get_command_buffer());
}

} // namespace erhe::scene_renderer
