#include "transform/transform_tool.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"

#include "animation/animation_keying.hpp"
#include "animation/animation_player.hpp"
#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp" // need to be able to pass to visualization
#include "renderers/render_context.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "windows/item_reference.hpp"

#include "erhe_commands/commands.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/transform_tool_config.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_renderer/text_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_utility/bit_helpers.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <glm/gtc/constants.hpp>

#include <imgui/imgui.h>

#include <fmt/format.h>

#include <algorithm>
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

namespace {

// All transform-handle line rendering (the handles themselves, the hover
// previews, the rotate protractor) uses the x-ray bucket: the occluded pass
// blends at full strength instead of the dim hidden-pass constant, so the
// gizmo lines stay readable inside content meshes.
constexpr erhe::renderer::Debug_renderer_config handle_line_config{
    .primitive_type    = erhe::graphics::Primitive_type::line,
    .stencil_reference = 2,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};

// Off-screen gizmo indicator (render_offscreen_indicator): filled triangle,
// x-ray so scene geometry near the view edge cannot occlude it.
constexpr erhe::renderer::Debug_renderer_config offscreen_indicator_fill_config{
    .primitive_type    = erhe::graphics::Primitive_type::triangle,
    .stencil_reference = 2,
    .draw_visible      = true,
    .draw_hidden       = true,
    .xray              = true
};

}

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

Create_frame_node_command::Create_frame_node_command(erhe::commands::Commands& commands, App_context& app_context)
    : Command  {commands, "Transform_tool.create_frame_node"}
    , m_context{app_context}
{
}

auto Create_frame_node_command::try_call() -> bool
{
    m_context.transform_tool->create_node_from_anchor();
    return true;
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
    , m_create_frame_node_command   {commands, app_context}
{
    ERHE_PROFILE_FUNCTION();

    auto& settings = shared.settings;
    settings.show_translate = transform_tool_config.show_translate;
    settings.show_rotate    = transform_tool_config.show_rotate;
    settings.show_scale     = transform_tool_config.show_scale;

    static_cast<void>(executor);
    static_cast<void>(mesh_memory); // handles are debug-rendered, no meshes
    shared.visualizations = std::make_unique<Handle_visualizations>(app_context);
    shared.visualizations_ready.store(true);

    set_base_priority(c_priority);
    set_description  ("Transform");

    commands.register_command(&m_drag_command);
    commands.bind_command_to_mouse_drag(&m_drag_command, erhe::window::Mouse_button_left, true);

    commands.register_command(&m_create_frame_node_command);
    commands.bind_command_to_key(&m_create_frame_node_command, erhe::window::Key_n);
    m_create_frame_node_command.set_host(this);

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
    m_active_scene_subscription = app_message_bus.active_scene.subscribe(
        [&](Active_scene_changed_message& message) {
            on_active_scene(message);
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
    // In component mode the gizmo tracks the mesh component selection, not the node
    // selection; the anchor is recomputed each idle frame in update_for_view().
    if (shared.component_mode) {
        return;
    }
    update_target_nodes(nullptr);
}

void Transform_tool::on_active_scene(Active_scene_changed_message&)
{
    // Rebind the gizmo to the new active scene's selection (window-focus
    // activation changes the active scene without a selection change).
    if (shared.component_mode) {
        return;
    }
    update_target_nodes(nullptr);
    update_visibility();
}

void Transform_tool::on_animation_update(Animation_update_message&)
{
    if (shared.component_mode) {
        return;
    }
    update_target_nodes(nullptr);
}

void Transform_tool::on_node_touched(Node_touched_message& message)
{
    if (shared.component_mode) {
        return;
    }
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

    const ImVec2 mode_button_size{ImGui::GetContentRegionAvail().x / 4.0f, 0.0f};
    const auto reference_mode_button = [&](const char* label, const Transform_reference_mode mode) {
        if (
            erhe::imgui::make_button(
                label,
                (settings.reference_mode == mode) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal,
                mode_button_size
            )
        ) {
            if (settings.reference_mode != mode) {
                settings.reference_mode = mode;
                on_reference_settings_changed();
            }
        }
    };
    // The note is produced by the node-selection path; in component mode the
    // gizmo is driven by the mesh component selection and the note is stale.
    if (!shared.component_mode && !m_transform_target_note.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.8f, 0.4f, 1.0f});
        ImGui::TextWrapped("%s", m_transform_target_note.c_str());
        ImGui::PopStyleColor();
    }

    reference_mode_button("Global",    Transform_reference_mode::global);
    ImGui::SameLine();
    reference_mode_button("Local",     Transform_reference_mode::local);
    ImGui::SameLine();
    reference_mode_button("Reference", Transform_reference_mode::reference);
    ImGui::SameLine();
    reference_mode_button("Selection", Transform_reference_mode::selection);

    if (settings.reference_mode == Transform_reference_mode::reference) {
        // Build the picker candidate list (reused scratch, capacity retained).
        m_reference_candidates.clear();
        Scene_root* scene_root = m_context.scene_commands->get_scene_root(static_cast<erhe::scene::Node*>(nullptr));
        if (scene_root != nullptr) {
            scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
                if (node) {
                    m_reference_candidates.push_back(node);
                }
                return true;
            });
        }
        Item_reference_options options;
        options.candidates = m_reference_candidates;
        ImGui::TextUnformatted("Reference node");
        ImGui::SameLine();
        // Reference node: drag a node from the scene tree onto the field (shows the drop-target
        // highlight), drag the field out to use the node elsewhere, pick from the list, or add it
        // to the selection to inspect it in the Properties window.
        if (
            item_reference_imgui<erhe::scene::Node>(
                m_context, "##reference_node", shared.reference_node, erhe::scene::Node::get_static_type(), options
            )
        ) {
            on_reference_settings_changed();
        }
    }

    if (settings.reference_mode == Transform_reference_mode::selection) {
        ImGui::SliderFloat("Edge normal blend", &settings.edge_normal_blend, 0.0f, 1.0f);
    }

    if (erhe::imgui::make_button("Create node from frame", erhe::imgui::Item_mode::normal, button_size)) {
        create_node_from_anchor();
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

    // Persistent gizmo preferences (Negative Translate Handles, Hover
    // Preview, Visible Ring Arcs Only, Active Rotate Ring Size, Rotation
    // Sector Anchoring) are edited in the Settings window's Transform Tool
    // section; they are read live each frame so edits there take effect
    // immediately.

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

    }

    ImGui::Separator();
}

void Transform_tool_shared::apply_reference_frame()
{
    if (settings.reference_mode == Transform_reference_mode::reference) {
        const std::shared_ptr<erhe::scene::Node> node = reference_node.lock();
        if (node) {
            world_from_anchor_initial_state = node->world_from_node_transform();
        }
    }
    world_from_anchor = world_from_anchor_initial_state;
}

void Transform_tool::on_reference_settings_changed()
{
    // The mesh-component path re-derives the anchor every idle frame (see
    // update_for_view -> Mesh_component_transform::update_anchor), so only the
    // node-selection path needs an explicit refresh when the mode or reference
    // node changes.
    if (shared.component_mode) {
        return;
    }
    update_target_nodes(nullptr);
}

auto Transform_tool::resolve_transform_target(
    const std::shared_ptr<erhe::scene::Node>& node
) -> std::shared_ptr<erhe::scene::Node>
{
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
    if (!mesh || !mesh->skin) {
        return node;
    }

    // Skinning ignores the mesh node's transform (glTF 2.0 Specification
    // "Joint Hierarchy": "Only the joint transforms are applied to the skinned
    // mesh; the transform of the skinned mesh node MUST be ignored"). erhe
    // implements exactly that - Joint_buffer builds world_from_bind from the
    // joint nodes and standard.vert substitutes it for world_from_node - so
    // dragging the host node would move the gizmo and leave the mesh in place.
    // Drive the skin's transform root instead: it is an ancestor of every
    // joint, so moving it moves the posed result rigidly.
    const std::shared_ptr<erhe::scene::Node> skin_root = erhe::scene::get_skin_transform_root(*mesh->skin);
    if (!skin_root) {
        return node; // no joints, or joints in disjoint trees - nothing better to offer
    }

    // Joints inside the mesh node's own subtree are the one case where the host
    // node does drive the skinned result (it transforms the joints with it), so
    // leave those alone.
    if ((skin_root == node) || skin_root->is_ancestor(node.get())) {
        return node;
    }

    return skin_root;
}

void Transform_tool::update_target_nodes(erhe::scene::Node* node_filter)
{
    // The gizmo operates on one scene at a time: the ACTIVE scene
    // (Selection::get_active_scene_root - follows selection changes and
    // viewport / hierarchy window focus). Selection in other scenes persists
    // but never feeds the gizmo, so the anchor is always an average within
    // one world space and a drag can only ever move one scene's nodes.
    static const std::vector<std::shared_ptr<erhe::Item_base>> s_no_selection{};
    const std::shared_ptr<Scene_root> active_scene_root = m_context.selection->get_active_scene_root();
    const auto& selection = active_scene_root
        ? m_context.selection->get_hosted_selection(static_cast<erhe::Item_host*>(active_scene_root.get()))
        : s_no_selection;

    vec3 cumulative_world_translation{0.0f, 0.0f, 0.0f};
    quat cumulative_world_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    vec3 cumulative_world_scale      {0.0f, 0.0f, 0.0f};
    std::size_t node_count{0};

    // Resolve the selection to the nodes the gizmo actually drives. This is
    // identity for everything except skinned meshes, which redirect to their
    // skin transform root (see resolve_transform_target). Duplicates must be
    // collapsed: several skinned meshes sharing one skin resolve to the same
    // root, and a drag would otherwise apply its delta once per mesh. The same
    // resolution runs on both the rebuild and the node_filter refresh path, so
    // shared.entries stays index-aligned between them.
    m_target_nodes.clear();
    m_transform_target_note.clear();
    std::size_t redirect_count{0};
    for (const auto& item : selection) {
        const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }
        std::shared_ptr<erhe::scene::Node> target = resolve_transform_target(node);
        if (!target) {
            continue;
        }
        if (target != node) {
            // A redirected gizmo sits somewhere other than the selected node,
            // which reads as a bug unless it is explained. Report the first
            // redirection (and how many more there are) in the Transform window.
            if (redirect_count == 0) {
                m_transform_target_note = fmt::format(
                    "Gizmo drives '{}': skinning ignores the transform of skinned mesh node '{}'.",
                    target->get_name(), node->get_name()
                );
            }
            ++redirect_count;
        }
        if (std::find(m_target_nodes.begin(), m_target_nodes.end(), target) != m_target_nodes.end()) {
            continue;
        }
        m_target_nodes.push_back(std::move(target));
    }
    if (redirect_count > 1) {
        m_transform_target_note += fmt::format(" (+{} more)", redirect_count - 1);
    }

    if (node_filter == nullptr) {
        shared.entries.clear();
    }
    std::size_t i = 0;

    for (const std::shared_ptr<erhe::scene::Node>& node : m_target_nodes) {
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
        // The Transform window's numeric-edit state is rebuilt from the
        // selection each draw (transform_properties), but not when the
        // selection is empty - the stale state would keep the previous first
        // node (possibly of a closed scene) alive through its shared_ptr.
        // Drop it here, on the same selection / active-scene update that
        // emptied the entries, independent of window visibility.
        if (node_filter == nullptr) {
            m_edit_state = Edit_state{};
        }
    } else {
        shared.world_from_anchor_initial_state.set_trs(
            cumulative_world_translation / static_cast<float>(node_count),
            cumulative_world_rotation,
            cumulative_world_scale / static_cast<float>(node_count)
        );
    }

    // Finalize the reference frame: in Reference mode the chosen reference node
    // replaces the frame entirely; otherwise the selection-derived frame stands.
    // The same finalizer runs for node and mesh-component selections, so
    // consumers read world_from_anchor without caring which origin produced it.
    shared.apply_reference_frame();

    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->set_anchor(shared.world_from_anchor);
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
    if (shared.component_mode) {
        apply_component_transform(
            erhe::scene::translate(shared.world_from_anchor_initial_state, translation).get_matrix()
        );
        return;
    }
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
    if (shared.component_mode) {
        const mat4 translate   = erhe::math::create_translation<float>(vec3{-center_of_rotation});
        const mat4 untranslate = erhe::math::create_translation<float>(vec3{ center_of_rotation});
        apply_component_transform(
            untranslate * mat4_cast(rotation) * translate * shared.world_from_anchor_initial_state.get_matrix()
        );
        return;
    }
    if (shared.settings.is_local() && shared.entries.size() == 1) {
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
    if (shared.component_mode) {
        const mat4 translate   = erhe::math::create_translation<float>(vec3{-center_of_scale});
        const mat4 untranslate = erhe::math::create_translation<float>(vec3{ center_of_scale});
        apply_component_transform(
            untranslate * glm::scale(mat4{1.0f}, scale) * translate * shared.world_from_anchor_initial_state.get_matrix()
        );
        return;
    }
    if (shared.settings.is_local() && shared.entries.size() == 1) {
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

void Transform_tool::apply_translation_edit(const glm::vec3 translation, const bool local)
{
    if (shared.component_mode) {
        if (!is_component_edit_active()) {
            begin_component_edit();
        }
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_translation(translation);
        apply_component_transform(updated_world_from_anchor.get_matrix());
        return;
    }
    if (shared.entries.empty()) {
        return;
    }
    if (!local || (shared.entries.size() > 1)) {
        adjust_translation(translation - shared.world_from_anchor_initial_state.get_translation());
        return;
    }
    // In local mode the edited value is in parent space; apply it directly
    // to parent_from_node instead of treating it as a world space value.
    touch();
    Transform_entry& entry = shared.entries.front();
    if (!entry.node) {
        return;
    }
    Trs_transform parent_from_node = entry.parent_from_node_before;
    parent_from_node.set_translation(translation);
    entry.node->set_parent_from_node(parent_from_node);
    shared.world_from_anchor.set(entry.node->world_from_node());
    update_transforms();
}

void Transform_tool::apply_rotation_edit(const glm::quat rotation, const bool local)
{
    if (shared.component_mode) {
        if (!is_component_edit_active()) {
            begin_component_edit();
        }
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_rotation(rotation);
        apply_component_transform(updated_world_from_anchor.get_matrix());
        return;
    }
    if (shared.entries.empty()) {
        return;
    }
    touch();
    if (!local || (shared.entries.size() > 1)) {
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                return;
            }
            Trs_transform world_from_node = entry.world_from_node_before;
            world_from_node.set_rotation(rotation);
            node->set_world_from_node(world_from_node);
        }
        shared.world_from_anchor.set_rotation(rotation);
    } else {
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                return;
            }
            Trs_transform parent_from_node = entry.parent_from_node_before;
            parent_from_node.set_rotation(rotation);
            node->set_parent_from_node(parent_from_node);
            shared.world_from_anchor.set(node->world_from_node());
        }
    }
    update_transforms();
}

void Transform_tool::apply_scale_edit(const glm::vec3 scale, const bool local)
{
    if (shared.component_mode) {
        if (!is_component_edit_active()) {
            begin_component_edit();
        }
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_scale(scale);
        apply_component_transform(updated_world_from_anchor.get_matrix());
        return;
    }
    if (shared.entries.empty()) {
        return;
    }
    if (!local || (shared.entries.size() > 1)) {
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_scale(scale);
        adjust(updated_world_from_anchor.get_matrix());
        return;
    }
    // In local mode the edited value is in parent space; apply it directly
    // to parent_from_node instead of treating it as a world space value.
    touch();
    Transform_entry& entry = shared.entries.front();
    if (!entry.node) {
        return;
    }
    Trs_transform parent_from_node = entry.parent_from_node_before;
    parent_from_node.set_scale(scale);
    entry.node->set_parent_from_node(parent_from_node);
    shared.world_from_anchor.set(entry.node->world_from_node());
    update_transforms();
}

void Transform_tool::apply_skew_edit(const glm::vec3 skew, const bool local)
{
    if (shared.component_mode) {
        if (!is_component_edit_active()) {
            begin_component_edit();
        }
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_skew(skew);
        apply_component_transform(updated_world_from_anchor.get_matrix());
        return;
    }
    if (shared.entries.empty()) {
        return;
    }
    if (!local || (shared.entries.size() > 1)) {
        Trs_transform updated_world_from_anchor = shared.world_from_anchor_initial_state;
        updated_world_from_anchor.set_skew(skew);
        adjust(updated_world_from_anchor.get_matrix());
        return;
    }
    // In local mode the edited value is in parent space; apply it directly
    // to parent_from_node instead of treating it as a world space value.
    touch();
    Transform_entry& entry = shared.entries.front();
    if (!entry.node) {
        return;
    }
    Trs_transform parent_from_node = entry.parent_from_node_before;
    parent_from_node.set_skew(skew);
    entry.node->set_parent_from_node(parent_from_node);
    shared.world_from_anchor.set(entry.node->world_from_node());
    update_transforms();
}

void Transform_tool::update_hover()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        log_trs_tool->debug("scene_view == nullptr");
        m_hover_handle          = Handle::e_handle_none;
        m_box_face_hover_active = false;
        m_pick_active           = false;
        m_ray_sphere_entry.reset();
        m_ray_sphere_exit.reset();
        m_ray_sphere_plane_crossing.reset();
        return;
    }

    // The gizmo targets the active scene only: in views of other scenes the
    // handles are hidden and must not arm.
    if (!is_scene_view_of_active_scene(scene_view)) {
        m_hover_handle          = Handle::e_handle_none;
        m_box_face_hover_active = false;
        m_pick_active           = false;
        m_hover_tool            = nullptr;
        m_ray_sphere_entry.reset();
        m_ray_sphere_exit.reset();
        m_ray_sphere_plane_crossing.reset();
        return;
    }

    // All handles are hit tested analytically against the control ray
    // (Handle_visualizations::pick) - there are no gizmo meshes.
    Handle new_handle = Handle::e_handle_none;
    m_pick_active = false;
    m_ray_sphere_entry.reset();
    m_ray_sphere_exit.reset();
    m_ray_sphere_plane_crossing.reset();
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        const std::optional<glm::vec3> origin_opt    = scene_view->get_control_ray_origin_in_world();
        const std::optional<glm::vec3> direction_opt = scene_view->get_control_ray_direction_in_world();
        if (origin_opt.has_value() && direction_opt.has_value()) {
            // View-dependent shown/hidden choices are made from the camera
            // (in XR the head - one mono decision both eyes share), matching
            // render(); only the intersection uses the control ray, which in
            // XR originates at the controller.
            glm::vec3 eye_position = origin_opt.value();
            const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
            const erhe::scene::Node* camera_node = camera ? camera->get_node() : nullptr;
            if (camera_node != nullptr) {
                eye_position = glm::vec3{camera_node->position_in_world()};
            }
            const std::optional<Handle_pick> pick = visualizations->pick(eye_position, origin_opt.value(), direction_opt.value());
            if (pick.has_value()) {
                new_handle      = pick->handle;
                m_pick_position = pick->position;
                m_pick_active   = true;
            }
            // Rotation-sphere crossing for the XR controller ray stop:
            // computed regardless of the pick result - a ray can hit a
            // visible handle AND cross the sphere, or cross only the
            // (invisible) sphere interior.
            const std::optional<Handle_visualizations::Rotate_sphere_intersection> sphere =
                visualizations->intersect_rotate_sphere(origin_opt.value(), direction_opt.value());
            if (sphere.has_value()) {
                m_ray_sphere_entry          = sphere->entry;
                m_ray_sphere_exit           = sphere->exit;
                m_ray_sphere_plane_crossing = sphere->first_plane_crossing;
            }
        }
    }

    // When no handle is picked, fall back to the ray vs bounding-box-face test
    // so every box face is draggable, not only the face-center cones.
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
    if (!is_scene_view_of_active_scene(scene_view)) {
        log_trs_tool->trace("Transform tool cannot start drag - Hovered view does not show the active scene");
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
        // Drag started on a bounding-box face picked via ray-face intersection;
        // use the stored face hit position.
        initial_drag_position_in_world = m_box_face_hover_position;
    } else if (m_pick_active) {
        // Drag started on an analytically picked handle; use the stored grab point.
        initial_drag_position_in_world = m_pick_position;
    } else {
        log_trs_tool->trace("Transform tool cannot start drag - Pointer is not hovering over tool handle");
        return false;
    }

    shared.set_initial_drag_position_in_world(initial_drag_position_in_world);
    shared.initial_drag_position_distance_to_camera = distance(
        vec3{camera_node->position_in_world()},
        initial_drag_position_in_world
    );

    if (shared.entries.empty() && !shared.component_mode) {
        log_trs_tool->trace("drag not possible - no selection");
        return false;
    }

    const unsigned int axis_mask = get_axis_mask(m_active_handle);
    // Begin the component edit only once the subtool drag has actually started: a
    // subtool begin() can fail (e.g. the rotate ring hit edge-on), in which case
    // end_drag() never runs and a prematurely-begun component edit would be left
    // stuck active, freezing the gizmo anchor.
    const bool started = m_active_tool->begin(axis_mask, scene_view);
    if (started && shared.component_mode) {
        begin_component_edit();
    }
    return started;
}

void Transform_tool::end_drag()
{
    log_trs_tool->trace("ending drag");

    if (m_active_tool != nullptr) {
        m_active_tool->end();
    }

    // In component mode the node record path (record_transform_operation, called via
    // Subtool::end above) is a no-op because shared.entries is empty; queue the mesh
    // vertex operation instead.
    if (shared.component_mode && is_component_edit_active()) {
        commit_component_edit();
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

auto Transform_tool::get_hover_handle_position_in_world() const -> std::optional<glm::vec3>
{
    // Visible handle first: the ray stops at its first intersection point
    // (arrows, rings, quads, center cube, view ring). The arcball region
    // (e_handle_rotate_free) is the INVISIBLE rotation sphere interior, so
    // it falls through to the sphere-exit rule below. Box-face hover is
    // deliberately excluded: the box hugs the selection's surface, so for
    // the XR ray it reads as the mesh surface, not as a distinct handle.
    if (
        (m_hover_handle != Handle::e_handle_none)        &&
        (m_hover_handle != Handle::e_handle_rotate_free) &&
        m_pick_active                                    &&
        !m_box_face_hover_active
    ) {
        return m_pick_position;
    }
    // Rotation sphere crossed without a visible handle in front: the ray
    // stops at the FARTHEST intersection with the sphere, so entering the
    // gizmo region anywhere - including its invisible parts - reads as
    // "pointing at the tool".
    return m_ray_sphere_exit;
}

auto Transform_tool::get_ray_sphere_entry_position_in_world() const -> std::optional<glm::vec3>
{
    return m_ray_sphere_entry;
}

auto Transform_tool::get_ray_sphere_plane_crossing_position_in_world() const -> std::optional<glm::vec3>
{
    return m_ray_sphere_plane_crossing;
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
        { 0.0f,  1.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        { 1.0f,  0.0f,  0.0f},
        {-1.0f,  0.0f,  0.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  0.0f, -1.0f}
    };

    // X-ray bucket like the gizmo handles: the occluded pass blends at full
    // strength, so the ray / hit lines stay in front of mesh surface detail
    // (edge lines in particular) instead of dropping to the dim hidden pass
    // wherever the coplanar edge lines win the depth test.
    erhe::renderer::Primitive_renderer line_renderer = m_context.debug_renderer->get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );
    // The ray lines get a higher stencil reference so they alpha-blend over
    // the hit markers where they overlap (ray antiparallel to the surface
    // normal) - see draw_ray_hit().
    erhe::renderer::Primitive_renderer ray_line_renderer = m_context.debug_renderer->get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 3,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );

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

            draw_ray_hit(line_renderer, ray, hit, ray_hit_style, &ray_line_renderer);
        }
    }
}

void Transform_tool::render_initial_position_ray()
{
    // One extra ray during translate drags: from the anchor's drag-start
    // position straight down (world -Y), showing where the travel began
    // relative to whatever lies below. The first dragged mesh is excluded
    // from the trace so a barely-moved node does not shadow its own start
    // point.
    if (shared.entries.empty()) {
        return;
    }
    const Transform_entry& entry = shared.entries.front();
    if (!entry.node) {
        return;
    }
    std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(entry.node.get());
    auto* scene_root = static_cast<Scene_root*>(entry.node->node_data.host);
    if (scene_root == nullptr) {
        return;
    }

    // Same buckets as render_rays(): markers at stencil reference 2, ray
    // lines at 3 so they alpha-blend over the markers (see draw_ray_hit()).
    erhe::renderer::Primitive_renderer line_renderer = m_context.debug_renderer->get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );
    erhe::renderer::Primitive_renderer ray_line_renderer = m_context.debug_renderer->get(
        erhe::renderer::Debug_renderer_config{
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 3,
            .draw_visible      = true,
            .draw_hidden       = true,
            .xray              = true
        }
    );

    erhe::raytrace::Ray ray{
        .origin    = shared.world_from_anchor_initial_state.get_translation(),
        .t_near    = 0.0f,
        .direction = vec3{0.0f, -1.0f, 0.0f},
        .time      = 0.0f,
        .t_far     = 9999.0f,
        .mask      = Raytrace_node_mask::content,
        .id        = 0,
        .flags     = 0
    };
    erhe::raytrace::Hit hit;
    if (project_ray(&scene_root->get_raytrace_scene(), mesh.get(), ray, hit)) {
        const Ray_hit_style ray_hit_style{
            .ray_color     = vec4{1.0f, 0.0f, 1.0f, 1.0f},
            .ray_thickness = 8.0f,
            .ray_length    = 0.5f,
            .hit_color     = vec4{0.8f, 0.2f, 0.8f, 0.75f},
            .hit_thickness = 8.0f,
            .hit_size      = 0.10f
        };
        draw_ray_hit(line_renderer, ray, hit, ray_hit_style, &ray_line_renderer);
    }
}

void Transform_tool::tool_render(const Render_context& context)
{
    ERHE_PROFILE_FUNCTION();

    // The gizmo (and its debug lines: cast rays, bounding-box cube) targets
    // the active scene; draw nothing into views of other scenes.
    if (!is_scene_view_of_active_scene(&context.scene_view)) {
        return;
    }

    // Optional translate-drag debug visualization (Settings window): cast
    // rays from each dragged node along the world axes and mark the nearest
    // hits with purple lines, like the physics tool's drag visualization.
    const Handle_type active_type = get_handle_type(m_active_handle);
    const bool translate_drag_active =
        (active_type == Handle_type::e_handle_type_translate_axis) ||
        (active_type == Handle_type::e_handle_type_translate_plane);
    if (m_context.editor_settings->transform_tool.translate_cast_rays && translate_drag_active) {
        for (auto& entry : shared.entries) {
            auto& node = entry.node;
            if (!node) {
                continue;
            }
            render_rays(*node.get());
        }
        render_initial_position_ray();
    }

    // All gizmo handles are drawn with the debug primitive renderer
    // (x-ray lines and filled triangles) - there are no handle meshes.
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->render(context, m_hover_handle, m_active_handle);
    }

    if (m_context.editor_settings->transform_tool.hover_preview) {
        render_hover_preview(context);
    }
    render_translate_drag_guides(context);
    render_drag_readout(context);
    render_offscreen_indicator(context);

    m_context.rotate_tool->render(context);
}

void Transform_tool::render_offscreen_indicator(const Render_context& context)
{
    // Same presence condition as Handle_visualizations::has_target(): the
    // gizmo exists only when a node selection or a component anchor drives it.
    if (shared.entries.empty() && !shared.component_mode) {
        return;
    }
    if (context.views.empty()) {
        return;
    }

    const vec3 target = shared.world_from_anchor.get_translation();

    // Per-view frustum classification of the gizmo anchor, in that view's
    // camera space (X right, Y up, camera looking down -Z).
    class View_classification
    {
    public:
        bool usable{false}; // supported projection type with a camera node
        bool inside{false};
    };

    const auto is_perspective = [](const erhe::scene::Projection::Type type) -> bool {
        return
            (type == erhe::scene::Projection::Type::perspective) ||
            (type == erhe::scene::Projection::Type::perspective_xr) ||
            (type == erhe::scene::Projection::Type::perspective_horizontal) ||
            (type == erhe::scene::Projection::Type::perspective_vertical);
    };
    const auto is_orthogonal = [](const erhe::scene::Projection::Type type) -> bool {
        return
            (type == erhe::scene::Projection::Type::orthogonal) ||
            (type == erhe::scene::Projection::Type::orthogonal_horizontal) ||
            (type == erhe::scene::Projection::Type::orthogonal_vertical) ||
            (type == erhe::scene::Projection::Type::orthogonal_rectangle);
    };

    const auto classify = [&](const erhe::scene_renderer::Camera_view_input& view) -> View_classification {
        View_classification result{};
        if ((view.projection == nullptr) || (view.node == nullptr)) {
            return result;
        }
        const erhe::scene::Projection::Type type = view.projection->projection_type;
        const bool perspective = is_perspective(type);
        const bool orthogonal  = is_orthogonal (type);
        if (!perspective && !orthogonal) {
            return result;
        }
        result.usable = true;

        const vec3  p     = vec3{view.node->node_from_world() * vec4{target, 1.0f}};
        const float depth = -p.z;
        if ((depth < view.projection->z_near) || (depth > view.projection->z_far)) {
            return result; // behind the near plane (or beyond far) -> outside
        }
        const erhe::scene::Projection::Fov_sides fov = view.projection->get_fov_sides(view.viewport);
        // Lateral frustum extents at the anchor's depth: angular sides for
        // perspective projections, fixed world-unit sides for orthogonal.
        const float x_min = perspective ? (depth * std::tan(fov.left )) : fov.left;
        const float x_max = perspective ? (depth * std::tan(fov.right)) : fov.right;
        const float y_min = perspective ? (depth * std::tan(fov.down )) : fov.down;
        const float y_max = perspective ? (depth * std::tan(fov.up   )) : fov.up;
        result.inside =
            (p.x >= x_min) && (p.x <= x_max) &&
            (p.y >= y_min) && (p.y <= y_max);
        return result;
    };

    // The indicator shows only when the anchor is outside EVERY rendered
    // view's frustum (in XR: outside both eyes), so a gizmo visible in one
    // eye never gets a distracting edge marker in the other.
    bool any_usable = false;
    for (const erhe::scene_renderer::Camera_view_input& view : context.views) {
        const View_classification classification = classify(view);
        if (!classification.usable) {
            continue;
        }
        any_usable = true;
        if (classification.inside) {
            return;
        }
    }
    if (!any_usable) {
        return;
    }

    // Build the triangle from the first usable view. World-space debug
    // primitives are submitted once and fanned out to all views, so in XR a
    // single triangle placed at the left eye's frustum edge is seen by both
    // eyes; the per-eye frusta differ by only the small IPD offset.
    const erhe::scene_renderer::Camera_view_input* placement_view = nullptr;
    for (const erhe::scene_renderer::Camera_view_input& view : context.views) {
        if ((view.projection != nullptr) && (view.node != nullptr) &&
            (is_perspective(view.projection->projection_type) || is_orthogonal(view.projection->projection_type)))
        {
            placement_view = &view;
            break;
        }
    }
    if (placement_view == nullptr) {
        return;
    }

    const erhe::scene::Projection&           projection = *placement_view->projection;
    const erhe::scene::Node&                 node       = *placement_view->node;
    const bool                               perspective = is_perspective(projection.projection_type);
    const erhe::scene::Projection::Fov_sides fov         = projection.get_fov_sides(placement_view->viewport);

    // Depth of the plane (in front of the camera) the indicator is drawn on.
    // Perspective: ~1 m, kept inside the clip range; orthogonal: mid range
    // (lateral extents are depth-independent there).
    const float d_ref = perspective
        ? std::clamp(1.0f, 2.0f * projection.z_near, 0.5f * projection.z_far)
        : (0.5f * (projection.z_near + projection.z_far));

    const float x_min = perspective ? (d_ref * std::tan(fov.left )) : fov.left;
    const float x_max = perspective ? (d_ref * std::tan(fov.right)) : fov.right;
    const float y_min = perspective ? (d_ref * std::tan(fov.down )) : fov.down;
    const float y_max = perspective ? (d_ref * std::tan(fov.up   )) : fov.up;
    const float width  = x_max - x_min;
    const float height = y_max - y_min;
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return;
    }
    const vec2  rect_center{0.5f * (x_min + x_max), 0.5f * (y_min + y_max)};
    const float indicator_size = 0.05f * std::min(width, height);

    // Direction from the view center toward the anchor, on the d_ref plane.
    const vec3  p_view = vec3{node.node_from_world() * vec4{target, 1.0f}};
    const float depth  = -p_view.z;
    vec2 direction;
    if (perspective && (depth <= projection.z_near)) {
        // At or behind the camera plane: no stable plane projection; the
        // view-space lateral offset still tells which side the anchor is on.
        direction = vec2{p_view.x, p_view.y} - rect_center;
    } else {
        const vec2 q = perspective
            ? (vec2{p_view.x, p_view.y} * (d_ref / depth))
            : vec2{p_view.x, p_view.y};
        direction = q - rect_center;
    }
    const float direction_length = glm::length(direction);
    if (!(direction_length > 1e-6f) || !std::isfinite(direction_length)) {
        direction = vec2{0.0f, -1.0f}; // exactly behind/ahead of center: point down
    } else {
        direction = direction / direction_length;
    }

    // Clamp a ray from the rect center along `direction` to the border of
    // the rect inset by the triangle's own extent, so the triangle stays
    // fully inside the view.
    const float margin       = 2.0f * indicator_size;
    const float half_extent_x = std::max(0.5f * width  - margin, 0.0f);
    const float half_extent_y = std::max(0.5f * height - margin, 0.0f);
    float t = std::numeric_limits<float>::max();
    if (std::abs(direction.x) > 1e-6f) {
        t = std::min(t, half_extent_x / std::abs(direction.x));
    }
    if (std::abs(direction.y) > 1e-6f) {
        t = std::min(t, half_extent_y / std::abs(direction.y));
    }
    if (!std::isfinite(t)) {
        return;
    }
    const vec2 edge_point = rect_center + (direction * t);

    // Arrow triangle on the d_ref plane: tip at the clamped edge point
    // pointing along `direction`, base inward.
    const vec2 u = direction;
    const vec2 v{-u.y, u.x};
    const vec2 tip    = edge_point + (u * indicator_size);
    const vec2 base_a = edge_point - (u * (0.8f * indicator_size)) + (v * (0.6f * indicator_size));
    const vec2 base_b = edge_point - (u * (0.8f * indicator_size)) - (v * (0.6f * indicator_size));

    const mat4 world_from_view = node.world_from_node();
    const auto to_world = [&world_from_view, d_ref](const vec2 point) -> vec3 {
        return vec3{world_from_view * vec4{point.x, point.y, -d_ref, 1.0f}};
    };

    constexpr vec4 indicator_yellow{1.0f, 1.0f, 0.0f, 0.85f};
    erhe::renderer::Primitive_renderer triangle_renderer = context.get(offscreen_indicator_fill_config);
    triangle_renderer.add_triangle(mat4{1.0f}, indicator_yellow, to_world(tip), to_world(base_a), to_world(base_b));
}

void Transform_tool::render_translate_drag_guides(const Render_context& context)
{
    // Active translate drag feedback (shown instead of the hover previews
    // for the duration of the drag), anchored on the NODE's travel: the
    // yellow traveled segment (axis drag: along the axis, plane drag: the
    // travel diagonal) from the anchor's position at drag start to its
    // current (snapped) position - the delta color, matching the delta text
    // at its midpoint and the rotate sector. Kept subtle: 1 px, reduced
    // alpha.
    const Handle_type type       = get_handle_type(m_active_handle);
    const bool        axis_drag  = (type == Handle_type::e_handle_type_translate_axis);
    const bool        plane_drag = (type == Handle_type::e_handle_type_translate_plane);
    if (!axis_drag && !plane_drag) {
        return;
    }
    constexpr float guide_width = -1.0f; // negative = constant screen-space pixels
    constexpr float guide_alpha = 0.75f;
    constexpr vec4  delta_yellow{1.0f, 1.0f, 0.0f, guide_alpha};

    const vec3 p0 = shared.world_from_anchor_initial_state.get_translation();
    const vec3 p1 = shared.world_from_anchor.get_translation();

    erhe::renderer::Primitive_renderer line_renderer = context.get(handle_line_config);
    line_renderer.set_thickness(guide_width);

    // End-stop markers: a short line across each end of a traveled segment,
    // orthogonal to it in screen space (dimension-line style). Built in
    // window space and unprojected back to world at each end's own depth,
    // because the debug line renderer takes world positions. Used on the
    // axis-drag segment and the plane-drag diagonal (both yellow).
    const auto add_end_stops = [&](const vec3& a, const vec3& b) {
        if (context.camera == nullptr) {
            return;
        }
        const auto projection_transforms = context.camera->projection_transforms(
            context.viewport,
            context.scene_view.get_reverse_depth(),
            context.scene_view.get_depth_range(),
            context.scene_view.get_conventions()
        );
        const mat4 clip_from_world = projection_transforms.clip_from_world.get_matrix();
        const mat4 world_from_clip = projection_transforms.clip_from_world.get_inverse_matrix();
        const vec3 w0 = context.viewport.project_to_screen_space(clip_from_world, a, 0.0f, 1.0f, context.scene_view.get_conventions());
        const vec3 w1 = context.viewport.project_to_screen_space(clip_from_world, b, 0.0f, 1.0f, context.scene_view.get_conventions());
        const vec2  delta_px  = vec2{w1} - vec2{w0};
        const float length_px = glm::length(delta_px);
        if (length_px < 1.0f) {
            return;
        }
        constexpr float stop_half_length_px = 5.0f;
        const vec2 ortho_px = (stop_half_length_px / length_px) * vec2{-delta_px.y, delta_px.x};
        for (const vec3& w : {w0, w1}) {
            const auto unproject_window = [&](const vec2 position_px) -> std::optional<vec3> {
                return erhe::math::unproject<float>(
                    world_from_clip,
                    vec3{position_px, w.z},
                    0.0f,
                    1.0f,
                    static_cast<float>(context.viewport.x),
                    static_cast<float>(context.viewport.y),
                    static_cast<float>(context.viewport.width),
                    static_cast<float>(context.viewport.height),
                    context.scene_view.get_conventions()
                );
            };
            const std::optional<vec3> stop_a = unproject_window(vec2{w} + ortho_px);
            const std::optional<vec3> stop_b = unproject_window(vec2{w} - ortho_px);
            if (stop_a.has_value() && stop_b.has_value()) {
                line_renderer.add_lines({{stop_a.value(), stop_b.value()}});
            }
        }
    };

    if (axis_drag) {
        line_renderer.set_line_color(delta_yellow);
        line_renderer.add_lines({{p0, p1}});
        add_end_stops(p0, p1);
        return;
    }

    // Plane drag: just the yellow diagonal with end stops - the axis-colored
    // rectangle edges were dropped as noise.
    line_renderer.set_line_color(delta_yellow);
    line_renderer.add_lines({{p0, p1}});
    add_end_stops(p0, p1);
}

void Transform_tool::render_drag_readout(const Render_context& context)
{
    if (m_active_handle == Handle::e_handle_none) {
        return;
    }
    erhe::renderer::Text_renderer* text_renderer = m_context.text_renderer;
    if ((text_renderer == nullptr) || !text_renderer->config.enabled || (context.camera == nullptr)) {
        return;
    }
    // Text readouts are window-space; the headset render context has no
    // viewport scene view. No text in XR (for now).
    if (context.viewport_scene_view == nullptr) {
        return;
    }

    std::string initial_line;
    std::string current_line;
    switch (get_handle_type(m_active_handle)) {
        case Handle_type::e_handle_type_translate_axis:
        case Handle_type::e_handle_type_translate_plane: {
            // Bare coordinate values only. The initial position is identified
            // by place (printed at its own window projection); the yellow
            // delta sits at the midpoint of the travel (the guide segment /
            // plane-drag diagonal, drawn in the same yellow); the current
            // position goes below the hover text's mesh name, whose anchor
            // (+50 px x) and 16 px line step are mirrored from
            // Hover_tool::tool_render().
            const vec3 p0 = shared.world_from_anchor_initial_state.get_translation();
            const vec3 p1 = shared.world_from_anchor.get_translation();

            // All three labels are placed first so the yellow delta can be
            // hidden when its ink box would run into the initial or current
            // label (same policy as the rotate protractor's delta label).
            // Measure bounds are in font space (baseline origin, y up); with
            // a top-left framebuffer origin the text renderer flips glyph y.
            const bool top_left = context.scene_view.get_framebuffer_origin() == erhe::math::Framebuffer_origin::top_left;
            struct Placed_label {
                bool        valid{false};
                glm::vec2   print_position{0.0f};
                glm::vec2   rect_min{0.0f};
                glm::vec2   rect_max{0.0f};
                std::string text;
            };
            const auto place = [&](const glm::vec2 print_position, std::string text) -> Placed_label {
                const erhe::ui::Rectangle bounds = text_renderer->measure(text);
                if (top_left) {
                    return Placed_label{
                        true,
                        print_position,
                        {print_position.x + bounds.min().x, print_position.y - bounds.max().y},
                        {print_position.x + bounds.max().x, print_position.y - bounds.min().y},
                        std::move(text)
                    };
                }
                return Placed_label{
                    true,
                    print_position,
                    {print_position.x + bounds.min().x, print_position.y + bounds.min().y},
                    {print_position.x + bounds.max().x, print_position.y + bounds.max().y},
                    std::move(text)
                };
            };
            const auto place_centered = [&](const vec3 position_in_world, std::string text) -> Placed_label {
                const std::optional<vec3> position_in_window = context.viewport_scene_view->project_to_viewport(position_in_world);
                if (!position_in_window.has_value()) {
                    return Placed_label{};
                }
                const float width = static_cast<float>(text_renderer->measure(text).size().x);
                return place({position_in_window.value().x - 0.5f * width, position_in_window.value().y}, std::move(text));
            };

            const vec3 delta = p1 - p0;
            const Placed_label placed_initial = place_centered(p0,                fmt::format("{:.3f}, {:.3f}, {:.3f}", p0.x, p0.y, p0.z));
            const Placed_label placed_delta   = place_centered(0.5f * (p0 + p1), fmt::format("{:.3f}, {:.3f}, {:.3f}", delta.x, delta.y, delta.z));

            // Current position below the hover text's mesh name, whose anchor
            // (+50 px x) and 16 px line step are mirrored from
            // Hover_tool::tool_render(); needs a valid hover entry.
            Placed_label placed_current;
            const Hover_entry* entry = context.scene_view.get_nearest_hover(
                context.scene_view.get_pickable_slot_mask(
                    Hover_entry::content_bit | Hover_entry::grid_bit | Hover_entry::rendertarget_bit
                )
            );
            if ((entry != nullptr) && entry->valid && entry->position.has_value() && (entry->slot != Hover_entry::rendertarget_slot)) {
                const std::optional<vec3> name_in_window = context.viewport_scene_view->project_to_viewport(entry->position.value());
                if (name_in_window.has_value()) {
                    placed_current = place(
                        {name_in_window.value().x + 50.0f, name_in_window.value().y + 16.0f},
                        fmt::format("{:.3f}, {:.3f}, {:.3f}", p1.x, p1.y, p1.z)
                    );
                }
            }

            const auto overlaps = [](const Placed_label& a, const Placed_label& b) -> bool {
                constexpr float padding = 2.0f;
                return
                    a.valid && b.valid &&
                    (a.rect_min.x - padding < b.rect_max.x) && (a.rect_max.x + padding > b.rect_min.x) &&
                    (a.rect_min.y - padding < b.rect_max.y) && (a.rect_max.y + padding > b.rect_min.y);
            };
            const bool delta_fits =
                placed_delta.valid &&
                !overlaps(placed_delta, placed_initial) &&
                !overlaps(placed_delta, placed_current);

            if (placed_initial.valid) {
                text_renderer->print(vec3{placed_initial.print_position, -0.5f}, 0xffffffffu, placed_initial.text);
            }
            if (delta_fits) {
                text_renderer->print(vec3{placed_delta.print_position, -0.5f}, 0xff00ffffu, placed_delta.text);
            }
            if (placed_current.valid) {
                text_renderer->print(vec3{placed_current.print_position, -0.5f}, 0xffffffffu, placed_current.text);
            }
            return;
        }
        case Handle_type::e_handle_type_scale_axis:
        case Handle_type::e_handle_type_scale_plane:
        case Handle_type::e_handle_type_scale_uniform:
        case Handle_type::e_handle_type_box_scale: {
            const vec3 s0 = shared.world_from_anchor_initial_state.get_scale();
            const vec3 s1 = shared.world_from_anchor.get_scale();
            initial_line = fmt::format("initial ({:.3f}, {:.3f}, {:.3f})", s0.x, s0.y, s0.z);
            current_line = fmt::format("current ({:.3f}, {:.3f}, {:.3f})", s1.x, s1.y, s1.z);
            break;
        }
        default: {
            return;
        }
    }

    const auto projection_transforms = context.camera->projection_transforms(
        context.viewport,
        context.scene_view.get_reverse_depth(),
        context.scene_view.get_depth_range(),
        context.scene_view.get_conventions()
    );
    const mat4 clip_from_world  = projection_transforms.clip_from_world.get_matrix();
    const vec3 anchor           = shared.world_from_anchor.get_translation();
    const vec3 anchor_in_window = context.viewport.project_to_screen_space(
        clip_from_world, anchor, 0.0f, 1.0f, context.scene_view.get_conventions()
    );

    // Text block goes just below the gizmo sphere: measure the gizmo radius in
    // pixels by projecting a point one radius toward the camera's right.
    float radius_px = 48.0f;
    const Handle_visualizations* visualizations = shared.get_visualizations();
    const auto*                  camera_node    = context.get_camera_node();
    if ((visualizations != nullptr) && (camera_node != nullptr)) {
        const float radius = visualizations->get_gizmo_radius();
        if (std::isfinite(radius) && (radius > 0.0f)) {
            const vec3 camera_right   = normalize(vec3{camera_node->world_from_node()[0]});
            const vec3 edge_in_window = context.viewport.project_to_screen_space(
                clip_from_world, anchor + radius * camera_right, 0.0f, 1.0f, context.scene_view.get_conventions()
            );
            radius_px = distance(vec2{anchor_in_window}, vec2{edge_in_window});
        }
    }

    const float    line_height = text_renderer->font_size() * 1.5f;
    const float    down        = (context.scene_view.get_framebuffer_origin() == erhe::math::Framebuffer_origin::top_left) ? 1.0f : -1.0f;
    const uint32_t text_color  = 0xffffffffu; // abgr
    float y = anchor_in_window.y + down * (radius_px + line_height);
    for (const std::string* line : {&initial_line, &current_line}) {
        const float width = static_cast<float>(text_renderer->measure(*line).size().x);
        text_renderer->print(vec3{anchor_in_window.x - 0.5f * width, y, -anchor_in_window.z}, text_color, *line);
        y += down * line_height;
    }
}

void Transform_tool::render_hover_preview(const Render_context& context)
{
    // Preview the hovered handle. Active drags draw their own feedback
    // (render_translate_drag_guides(), Rotate_tool::render()), so every
    // preview here is pre-drag only. Axis handles (translate arrows, rotate
    // ring axes) have no guide line: the infinite axis line was dropped.
    // Plane-translate handles have no preview either: the old plane grid was
    // dropped in favor of Handle_visualizations showing both direction
    // arrows of the plane's two axes while the sector is hovered.
    const Handle handle = (m_active_handle != Handle::e_handle_none) ? m_active_handle : m_hover_handle;
    const Handle_type type = get_handle_type(handle);
    const bool rotate = (type == Handle_type::e_handle_type_rotate) && (m_active_handle == Handle::e_handle_none);
    if (!rotate) {
        return;
    }

    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations == nullptr) {
        return;
    }

    // The preview frame matches the gizmo: anchor position always, anchor
    // orientation in every mode except Global (world axes).
    const vec3 center = shared.world_from_anchor.get_translation();
    const mat3 basis  = shared.settings.use_anchor_orientation()
        ? mat3_cast(shared.world_from_anchor.get_rotation())
        : mat3{1.0f};

    // World-space rotate-ring radius with the view-dependent handle scale
    // applied, so the preview keeps a constant size relative to the gizmo.
    const float radius = visualizations->get_gizmo_radius();
    if (!(radius > 0.0f) || !std::isfinite(radius)) {
        return;
    }

    const vec4 color = get_axis_color(get_axis_mask(handle), context.app_context.editor_settings->transform_tool);

    erhe::renderer::Primitive_renderer line_renderer = context.get(handle_line_config);

    if (rotate) {
        // Free (arcball) rotation has no rotation plane to preview.
        if (handle == Handle::e_handle_rotate_free) {
            return;
        }
        // Hover highlight: a filled low-alpha annulus hugging the hovered
        // ring - the ring circle widened a small amount inward and outward.
        // No depth test (x-ray fill) so the band reads at full strength even
        // where scene content covers the ring. The view-rotate ring's plane
        // is camera-aligned (perpendicular to the eye-to-anchor direction),
        // matching its drag.
        vec3  side1{0.0f};
        vec3  side2{0.0f};
        float ring_r = radius;
        if (handle == Handle::e_handle_rotate_view) {
            const auto* camera_node = context.get_camera_node();
            if (camera_node == nullptr) {
                return;
            }
            const vec3 view_dir = normalize(vec3{camera_node->position_in_world()} - center);
            const vec3 ref      = (std::abs(view_dir.y) < 0.9f) ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
            side1  = normalize(cross(view_dir, ref));
            side2  = normalize(cross(view_dir, side1));
            ring_r = visualizations->get_view_ring_radius();
        } else {
            const int axis_index = static_cast<int>(get_handle_axis(handle)) - 1;
            side1 = basis[(axis_index + 1) % 3];
            side2 = basis[(axis_index + 2) % 3];
        }

        const float half_width = 0.08f * ring_r;
        const float r_inner    = ring_r - half_width;
        const float r_outer    = ring_r + half_width;
        constexpr int sector_count = 64;
        std::vector<vec3>     band_positions;
        std::vector<uint32_t> band_indices;
        band_positions.reserve(2 * sector_count);
        band_indices.reserve(6 * sector_count);
        for (int i = 0; i < sector_count; ++i) {
            const float theta     = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(sector_count);
            const vec3  direction = std::cos(theta) * side1 + std::sin(theta) * side2;
            band_positions.push_back(center + r_inner * direction);
            band_positions.push_back(center + r_outer * direction);
        }
        for (int i = 0; i < sector_count; ++i) {
            const uint32_t inner_0 = static_cast<uint32_t>(2 * i);
            const uint32_t outer_0 = inner_0 + 1;
            const uint32_t inner_1 = static_cast<uint32_t>(2 * ((i + 1) % sector_count));
            const uint32_t outer_1 = inner_1 + 1;
            band_indices.push_back(inner_0);
            band_indices.push_back(outer_0);
            band_indices.push_back(outer_1);
            band_indices.push_back(inner_0);
            band_indices.push_back(outer_1);
            band_indices.push_back(inner_1);
        }
        erhe::renderer::Primitive_renderer triangle_renderer = context.get(
            erhe::renderer::Debug_renderer_config{
                .primitive_type    = erhe::graphics::Primitive_type::triangle,
                .stencil_reference = 2,
                .draw_visible      = true,
                .draw_hidden       = true,
                .xray              = true
            }
        );
        triangle_renderer.add_triangles(mat4{1.0f}, vec4{vec3{color}, 0.2f}, band_positions, band_indices);
    }
}

#pragma endregion Render

void Transform_tool::update_for_view(Scene_view* scene_view)
{
    // Keep the gizmo anchor tracking the live mesh component selection (or a
    // designated lattice node's selected control point) while idle. Skipped
    // during an active gizmo drag or numeric component edit so the captured
    // initial anchor is not stomped mid-edit.
    if (!is_transform_tool_active() && !m_component_transform.is_active() && !m_lattice_point_transform.is_active()) {
        Mesh_component_selection* mesh_component_selection = m_context.mesh_component_selection;
        // Bone mode is not a mesh component mode: a selected bone is an
        // ordinary joint Node, so the gizmo must fall through to the node
        // selection below rather than look for mesh components.
        const bool want_component =
            (mesh_component_selection != nullptr) &&
            is_mesh_component_mode(mesh_component_selection->get_mode());
        if (want_component) {
            m_component_source = m_component_transform.update_anchor(m_context, shared)
                ? Component_source::mesh_components
                : Component_source::none;
        } else if (m_lattice_point_transform.update_anchor(m_context, shared)) {
            // A display/ghost designated Lattice_node is bound into the active
            // scene: its selected control point owns the gizmo (viewport lattice
            // editing). Clearing the designation returns the gizmo to the node
            // selection below.
            m_component_source = Component_source::lattice_point;
        } else if (shared.component_mode) {
            // Left component / lattice mode: restore the node gizmo.
            m_component_source = Component_source::none;
            shared.component_mode = false;
            update_target_nodes(nullptr);
        } else {
            m_component_source = Component_source::none;
        }
    }

    // Refresh the visualizations' scene view FIRST: update_transforms()
    // dereferences the visualizations' cached scene view. Refreshing
    // afterwards (as this used to) made that a stale pointer from the
    // previous message -- a use-after-free when the previous viewport had
    // been destroyed (scene closed). Per-view gizmo scoping needs no state
    // here: rendering and picking are both gated on views of the active
    // scene at their call sites.
    Handle_visualizations* visualizations = shared.get_visualizations();
    if (visualizations != nullptr) {
        visualizations->update_for_view(scene_view);
    }
    update_transforms();
}

auto Transform_tool::is_scene_view_of_active_scene(Scene_view* scene_view) const -> bool
{
    if (scene_view == nullptr) {
        return false;
    }
    const std::shared_ptr<Scene_root> view_scene_root   = scene_view->get_scene_root();
    const std::shared_ptr<Scene_root> active_scene_root = m_context.selection->get_active_scene_root();
    return view_scene_root && (view_scene_root == active_scene_root);
}

void Transform_tool::update_visibility()
{
    // Handle visibility is evaluated directly at render / pick time
    // (Handle_visualizations::is_handle_shown); only the transforms and the
    // selection box need refreshing here.
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

void Transform_tool::apply_component_transform(const glm::mat4& updated_world_from_anchor)
{
    // Note: the component path does not use shared.touched (which gates the node
    // record path); commit is driven by the producer's is_active().
    if (m_component_source == Component_source::lattice_point) {
        m_lattice_point_transform.apply(m_context, shared, updated_world_from_anchor);
    } else {
        m_component_transform.apply(m_context, shared, updated_world_from_anchor);
    }
    shared.world_from_anchor.set(updated_world_from_anchor);
    update_transforms();
}

void Transform_tool::begin_component_edit()
{
    if (m_component_source == Component_source::lattice_point) {
        m_lattice_point_transform.begin(m_context);
    } else {
        m_component_transform.begin(m_context);
    }
}

void Transform_tool::commit_component_edit()
{
    if (m_lattice_point_transform.is_active()) {
        m_lattice_point_transform.commit(m_context);
    }
    if (m_component_transform.is_active()) {
        m_component_transform.commit(m_context);
    }
}

auto Transform_tool::is_component_edit_active() const -> bool
{
    return m_component_transform.is_active() || m_lattice_point_transform.is_active();
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

    // Autokey (LightWave-style): when enabled and an animation is targeted,
    // key the edited nodes at the current play position. The keying operation
    // joins the same compound, so a single undo reverts both the transform
    // and the keys it created.
    Animation_player* player = m_context.animation_player;
    if ((player != nullptr) && (player->get_autokey_mode() != Autokey_mode::off)) {
        const std::shared_ptr<erhe::scene::Animation>& animation = player->get_animation();
        if (animation) {
            constexpr float epsilon = 1.0e-6f;
            const bool key_all = player->get_autokey_mode() == Autokey_mode::all_paths;
            std::vector<Keying_request> requests;
            requests.reserve(shared.entries.size());
            for (auto& entry : shared.entries) {
                const erhe::scene::Trs_transform& before = entry.parent_from_node_before;
                const erhe::scene::Trs_transform& after  = entry.node->parent_from_node_transform();
                const bool translation_changed = glm::any(glm::greaterThan(glm::abs(after.get_translation() - before.get_translation()), glm::vec3{epsilon}));
                const bool scale_changed       = glm::any(glm::greaterThan(glm::abs(after.get_scale()       - before.get_scale()),       glm::vec3{epsilon}));
                const glm::quat rotation_delta = after.get_rotation() - before.get_rotation();
                const bool rotation_changed =
                    (std::abs(rotation_delta.x) > epsilon) ||
                    (std::abs(rotation_delta.y) > epsilon) ||
                    (std::abs(rotation_delta.z) > epsilon) ||
                    (std::abs(rotation_delta.w) > epsilon);
                requests.push_back(
                    Keying_request{
                        .node            = entry.node,
                        .key_translation = key_all || translation_changed,
                        .key_rotation    = key_all || rotation_changed,
                        .key_scale       = key_all || scale_changed
                    }
                );
            }
            std::shared_ptr<Operation> keying_operation = key_nodes(animation, requests, player->get_time());
            if (keying_operation) {
                compompound_parameters.operations.push_back(std::move(keying_operation));
                player->on_animation_edited(animation);
            }
        }
    }

    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            std::move(compompound_parameters)
        )
    );
}

void Transform_tool::create_node_from_anchor()
{
    // Resolve the scene root for the new node (handles selection / viewport /
    // single-scene fallbacks). The new node is parented to the scene root, so its
    // parent space equals world space and the gizmo anchor frame can be baked in
    // directly.
    Scene_root* scene_root = m_context.scene_commands->get_scene_root(static_cast<erhe::scene::Node*>(nullptr));
    if (scene_root == nullptr) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> root_node = scene_root->get_hosted_scene()->get_root_node();
    if (!root_node) {
        return;
    }

    auto new_node = std::make_shared<erhe::scene::Node>("frame node");
    new_node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
    new_node->set_parent_from_node(shared.world_from_anchor);

    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
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
    const bool component_mode = shared.component_mode;
    if (component_mode) {
        // Mesh component editing: there is no node; all edits are world-space deltas
        // around the selection-centroid anchor.
        m_multiselect        = false;
        m_first_node         = nullptr;
        m_world_from_parent  = glm::mat4{1.0f};
        m_use_world_mode     = true;
        m_transform          = &shared.world_from_anchor;
        m_rotation_transform = &shared.world_from_anchor;
    } else {
        m_multiselect       = shared.entries.size() > 1;
        m_first_node        = shared.entries.front().node;
        m_world_from_parent = m_first_node->world_from_parent();
        m_use_world_mode    = !shared.settings.is_local() || m_multiselect;

        m_transform =
            m_use_world_mode
                ? &shared.world_from_anchor
                : &shared.entries.front().node->parent_from_node_transform();

        m_rotation_transform =
            (m_use_world_mode || m_multiselect)
                ? &shared.world_from_anchor
                : &shared.entries.front().node->parent_from_node_transform();
    }

    m_scale       = m_transform->get_scale      ();
    m_rotation    = m_rotation_transform->get_rotation();
    m_translation = m_transform->get_translation();
    m_skew        = m_transform->get_skew       ();

    const glm::mat4 m           = m_transform->get_matrix();
    const float     determinant = glm::determinant(m);
    if (determinant < 0.0f) {
        ImGui::Text("Negative determinant (%.9f)", determinant);
    }

    bool euler_matches_gizmo = true;
    if (!component_mode) {
        const std::shared_ptr<erhe::scene::Node> first_parent = m_first_node->get_parent_node();
        euler_matches_gizmo = !m_multiselect &&
            (
                !m_use_world_mode ||
                !first_parent ||
                first_parent == m_first_node->get_scene()->get_root_node()
            );
    }

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
        transform_tool.apply_translation_edit(m_translation, !m_use_world_mode);
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
        transform_tool.apply_rotation_edit(rotation_inspector.get_quaternion(), !m_use_world_mode);
    }

    if (m_scale_state.value_changed) {
        transform_tool.apply_scale_edit(m_scale, !m_use_world_mode);
    }

    if (m_skew_state.value_changed) {
        transform_tool.apply_skew_edit(m_skew, !m_use_world_mode);
    }

    Value_edit_state edit_state;
    edit_state.combine(m_translate_state);
    edit_state.combine(rotate_state);
    edit_state.combine(m_scale_state);
    edit_state.combine(m_skew_state);

    if (edit_state.edit_ended) {
        if (component_mode) {
            transform_tool.commit_component_edit();
        } else if (shared.touched) {
            transform_tool.record_transform_operation();
        }
    }
}

void Transform_tool::transform_properties()
{
    if (shared.entries.empty() && !shared.component_mode) {
        return;
    }

    m_edit_state = Edit_state(shared, *this, m_rotation, m_property_editor);
}

}

