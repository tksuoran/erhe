#include "xr/headset_view.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "erhe_xr/generated/headset_config.hpp"
#include "erhe_xr/generated/perf_settings_level.hpp"
#include "config/generated/viewport_config_data.hpp"
#include "app_context.hpp"
#include "erhe_imgui/windows/performance_window.hpp"
#include "xr/xr_perf_metric_plot.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "rendertarget_imgui_host.hpp"
#include "renderers/id_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/render_context.hpp"
#include "renderers/render_style.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#include "time.hpp"
#include "tools/tools.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/hand_tracker.hpp"

#include "erhe_utility/bit_helpers.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/view.hpp"
#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_graphics/device.hpp"

#include <glm/gtx/matrix_operation.hpp>
#include "erhe_xr/headset.hpp"
#include "erhe_xr/xr_instance.hpp"
#include "erhe_xr/xr_log.hpp"
#include "erhe_xr/xr_session.hpp"

#include <imgui/imgui.h>

namespace editor {

using erhe::graphics::Color_blend_state;

namespace {

// Map the user-facing Perf_settings_level enum to the OpenXR
// XR_PERF_SETTINGS_LEVEL_*_EXT integer value, or -1 for "skip the call".
auto perf_settings_level_to_xr(Perf_settings_level level) -> int
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    switch (level) {
        case Perf_settings_level::e_unset:          return -1;
        case Perf_settings_level::e_power_savings:  return XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
        case Perf_settings_level::e_sustained_low:  return XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
        case Perf_settings_level::e_sustained_high: return XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
        case Perf_settings_level::e_boost:          return XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
    }
#else
    static_cast<void>(level);
#endif
    return -1;
}

// Step a current XR_PERF_SETTINGS_LEVEL_*_EXT down by one notch
// (BOOST -> SUSTAINED_HIGH -> SUSTAINED_LOW -> POWER_SAVINGS). If the level
// is already at the bottom or unset, returns the same value unchanged.
#if defined(ERHE_XR_LIBRARY_OPENXR)
auto step_down_perf_level(int level) -> int
{
    if (level == XR_PERF_SETTINGS_LEVEL_BOOST_EXT)          return XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
    if (level == XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT) return XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT;
    if (level == XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT)  return XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT;
    return level;
}
#endif

} // anonymous namespace

#pragma region Headset_camera_offset_move_command
Headset_camera_offset_move_command::Headset_camera_offset_move_command(erhe::commands::Commands& commands, erhe::math::Input_axis& variable, char axis)
    : Command   {commands, ""}
    , m_variable{variable}
    , m_axis    {axis}
{
}

auto Headset_camera_offset_move_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    m_variable.adjust(input.variant.float_value);
    return true;
}

#pragma endregion Fly_camera_variable_float_command

Headset_view_node::Headset_view_node(erhe::rendergraph::Rendergraph& rendergraph, Headset_view& headset_view)
    : erhe::rendergraph::Rendergraph_node{rendergraph, "Headset"}
    , m_headset_view                     {headset_view}
{
    register_input("shadow_maps",          erhe::rendergraph::Rendergraph_node_key::shadow_maps);
    register_input("rendertarget texture", erhe::rendergraph::Rendergraph_node_key::rendertarget_texture);
}

Headset_view_node::~Headset_view_node()
{
}

void Headset_view_node::execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer)
{
    ERHE_PROFILE_FUNCTION();

    m_headset_view.render_headset(command_buffer);
}

Headset_view::Headset_view(
    const Viewport_config_data&     viewport_config_data,
    erhe::commands::Commands&       commands,
    erhe::graphics::Device&         graphics_device,
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::imgui::Imgui_windows&     imgui_windows,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window&   context_window,
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*              headset,
#endif
    App_context&                    app_context,
    App_rendering&                  app_rendering,
    App_settings&                   app_settings
)
    : Scene_view               {app_context, make_viewport_config(viewport_config_data)}
    , erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Headset", "headset", true}
    , m_translate_x            {}
    , m_translate_y            {}
    , m_translate_z            {}
    , m_offset_x_command       {commands, m_translate_x, 'x'}
    , m_offset_y_command       {commands, m_translate_y, 'y'}
    , m_offset_z_command       {commands, m_translate_z, 'z'}
    , m_app_context            {app_context}
    , m_context_window         {context_window}
    , m_headset                {headset}
{
    ERHE_PROFILE_FUNCTION();
    if (headset == nullptr) {
        return;
    }

    // Latch the multiview capability flag once at startup. Xr_session
    // already considered the device cap, view count, and matching extents;
    // we just thread it through to choose the render path each frame.
    if (m_headset->get_xr_session() != nullptr) {
        m_use_multiview = m_headset->get_xr_session()->is_multiview_enabled();
        if (m_use_multiview) {
            log_xr->info("Headset_view: multiview render path enabled");
        }
    }
    app_rendering.add(this);

    commands.register_command(&m_offset_x_command);
    commands.register_command(&m_offset_y_command);
    commands.register_command(&m_offset_z_command);
    commands.bind_command_to_controller_axis(&m_offset_x_command, 0);
    commands.bind_command_to_controller_axis(&m_offset_y_command, 2);
    commands.bind_command_to_controller_axis(&m_offset_z_command, 1);
    m_translate_x.set_damp     (0.92f);
    m_translate_y.set_damp     (0.92f);
    m_translate_z.set_damp     (0.92f);
    m_translate_x.set_max_delta(0.004f);
    m_translate_y.set_max_delta(0.004f);
    m_translate_z.set_max_delta(0.004f);

    m_rendergraph_node = std::make_shared<Headset_view_node>(rendergraph, *this);

    m_shadow_render_node = app_rendering.create_shadow_node_for_scene_view(graphics_device, rendergraph, app_settings, *this);
    rendergraph.connect(erhe::rendergraph::Rendergraph_node_key::shadow_maps, m_shadow_render_node.get(), m_rendergraph_node.get());

    if (m_context.OpenXR_mirror) {
        erhe::graphics::Render_pass_descriptor render_pass_descriptor;
        render_pass_descriptor.swapchain = graphics_device.get_surface()->get_swapchain();
        render_pass_descriptor.color_attachments[0].load_action  = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.color_attachments[0].usage_after  = erhe::graphics::Image_usage_flag_bit_mask::present;
        render_pass_descriptor.color_attachments[0].layout_after = erhe::graphics::Image_layout::present_src;
        render_pass_descriptor.depth_attachment    .load_action  = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.stencil_attachment  .load_action = erhe::graphics::Load_action::Clear;
        render_pass_descriptor.render_target_width  = context_window.get_width();
        render_pass_descriptor.render_target_height = context_window.get_height();
        render_pass_descriptor.debug_label          = "OpenXR mirror window Render_pass";
        m_mirror_mode_window_render_pass = std::make_unique<erhe::graphics::Render_pass>(graphics_device, render_pass_descriptor);
    }
}

Headset_view::~Headset_view()
{
    // Unregister and destroy any XR perf-metric plots before the
    // unique_ptrs are released. The Performance window is owned by the
    // editor and outlives Headset_view in normal shutdown order.
    if ((m_app_context.performance_window != nullptr) && m_xr_perf_plots_registered) {
        for (std::unique_ptr<Xr_perf_metric_plot>& plot : m_xr_perf_metric_plots) {
            m_app_context.performance_window->unregister_plot(plot.get());
        }
    }
    m_xr_perf_metric_plots.clear();
}

void Headset_view::attach_to_scene(std::shared_ptr<Scene_root> scene_root, erhe::scene_renderer::Mesh_memory& mesh_memory)
{
    m_scene_root = scene_root;

    setup_root_camera();

    {
        ERHE_PROFILE_SCOPE("make Controller_visualization");

        m_controller_visualization = std::make_unique<Controller_visualization>(
            m_root_node.get(),
            mesh_memory,
            *get_scene_root().get()
        );
    }

}

void Headset_view::update_fixed_step()
{
    m_translate_x.update();
    m_translate_y.update();
    m_translate_z.update();
    const float tx =  m_translate_x.current_value();
    const float ty = -m_translate_y.current_value();
    const float tz =  m_translate_z.current_value();
    if (tx != 0.0f || ty != 0.0f || tz != 0.0f) {
        const glm::vec3 translation{tx, ty, tz};
        m_camera_offset += translation;
    }
}

void Headset_view::imgui()
{
    if (!m_context.OpenXR) {
        return;
    }
    // Scene selection
    auto                        old_scene_root = m_scene_root;
    std::shared_ptr<Scene_root> scene_root     = get_scene_root();
    const bool combo_used = m_app_context.app_scenes->scene_combo("##Scene", scene_root, false);
    if (combo_used) {
        m_scene_root = scene_root;
    }

    // Shader selection
    //ImGui::Combo(
    //    "Shader Debug",
    //    reinterpret_cast<int*>(&m_shader_stages_variant),
    //    c_shader_stages_variant_strings,
    //    IM_ARRAYSIZE(c_shader_stages_variant_strings),
    //    IM_ARRAYSIZE(c_shader_stages_variant_strings)
    //);

    ImGui::Combo(
        "Shader Debug",
        &m_selected_shader_debug,
        erhe::scene_renderer::c_shader_debug_strings,
        IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings),
        IM_ARRAYSIZE(erhe::scene_renderer::c_shader_debug_strings)
    );

    erhe::imgui::Imgui_host* imgui_host = get_imgui_host();
    if (imgui_host != nullptr) {
        const glm::vec2 mouse_position = imgui_host->get_mouse_position();
        ImGui::Text("Mouse position x = %f, y = %f", mouse_position.x, mouse_position.y);
    }
}

void Headset_view::render(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return;
    }

    // TODO Handle selection stencil
    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});

    constexpr glm::vec4 red   {1.0f, 0.0f, 0.0f, 1.0f};
    constexpr glm::vec4 green {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 blue  {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr glm::vec4 cyan  {0.0f, 1.0f, 0.0f, 1.0f};
    constexpr glm::vec4 orange{1.0f, 0.8f, 0.5f, 0.5f};

    line_renderer.set_thickness(4.0f);
    for (const auto& finger_input : m_finger_inputs) {
        const glm::vec4 color = green;
        line_renderer.set_line_color(color);
        line_renderer.add_lines({{finger_input.finger_point, finger_input.point}});
    }

    erhe::xr::Xr_actions*     left_actions   = m_headset->get_actions_left();
    erhe::xr::Xr_actions*     right_actions  = m_headset->get_actions_right();
    erhe::xr::Xr_action_pose* left_aim_pose  = (left_actions  != nullptr) ? left_actions ->aim_pose : nullptr;
    erhe::xr::Xr_action_pose* right_aim_pose = (right_actions != nullptr) ? right_actions->aim_pose : nullptr;
    const bool can_use_left  = (left_aim_pose  != nullptr) && (left_aim_pose ->location.locationFlags != 0);
    const bool can_use_right = (right_aim_pose != nullptr) && (right_aim_pose->location.locationFlags != 0);
    erhe::xr::Xr_action_pose* pose = can_use_right ? right_aim_pose : can_use_left ? left_aim_pose : nullptr;

    if (pose != nullptr) {
        erhe::xr::Xr_action_float*   trigger_value_action = (pose == right_aim_pose) ? right_actions->trigger_value : left_actions->trigger_value;
        erhe::xr::Xr_action_boolean* click_action         = (pose == right_aim_pose) ? right_actions->a_click       : left_actions->x_click;
        const float trigger_value = (trigger_value_action != nullptr) ? trigger_value_action->state.currentState : 0.0f;
        const bool click = (click_action != nullptr) && (click_action->state.currentState == XR_TRUE);

        const auto* nearest = get_nearest_hover(Hover_entry::all_bits);
        bool use_hover = 
            (nearest != nullptr) &&
            nearest->position.has_value() &&
            get_control_ray_origin_in_world().has_value() &&
            get_control_ray_direction_in_world().has_value();

        const auto position = use_hover
            ? get_control_ray_origin_in_world().value()
            : pose->position + get_camera_offset();

        const auto orientation = glm::mat4_cast(pose->orientation);

        const glm::vec3 direction = use_hover
            ? get_control_ray_direction_in_world().value() 
            : glm::vec3{orientation * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};

        const auto tip = use_hover
            ? nearest->position.value() 
            : position + trigger_value * direction;

        using namespace erhe::utility;
        bool is_content      = (nearest != nullptr) && test_bit_set(nearest->mask, Hover_entry::content_bit);
        bool is_tool         = (nearest != nullptr) && test_bit_set(nearest->mask, Hover_entry::tool_bit);
        bool is_rendertarget = (nearest != nullptr) && test_bit_set(nearest->mask, Hover_entry::rendertarget_bit);
        glm::vec4 type_color =
            is_content      ? glm::vec4{0.8f, 0.5f, 0.3f, 1.0f} :
            is_tool         ? glm::vec4{1.0f, 0.0f, 1.0f, 1.0f} :
            is_rendertarget ? glm::vec4{0.6f, 0.6f, 0.6f, 1.0f} :
                              glm::vec4{0.4f, 0.4f, 0.4f, 1.0f};
        glm::vec4 near_color = click ? glm::vec4{1.0f, 1.0f, 1.0f, 1.0f} : type_color;
        glm::vec4 far_color  = glm::vec4{type_color.x, type_color.y, type_color.z, 0.4f};
        line_renderer.add_line(
            near_color, 0.0f, position,
            near_color, 2.0f, tip
        );
        if ((trigger_value < 1.0f) && !is_rendertarget) {
            line_renderer.add_line(
                far_color, 2.0f, tip,
                far_color, 8.0f, position + 100.0f * direction
            );
        }
    }
}

auto Headset_view::get_headset_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>
{
    ERHE_PROFILE_FUNCTION();

    auto match_color_texture = [&render_view](const auto& i) {
        return i->get_color_texture() == render_view.color_texture;
    };

    const auto i = std::find_if(m_view_resources.begin(), m_view_resources.end(), match_color_texture);
    if (i == m_view_resources.end()) {
        auto resource = std::make_shared<Headset_view_resources>(
            *m_context.graphics_device,
            render_view,                               // erhe::xr::Render_view& render_view,
            *this,                                     // Headset_view&          headset_view,
            static_cast<std::size_t>(render_view.slot) // const std::size_t      slot
        );
        //const std::string label = fmt::format(
        //    "Headset Viewport_scene_view slot {} color texture {}",
        //    render_view.slot,
        //    render_view.color_texture
        //);
        m_view_resources.push_back(resource);
        return resource;
    }
    return *i;
}

auto Headset_view::get_multiview_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t slot = static_cast<std::size_t>(render_view.slot);
    if (m_multiview_view_resources.size() <= slot) {
        m_multiview_view_resources.resize(slot + 1);
    }
    std::shared_ptr<Headset_view_resources>& entry = m_multiview_view_resources[slot];
    if (!entry) {
        entry = std::make_shared<Headset_view_resources>(
            *m_context.graphics_device,
            render_view,
            *this,
            slot
        );
    }
    return entry;
}

static constexpr std::string_view c_id_headset_clear{"HS clear"};
static constexpr std::string_view c_id_headset_render_content{"HS render content"};

void Headset_view::update_pointer_context_from_controller()
{
    ERHE_PROFILE_FUNCTION();

    erhe::xr::Xr_actions*     left_actions   = m_headset->get_actions_left();
    erhe::xr::Xr_actions*     right_actions  = m_headset->get_actions_right();
    erhe::xr::Xr_action_pose* left_aim_pose  = (left_actions  != nullptr) ? left_actions ->aim_pose : nullptr;
    erhe::xr::Xr_action_pose* right_aim_pose = (right_actions != nullptr) ? right_actions->aim_pose : nullptr;
    erhe::xr::Xr_action_pose* pose =
        (
            (right_aim_pose != nullptr) &&
            (right_aim_pose->location.locationFlags != 0)
        )
            ? right_aim_pose
            : left_aim_pose;

    if (pose == nullptr) {
        return;
    }

    // TODO optimize this transform computation
    const glm::mat4 orientation = glm::mat4_cast(pose->orientation);
    const glm::mat4 translation = glm::translate(glm::mat4{1}, pose->position + get_camera_offset());
    const glm::mat4 m           = translation * orientation;

    this->Scene_view::set_world_from_control(m);
    this->Scene_view::update_hover_with_raytrace();
    // Hybrid picker: raytrace (above) owns static meshes; the ID renderer
    // covers skinned meshes (its rest-pose BVH cannot). This reads the ID
    // buffer rendered from the controller-aligned pick camera in the
    // previous frame's update_id_render() and merges any closer hit.
    update_hover_with_id_render();
    this->Scene_view::update_grid_hover();
}

void Headset_view::update_id_render(erhe::graphics::Command_buffer& command_buffer)
{
    if ((m_context.id_renderer == nullptr) || !m_pointer_pick_camera || !m_pointer_pick_node) {
        return;
    }
    const std::optional<glm::mat4> world_from_control = get_world_from_control();
    if (!world_from_control.has_value()) {
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = get_scene_root();
    if (!scene_root) {
        return;
    }
    Scene_root* tool_scene_root = m_context.tools->get_tool_scene_root().get();
    if (tool_scene_root == nullptr) {
        return;
    }

    // Place the pick camera at the controller aim pose. world_from_control
    // already looks down -Z along the control ray (see
    // get_control_ray_direction_in_world), which matches the camera's own
    // -Z view direction, so it can be used directly as the node transform.
    m_pointer_pick_node->set_parent_from_node(world_from_control.value());

    // Node world transforms (incl. the pick camera) and the joint matrices
    // the ID skinning variant samples must be current before the pass runs.
    scene_root->get_scene().update_node_transforms();
    tool_scene_root->get_hosted_scene()->update_node_transforms();

    // Joint UBO/SSBO, exactly as App_rendering::render_id: bind the same
    // Joint_buffer Forward_renderer uses so the ID skinning branch reads
    // posed joint matrices. Static-only scenes leave it null.
    erhe::scene_renderer::Joint_buffer*                 joint_buffer{nullptr};
    std::span<const std::shared_ptr<erhe::scene::Skin>> skins{};
    if (m_context.forward_renderer != nullptr) {
        erhe::scene::Scene* hosted_scene = scene_root->get_hosted_scene();
        if (hosted_scene != nullptr) {
            const std::vector<std::shared_ptr<erhe::scene::Skin>>& scene_skins = hosted_scene->get_skins();
            if (!scene_skins.empty()) {
                joint_buffer = &m_context.forward_renderer->get_joint_buffer();
                skins        = std::span<const std::shared_ptr<erhe::scene::Skin>>{scene_skins.data(), scene_skins.size()};
            }
        }
    }

    const int                  extent = Id_renderer::get_extent();
    const erhe::math::Viewport viewport{.x = 0, .y = 0, .width = extent, .height = extent};

    const auto& layers      = scene_root->layers();
    const auto& tool_layers = tool_scene_root->layers();

    m_context.id_renderer->render(
        Id_renderer::Render_parameters{
            .command_buffer     = command_buffer,
            .viewport           = viewport,
            .camera             = *m_pointer_pick_camera,
            .content_mesh_spans = { layers.content()->meshes, layers.rendertarget()->meshes },
            .tool_mesh_spans    = { tool_layers.tool()->meshes },
            .x                  = extent / 2,
            .y                  = extent / 2,
            .reverse_depth      = get_reverse_depth(),
            .depth_range        = get_depth_range(),
            .conventions        = get_conventions(),
            .joint_buffer       = joint_buffer,
            .skins              = skins,
            // Same repurposing of id_renderer.enabled as the desktop path:
            // false -> ID pass covers skinned meshes only (raytrace covers
            // static), true -> ID pass covers everything.
            .skinning_filter    = m_context.id_renderer->enabled
                ? Id_renderer::Skinning_filter::all
                : Id_renderer::Skinning_filter::skinned_only,
        }
    );
}

auto Headset_view::get_pick_position_in_world(const float depth) const -> std::optional<glm::vec3>
{
    if (!m_pointer_pick_camera) {
        return {};
    }
    const int                  extent = Id_renderer::get_extent();
    const erhe::math::Viewport viewport{.x = 0, .y = 0, .width = extent, .height = extent};
    const erhe::scene::Camera_projection_transforms projection_transforms =
        m_pointer_pick_camera->projection_transforms(viewport, get_reverse_depth(), get_depth_range(), get_conventions());
    const glm::mat4 world_from_clip = projection_transforms.clip_from_world.get_inverse_matrix();

    // The pick is read at the framebuffer centre (the control ray); unproject
    // that window position at the sampled depth back into world space.
    return erhe::math::unproject<float>(
        world_from_clip,
        glm::vec3{static_cast<float>(extent) * 0.5f, static_cast<float>(extent) * 0.5f, depth},
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        static_cast<float>(extent),
        static_cast<float>(extent),
        get_conventions()
    );
}

void Headset_view::update_hover_with_id_render()
{
    if (m_context.id_renderer == nullptr) {
        return;
    }
    const int extent = Id_renderer::get_extent();
    const Id_renderer::Id_query_result id_query = m_context.id_renderer->get(extent / 2, extent / 2);
    if (!id_query.valid) {
        return;
    }

    Hover_entry entry{
        .valid                      = id_query.valid,
        .scene_mesh_weak            = id_query.mesh,
        .scene_mesh_primitive_index = id_query.index_of_gltf_primitive_in_mesh,
        .position                   = get_pick_position_in_world(id_query.depth),
        .triangle                   = static_cast<uint32_t>(id_query.triangle_id)
    };

    std::shared_ptr<erhe::scene::Mesh> scene_mesh = entry.scene_mesh_weak.lock();
    if (scene_mesh) {
        const erhe::scene::Node* node = scene_mesh->get_node();
        ERHE_VERIFY(node != nullptr);
        const erhe::scene::Mesh_primitive& mesh_primitive = scene_mesh->get_primitives()[entry.scene_mesh_primitive_index];
        const erhe::primitive::Primitive&  primitive      = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive.get_shape_for_raytrace();
        if (shape) {
            entry.geometry = shape->get_geometry();
            if (entry.geometry) {
                const GEO::Mesh& geo_mesh = entry.geometry->get_mesh();
                entry.facet = shape->get_mesh_facet_from_triangle(entry.triangle);
                if (entry.facet != GEO::NO_INDEX) {
                    const bool       negative_determinant   = (node->get_flag_bits() & erhe::Item_flags::negative_determinant) == erhe::Item_flags::negative_determinant;
                    const GEO::vec3f facet_normal           = erhe::geometry::mesh_facet_normalf(geo_mesh, entry.facet);
                    const glm::vec3  local_normal           = erhe::geometry::to_glm_vec3(facet_normal);
                    const glm::mat4  world_from_node        = node->world_from_node();
                    const glm::mat4  normal_world_from_node = glm::transpose(glm::adjugate(world_from_node));
                    const glm::vec3  normal_in_world        = glm::vec3{normal_world_from_node * glm::vec4{local_normal, 0.0f}};
                    const glm::vec3  unit_normal            = glm::normalize(normal_in_world);
                    entry.normal = negative_determinant ? -unit_normal : unit_normal;
                }
            }
        }
    }

    using namespace erhe::utility;
    const uint64_t flags = (id_query.mesh != nullptr) && scene_mesh ? scene_mesh->get_flag_bits() : 0;
    const bool hover_content      = id_query.mesh && test_bit_set(flags, erhe::Item_flags::content     );
    const bool hover_tool         = id_query.mesh && test_bit_set(flags, erhe::Item_flags::tool        );
    const bool hover_brush        = id_query.mesh && test_bit_set(flags, erhe::Item_flags::brush       );
    const bool hover_rendertarget = id_query.mesh && test_bit_set(flags, erhe::Item_flags::rendertarget);

    // Merge into the slot the mesh's role flag matches; merge_hover only
    // overrides a raytrace result when this candidate is closer along the
    // control ray. Mirrors Viewport_scene_view::update_hover_with_id_render.
    if (hover_content     ) { merge_hover(Hover_entry::content_slot     , entry); }
    if (hover_tool        ) { merge_hover(Hover_entry::tool_slot        , entry); }
    if (hover_brush       ) { merge_hover(Hover_entry::brush_slot       , entry); }
    if (hover_rendertarget) { merge_hover(Hover_entry::rendertarget_slot, entry); }
}

auto Headset_view::begin_frame() -> bool
{
    m_frame_timing = erhe::xr::Frame_timing{};
    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return false;
    }

    if (!is_active()) {
        return false;
    }

    m_frame_timing = m_headset->begin_frame_();
    return m_frame_timing.begin_ok;
}

auto Headset_view::render_headset(erhe::graphics::Command_buffer& command_buffer) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return false;
    }

    if (!m_poll_events_ok) {
        return false; // TODO Is this correct?
    }
    if (!m_frame_timing.begin_ok) {
        return false;
    }
    if (!m_update_actions_ok) {
        return false; // TODO Is this correct?
    }

    if (m_request_renderdoc_capture) {
        m_app_context.graphics_device->start_frame_capture();
        m_renderdoc_capture_started = true;
        m_request_renderdoc_capture = false;
    }

    // Skinned-mesh pick. Rendered once per frame (NOT per view) from the
    // controller-aligned pick camera into the Id_renderer's own offscreen
    // 2D target, before the eye / multiview passes are recorded. The result
    // is read back next frame in update_hover_with_id_render(). Independent
    // of the multiview vs per-eye choice below: the ID pass is single-view
    // and writes its own framebuffer.
    if (m_frame_timing.should_render) {
        update_id_render(command_buffer);
    }

    // Multiview render path. Acquires the shared layered swapchain,
    // opens a single Render_pass with view_mask = (1 << view_count) -
    // 1, builds per-view Camera_view_inputs, and drives composer.render
    // once. The pipelines handed to Forward_renderer on this path must
    // carry the multiview-compiled shader_stages so gl_ViewIndex
    // resolves and each eye reads its own camera entry from
    // cameras[gl_ViewIndex]. Content_wide_line_renderer and
    // Debug_renderer feed the same multiview pass via per-view-strided
    // compute (see doc/debug_renderer_multiview.md). Mirror mode is still
    // skipped under multiview; the ID pick pass runs once above,
    // independent of this path.
    if (m_frame_timing.should_render && m_use_multiview) {
        auto multiview_callback = [this](const erhe::xr::Render_views_frame& frame_in, erhe::graphics::Command_buffer& views_cb) -> bool {
            erhe::graphics::Texture* color_texture         = frame_in.shared_color_texture;
            erhe::graphics::Texture* depth_stencil_texture = frame_in.shared_depth_stencil_texture;
            ERHE_VERIFY(color_texture != nullptr);

            // Update each per-eye Headset_view_resources from the
            // matching Render_view so the Camera nodes / projections
            // are current. update() also writes back near/far_depth
            // into the Render_view, which Xr_session reads for
            // composition_layer_depth_infos. Cast away const on the
            // frame views to allow the back-write -- the frame is
            // owned by Xr_session and the callback is the documented
            // place to update it.
            erhe::xr::Render_views_frame& frame = const_cast<erhe::xr::Render_views_frame&>(frame_in);
            std::vector<erhe::scene_renderer::Camera_view_input> view_inputs;
            view_inputs.reserve(frame.views.size());
            const erhe::math::Viewport viewport_xy{
                .x      = 0,
                .y      = 0,
                .width  = static_cast<int>(frame.width),
                .height = static_cast<int>(frame.height)
            };
            erhe::scene::Camera* primary_camera = nullptr;
            for (erhe::xr::Render_view& render_view : frame.views) {
                const std::shared_ptr<Headset_view_resources>& view_resources = get_multiview_view_resources(render_view);
                if (!view_resources->is_valid()) {
                    return false;
                }
                const erhe::scene::Projection::Fov_sides fov_sides{render_view.fov_left, render_view.fov_right, render_view.fov_up, render_view.fov_down};
                view_resources->update(render_view, fov_sides);
                erhe::scene::Camera* camera = view_resources->get_camera();
                if (primary_camera == nullptr) {
                    primary_camera = camera;
                }
                view_inputs.push_back(erhe::scene_renderer::Camera_view_input{
                    .projection = camera->projection(),
                    .node       = camera->get_node(),
                    .viewport   = viewport_xy
                });
            }

            // Update scene transforms once per frame (not per view).
            std::shared_ptr<Scene_root> scene_root = get_scene_root();
            ERHE_VERIFY(scene_root);
            scene_root->get_scene().update_node_transforms();
            m_app_context.tools->get_tool_scene_root()->get_hosted_scene()->update_node_transforms();
            m_app_context.app_message_bus->hover_scene_view.send_message(Hover_scene_view_message{.scene_view = this});
            m_app_context.app_message_bus->render_scene_view.send_message(Render_scene_view_message{.scene_view = this});

            // Debug_renderer multi-camera setup (Phase 3 of the
            // multiview port). Build one erhe::renderer::View per eye
            // so Debug_renderer's compute can fan world-space line
            // submissions out to a per-eye triangle slab and the
            // multiview vertex shader can pick the right slab via
            // gl_ViewIndex inside the multiview render pass below.
            // Tool / Renderable submissions push lines via
            // Primitive_renderer once -- the lines are world-space and
            // identical across views.
            std::vector<erhe::renderer::View> debug_views;
            debug_views.reserve(view_inputs.size());
            for (const erhe::scene_renderer::Camera_view_input& view_input : view_inputs) {
                ERHE_VERIFY(view_input.projection != nullptr);
                ERHE_VERIFY(view_input.node       != nullptr);
                const erhe::scene::Transform clip_from_camera = view_input.projection->clip_from_node_transform(
                    view_input.viewport, get_reverse_depth(), get_depth_range(), get_conventions()
                );
                const glm::mat4              clip_from_world = clip_from_camera.get_matrix() * view_input.node->node_from_world();
                const glm::mat4              world_from_node = view_input.node->world_from_node();
                const erhe::scene::Projection::Fov_sides fov  = view_input.projection->get_fov_sides(view_input.viewport);
                debug_views.push_back(erhe::renderer::View{
                    .clip_from_world = clip_from_world,
                    .viewport        = glm::vec4{
                        static_cast<float>(viewport_xy.x),
                        static_cast<float>(viewport_xy.y),
                        static_cast<float>(viewport_xy.width),
                        static_cast<float>(viewport_xy.height)
                    },
                    .fov_sides = glm::vec4{fov.left, fov.right, fov.up, fov.down},
                    .view_position_in_world = glm::vec4{world_from_node[3]}
                });
            }
            m_app_context.debug_renderer->begin_frame(
                viewport_xy,
                std::span<const erhe::renderer::View>{debug_views}
            );

            // Render_context shared by the wide-line feed (below) and the
            // tool / renderable submission that follows. Encoder is
            // nullptr at this point; the wide-line feed only needs
            // viewport_config to resolve per-pass render styles, and the
            // tool / renderable submission stage does not need the encoder
            // for line accumulation. multiview_views is set so renderables
            // that are multiview-aware (Forward_renderer's compositor
            // path, Debug_renderer) pick the multiview shader stages.
            Render_context cpu_render_context {
                .command_buffer      = &views_cb,
                .encoder             = nullptr,
                .render_pass         = nullptr,
                .app_context         = m_app_context,
                .scene_view          = *this,
                .viewport_config     = m_viewport_config,
                .camera              = primary_camera,
                .viewport_scene_view = nullptr,
                .viewport            = viewport_xy,
                .views               = std::span<const erhe::scene_renderer::Camera_view_input>{view_inputs}
            };

            // Content wide-line compute path. Mirror viewport_scene_view's
            // setup but feed every view's clip_from_world to the renderer
            // via Camera_view_inputs so the compute shader writes one
            // triangle slab per eye to a single SSBO. The multiview
            // vertex shader then picks the per-eye slab via gl_ViewIndex
            // inside the layered render pass below.
            erhe::scene_renderer::Content_wide_line_renderer* content_wide_line_renderer = m_app_context.content_wide_line_renderer;
            const bool drive_wide_lines = (content_wide_line_renderer != nullptr) && content_wide_line_renderer->is_enabled();
            if (drive_wide_lines) {
                content_wide_line_renderer->begin_frame();

                erhe::scene::Scene* hosted_scene = scene_root->get_hosted_scene();
                if (hosted_scene != nullptr) {
                    erhe::graphics::Scoped_debug_group content_wide_line_renderer_debug_group{
                        views_cb,
                        "content_wide_line_renderer (multiview)"
                    };
                    auto feed_pass = [&](const Composition_pass* pass) {
                        if (pass == nullptr) {
                            return;
                        }
                        auto& data = pass->data;
                        if ((data.primitive_mode != erhe::primitive::Primitive_mode::edge_lines) || !data.enabled) {
                            return;
                        }
                        // Pass settings resolution mirrors Composition_pass /
                        // viewport_scene_view: explicit primitive_settings wins,
                        // otherwise derive from the render style. The default
                        // Primitive_interface_settings::constant_color0 is white,
                        // so omitting the get_render_style branch leaves render-
                        // style-driven passes (opaque_edge_lines_*) rendering
                        // white instead of the configured line color.
                        erhe::scene_renderer::Primitive_interface_settings settings;
                        if (data.primitive_settings.has_value()) {
                            settings = data.primitive_settings.value();
                        } else if (data.get_render_style) {
                            const Render_style_data& style = data.get_render_style(cpu_render_context);
                            settings = get_primitive_settings(style, data.primitive_mode);
                        }
                        const glm::vec4 color      = settings.constant_color0;
                        const float     line_width = settings.constant_size;
                        const auto&     filter     = data.filter;
                        const uint32_t  group      = data.content_wide_line_group;
                        for (const auto layer_id : data.mesh_layers) {
                            const auto mesh_layer = hosted_scene->get_mesh_layer_by_id(layer_id);
                            if (mesh_layer) {
                                for (const auto& mesh : mesh_layer->meshes) {
                                    if (filter(mesh->get_flag_bits())) {
                                        content_wide_line_renderer->add_mesh(*m_context.mesh_memory, *mesh, color, line_width, group);
                                    }
                                }
                            }
                        }
                    };
                    // Selection outline gets the breathing animation
                    // matching viewport_scene_view's feed loop so the
                    // headset and desktop mirror render the same
                    // pulsing colour / width. The outline pass uses
                    // explicit primitive_settings, so we patch the
                    // colour/width into the pass before feeding.
                    if (m_app_context.app_rendering->selection_outline) {
                        const int64_t   t0_ns         = m_app_context.time->get_host_system_time_ns();
                        const double    t0            = static_cast<double>(t0_ns) / 1'000'000'000.0;
                        const float     period        = 1.0f / m_viewport_config.selection_highlight_frequency;
                        const float     t1            = static_cast<float>(::fmod(t0, period));
                        const float     t2            = static_cast<float>(0.5f + (2.0f * std::abs(2.0f * (t1 / period - std::floor(t1 / period + 0.5f))) - 1.0f) * 0.5f);
                        const glm::vec4 outline_color = glm::mix(m_viewport_config.selection_highlight_low, m_viewport_config.selection_highlight_high, t2);
                        const float     outline_width = m_viewport_config.selection_highlight_width_low * (1.0f - t2) + m_viewport_config.selection_highlight_width_high * t2;
                        m_app_context.app_rendering->selection_outline->data.primitive_settings = erhe::scene_renderer::Primitive_interface_settings{
                            .color_source    = erhe::scene_renderer::Primitive_color_source::constant_color,
                            .constant_color0 = outline_color,
                            .size_source     = erhe::scene_renderer::Primitive_size_source::constant_size,
                            .constant_size   = outline_width
                        };
                    }
                    feed_pass(m_app_context.app_rendering->selection_outline.get());
                    feed_pass(m_app_context.app_rendering->edge_lines_not_selected.get());
                    feed_pass(m_app_context.app_rendering->edge_lines_selected.get());
                    feed_pass(m_app_context.app_rendering->translucent_outline.get());
                }
            }

            m_app_context.tools        ->render_viewport_tools(cpu_render_context);
            m_app_context.app_rendering->render_viewport_renderables(cpu_render_context);

            // Combined compute pass for both renderers. Either one (or
            // both) may have queued work; the barrier below covers SSBO
            // and vertex-attribute readers downstream regardless of
            // which compute ran. Skipping when neither needs compute
            // avoids a stray Compute_command_encoder + barrier on
            // backends that gate compute.
            // Wide lines only dispatch compute on the compute backend; the
            // geometry-shader backend expands lines at render time (compute()
            // is a no-op) and runs on devices without glMemoryBarrier, so it
            // must not drive the compute encoder / barrier here.
            const bool wide_lines_compute = drive_wide_lines && content_wide_line_renderer->uses_compute();
            const bool need_compute = wide_lines_compute || m_app_context.debug_renderer->use_compute();
            if (need_compute) {
                {
                    erhe::graphics::Compute_command_encoder compute_encoder = m_app_context.graphics_device->make_compute_command_encoder(views_cb);
                    if (wide_lines_compute) {
                        content_wide_line_renderer->set_view_params(
                            std::span<const erhe::scene_renderer::Camera_view_input>{view_inputs},
                            get_reverse_depth(),
                            get_depth_range(),
                            get_conventions()
                        );
                        // Hand the per-frame joint UBO/SSBO range to the
                        // renderer so the compute pre-pass poses skinned
                        // content edge lines / selection outlines. Without
                        // this the skinned line geometry is generated in
                        // bind (T) pose. Mirrors viewport_scene_view: the
                        // update allocates a disjoint ring range from the one
                        // Forward_renderer's later render() requests.
                        erhe::scene::Scene* scene_for_joints = scene_root->get_hosted_scene();
                        if ((scene_for_joints != nullptr) && (m_app_context.forward_renderer != nullptr)) {
                            erhe::scene_renderer::Joint_buffer& joint_buffer = m_app_context.forward_renderer->get_joint_buffer();
                            erhe::graphics::Ring_buffer_range joint_buffer_range = joint_buffer.update(
                                glm::uvec4{0, 0, 0, 0}, {}, scene_for_joints->get_skins()
                            );
                            content_wide_line_renderer->set_joint_buffer(&joint_buffer, std::move(joint_buffer_range));
                        }
                        content_wide_line_renderer->compute(compute_encoder);
                    }
                    // Debug_renderer::compute() early-returns when
                    // use_compute is false, so calling it
                    // unconditionally here is safe.
                    m_app_context.debug_renderer->compute(compute_encoder);
                }
                // Compute -> vertex barrier. The single-view per-eye
                // path reads the triangle buffer via the input
                // assembler (vertex_attrib_array_barrier_bit). The
                // multiview path's vertex shader reads it as a
                // read-only SSBO instead (shader_storage_barrier_bit)
                // so the multiview vertex stage can index the per-eye
                // slab via gl_ViewIndex. Issue both bits because
                // multiview content rendering, multiview Debug_renderer,
                // and (single-view) tool / mirror dispatches share
                // this command buffer.
                // Must be emitted after the compute encoder scope ends;
                // on Metal the cb cannot be split while a compute
                // encoder is open.
                views_cb.memory_barrier(
                    erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                    erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
                );
            }

            erhe::graphics::Render_pass_descriptor render_pass_descriptor{};
            render_pass_descriptor.color_attachments[0].texture       = color_texture;
            render_pass_descriptor.color_attachments[0].load_action   = erhe::graphics::Load_action::Clear;
            render_pass_descriptor.color_attachments[0].store_action  = erhe::graphics::Store_action::Store;
            render_pass_descriptor.color_attachments[0].usage_before  = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
            render_pass_descriptor.color_attachments[0].layout_before = erhe::graphics::Image_layout::color_attachment_optimal;
            render_pass_descriptor.color_attachments[0].usage_after   = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
            render_pass_descriptor.color_attachments[0].layout_after  = erhe::graphics::Image_layout::color_attachment_optimal;
            render_pass_descriptor.color_attachments[0].clear_value   = {0.05, 0.10, 0.20, 1.0};
            if (depth_stencil_texture != nullptr) {
                render_pass_descriptor.depth_attachment.texture       = depth_stencil_texture;
                render_pass_descriptor.depth_attachment.load_action   = erhe::graphics::Load_action::Clear;
                // Clear to the convention's far value (reverse-Z 0.0, forward-Z 1.0).
                // Without this it defaults to 0.0, which under forward-Z is the near
                // plane, so every fragment fails the depth test and only the clear
                // color shows (no 3D scene).
                render_pass_descriptor.depth_attachment.clear_value[0] = get_reverse_depth() ? 0.0 : 1.0;
                render_pass_descriptor.depth_attachment.store_action  = erhe::graphics::Store_action::Store;
                render_pass_descriptor.depth_attachment.usage_before  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                render_pass_descriptor.depth_attachment.layout_before = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                render_pass_descriptor.depth_attachment.usage_after   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                render_pass_descriptor.depth_attachment.layout_after  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                // When the depth swapchain format carries a stencil aspect (d24_s8,
                // d32_s8), declare the stencil attachment so stencilLoadOp = CLEAR
                // and the per-frame stencil starts at 0. Without this the Vulkan
                // render pass leaves stencilLoadOp = DONT_CARE and stencil contents
                // leak across frames, breaking the selection-outline scheme that
                // tests bit 7 written by polygon_fill_standard_selected_*. The
                // per-eye path mirrors this in headset_view_resources.cpp.
                if (erhe::dataformat::get_stencil_size_bits(depth_stencil_texture->get_pixelformat()) > 0) {
                    render_pass_descriptor.stencil_attachment.texture       = depth_stencil_texture;
                    render_pass_descriptor.stencil_attachment.load_action   = erhe::graphics::Load_action::Clear;
                    render_pass_descriptor.stencil_attachment.store_action  = erhe::graphics::Store_action::Dont_care;
                    render_pass_descriptor.stencil_attachment.usage_before  = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                    render_pass_descriptor.stencil_attachment.layout_before = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                    render_pass_descriptor.stencil_attachment.usage_after   = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                    render_pass_descriptor.stencil_attachment.layout_after  = erhe::graphics::Image_layout::depth_stencil_attachment_optimal;
                }
            }
            render_pass_descriptor.render_target_width  = static_cast<int>(frame.width);
            render_pass_descriptor.render_target_height = static_cast<int>(frame.height);
            render_pass_descriptor.view_mask            = frame.view_mask;
            // Fixed foveated rendering: attach the shared (2D-array) fragment
            // density map when present (nullptr otherwise; ignored on non-Vulkan).
            render_pass_descriptor.fragment_density_map_texture = frame.shared_fragment_density_map_texture;
            render_pass_descriptor.debug_label          = erhe::utility::Debug_label{"XR multiview"};

            erhe::graphics::Render_pass multiview_render_pass{*m_app_context.graphics_device, render_pass_descriptor};

            {
                erhe::graphics::Render_command_encoder encoder = m_app_context.graphics_device->make_render_command_encoder(views_cb);
                erhe::graphics::Scoped_render_pass scoped_render_pass{multiview_render_pass, views_cb};

                Render_context render_context {
                    .command_buffer      = &views_cb,
                    .encoder             = &encoder,
                    .render_pass         = &multiview_render_pass,
                    .app_context         = m_app_context,
                    .scene_view          = *this,
                    .viewport_config     = m_viewport_config,
                    .camera              = primary_camera,
                    .viewport_scene_view = nullptr,
                    .viewport            = viewport_xy,
                    .shader_debug        = static_cast<erhe::scene_renderer::Shader_debug>(m_selected_shader_debug),
                    .views               = std::span<const erhe::scene_renderer::Camera_view_input>{view_inputs}
                };
                m_app_context.app_rendering->render_composer(render_context);

                // Debug_renderer multiview render. Inside the multiview
                // render pass so gl_ViewIndex resolves to the right
                // layer for each draw fanned out by the compute stage
                // above. multiview = true picks the multiview-compiled
                // shader stages and the multiview bind group layout.
                m_app_context.debug_renderer->render(
                    encoder, multiview_render_pass, viewport_xy, /*multiview=*/true
                );
            }
            if (drive_wide_lines) {
                content_wide_line_renderer->end_frame();
            }
            m_app_context.debug_renderer->end_frame();
            return true;
        };
        m_headset->render_multiview(command_buffer, multiview_callback);
        ++m_frame_number;
        if (m_renderdoc_capture_started) {
            m_app_context.graphics_device->end_frame_capture();
            m_renderdoc_capture_started = false;
        }
        m_headset->end_frame(m_frame_timing.should_render);
        return true;
    }

    if (m_frame_timing.should_render) {
        bool first_view = true;
        auto callback = [this, &first_view](erhe::xr::Render_view& render_view, erhe::graphics::Command_buffer& view_cb) -> bool {
            const std::shared_ptr<Headset_view_resources>& view_resources = get_headset_view_resources(render_view);
            if (!view_resources->is_valid()) {
                return false;
            }

            erhe::scene::Projection::Fov_sides fov_sides{render_view.fov_left, render_view.fov_right, render_view.fov_up, render_view.fov_down};
            if (m_context.OpenXR_mirror && first_view) {
                erhe::window::Context_window* window = m_context.context_window;
                //constexpr float near_value = 1.0f;
                float left_over_near  = std::tan(render_view.fov_left );
                float right_over_near = std::tan(render_view.fov_right);
                float up_over_near    = std::tan(render_view.fov_up   );
                float down_over_near  = std::tan(render_view.fov_down );
                float left            = /* near_value * */ left_over_near;
                float right           = /* near_value * */ right_over_near;
                float up              = /* near_value * */ up_over_near;
                float down            = /* near_value * */ down_over_near;
                float desired_aspect  = static_cast<float>(window->get_width()) / static_cast<float>(window->get_height());
                // (expansion + right - left) / (up - down) = desired_aspect   || * (up - down)
                // expansion + right - left = desired_aspect * (up - down)     || -right +left
                // expansion = desired_aspect * (up - donw) - right + left
                float expansion       = desired_aspect * (up - down) - right + left;
                float expanded_left   = left - 0.5f * expansion;
                float expanded_right  = right + 0.5f * expansion;
                erhe::scene::Projection::Fov_sides expanded_fov_sides{
                    std::atan(expanded_left),
                    std::atan(expanded_right),
                    render_view.fov_up,
                    render_view.fov_down
                };
                view_resources->update(render_view, expanded_fov_sides);
            } else {
                view_resources->update(render_view, fov_sides);
            }

            // Update scene node transforms
            // TODO can we do most of this only once per frame, not for each view?
            {
#if 1
                std::shared_ptr<Scene_root> scene_root = get_scene_root();
                ERHE_VERIFY(scene_root);
                scene_root->get_scene().update_node_transforms();
#endif
                m_context.tools->get_tool_scene_root()->get_hosted_scene()->update_node_transforms();

                // TODO Consider multiple scene view being able to be (hover) active
                //      (viewport window and headset view).
                m_context.app_message_bus->hover_scene_view.send_message(
                    Hover_scene_view_message{.scene_view = this}
                );
                m_context.app_message_bus->render_scene_view.send_message(
                    Render_scene_view_message{.scene_view = this}
                );
            }

            auto& graphics_device = *m_context.graphics_device;
            erhe::graphics::Render_pass* mirror_render_pass = m_mirror_mode_window_render_pass.get();
            erhe::graphics::Render_pass* view_render_pass   = view_resources->get_render_pass();
            erhe::graphics::Render_pass* render_pass        = view_render_pass;
            erhe::math::Viewport viewport{
                .x      = 0,
                .y      = 0,
                .width  = static_cast<int>(render_view.width),
                .height = static_cast<int>(render_view.height)
            };

            erhe::scene::Camera* view_camera = view_resources->get_camera();
            const erhe::scene_renderer::Camera_view_input single_view_input{
                .projection = view_camera->projection(),
                .node       = view_camera->get_node(),
                .viewport   = viewport
            };
            Render_context render_context {
                .command_buffer      = &view_cb,
                .encoder             = nullptr, // filled in later once we start render pass
                .app_context         = m_context,
                .scene_view          = *this,
                .viewport_config     = m_viewport_config,
                .camera              = view_camera,
                .viewport_scene_view = nullptr,
                .viewport            = viewport,
                .shader_debug        = static_cast<erhe::scene_renderer::Shader_debug>(m_selected_shader_debug),
                .views               = std::span<const erhe::scene_renderer::Camera_view_input>(&single_view_input, 1)
            };
            const erhe::renderer::View debug_view = erhe::renderer::Debug_renderer::view_from_camera(
                *render_context.camera,
                render_context.viewport,
                m_context.graphics_device->get_info().coordinate_conventions
            );
            m_context.debug_renderer->begin_frame(
                render_context.viewport,
                std::span<const erhe::renderer::View>(&debug_view, 1)
            );
            m_context.tools         ->render_viewport_tools(render_context);
            m_context.app_rendering ->render_viewport_renderables(render_context);

            if (m_context.debug_renderer->use_compute()) {
                {
                    erhe::graphics::Compute_command_encoder compute_encoder = graphics_device.make_compute_command_encoder(view_cb);
                    m_context.debug_renderer->compute(compute_encoder);
                }
                // Compute -> vertex barrier. The unified debug-line
                // vertex shader reads pre-transformed triangles via SSBO
                // (shader_storage_barrier_bit); legacy debug-line paths
                // may still consume the vertex buffer through the input
                // assembler (vertex_attrib_array_barrier_bit). Must be
                // emitted after the compute encoder scope ends.
                view_cb.memory_barrier(
                    erhe::graphics::Memory_barrier_mask::vertex_attrib_array_barrier_bit |
                    erhe::graphics::Memory_barrier_mask::shader_storage_barrier_bit
                );
            }

            {
                erhe::graphics::Render_command_encoder encoder = graphics_device.make_render_command_encoder(view_cb);
                erhe::graphics::Scoped_render_pass scoped_render_pass{*render_pass, view_cb};
                render_context.encoder     = &encoder;
                render_context.render_pass = render_pass;
                ERHE_VERIFY(render_view.width  == static_cast<uint32_t>(render_pass->get_render_target_width()));
                ERHE_VERIFY(render_view.height == static_cast<uint32_t>(render_pass->get_render_target_height()));

                // TODO This conflicts with hud grab - proper command for this?
                //
                // const auto* squeeze_click = m_headset->get_actions_right().squeeze_click;
                // const auto* squeeze_value = m_headset->get_actions_right().squeeze_value;
                // if (
                //     ((squeeze_click != nullptr) && (squeeze_click->state.currentState == XR_TRUE)) ||
                //     ((squeeze_value != nullptr) && (squeeze_value->state.currentState >= 0.5f))
                // ) {
                //     gl::clear_color(0.0f, 0.0f, 0.0f, 0.0f);
                //     gl::clear(gl::Clear_buffer_mask::color_buffer_bit);
                //     render = false;
                // }

                // When in mirror mode, speed up rendering by only rendering first view
                if (!m_context.OpenXR_mirror || first_view) {
                    m_context.app_rendering ->render_composer(render_context);
                    m_context.debug_renderer->render(encoder, *render_pass, render_context.viewport);
                }
            } // end of render encoder scope - end of render pass
            m_context.debug_renderer->end_frame();

            if (m_context.OpenXR_mirror && first_view) {
                // Copy from OpenXR to default framebuffer (window)
                int src_width  = view_resources->get_width();
                int src_height = view_resources->get_height();
                int dst_width  = m_context.context_window->get_width();
                int dst_height = m_context.context_window->get_height();

                // - fit all one to one if possible
                // - pad if not enough in src
                // - crop if too much in src
                int width_diff = std::abs(src_width - dst_width);
                int src_x0 = (src_width > dst_width ) ? width_diff / 2     : 0;
                //int src_x1 = (src_width > dst_width ) ? src_x0 + dst_width : src_width;
                int dst_x0 = (src_width > dst_width ) ? 0                  : width_diff / 2;
                //int dst_x1 = (src_width > dst_width ) ? dst_width          : dst_x0 + src_width;

                int height_diff = std::abs(src_height - dst_height);
                int src_y0 = (src_height > dst_height) ? height_diff / 2     : 0;
                //int src_y1 = (src_height > dst_height) ? src_y0 + dst_height : src_height;
                int dst_y0 = (src_height > dst_height) ? 0                   : height_diff / 2;
                //int dst_y1 = (src_height > dst_height) ? dst_height          : src_y0 + src_height;

                {
                    erhe::graphics::Blit_command_encoder blit_encoder = graphics_device.make_blit_command_encoder(view_cb);
                    blit_encoder.blit_framebuffer(
                        *view_render_pass,
                        glm::ivec2{src_x0, src_y0},
                        glm::ivec2{src_width, src_height},
                        *mirror_render_pass,
                        glm::ivec2{dst_x0, dst_y0}
                    );
                }

                /// const GLint src_fbo = view_render_pass->gl_name();
                /// const GLint dst_fbo = mirror_render_pass->gl_name();
                /// 
                /// auto get = [&mirror_render_pass](gl::Framebuffer_attachment_parameter_name parameter) -> GLint {
                ///     GLint value{0};
                ///     gl::get_named_framebuffer_attachment_parameter_iv(
                ///         mirror_render_pass->gl_name(),
                ///         gl::Framebuffer_attachment::back_left,
                ///         parameter,
                ///         &value
                ///     );
                ///     return value;
                /// };
                /// 
                /// gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, src_fbo);
                /// gl::named_framebuffer_read_buffer(src_fbo, gl::Color_buffer::color_attachment0);
                /// gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, dst_fbo);
                /// const gl::Color_buffer draw_buffer = gl::Color_buffer::back;
                /// gl::named_framebuffer_draw_buffers(dst_fbo, 1, &draw_buffer);
                /// 
                /// gl::blit_framebuffer(
                ///     src_x0, src_y0, src_x1, src_y1,
                ///     dst_x0, dst_y0, dst_x1, dst_y1,
                ///     gl::Clear_buffer_mask::color_buffer_bit, gl::Blit_framebuffer_filter::nearest
                /// );
#if defined(ERHE_GRAPHICS_API_OPENGL)
                m_context_window.swap_buffers();
#endif // TODO
            }
            first_view = false;

            return true;
        };
        m_headset->render(command_buffer, callback);
        ++m_frame_number;
    }

    if (m_renderdoc_capture_started) {
        m_app_context.graphics_device->end_frame_capture();
        m_renderdoc_capture_started = false;
    }

    m_headset->end_frame(m_frame_timing.should_render);

    // Paranoid
    m_frame_timing = erhe::xr::Frame_timing{};

    return true;
}

void Headset_view::setup_root_camera()
{
    ERHE_PROFILE_FUNCTION();

    const glm::mat4 m = erhe::math::create_look_at(
        glm::vec3{0.0f, 0.0f,  0.0f}, // eye
        glm::vec3{0.0f, 0.0f, -1.0f}, // look at
        glm::vec3{0.0f, 1.0f,  0.0f}  // up
    );

    m_root_node = get_scene_root()->get_scene().get_root_node();
    m_headset_node = std::make_shared<erhe::scene::Node>("Headset Root Node");
    m_headset_node->set_parent(m_root_node);
    m_headset_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::visible);

    m_root_camera = std::make_shared<erhe::scene::Camera>("Root Camera");
    m_root_camera->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::visible);
    auto& projection = *m_root_camera->projection();
    projection.fov_y           = glm::radians(35.0f);
    projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
    projection.z_near          = 0.03f;
    projection.z_far           = 200.0f;
    m_headset_node->attach(m_root_camera);

    setup_pointer_pick_camera();
}

void Headset_view::setup_pointer_pick_camera()
{
    // Dedicated single-view camera used only as a projection source for the
    // Id_renderer skinned-mesh pick (see update_id_render). It is placed at
    // the controller aim pose each frame and looks down the control ray
    // (-Z). A narrow vertical fov concentrates angular resolution around the
    // ray so the centre texel samples the surface the controller points at.
    // No content / visible flags: it must never be composited or listed.
    m_pointer_pick_node   = std::make_shared<erhe::scene::Node>("Pointer Pick Camera Node");
    m_pointer_pick_camera = std::make_shared<erhe::scene::Camera>("Pointer Pick Camera");
    erhe::scene::Projection& projection = *m_pointer_pick_camera->projection();
    projection.projection_type = erhe::scene::Projection::Type::perspective_vertical;
    projection.fov_y           = glm::radians(10.0f);
    projection.z_near          = 0.03f;
    projection.z_far           = 200.0f;
    m_pointer_pick_node->attach(m_pointer_pick_camera);
    m_pointer_pick_node->set_parent(m_root_node);
}

void Headset_view::update_camera_node()
{
    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return;
    }

    glm::vec3 position{};
    glm::quat orientation{};
    if (!m_headset->get_headset_pose(position, orientation)) {
        return;
    }
    const glm::mat4 m_orientation   = glm::mat4_cast(orientation);
    const glm::mat4 m_translation   = glm::translate(glm::mat4{1}, position + get_camera_offset());
    const glm::mat4 world_from_view = m_translation * m_orientation;
    m_headset_node->set_world_from_node(world_from_view);
    m_headset_node->update_transform(0); // TODO
}

auto Headset_view::get_camera_offset() const -> glm::vec3
{
    return m_camera_offset;
}

auto Headset_view::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    ERHE_VERIFY(get_scene_root());
    return m_root_node;
}

auto Headset_view::is_active() const -> bool
{
    return (m_headset != nullptr) ? m_headset->is_active() : false;
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return m_root_camera;
}

auto Headset_view::get_perspective_scale() const -> float
{
    return 1.0f; // TODO
}

auto Headset_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return m_rendergraph_node.get();
}

auto Headset_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return m_shadow_render_node.get();
}

void Headset_view::add_finger_input(const Finger_point& finger_input)
{
    m_finger_inputs.push_back(finger_input);
}

auto Headset_view::finger_to_viewport_distance_threshold() const -> float
{
    return m_finger_to_viewport_distance_threshold;
}

auto Headset_view::get_headset() const -> erhe::xr::Headset*
{
    return m_headset;
}

auto Headset_view::poll_events() -> bool
{
    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return false;
    }
    m_poll_events_ok = m_headset->poll_events();
    return m_poll_events_ok;
}

auto Headset_view::update_actions() -> bool
{
    ERHE_PROFILE_FUNCTION();

    m_update_actions_ok = false;
    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return false;
    }

    if (!m_poll_events_ok) {
        return false;
    }

    m_finger_inputs.clear();

    if (!m_frame_timing.begin_ok) {
        return false;
    }

    if (!m_headset->update_actions()) {
        return false;
    }

    update_camera_node();

    update_pointer_context_from_controller();
    erhe::xr::Xr_instance* instance = m_headset->get_xr_instance();
    if (instance == nullptr) {
        return false;
    }
    erhe::xr::Xr_session* session = m_headset->get_xr_session();
    if (session == nullptr) {
        return false;
    }
    const XrSession xr_session = session->get_xr_session();

    // Lazy: once the session has enumerated XR_FB_performance_metrics
    // counters, build one Plot per counter and hand them to the
    // Performance window. Done once per Headset_view lifetime.
    if (!m_xr_perf_plots_registered &&
        (m_app_context.performance_window != nullptr) &&
        instance->extensions.META_performance_metrics)
    {
        const std::vector<erhe::xr::Xr_perf_counter>& counters = session->get_perf_counters();
        if (!counters.empty()) {
            m_xr_perf_metric_plots.reserve(counters.size());
            for (const erhe::xr::Xr_perf_counter& c : counters) {
                auto plot = std::make_unique<Xr_perf_metric_plot>(&c);
                m_app_context.performance_window->register_plot(plot.get());
                m_xr_perf_metric_plots.push_back(std::move(plot));
            }
            m_xr_perf_plots_registered = true;
        }
    }

    // Apply the user-configured CPU/GPU performance level (idempotent: only
    // re-issues the call when the configured level differs from the last
    // applied one). Then drain any thermal-warning event the runtime sent
    // since the last tick and step the level down if requested.
    if (m_app_context.editor_settings != nullptr) {
        const Headset_config& cfg = m_app_context.editor_settings->headset;
        int cpu_level = perf_settings_level_to_xr(cfg.cpu_performance_level);
        int gpu_level = perf_settings_level_to_xr(cfg.gpu_performance_level);

        if ((cpu_level != m_applied_cpu_level) || (gpu_level != m_applied_gpu_level)) {
            instance->apply_performance_level(xr_session, cpu_level, gpu_level);
            m_applied_cpu_level = cpu_level;
            m_applied_gpu_level = gpu_level;
        }

        std::optional<XrEventDataPerfSettingsEXT> warning = instance->take_latest_thermal_warning();
        if (warning.has_value() && cfg.boost_on_thermal_warning) {
            // Step down the affected domain only.
            int new_cpu = m_applied_cpu_level;
            int new_gpu = m_applied_gpu_level;
            if (warning->domain == XR_PERF_SETTINGS_DOMAIN_CPU_EXT && (m_applied_cpu_level >= 0)) {
                new_cpu = step_down_perf_level(m_applied_cpu_level);
            } else if (warning->domain == XR_PERF_SETTINGS_DOMAIN_GPU_EXT && (m_applied_gpu_level >= 0)) {
                new_gpu = step_down_perf_level(m_applied_gpu_level);
            }
            if ((new_cpu != m_applied_cpu_level) || (new_gpu != m_applied_gpu_level)) {
                instance->apply_performance_level(xr_session, new_cpu, new_gpu);
                m_applied_cpu_level = new_cpu;
                m_applied_gpu_level = new_gpu;
            }
        }
    }

    // Inject XR input events
    std::vector<erhe::window::Input_event>& input_events = m_context.context_window->get_input_events();
    for (erhe::xr::Xr_action_boolean& action : instance->get_boolean_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_boolean_event,
                    .timestamp_ns = 0, // TODO
                    .u = {
                        .xr_boolean_event = {
                            .action = &action,
                            .value  = action.state.currentState == XR_TRUE
                        }
                    }
                }
            );
        }
    }
    for (erhe::xr::Xr_action_float& action : instance->get_float_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_float_event,
                    .timestamp_ns = 0, // TODO
                    .u = {
                        .xr_float_event = {
                            .action = &action,
                            .value  = action.state.currentState
                        }
                    }
                }
            );
        }
    }
    for (erhe::xr::Xr_action_vector2f& action : instance->get_vector2f_actions()) {
        action.get(xr_session);
        if (action.state.changedSinceLastSync == XR_TRUE) {
            input_events.push_back(
                erhe::window::Input_event{
                    .type = erhe::window::Input_event_type::xr_vector2f_event,
                    .timestamp_ns = 0, // TODO
                    .u = {
                        .xr_vector2f_event = {
                            .action = &action,
                            .x = action.state.currentState.x,
                            .y = action.state.currentState.y
                        }
                    }
                }
            );
        }
    }

    if (m_controller_visualization) {
        // TODO both controllers
        erhe::xr::Xr_actions*     left_actions   = m_headset->get_actions_left();
        erhe::xr::Xr_actions*     right_actions  = m_headset->get_actions_right();
        erhe::xr::Xr_action_pose* left_aim_pose  = (left_actions  != nullptr) ? left_actions ->aim_pose : nullptr;
        erhe::xr::Xr_action_pose* right_aim_pose = (right_actions != nullptr) ? right_actions->aim_pose : nullptr;
        auto* pose = ((right_aim_pose != nullptr) && (right_aim_pose->location.locationFlags != 0)) ? right_aim_pose : left_aim_pose;
        if (pose != nullptr) {
            erhe::xr::Xr_action_pose pose_with_offset = *pose;
            pose_with_offset.position += get_camera_offset();
            m_controller_visualization->update(&pose_with_offset);
        }
    }
    //if (m_hand_tracker)
    //{
    //    m_hand_tracker->update_hands(*m_headset.get());
    //}
    m_update_actions_ok = true;
    return true;
}

void Headset_view::request_renderdoc_capture()
{
    m_request_renderdoc_capture = true;
}

void Headset_view::end_frame()
{
    if (!m_context.OpenXR || (m_headset == nullptr)) {
        return;
    }

    m_finger_inputs.clear();

    // TODO These should be done when session state changes so that polling no longer happens
    erhe::xr::Xr_instance* instance = m_headset->get_xr_instance();
    if (instance == nullptr) {
        return;
    }
    for (erhe::xr::Xr_action_boolean& action : instance->get_boolean_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (erhe::xr::Xr_action_float& action : instance->get_float_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
    for (erhe::xr::Xr_action_vector2f& action : instance->get_vector2f_actions()) {
        action.state.changedSinceLastSync = XR_FALSE;
    }
}

}
