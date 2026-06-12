#include "transform/transform_tool.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp" // need to be able to pass to visualization
#include "renderers/render_context.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"

#include "erhe_commands/commands.hpp"
#include "config/generated/transform_tool_config.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <imgui/imgui.h>

#include <cmath>
#include <limits>

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

Transform_tool_drag_command::Transform_tool_drag_command(erhe::commands::Commands& commands, App_context& app_context)
    : Command  {commands, "Transform_tool.drag"}
    , m_context{app_context}
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
    const Transform_tool_config&       transform_tool_config,
    tf::Executor&                      executor,
    erhe::commands::Commands&          commands,
    erhe::imgui::Imgui_renderer&       imgui_renderer,
    erhe::imgui::Imgui_windows&        imgui_windows,
    App_context&                       app_context,
    App_message_bus&                   app_message_bus,
    Headset_view&                      headset_view,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Tools&                             tools,
    Move_tool&                         move_tool,
    Rotate_tool&                       rotate_tool,
    Scale_tool&                        scale_tool
)
    : Tool                          {app_context, tools}
    , m_window                      {imgui_renderer, imgui_windows, "Transform", "transform", [this]() { window_imgui(); }}
    , m_drag_command                {commands, app_context}
    , m_drag_redirect_update_command{commands, m_drag_command}
    , m_drag_enable_command         {commands, m_drag_redirect_update_command}
{
    ERHE_PROFILE_FUNCTION();

    auto& settings = shared.settings;
    settings.show_translate = transform_tool_config.show_translate;
    settings.show_rotate    = transform_tool_config.show_rotate;

    //executor.silent_async(
    //    [this, &editor_context, &mesh_memory, &tools](){
    // TODO
    static_cast<void>(executor);
    shared.visualizations = std::make_unique<Handle_visualizations>(app_context, mesh_memory, tools);
    shared.visualizations_ready.store(true);
    //    }
    //);

    set_base_priority(c_priority);
    set_description  ("Transform");

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

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [&](Hover_scene_view_message& message) {
            on_hover_scene_view(message);
        }
    );
    m_hover_mesh_subscription = app_message_bus.hover_mesh.subscribe(
        [&](Hover_mesh_message& message) {
            on_hover_mesh(message);
        }
    );
    m_selection_subscription = app_message_bus.selection.subscribe(
        [&](Selection_message& message) {
            on_selection(message);
        }
    );
    m_animation_update_subscription = app_message_bus.animation_update.subscribe(
        [&](Animation_update_message& message) {
            on_animation_update(message);
        }
    );
    m_node_touched_subscription = app_message_bus.node_touched.subscribe(
        [&](Node_touched_message& message) {
            on_node_touched(message);
        }
    );
    m_render_scene_view_subscription = app_message_bus.render_scene_view.subscribe(
        [&](Render_scene_view_message& message) {
            on_render_scene_view(message);
        }
    );

    m_drag_command.set_host(this);

    auto record_fn = [this]() { record_transform_operation(); };
    move_tool.set_transform_shared(shared, record_fn);
    rotate_tool.set_transform_shared(shared, record_fn);
    scale_tool.set_transform_shared(shared, record_fn);
}

void Transform_tool::on_hover_scene_view(Hover_scene_view_message& message)
{
    Tool::on_message(message);
}

void Transform_tool::on_hover_mesh(Hover_mesh_message&)
{
    update_hover();
}

void Transform_tool::on_selection(Selection_message&)
{
    update_target_nodes(nullptr);
}

void Transform_tool::on_animation_update(Animation_update_message&)
{
    update_target_nodes(nullptr);
}

void Transform_tool::on_node_touched(Node_touched_message& message)
{
    update_target_nodes(message.node);
}

void Transform_tool::on_render_scene_view(Render_scene_view_message& message)
{
    update_for_view(message.scene_view);
}

void Transform_tool::viewport_toolbar()
{
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations == nullptr) {
        return;
    }
    visualizations->viewport_toolbar();
}

auto Transform_tool::is_transform_tool_active() const -> bool
{
    return (m_active_tool == nullptr)
        ? false
        : m_active_tool->is_active();
}

void Transform_tool::window_imgui()
{
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

    ImGui::TextUnformatted("Scale gizmo");
    if (
        erhe::imgui::make_button(
            "Basic",
            (settings.scale_gizmo_mode == Scale_gizmo_mode::basic) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal,
            button_size
        )
    ) {
        if (settings.scale_gizmo_mode != Scale_gizmo_mode::basic) {
            settings.scale_gizmo_mode = Scale_gizmo_mode::basic;
            update_visibility();
        }
    }
    ImGui::SameLine();
    if (
        erhe::imgui::make_button(
            "Bounding box",
            (settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal,
            button_size
        )
    ) {
        if (settings.scale_gizmo_mode != Scale_gizmo_mode::bounding_box) {
            settings.scale_gizmo_mode = Scale_gizmo_mode::bounding_box;
            update_visibility();
        }
    }

    // ImGui::TextUnformatted(is_transform_tool_active() ? "Active" : "Inactive");

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
}

void Transform_tool::update_target_nodes(erhe::scene::Node* node_filter)
{
    const auto& selection = m_context.selection->get_selected_items();

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
        visualizations->update_visibility(shared.settings);
    }
}

void Transform_tool::adjust(const mat4& updated_world_from_anchor)
{
    using namespace erhe::utility;

    touch();
    for (auto& entry : shared.entries) {
        const auto& node = entry.node;
        if (!node) {
            continue;
        }
        const bool node_lock_viewport_transform = test_bit_set(node->get_flag_bits(), erhe::Item_flags::lock_viewport_transform);
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

void Transform_tool::adjust_translation(const glm::vec3 translation)
{
    using namespace erhe::utility;
    touch();
    for (auto& entry : shared.entries) {
        auto& node = entry.node;
        if (!node) {
            continue;
        }
        const bool node_lock_viewport_transform = test_bit_set(node->get_flag_bits(), erhe::Item_flags::lock_viewport_transform);
        if (node_lock_viewport_transform) {
            continue;
        }

        node->set_world_from_node(erhe::scene::translate(entry.world_from_node_before, translation));
    }
    shared.world_from_anchor = erhe::scene::translate(shared.world_from_anchor_initial_state, translation);
    update_transforms();
}

void Transform_tool::adjust_rotation(const vec3 center_of_rotation, const quat rotation)
{
    using namespace erhe::utility;
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                continue;
            }
            const bool node_lock_viewport_transform = test_all_rhs_bits_set(node->get_flag_bits(), erhe::Item_flags::lock_viewport_transform);
            if (node_lock_viewport_transform) {
                continue;
            }

            node->set_world_from_node(erhe::scene::rotate(entry.world_from_node_before, rotation));
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
    using namespace erhe::utility;
    if (shared.settings.local && shared.entries.size() == 1) {
        touch();
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                continue;
            }
            const bool node_lock_viewport_transform = test_bit_set(node->get_flag_bits(), erhe::Item_flags::lock_viewport_transform);
            if (node_lock_viewport_transform) {
                continue;
            }

            node->set_world_from_node(erhe::scene::scale(entry.world_from_node_before, scale));
        }
        shared.world_from_anchor = erhe::scene::scale(shared.world_from_anchor_initial_state, scale);
    } else {
        const mat4 translate   = erhe::math::create_translation<float>(vec3{-center_of_scale});
        const mat4 untranslate = erhe::math::create_translation<float>(vec3{ center_of_scale});
        adjust(
            untranslate * glm::scale(mat4{1.0f}, scale) * translate * shared.world_from_anchor_initial_state.get_matrix()
        );
    }
    update_transforms();
}

void Transform_tool::update_hover()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_trs_tool->debug("scene_view == nullptr");
        m_hover_handle          = Handle::e_handle_none;
        m_box_face_hover_active = false;
        return;
    }

    Handle new_handle = Handle::e_handle_none;

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    std::shared_ptr<erhe::scene::Mesh> scene_mesh = tool.scene_mesh_weak.lock();
    if (tool.valid && scene_mesh) {
        new_handle = get_handle(scene_mesh.get());
    }

    // When no gizmo mesh (e.g. a bounding-box cone) is hovered, fall back to a custom
    // ray vs bounding-box-face test so every face is draggable without face meshes.
    m_box_face_hover_active = false;
    if ((new_handle == Handle::e_handle_none) && update_box_face_hover(scene_view)) {
        new_handle              = m_box_face_hover_handle;
        m_box_face_hover_active = true;
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

auto Transform_tool::update_box_face_hover(Scene_view* scene_view) -> bool
{
    if (
        !shared.settings.show_scale ||
        (shared.settings.scale_gizmo_mode != Scale_gizmo_mode::bounding_box)
    ) {
        return false;
    }
    Handle_visualizations* visualizations = shared.get_visualizations();
    if ((visualizations == nullptr) || !visualizations->is_box_valid()) {
        return false;
    }

    const std::optional<glm::vec3> origin_opt    = scene_view->get_control_ray_origin_in_world();
    const std::optional<glm::vec3> direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!origin_opt.has_value() || !direction_opt.has_value()) {
        return false;
    }

    const glm::mat4         box_frame = visualizations->get_box_frame();
    const glm::mat4         box_inv   = glm::inverse(box_frame);
    const erhe::math::Aabb& aabb      = visualizations->get_box_aabb();
    const glm::vec3         o_box     = glm::vec3{box_inv * glm::vec4{origin_opt.value(),    1.0f}};
    const glm::vec3         d_box     = glm::vec3{box_inv * glm::vec4{direction_opt.value(), 0.0f}};

    const Handle pos_handles[3] = {
        Handle::e_handle_box_scale_pos_x,
        Handle::e_handle_box_scale_pos_y,
        Handle::e_handle_box_scale_pos_z
    };
    const Handle neg_handles[3] = {
        Handle::e_handle_box_scale_neg_x,
        Handle::e_handle_box_scale_neg_y,
        Handle::e_handle_box_scale_neg_z
    };

    float  best_t     {std::numeric_limits<float>::max()};
    Handle best_handle{Handle::e_handle_none};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d_box[axis]) < 1e-6f) {
            continue;
        }
        const int axis_b = (axis + 1) % 3;
        const int axis_c = (axis + 2) % 3;
        for (int sign = 0; sign < 2; ++sign) {
            const float face_coord = (sign == 0) ? aabb.max[axis] : aabb.min[axis];
            const float t          = (face_coord - o_box[axis]) / d_box[axis];
            if (t <= 0.0f) {
                continue;
            }
            const glm::vec3 hit_box = o_box + (t * d_box);
            if (
                (hit_box[axis_b] < aabb.min[axis_b]) || (hit_box[axis_b] > aabb.max[axis_b]) ||
                (hit_box[axis_c] < aabb.min[axis_c]) || (hit_box[axis_c] > aabb.max[axis_c])
            ) {
                continue;
            }
            if (t < best_t) {
                best_t      = t;
                best_handle = (sign == 0) ? pos_handles[axis] : neg_handles[axis];
                m_box_face_hover_position = glm::vec3{box_frame * glm::vec4{hit_box, 1.0f}};
            }
        }
    }

    if (best_handle == Handle::e_handle_none) {
        return false;
    }
    m_box_face_hover_handle = best_handle;
    return true;
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

    glm::vec3 initial_drag_position_in_world{0.0f};
    if (m_box_face_hover_active) {
        // Drag started on a bounding-box face picked via ray-face intersection; there is
        // no gizmo mesh under the pointer, so use the stored face hit position.
        initial_drag_position_in_world = m_box_face_hover_position;
    } else {
        const auto& hover_entry = scene_view->get_hover(Hover_entry::tool_slot);
        std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = hover_entry.scene_mesh_weak.lock();
        if (!hover_entry.valid || !hover_scene_mesh) {
            log_trs_tool->trace("Transform tool cannot start drag - Pointer is not hovering over tool handle");
            return false;
        }
        initial_drag_position_in_world = hover_entry.position.value();
    }

    shared.set_initial_drag_position_in_world(initial_drag_position_in_world);
    shared.initial_drag_position_distance_to_camera = distance(
        vec3{camera_node->position_in_world()},
        initial_drag_position_in_world
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
    shared.initial_drag_position_distance_to_camera = 0.0;

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

    std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(&node);
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

    erhe::renderer::Primitive_renderer line_renderer = m_context.debug_renderer->get({erhe::graphics::Primitive_type::line, 2, true, true});

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

    if (
        shared.settings.show_scale &&
        (shared.settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box)
    ) {
        Handle_visualizations* visualizations = shared.get_visualizations();
        if ((visualizations != nullptr) && visualizations->is_box_valid()) {
            const erhe::math::Aabb& aabb = visualizations->get_box_aabb();
            erhe::renderer::Primitive_renderer line_renderer = m_context.debug_renderer->get(
                {erhe::graphics::Primitive_type::line, 2, true, true}
            );
            line_renderer.add_cube(
                visualizations->get_box_frame(),
                glm::vec4{1.0f, 0.7f, 0.1f, 1.0f},
                aabb.min,
                aabb.max
            );
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
        visualizations->update_visibility(shared.settings);
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

    const glm::mat4 m           = m_transform->get_matrix();
    const float     determinant = glm::determinant(m);
    if (determinant < 0.0f) {
        ImGui::Text("Negative determinant (%.9f)", determinant);
    }

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
        if (m_use_world_mode) {
            transform_tool.adjust_translation(m_translation - shared.world_from_anchor_initial_state.get_translation());
        } else {
            // In local mode m_translation is in parent space; apply it directly
            // to parent_from_node instead of treating it as a world space value.
            transform_tool.touch();
            Trs_transform parent_from_node = shared.entries.front().parent_from_node_before;
            parent_from_node.set_translation(m_translation);
            m_first_node->set_parent_from_node(parent_from_node);
            shared.world_from_anchor.set(m_first_node->world_from_node());
            transform_tool.update_transforms();
        }
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
        if (m_use_world_mode) {
            Trs_transform n = shared.world_from_anchor_initial_state;
            n.set_scale(m_scale);
            transform_tool.adjust(n.get_matrix());
        } else {
            // In local mode m_scale is in parent space; apply it directly
            // to parent_from_node instead of treating it as a world space value.
            transform_tool.touch();
            Trs_transform parent_from_node = shared.entries.front().parent_from_node_before;
            parent_from_node.set_scale(m_scale);
            m_first_node->set_parent_from_node(parent_from_node);
            shared.world_from_anchor.set(m_first_node->world_from_node());
            transform_tool.update_transforms();
        }
    }

    if (m_skew_state.value_changed) {
        if (m_use_world_mode) {
            Trs_transform n = shared.world_from_anchor_initial_state;
            n.set_skew(m_skew);
            transform_tool.adjust(n.get_matrix());
        } else {
            // In local mode m_skew is in parent space; apply it directly
            // to parent_from_node instead of treating it as a world space value.
            transform_tool.touch();
            Trs_transform parent_from_node = shared.entries.front().parent_from_node_before;
            parent_from_node.set_skew(m_skew);
            m_first_node->set_parent_from_node(parent_from_node);
            shared.world_from_anchor.set(m_first_node->world_from_node());
            transform_tool.update_transforms();
        }
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

}

