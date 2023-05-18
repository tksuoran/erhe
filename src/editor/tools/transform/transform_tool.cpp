#include "tools/transform/transform_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp" // need to be able to pass to visualization
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/move_tool.hpp"
#include "tools/transform/rotate_tool.hpp"
#include "tools/transform/scale_tool.hpp"
#include "windows/operations.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/raytrace/ray.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::normalize;
using glm::cross;
using glm::dot;
using glm::distance;
using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

#pragma region Commands

Transform_tool_drag_command::Transform_tool_drag_command()
    : Command{"Transform_tool.drag"}
{
}

void Transform_tool_drag_command::try_ready()
{
    if (g_transform_tool->on_drag_ready()) {
        set_ready();
    }
}

auto Transform_tool_drag_command::try_call() -> bool
{
    if (get_command_state() == erhe::application::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::application::State::Active) {
        return false; // We might be ready, but not consuming event yet
    }

    const bool still_active = g_transform_tool->on_drag();
    if (!still_active) {
        set_inactive();
    }
    return still_active;
}

void Transform_tool_drag_command::on_inactive()
{
    log_trs_tool->trace("TRS on_inactive");

    if (get_command_state() != erhe::application::State::Inactive) {
        g_transform_tool->end_drag();
    }
}

#pragma endregion Commands

Transform_tool* g_transform_tool{nullptr};

Transform_tool::Transform_tool()
    : Imgui_window                  {c_title}
    , erhe::components::Component   {c_type_name}
    , m_drag_redirect_update_command{m_drag_command}
    , m_drag_enable_command         {m_drag_redirect_update_command}
{
}

Transform_tool::~Transform_tool() noexcept
{
    ERHE_VERIFY(g_transform_tool == nullptr);
}

void Transform_tool::deinitialize_component()
{
    ERHE_VERIFY(g_transform_tool == this);
    m_drag_command.set_host(nullptr);
    shared.entries.clear();
    shared.visualization.reset();
    m_tool_node.reset();

    g_transform_tool = nullptr;
}

void Transform_tool::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<Editor_message_bus>();
    require<Editor_scenes >();
    require<Mesh_memory   >();
    require<Selection_tool>();
    require<Tools         >();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    require<Headset_view  >();
#endif
}

void Transform_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(g_transform_tool == nullptr);
    g_transform_tool = this; // visualizations needs config

    auto ini = erhe::application::get_ini("erhe.ini", "transform_tool");
    auto& settings = shared.settings;
    ini->get("scale",          settings.gizmo_scale);
    ini->get("show_translate", settings.show_translate);
    ini->get("show_rotate",    settings.show_rotate);

    const erhe::application::Scoped_gl_context gl_context;

    const auto& tool_scene_root = g_tools->get_tool_scene_root();
    if (!tool_scene_root) {
        return;
    }
    shared.visualization.emplace(*this);

    set_base_priority(c_priority);
    set_description  (c_title);

    g_tools->register_tool(this);

    erhe::application::g_imgui_windows->register_imgui_window(this, "Transform Tool");

    auto& commands = *erhe::application::g_commands;
    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_left, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    const auto* headset = g_headset_view->get_headset();
    if (headset != nullptr) {
        auto& xr_right = headset->get_actions_right();
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.trigger_click, erhe::application::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right.a_click,       erhe::application::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#endif

    g_editor_message_bus->add_receiver(
        [&](Editor_message& message)
        {
            on_message(message);
        }
    );

    m_drag_command.set_host(this);
}

void Transform_tool::on_message(Editor_message& message)
{
    Tool::on_message(message);

    using namespace erhe::toolkit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_mesh)) {
        update_hover();
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_selection)) {
        update_target_nodes();
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view)) {
        update_for_view(message.scene_view);
    }
}


//// void Transform_tool::set_local(const bool local)
//// {
////     m_visualization->set_local(local);
//// }

void Transform_tool::viewport_toolbar(bool& hovered)
{
    if (g_icon_set != nullptr) {
        shared.visualization->viewport_toolbar(hovered);
    }
}

auto Transform_tool::is_transform_tool_active() const -> bool
{
    return (m_active_tool == nullptr)
        ? false
        : m_active_tool->is_active();
}

void Transform_tool::imgui()
{
    shared.visualization->imgui();
#if defined(ERHE_GUI_LIBRARY_IMGUI)

    ImGui::Separator();

    ImGui::Checkbox("Cast Rays", &shared.settings.cast_rays);

    ImGui::Text("Hover handle: %s", c_str(m_hover_handle));
    ImGui::Text("Active handle: %s", c_str(m_active_handle));

    g_move_tool->imgui();

    ImGui::Separator();
#endif
}

void Transform_tool::update_target_nodes()
{
    const auto& selection = g_selection_tool->get_selection();

    glm::vec3 cumulative_translation{0.0f, 0.0f, 0.0f};
    glm::quat cumulative_rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 cumulative_scale{1.0f, 1.0f, 1.0f};
    std::size_t node_count{0};
    shared.entries.clear();
    for (const auto& item : selection) {
        std::shared_ptr<erhe::scene::Node> node = as_node(item);
        if (!node) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        glm::vec3 scale;
        glm::quat orientation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(world_from_node, scale, orientation, translation, skew, perspective);
        cumulative_translation += translation;
        cumulative_rotation = orientation; //cumulative_rotation *= orientation;
        cumulative_scale += scale;
        ++node_count;
        shared.entries.push_back(
            Entry{
                .node                    = node,
                .parent_from_node_before = node->parent_from_node_transform()
            }
        );
    }

    Anchor_state state;
    state.pivot_point_in_world = cumulative_translation / static_cast<float>(node_count);
    state.anchor_translation   = erhe::toolkit::create_translation<float>(state.pivot_point_in_world);
    state.anchor_rotation      = cumulative_rotation; // glm::pow(cumulative_rotation, 1.0f / static_cast<float>(node_count));
    //m_anchor_scale         = erhe::toolkit::create_scale<float>(cumulative_scale / static_cast<float>(node_count));
    state.world_from_anchor    = state.anchor_translation * glm::mat4{state.anchor_rotation};// * anchor_scale;

    shared.anchor_state_initial = state;
    shared.world_from_anchor    = state.world_from_anchor;
    shared.anchor_from_world    = glm::inverse(shared.world_from_anchor); // TODO compute directly

    shared.visualization->set_anchor(
        erhe::scene::Transform{state.world_from_anchor}
    );
    shared.visualization->update_visibility();
}

void Transform_tool::update_world_from_anchor_transform(
    const mat4& updated_world_from_anchor
)
{
    glm::mat4 updated_anchor_from_world = glm::inverse(updated_world_from_anchor);
    for (auto& entry : shared.entries) {
        const auto& node = entry.node;
        if (!node) {
            return;
        }

        const glm::mat4 previous_anchor_from_node = shared.anchor_from_world  * node->world_from_node();
        const glm::mat4 updated_world_from_node   = updated_world_from_anchor * previous_anchor_from_node;

        const auto& parent = node->parent().lock();
        const mat4 parent_from_world = parent
            ? mat4{parent->node_from_world()} * updated_world_from_node
            : updated_world_from_node;
        node->set_parent_from_node(mat4{parent_from_world});
    }

    shared.world_from_anchor = updated_world_from_anchor;
    shared.anchor_from_world = glm::inverse(shared.world_from_anchor);

    shared.visualization->set_anchor(erhe::scene::Transform{updated_world_from_anchor});
}

void Transform_tool::update_hover()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        m_hover_handle = Handle::e_handle_none;
        return;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || !tool.mesh) {
        if (m_hover_handle != Handle::e_handle_none) {
            m_hover_handle = Handle::e_handle_none;
        }
        return;
    }

    const auto new_handle = get_handle(tool.mesh.get());
    if (m_hover_handle == new_handle) {
        return;
    }
    m_hover_handle = get_handle(tool.mesh.get());

    m_hover_tool = [&]() -> Subtool* {
        switch (get_handle_tool(m_hover_handle)) {
            case Handle_tool::e_handle_tool_none     : return nullptr;
            case Handle_tool::e_handle_tool_translate: return g_move_tool;
            case Handle_tool::e_handle_tool_rotate   : return g_rotate_tool;
            case Handle_tool::e_handle_tool_scale    : return g_scale_tool;
            default                                  : return nullptr;
        }
    }();
}

auto Transform_tool::on_drag() -> bool
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_trs_tool->trace("TRS no scene view");
        end_drag();
        return false;
    }

    if (m_active_tool == nullptr) {
        return false;
    }

    return m_active_tool->update(scene_view);
}

auto Transform_tool::on_drag_ready() -> bool
{
    log_trs_tool->trace("TRS on_drag_ready");

    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_trs_tool->trace("Transform tool cannot start drag - Hover scene is not set");
        return false;
    }
    const auto camera = scene_view->get_camera();
    if (!camera) {
        log_trs_tool->trace("Transform tool cannot start drag - Scene view Camera is missing");
        return false;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        log_trs_tool->trace("Transform tool cannot start drag - Scene view Camera node is missing");
        return false;
    }

    m_active_handle = m_hover_handle;
    m_active_tool   = m_hover_tool;
    if (
        (m_active_handle == Handle::e_handle_none) ||
        (m_active_tool == nullptr)
    ) {
        log_trs_tool->trace("Transform tool cannot start drag - Pointer is not hovering over tool handle");
        return false;
    }

    const auto& hover_entry = scene_view->get_hover(Hover_entry::tool_slot);
    if (!hover_entry.valid || !hover_entry.mesh) {
        log_trs_tool->trace("Transform tool cannot start drag - Pointer is not hovering over tool handle");
        return false;
    }

    shared.drag.initial_position_in_world = hover_entry.position.value();
    shared.drag.initial_world_from_anchor = shared.anchor_state_initial.world_from_anchor;
    shared.drag.initial_anchor_from_world = glm::inverse(shared.drag.initial_world_from_anchor); // TODO Consider computing this directly, without inverse
    shared.drag.initial_distance          = glm::distance(
        glm::vec3{camera_node->position_in_world()},
        hover_entry.position.value()
    );

    if (shared.entries.empty()) {
        log_trs_tool->trace("drag not possible - no selection");
        return false;
    }

    const unsigned int axis_mask = get_axis_mask(m_active_handle);
    return m_active_tool->begin(axis_mask, scene_view);
}

void Transform_tool::end_drag()
{
    log_trs_tool->trace("ending drag");

    if (m_active_tool != nullptr) {
        m_active_tool->end();
    }

    m_active_handle = Handle::e_handle_none;
    m_active_tool   = nullptr;

    // TODO Consider mode where nodes physics is kept acquired until transform tool is deactivated
    release_node_physics();

    shared.drag.initial_position_in_world = vec3{0.0f};
    shared.drag.initial_world_from_anchor = mat4{1.0f};
    shared.drag.initial_distance          = 0.0;

    log_trs_tool->trace("drag ended");
}

void Transform_tool::acquire_node_physics()
{
    //// shared.touched = true;
    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        const auto node_physics = get_node_physics(node.get());
        auto* const rigid_body = node_physics
            ? node_physics->rigid_body()
            : nullptr;

        if (rigid_body == nullptr) {
            log_trs_tool->trace("TRS begin_move - no rigid body");
            continue;
        }

        entry.original_motion_mode = rigid_body->get_motion_mode();
        rigid_body->set_motion_mode(erhe::physics::Motion_mode::e_kinematic_physical);
        rigid_body->begin_move();
    }
}

void Transform_tool::release_node_physics()
{
    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        const auto node_physics = get_node_physics(node.get());
        if (node_physics && node_physics->rigid_body()) {
            ERHE_VERIFY(entry.original_motion_mode.has_value());
            log_trs_tool->trace("S restoring old physics node");
            auto* const rigid_body = node_physics->rigid_body();
            rigid_body->set_motion_mode     (entry.original_motion_mode.value());
            rigid_body->set_linear_velocity (glm::vec3{0.0f, 0.0f, 0.0f});
            rigid_body->set_angular_velocity(glm::vec3{0.0f, 0.0f, 0.0f});
            rigid_body->end_move            ();
            node_physics->handle_node_transform_update();
            entry.original_motion_mode.reset();
        }
    }
    shared.touched = false;
    log_trs_tool->trace("TRS end_move");
}

auto Transform_tool::get_active_handle() const -> Handle
{
    return m_active_handle;
}

auto Transform_tool::get_hover_handle() const -> Handle
{
    return m_hover_handle;
}

auto Transform_tool::get_handle(erhe::scene::Mesh* mesh) const -> Handle
{
    return shared.visualization->get_handle(mesh);
}


#pragma region Render
namespace {

void render_rays(erhe::scene::Node& node)
{
    ERHE_PROFILE_FUNCTION();

    std::shared_ptr<erhe::scene::Mesh> mesh = get_mesh(&node);
    if (!mesh) {
        return;
    }
    auto* scene_root = reinterpret_cast<Scene_root*>(node.node_data.host);
    if (scene_root == nullptr) {
        return;
    }
    glm::vec3 directions[] = {
        { 0.0f, -1.0f,  0.0f},
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f}
    };

    auto& line_renderer = *erhe::application::g_line_renderer_set->hidden.at(2).get();

    auto& raytrace_scene = scene_root->raytrace_scene();

    for (auto& d : directions) {
        erhe::raytrace::Ray ray{
            .origin    = node.position_in_world(),
            .t_near    = 0.0f,
            .direction = d,
            .time      = 0.0f,
            .t_far     = 9999.0f,
            .mask      = Raytrace_node_mask::content,
            .id        = 0,
            .flags     = 0
        };

        erhe::raytrace::Hit hit;
        if (project_ray(&raytrace_scene, mesh.get(), ray, hit)) {
            Ray_hit_style ray_hit_style
            {
                .ray_color     = glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
                .ray_thickness = 8.0f,
                .ray_length    = 0.5f,
                .hit_color     = glm::vec4{0.8f, 0.2f, 0.8f, 0.75f},
                .hit_thickness = 8.0f,
                .hit_size      = 0.10f
            };

            draw_ray_hit(line_renderer, ray, hit, ray_hit_style);
        }
    }
}

}

void Transform_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION();

    if (context.camera == nullptr) {
        return;
    }

    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        if (shared.settings.cast_rays)
        {
            render_rays(*node.get());
        }
    }
    g_rotate_tool->render(context);
}
#pragma endregion Render

void Transform_tool::update_for_view(Scene_view* scene_view)
{
    // TODO Should this be elsewhere?
    bool can_update_target_nodes = (m_active_tool == nullptr) || !m_active_tool->is_active();
    if (can_update_target_nodes) {
        update_target_nodes();
    }

    update_visibility();
    shared.visualization->update_for_view(scene_view);
    update_transforms();

    //if (root() == nullptr)
    //{
    //    return;
    //}
    //
    //const vec3  V0      = vec3{root()->position_in_world()} - vec3{camera->position_in_world()};
    //const vec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0});
    //const float v_dot_n = dot(V, m_rotation.normal);
    //->tail_log.trace("R: {} @ {}", root()->name(), root()->position_in_world());
    //->tail_log.trace("C: {} @ {}", camera->name(), camera->position_in_world());
    //->tail_log.trace("V: {}", vec3{V});
    //->tail_log.trace("N: {}", vec3{m_rotation.normal});
    //->tail_log.trace("V.N = {}", v_dot_n);
}

void Transform_tool::update_visibility()
{
    shared.visualization->update_visibility();
    update_transforms();
}

void Transform_tool::update_transforms()
{
    shared.visualization->update_transforms();
}

auto Transform_tool::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return g_tools->get_tool_scene_root();
}

}