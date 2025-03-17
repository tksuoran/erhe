#include "tools/transform/transform_tool.hpp"
#include "tools/transform/move_tool.hpp"
#include "tools/transform/rotate_tool.hpp"
#include "tools/transform/scale_tool.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "editor_settings.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"

#include "renderers/mesh_memory.hpp" // need to be able to pass to visualization
#include "renderers/render_context.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/rotate_tool.hpp"

#include "erhe_bit/bit_helpers.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_configuration/configuration.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/scoped_line_renderer.hpp"
#include "erhe_scene/mesh.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#endif

#include <unordered_map>

namespace editor {

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

Transform_tool_drag_command::Transform_tool_drag_command(erhe::commands::Commands& commands, Editor_context& editor_context)
    : Command  {commands, "Transform_tool.drag"}
    , m_context{editor_context}
{
}

void Transform_tool_drag_command::try_ready()
{
    if (m_context.transform_tool->on_drag_ready()) {
        set_ready();
    }
}

auto Transform_tool_drag_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false; // We might be ready, but not consuming event yet
    }

    const bool still_active = m_context.transform_tool->on_drag();
    if (!still_active) {
        set_inactive();
    }
    return still_active;
}

void Transform_tool_drag_command::on_inactive()
{
    log_trs_tool->trace("TRS on_inactive");

    if (get_command_state() != erhe::commands::State::Inactive) {
        m_context.transform_tool->end_drag();
    }
}

#pragma endregion Commands

Transform_tool::Transform_tool(
    tf::Executor&                executor,
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context,
    Editor_message_bus&          editor_message_bus,
    Headset_view&                headset_view,
    Mesh_memory&                 mesh_memory,
    Tools&                       tools
)
    : Imgui_window                  {imgui_renderer, imgui_windows, "Transform", "transform"}
    , Tool                          {editor_context}
    , m_drag_command                {commands, editor_context}
    , m_drag_redirect_update_command{commands, m_drag_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command}
{
    ERHE_PROFILE_FUNCTION();

    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "transform_tool");
    auto& settings = shared.settings;
    ini.get("show_translate", settings.show_translate);
    ini.get("show_rotate",    settings.show_rotate);

    //executor.silent_async(
    //    [this, &editor_context, &mesh_memory, &tools](){
    // TODO
    static_cast<void>(executor);
    shared.visualizations = std::make_unique<Handle_visualizations>(editor_context, mesh_memory, tools);
    shared.visualizations_ready.store(true);
    //    }
    //);

    set_base_priority(c_priority);
    set_description  ("Transform");
    tools.register_tool(this);

    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::window::Mouse_button_left, true);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->trigger_click, erhe::commands::Button_trigger::Any);
        commands.bind_command_to_xr_boolean_action(&m_drag_enable_command, xr_right->a_click,       erhe::commands::Button_trigger::Any);
        commands.bind_command_to_update           (&m_drag_redirect_update_command);
    }
#else
    static_cast<void>(headset_view);
#endif

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );

    m_drag_command.set_host(this);
}

void Transform_tool::on_message(Editor_message& message)
{
    Tool::on_message(message);

    using namespace erhe::bit;
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
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations == nullptr) {
        return;
    }
    visualizations->viewport_toolbar(hovered);
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
    const ImVec2 button_size{ImGui::GetContentRegionAvail().x / 2, 0.0f};

    if (
        erhe::imgui::make_button(
            "Local",
            (settings.local) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal,
            button_size
        )
    ) {
        settings.local = true;
    }
    ImGui::SameLine();
    if (
        erhe::imgui::make_button(
            "Global",
            (!settings.local) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal,
            button_size
        )
    ) {
        settings.local = false;
    }

    ImGui::TextUnformatted(is_transform_tool_active() ? "Active" : "Inactive");

    //const bool show_translate = settings.show_translate;
    //const bool show_rotate    = settings.show_rotate;
    //const bool show_scale     = settings.show_scale;
    //ImGui::Checkbox("Translate Tool", &settings.show_translate);
    //ImGui::Checkbox("Rotate Tool",    &settings.show_rotate);
    //ImGui::Checkbox("Scale Tool",     &settings.show_scale);
    //ImGui::Checkbox("Hide Inactive",  &settings.hide_inactive);

    // if (
    //     (show_translate != settings.show_translate) ||
    //     (show_rotate    != settings.show_rotate   ) ||
    //     (show_scale     != settings.show_scale    )
    // ) {
    //     shared.visualization->update_visibility();
    // }

    transform_properties();

    if (m_active_tool != nullptr) {
        m_last_active_tool = m_active_tool;
    }
    if (m_last_active_tool != nullptr) {
        ImGui::Separator();

        m_last_active_tool->imgui(m_property_editor);

        ImGui::Separator();

        ImGui::Text("Hover handle: %s", c_str(m_hover_handle));
        ImGui::Text("Active handle: %s", c_str(m_active_handle));

        m_property_editor.reset();
        m_property_editor.add_entry("Cast Rays", [this](){ ImGui::Checkbox("##", &shared.settings.cast_rays); });
        m_property_editor.show_entries();
    }

    ImGui::Separator();
#endif
}

void Transform_tool::update_target_nodes(erhe::scene::Node* node_filter)
{
    const auto& selection = m_context.selection->get_selection();

    vec3 cumulative_world_translation{0.0f, 0.0f, 0.0f};
    quat cumulative_world_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    vec3 cumulative_world_scale      {0.0f, 0.0f, 0.0f};
    std::size_t node_count{0};

    if (node_filter == nullptr) {
        shared.entries.clear();
    }
    std::size_t i = 0;

    for (const auto& item : selection) {
        std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
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
                Transform_entry{
                    .node                    = node,
                    .parent_from_node_before = node->parent_from_node_transform(),
                    .world_from_node_before  = node->world_from_node_transform(),
                    .original_motion_mode    = {}
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

    if (node_count == 0) {
        shared.world_from_anchor_initial_state = erhe::scene::Trs_transform{};
    } else {
        shared.world_from_anchor_initial_state.set_trs(
            cumulative_world_translation / static_cast<float>(node_count),
            cumulative_world_rotation,
            cumulative_world_scale / static_cast<float>(node_count)
        );
    }

    shared.world_from_anchor = shared.world_from_anchor_initial_state;

    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->set_anchor(shared.world_from_anchor);
        visualizations->update_visibility();
    }
}

void Transform_tool::adjust(const mat4& updated_world_from_anchor)
{
    touch();
    for (auto& entry : shared.entries) {
        const auto& node = entry.node;
        if (!node) {
            continue;
        }
        const bool node_lock_viewport_transform = erhe::bit::test_all_rhs_bits_set(
            node->get_flag_bits(),
            erhe::Item_flags::lock_viewport_transform
        );
        if (node_lock_viewport_transform) {
            continue;
        }

        const mat4 world_from_node           = entry.world_from_node_before.get_matrix();
        const mat4 anchor_from_world         = shared.world_from_anchor_initial_state.get_inverse_matrix();
        const mat4 previous_anchor_from_node = anchor_from_world         * world_from_node;
        const mat4 updated_world_from_node   = updated_world_from_anchor * previous_anchor_from_node;

        const auto& parent = node->get_parent_node();
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
    touch();
    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        const bool node_lock_viewport_transform = erhe::bit::test_all_rhs_bits_set(
            node->get_flag_bits(),
            erhe::Item_flags::lock_viewport_transform
        );
        if (node_lock_viewport_transform) {
            continue;
        }

        node->set_world_from_node(
            erhe::scene::translate(entry.world_from_node_before, translation)
        );
    }
    shared.world_from_anchor = erhe::scene::translate(shared.world_from_anchor_initial_state, translation);
    update_transforms();
}

void Transform_tool::adjust_rotation(const vec3 center_of_rotation, const quat rotation)
{
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                continue;
            }
            const bool node_lock_viewport_transform = erhe::bit::test_all_rhs_bits_set(
                node->get_flag_bits(),
                erhe::Item_flags::lock_viewport_transform
            );
            if (node_lock_viewport_transform) {
                continue;
            }

            node->set_world_from_node(
                erhe::scene::rotate(entry.world_from_node_before, rotation)
            );
        }
        shared.world_from_anchor = erhe::scene::rotate(shared.world_from_anchor_initial_state, rotation);
    } else {
        const mat4 translate   = erhe::math::create_translation<float>(vec3{-center_of_rotation});
        const mat4 untranslate = erhe::math::create_translation<float>(vec3{ center_of_rotation});
        adjust(
            untranslate * mat4_cast(rotation) * translate * shared.world_from_anchor_initial_state.get_matrix()
        );
    }
    update_transforms();
}

void Transform_tool::adjust_scale(const vec3 center_of_scale, const vec3 scale)
{
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                continue;
            }
            const bool node_lock_viewport_transform = erhe::bit::test_all_rhs_bits_set(
                node->get_flag_bits(),
                erhe::Item_flags::lock_viewport_transform
            );
            if (node_lock_viewport_transform) {
                continue;
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
        if (m_hover_handle != Handle::e_handle_none) {
            m_hover_handle = Handle::e_handle_none;
        }
        return;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || (tool.scene_mesh == nullptr)) {
        if (m_hover_handle != Handle::e_handle_none) {
            m_hover_handle = Handle::e_handle_none;
        }
        return;
    }

    const auto new_handle = get_handle(tool.scene_mesh);
    if (m_hover_handle == new_handle) {
        return;
    }
    m_hover_handle = new_handle;

    m_hover_tool = [&]() -> Subtool* {
        switch (get_handle_tool(m_hover_handle)) {
            case Handle_tool::e_handle_tool_none     : return nullptr;
            case Handle_tool::e_handle_tool_translate: return m_context.move_tool;
            case Handle_tool::e_handle_tool_rotate   : return m_context.rotate_tool;
            case Handle_tool::e_handle_tool_scale    : return m_context.scale_tool;
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
    if (!hover_entry.valid || (hover_entry.scene_mesh == nullptr)) {
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
    Handle_visualizations* visualizations = shared.get_visualizations();
    return (visualizations != nullptr) ? visualizations->get_handle(mesh) : Handle::e_handle_none;
}

#pragma region Render

void Transform_tool::render_rays(erhe::scene::Node& node)
{
    ERHE_PROFILE_FUNCTION();

    std::shared_ptr<erhe::scene::Mesh> mesh = get_mesh(&node);
    if (!mesh) {
        return;
    }
    auto* scene_root = static_cast<Scene_root*>(node.node_data.host);
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

    erhe::renderer::Scoped_line_renderer line_renderer = m_context.line_renderer->get(2, true, true);

    auto& raytrace_scene = scene_root->get_raytrace_scene();

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
            Ray_hit_style ray_hit_style {
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

void Transform_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        if (shared.settings.cast_rays) {
            render_rays(*node.get());
        }
    }
    m_context.rotate_tool->render(context);
}
#pragma endregion Render

void Transform_tool::update_for_view(Scene_view* scene_view)
{
    update_visibility();
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->update_for_view(scene_view);
    }
    update_transforms();
}

void Transform_tool::update_visibility()
{
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->update_visibility();
    }
    update_transforms();
}

void Transform_tool::update_transforms()
{
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->set_anchor(shared.world_from_anchor);
        visualizations->update_transforms();
    };
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
    for (auto& entry : shared.entries) {
        auto node_operation = std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = entry.node,
                .parent_from_node_before = entry.parent_from_node_before,
                .parent_from_node_after  = entry.node->parent_from_node_transform()
            }
        );
        compompound_parameters.operations.push_back(node_operation);
    }
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            std::move(compompound_parameters)
        )
    );
}

Edit_state::Edit_state()
{
}

Edit_state::Edit_state(
    Transform_tool_shared& shared,
    Transform_tool&        transform_tool,
    Rotation_inspector&    rotation_inspector,
    Property_editor&       property_editor
)
{
    static_cast<void>(property_editor);
    m_multiselect       = shared.entries.size() > 1;
    m_first_node        = shared.entries.front().node;
    m_world_from_parent = m_first_node->world_from_parent();
    m_use_world_mode    = !shared.settings.local || m_multiselect;

    m_transform =
        m_use_world_mode
            ? &shared.world_from_anchor
            : &shared.entries.front().node->parent_from_node_transform();

    m_rotation_transform =
        (m_use_world_mode || m_multiselect)
            ? &shared.world_from_anchor
            : &shared.entries.front().node->parent_from_node_transform();

    m_scale       = m_transform->get_scale      ();
    m_rotation    = m_rotation_transform->get_rotation();
    m_translation = m_transform->get_translation();
    m_skew        = m_transform->get_skew       ();

    const std::shared_ptr<erhe::scene::Node> first_parent = m_first_node->get_parent_node();
    const bool euler_matches_gizmo = !m_multiselect &&
        (
            !m_use_world_mode ||
            !first_parent ||
            first_parent == m_first_node->get_scene()->get_root_node()
        );

    using namespace erhe::imgui;

    Property_editor& p = property_editor;
    p.reset();
    p.push_group("Translation", ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("X", 0xff8888ffu, 0xff222266u, [this](){ m_translate_state.combine(make_scalar_button(&m_translation.x, 0.0f, 0.0f, "##T.X")); });
    p.add_entry("Y", 0xff88ff88u, 0xff226622u, [this](){ m_translate_state.combine(make_scalar_button(&m_translation.y, 0.0f, 0.0f, "##T.Y")); });
    p.add_entry("Z", 0xffff8888u, 0xff662222u, [this](){ m_translate_state.combine(make_scalar_button(&m_translation.z, 0.0f, 0.0f, "##T.Z")); });
    p.pop_group();

    p.push_group("Rotation", ImGuiTreeNodeFlags_DefaultOpen);
    rotation_inspector.imgui(m_rotate_quaternion_state, m_rotate_euler_state, m_rotate_axis_angle_state, m_rotation, euler_matches_gizmo, p);
    p.pop_group();

    p.push_group("Scale", ImGuiTreeNodeFlags_DefaultOpen);
    p.add_entry("X", 0xff8888ffu, 0xff222266u, [this](){ m_scale_state.combine(make_scalar_button(&m_scale.x, 0.01f, FLT_MAX, "##S.X")); });
    p.add_entry("Y", 0xff88ff88u, 0xff226622u, [this](){ m_scale_state.combine(make_scalar_button(&m_scale.y, 0.01f, FLT_MAX, "##S.Y")); });
    p.add_entry("Z", 0xffff8888u, 0xff662222u, [this](){ m_scale_state.combine(make_scalar_button(&m_scale.z, 0.01f, FLT_MAX, "##S.Z")); });
    p.pop_group();

    p.push_group("Skew", ImGuiTreeNodeFlags_None);
    p.add_entry("X", 0xff8888ffu, 0xff222266u, [this](){ m_skew_state.combine(make_scalar_button(&m_skew.x, 0.0f, 0.0f, "##K.X")); });
    p.add_entry("Y", 0xff88ff88u, 0xff226622u, [this](){ m_skew_state.combine(make_scalar_button(&m_skew.y, 0.0f, 0.0f, "##K.Y")); });
    p.add_entry("Z", 0xffff8888u, 0xff662222u, [this](){ m_skew_state.combine(make_scalar_button(&m_skew.z, 0.0f, 0.0f, "##K.Z")); });
    p.pop_group();

    p.show_entries();

    if (m_translate_state.value_changed) {
        transform_tool.adjust_translation(m_translation - shared.world_from_anchor_initial_state.get_translation());
    }

    if (m_rotate_quaternion_state.value_changed) {
        rotation_inspector.update_from_quaternion();
    }
    if (m_rotate_euler_state.value_changed) {
        rotation_inspector.update_matrix_and_quaternion_from_euler_angles();
    }
    if (m_rotate_axis_angle_state.value_changed) {
        rotation_inspector.update_from_axis_angle();
    }
    erhe::imgui::Value_edit_state rotate_state;
    rotate_state.combine(m_rotate_quaternion_state);
    rotate_state.combine(m_rotate_euler_state);
    rotate_state.combine(m_rotate_axis_angle_state);

    rotation_inspector.set_active(rotate_state.active);

    if (rotate_state.value_changed) {
        if (m_use_world_mode || m_multiselect) {
            transform_tool.touch();
            for (auto& entry : shared.entries) {
                auto& node = entry.node;
                if (!node) {
                    return;
                }
                Trs_transform world_from_node = entry.world_from_node_before;
                world_from_node.set_rotation(rotation_inspector.get_quaternion());
                node->set_world_from_node(world_from_node);
            }
            shared.world_from_anchor.set_rotation(rotation_inspector.get_quaternion());
        } else {
            transform_tool.touch();
            for (auto& entry : shared.entries) {
                auto& node = entry.node;
                if (!node) {
                    return;
                }
                Trs_transform parent_from_node = entry.parent_from_node_before;
                parent_from_node.set_rotation(rotation_inspector.get_quaternion());
                node->set_parent_from_node(parent_from_node);
                shared.world_from_anchor.set(node->world_from_node());
            }
        }
        transform_tool.update_transforms();
    }

    if (m_scale_state.value_changed) {
        Trs_transform n = shared.world_from_anchor_initial_state;
        n.set_scale(m_scale);
        transform_tool.adjust(n.get_matrix());
    }

    if (m_skew_state.value_changed) {
        Trs_transform n = shared.world_from_anchor_initial_state;
        n.set_skew(m_skew);
        transform_tool.adjust(n.get_matrix());
    }

    Value_edit_state edit_state;
    edit_state.combine(m_translate_state);
    edit_state.combine(rotate_state);
    edit_state.combine(m_scale_state);
    edit_state.combine(m_skew_state);

    if (edit_state.edit_ended && shared.touched) {
        transform_tool.record_transform_operation();
    }
}

void Transform_tool::transform_properties()
{
    if (shared.entries.empty()) {
        return;
    }

    m_edit_state = Edit_state(shared, *this, m_rotation, m_property_editor);
}

} // namespace editor

