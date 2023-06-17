#include "tools/transform/transform_tool.hpp"

#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/compound_operation.hpp"
#include "operations/insert_operation.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"

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

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/matrix_interpolation.hpp>
#include <glm/gtx/quaternion.hpp>

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe/xr/xr_action.hpp"
#   include "erhe/xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

using glm::normalize;
using glm::cross;
using glm::dot;
using glm::distance;
using glm::mat3_cast;
using glm::mat4_cast;
using glm::quat_cast;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;

using Trs_transform = erhe::scene::Trs_transform;


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

    erhe::application::g_imgui_windows->register_imgui_window(this, "transform");

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
    if (
        test_any_rhs_bits_set(
            message.update_flags,
            Message_flag_bit::c_flag_bit_selection | Message_flag_bit::c_flag_bit_animation_update
        )
    ) {
        update_target_nodes(nullptr);
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_node_touched_operation_stack)) {
        update_target_nodes(message.node);
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_render_scene_view)) {
        update_for_view(message.scene_view);
    }
}

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
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto& settings = shared.settings;
    const bool   show_translate = settings.show_translate;
    const bool   show_rotate    = settings.show_rotate;
    const bool   show_scale     = settings.show_scale;
    const ImVec2 button_size{ImGui::GetContentRegionAvail().x / 2, 0.0f};
    const bool   multiselect    = shared.entries.size() > 1;

    if (multiselect) {
        ImGui::BeginDisabled();
    }
    if (
        erhe::application::make_button(
            "Local",
            (settings.local) ? erhe::application::Item_mode::active : erhe::application::Item_mode::normal,
            button_size
        )
    ) {
        settings.local = true;
    }
    ImGui::SameLine();
    if (
        erhe::application::make_button(
            "Global",
            (!settings.local) ? erhe::application::Item_mode::active : erhe::application::Item_mode::normal,
            button_size
        )
    ) {
        settings.local = false;
    }
    if (multiselect) {
        ImGui::EndDisabled();
    }

    //ImGui::SliderFloat("Scale", &settings.gizmo_scale, 1.0f, 10.0f);

    //ImGui::Checkbox("Translate Tool", &settings.show_translate);
    //ImGui::Checkbox("Rotate Tool",    &settings.show_rotate);
    //ImGui::Checkbox("Scale Tool",     &settings.show_scale);
    //ImGui::Checkbox("Hide Inactive",  &settings.hide_inactive);

    if (
        (show_translate != settings.show_translate) ||
        (show_rotate    != settings.show_rotate   ) ||
        (show_scale     != settings.show_scale    )
    ) {
        shared.visualization->update_visibility();
    }

    transform_properties();

    if (m_active_tool != nullptr) {
        ImGui::Separator();

        m_active_tool->imgui();

        ImGui::Separator();

        ImGui::Text("Hover handle: %s", c_str(m_hover_handle));
        ImGui::Text("Active handle: %s", c_str(m_active_handle));

        ImGui::Checkbox("Cast Rays", &shared.settings.cast_rays);
    }

    ImGui::Separator();
#endif
}

void Transform_tool::update_target_nodes(erhe::scene::Node* node_filter)
{
    const auto& selection = g_selection_tool->get_selection();

    vec3 cumulative_world_translation{0.0f, 0.0f, 0.0f};
    quat cumulative_world_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    vec3 cumulative_world_scale      {0.0f, 0.0f, 0.0f};
    std::size_t node_count{0};
    release_node_physics();
    if (node_filter == nullptr) {
        shared.entries.clear();
    }
    std::size_t i = 0;
    for (const auto& item : selection) {
        std::shared_ptr<erhe::scene::Node> node = as_node(item);
        if (!node) {
            continue;
        }
        const Trs_transform& world_from_node = node->world_from_node_transform();

        cumulative_world_translation += world_from_node.get_translation();
        cumulative_world_rotation     = world_from_node.get_rotation();
        cumulative_world_scale       += world_from_node.get_scale();

        ++node_count;
        if (node_filter == nullptr) {
            shared.entries.push_back(
                Entry{
                    .node                    = node,
                    .parent_from_node_before = node->parent_from_node_transform(),
                    .world_from_node_before  = node->world_from_node_transform()
                }
            );
        } else {
            if (node.get() == node_filter) {
                shared.entries.at(i).parent_from_node_before = node->parent_from_node_transform();
                shared.entries.at(i).world_from_node_before  = node->world_from_node_transform();
            }
            ++i;
        }
    }
    acquire_node_physics();

    shared.world_from_anchor_initial_state.set_trs(
        cumulative_world_translation / static_cast<float>(node_count),
        cumulative_world_rotation,
        cumulative_world_scale / static_cast<float>(node_count)
    );

    shared.world_from_anchor = shared.world_from_anchor_initial_state;

    shared.visualization->set_anchor(shared.world_from_anchor);
    shared.visualization->update_visibility();
}

void Transform_tool::adjust(const mat4& updated_world_from_anchor)
{
    touch();
    for (auto& entry : shared.entries) {
        const auto& node = entry.node;
        if (!node) {
            return;
        }
        const mat4 world_from_node           = entry.world_from_node_before.get_matrix();
        const mat4 anchor_from_world         = shared.world_from_anchor_initial_state.get_inverse_matrix();
        const mat4 previous_anchor_from_node = anchor_from_world         * world_from_node;
        const mat4 updated_world_from_node   = updated_world_from_anchor * previous_anchor_from_node;

        const auto& parent = node->parent().lock();
        const mat4 parent_from_world = [&]() -> mat4 {
            if (parent) {
                return parent->node_from_world() * updated_world_from_node;
            } else {
                return updated_world_from_node;
            }
        }();
        node->set_parent_from_node(parent_from_world);
    }

    shared.world_from_anchor.set(updated_world_from_anchor);
}

void Transform_tool::adjust_translation(const vec3 translation)
{
    if (shared.settings.local) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                return;
            }
            node->set_world_from_node(
                erhe::scene::translate(entry.world_from_node_before, translation)
            );
        }
        shared.world_from_anchor = erhe::scene::translate(shared.world_from_anchor_initial_state, translation);
    } else {
        adjust(
            glm::translate(
                shared.world_from_anchor_initial_state.get_matrix(),
                translation
            )
        );
    }
    update_transforms();
}

void Transform_tool::adjust_rotation(
    const vec3 center_of_rotation,
    const quat rotation
)
{
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                return;
            }
            node->set_world_from_node(
                erhe::scene::rotate(entry.world_from_node_before, rotation)
            );
        }
        shared.world_from_anchor = erhe::scene::rotate(shared.world_from_anchor_initial_state, rotation);
    } else {
        const mat4 translate   = erhe::toolkit::create_translation<float>(vec3{-center_of_rotation});
        const mat4 untranslate = erhe::toolkit::create_translation<float>(vec3{ center_of_rotation});
        adjust(
            untranslate * mat4_cast(rotation) * translate * shared.world_from_anchor_initial_state.get_matrix()
        );
    }
    update_transforms();
}

void Transform_tool::adjust_scale(
    const vec3 center_of_scale,
    const vec3 scale
)
{
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                return;
            }
            node->set_world_from_node(
                erhe::scene::scale(entry.world_from_node_before, scale)
            );
        }
        shared.world_from_anchor = erhe::scene::scale(shared.world_from_anchor_initial_state, scale);
    } else {
        adjust(
            glm::translate(
                glm::scale(
                    glm::translate(
                        shared.world_from_anchor_initial_state.get_matrix(),
                        center_of_scale
                    ),
                    scale
                ),
                -center_of_scale
            )
        );
    }
    update_transforms();
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

    shared.initial_drag_position_in_world = hover_entry.position.value();
    shared.initial_drag_distance = distance(
        vec3{camera_node->position_in_world()},
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
    shared.initial_drag_distance = 0.0;

    log_trs_tool->trace("drag ended");
}

void Transform_tool::acquire_node_physics()
{
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
            auto* const rigid_body = node_physics->rigid_body();
            if (entry.original_motion_mode.has_value()) {
                log_trs_tool->trace("S restoring old physics node");
                rigid_body->set_motion_mode(entry.original_motion_mode.value());
            }
            rigid_body->set_linear_velocity (vec3{0.0f, 0.0f, 0.0f});
            rigid_body->set_angular_velocity(vec3{0.0f, 0.0f, 0.0f});
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
    vec3 directions[] = {
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
                .ray_color     = vec4{1.0f, 0.0f, 1.0f, 1.0f},
                .ray_thickness = 8.0f,
                .ray_length    = 0.5f,
                .hit_color     = vec4{0.8f, 0.2f, 0.8f, 0.75f},
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
    update_visibility();
    shared.visualization->update_for_view(scene_view);
    update_transforms();
}

void Transform_tool::update_visibility()
{
    shared.visualization->update_visibility();
    update_transforms();
}

void Transform_tool::update_transforms()
{
    shared.visualization->set_anchor(shared.world_from_anchor);
    shared.visualization->update_transforms();
}

auto Transform_tool::get_tool_scene_root() -> std::shared_ptr<Scene_root>
{
    return g_tools->get_tool_scene_root();
}

void Transform_tool::touch()
{
    if (!shared.touched) {
        log_trs_tool->trace("TRS touch - not touched");
        shared.touched = true;
    }
}

void Transform_tool::record_transform_operation()
{
    if (!shared.touched || shared.entries.empty()) {
        return;
    }

    log_trs_tool->trace("creating transform operation");

    Compound_operation::Parameters compompound_parameters;
    for (auto& entry : g_transform_tool->shared.entries) {
        auto node_operation = std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = entry.node,
                .parent_from_node_before = entry.parent_from_node_before,
                .parent_from_node_after  = entry.node->parent_from_node_transform()
            }
        );
        compompound_parameters.operations.push_back(node_operation);
    }
    g_operation_stack->push(
        std::make_shared<Compound_operation>(
            std::move(compompound_parameters)
        )
    );
}

void Transform_tool::transform_properties()
{
    if (shared.entries.empty()) {
        return;
    }

    const bool  multiselect       = shared.entries.size() > 1;
    const auto& first_node        = shared.entries.front().node;
    mat4        world_from_parent = first_node->world_from_parent();
    bool        use_world_mode    = !shared.settings.local || multiselect;

    Trs_transform& transform =
        use_world_mode
            ? shared.world_from_anchor
            : shared.entries.front().node->parent_from_node_transform();

    vec3 scale       = transform.get_scale      ();
    quat rotation    = transform.get_rotation   ();
    vec3 translation = transform.get_translation();
    vec3 skew        = transform.get_skew       ();

    using namespace erhe::application;
    Value_edit_state translate_edit_state;
    if (ImGui::TreeNodeEx("Translation", ImGuiTreeNodeFlags_DefaultOpen)) {
        translate_edit_state.combine(make_scalar_button(&translation.x, 0.0f, 0.0f, 0xff8888ffu, 0xff222266u, "X", "##T.X"));
        translate_edit_state.combine(make_scalar_button(&translation.y, 0.0f, 0.0f, 0xff88ff88u, 0xff226622u, "Y", "##T.Y"));
        translate_edit_state.combine(make_scalar_button(&translation.z, 0.0f, 0.0f, 0xffff8888u, 0xff662222u, "Z", "##T.Z"));
        ImGui::TreePop();
    }
    if (translate_edit_state.value_changed) {
        adjust_translation(translation - shared.world_from_anchor_initial_state.get_translation());
    }

    Value_edit_state rotate_edit_state;
    if (ImGui::TreeNodeEx("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
        m_rotation.imgui(rotate_edit_state, rotation);
        ImGui::TreePop();
    }
    if (rotate_edit_state.value_changed) {
        Trs_transform n = shared.world_from_anchor_initial_state;
        n.set_rotation(m_rotation.get_quaternion());
        adjust(n.get_matrix());
    }

    Value_edit_state scale_edit_state;
    if (ImGui::TreeNodeEx("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
        scale_edit_state.combine(make_scalar_button(&scale.x, 0.01f, FLT_MAX, 0xff8888ffu, 0xff222266u, "X", "##S.X"));
        scale_edit_state.combine(make_scalar_button(&scale.y, 0.01f, FLT_MAX, 0xff88ff88u, 0xff226622u, "Y", "##S.Y"));
        scale_edit_state.combine(make_scalar_button(&scale.z, 0.01f, FLT_MAX, 0xffff8888u, 0xff662222u, "Z", "##S.Z"));
        ImGui::TreePop();
    }
    if (scale_edit_state.value_changed) {
        Trs_transform n = shared.world_from_anchor_initial_state;
        n.set_scale(scale);
        adjust(n.get_matrix());
    }

    Value_edit_state skew_edit_state;
    if (ImGui::TreeNodeEx("Skew"/*ImGuiTreeNodeFlags_DefaultOpen*/)) {
        skew_edit_state.combine(make_scalar_button(&skew.x, 0.0f, 0.0f, 0xff8888ffu, 0xff222266u, "X", "##K.X"));
        skew_edit_state.combine(make_scalar_button(&skew.y, 0.0f, 0.0f, 0xff88ff88u, 0xff226622u, "Y", "##K.Y"));
        skew_edit_state.combine(make_scalar_button(&skew.z, 0.0f, 0.0f, 0xffff8888u, 0xff662222u, "Z", "##K.Z"));
        ImGui::TreePop();
    }
    if (skew_edit_state.value_changed) {
        Trs_transform n = shared.world_from_anchor_initial_state;
        n.set_skew(skew);
        adjust(n.get_matrix());
    }

    Value_edit_state edit_state;
    edit_state.combine(translate_edit_state);
    edit_state.combine(rotate_edit_state);
    edit_state.combine(scale_edit_state);
    edit_state.combine(skew_edit_state);

    if (edit_state.edit_ended && shared.touched) {
        record_transform_operation();
    }
}

}