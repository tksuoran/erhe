#include "brushes/brush_tool.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_item_bits.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "assets/asset_manager.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "editor_log.hpp"
#include "level/kit_piece.hpp"
#include "level/level_kits.hpp"
#include "operations/compound_operation.hpp"
#include "transform/transform_tool.hpp"
#include "graphics/icon_set.hpp"
#include "grid/grid.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"
#include "windows/item_tree_window.hpp"
#include "time.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "config/generated/scene_config.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/xr_action.hpp"
#   include "erhe_xr/headset.hpp"
#endif

#include <imgui/imgui.h>

#include <geogram/basic/matrix.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using erhe::geometry::to_glm_mat4;
using erhe::geometry::to_geo_vec3f;
using erhe::geometry::to_geo_mat4f;

namespace editor {

#pragma region Commands
Brush_preview_command::Brush_preview_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.motion_preview"}
    , m_context{context}
{
}

auto Brush_preview_command::try_call() -> bool
{
    if ((get_command_state() != erhe::commands::State::Active) || !m_context.brush_tool->is_enabled()) {
        return false;
    }
    m_context.brush_tool->on_motion();
    return true;
}

Brush_insert_command::Brush_insert_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.insert"}
    , m_context{context}
{
}

void Brush_insert_command::try_ready()
{
    if (m_context.brush_tool->try_insert_ready()) {
        set_ready();
    }
}

auto Brush_insert_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    const bool consumed = m_context.brush_tool->try_insert();
    set_inactive();
    return consumed;
}

Brush_pick_command::Brush_pick_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Brush_tool.pick"}
    , m_context{context}
{
}

auto Brush_pick_command::try_call() -> bool
{
    return m_context.brush_tool->try_pick();
}

Brush_rotate_command::Brush_rotate_command(erhe::commands::Commands& commands, App_context& context, int direction)
    : Command    {commands, (direction > 0) ? "Brush_tool.rotate_cw" : "Brush_tool.rotate_ccw"}
    , m_context  {context}
    , m_direction{direction}
{
}

auto Brush_rotate_command::try_call() -> bool
{
    const bool consumed = m_context.brush_tool->try_rotate(m_direction);
    set_inactive();
    return consumed;
}

#pragma endregion Commands

Brush_tool::Brush_tool(
    const Scene_config&       scene_config,
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Headset_view&             headset_view,
    Icon_set&                 icon_set,
    Tools&                    tools
)
    : Tool                            {context, tools, Tool_flags::toolbox}
    , m_preview_command               {commands, context}
    , m_insert_command                {commands, context}
    , m_pick_command                  {commands, context}
    , m_rotate_cw_command             {commands, context, 1}
    , m_rotate_ccw_command            {commands, context, -1}
    , m_toggle_brush_preview_command  {
        commands,
        "Brush.toggle_preview",
        [this]() -> bool {
            m_show_preview = !m_show_preview;
            update_preview_mesh();
            return true;
        }
    }
    , m_pick_using_float_input_command{commands, m_pick_command, 0.6f, 0.4f}
{
    ERHE_PROFILE_FUNCTION();

    set_base_priority(Brush_tool::c_priority);
    set_description  ("Brush Tool");
    set_icon         (icon_set.custom_icons, icon_set.icons.brush_big);

    commands.register_command(&m_preview_command);
    commands.register_command(&m_insert_command);
    commands.register_command(&m_pick_command);
    commands.register_command(&m_pick_using_float_input_command);
    commands.register_command(&m_rotate_cw_command);
    commands.register_command(&m_rotate_ccw_command);
    commands.register_command(&m_toggle_brush_preview_command);
    commands.bind_command_to_update      (&m_preview_command);
    // Plain right press only: a right press with a modifier belongs to the camera (Alt + right drag tumbles).
    commands.bind_command_to_mouse_button(&m_insert_command, erhe::window::Mouse_button_right, erhe::commands::Button_trigger::Button_pressed, 0);
    //commands.bind_command_to_mouse_button(&m_rotate_cw_command,  erhe::window::Mouse_button_x1,    true);
    //commands.bind_command_to_mouse_button(&m_rotate_ccw_command, erhe::window::Mouse_button_x2,    true);
    commands.bind_command_to_key(&m_rotate_cw_command,            erhe::window::Key_z);
    commands.bind_command_to_key(&m_rotate_ccw_command,           erhe::window::Key_x);
    commands.bind_command_to_key(&m_toggle_brush_preview_command, erhe::window::Key_c);

#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*    headset  = headset_view.get_headset();
    erhe::xr::Xr_actions* xr_right = (headset != nullptr) ? headset->get_actions_right() : nullptr;
    if (xr_right != nullptr) {
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right->trigger_click, erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_boolean_action(&m_insert_command, xr_right->a_click,       erhe::commands::Button_trigger::Button_pressed);
        commands.bind_command_to_xr_float_action  (&m_pick_using_float_input_command, xr_right->squeeze_value);
        //commands.bind_command_to_xr_boolean_action(&m_pick_command,   xr_right->b_click,       erhe::commands::Button_trigger::Button_pressed);
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
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message& message) {
            on_selection(message);
        }
    );
    m_node_touched_subscription = app_message_bus.node_touched.subscribe(
        [this](Node_touched_message& message) {
            on_kit_join_target_changed(message.node);
        }
    );
    m_kit_piece_connectors_changed_subscription = app_message_bus.kit_piece_connectors_changed.subscribe(
        [this](Kit_piece_connectors_changed_message&) {
            on_kit_join_target_changed(nullptr);
        }
    );
    // A floor target's outline edit (its edge connectors move with the curve's revision) keeps the join on the floor.
    m_curve_changed_subscription = app_message_bus.curve_changed.subscribe(
        [this](Curve_changed_message& message) {
            if (message.curve) {
                on_kit_join_target_changed(message.curve.get());
            }
        }
    );
    // A floor target's values (thickness, wall thickness) move its edge connectors: read its frames again.
    m_kit_layout_changed_subscription = app_message_bus.kit_layout_changed.subscribe(
        [this](Kit_layout_changed_message& message) {
            if (m_kit_join.has_value() && (m_kit_join.value().target_id == message.node_id) && (m_context.level_kits != nullptr)) {
                m_context.level_kits->get_connector_indicators().forget_node(message.node_id);
                on_kit_join_target_changed(nullptr);
            }
        }
    );

    // K9.2: a parameter change of the join's target changes what it supplies: the join is read again at once.
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message& message) {
            if (m_kit_join.has_value() && message.mesh && (m_kit_join.value().target_id == static_cast<std::uint64_t>(message.mesh->get_id()))) {
                on_kit_join_target_changed(nullptr);
            }
        }
    );

    // K9.2: a material change of the join's target changes the wall.material it supplies.
    m_mesh_materials_changed_subscription = app_message_bus.mesh_materials_changed.subscribe(
        [this](Mesh_materials_changed_message& message) {
            if (m_kit_join.has_value() && message.mesh && (m_kit_join.value().target_id == static_cast<std::uint64_t>(message.mesh->get_id()))) {
                on_kit_join_target_changed(nullptr);
            }
        }
    );

    m_grid_scale = scene_config.object_scale;

    m_active_brush.set_user_label("brush tool active brush");

    m_preview_command               .set_host(this);
    m_insert_command                .set_host(this);
    m_pick_command                  .set_host(this);
    m_rotate_cw_command             .set_host(this);
    m_rotate_ccw_command            .set_host(this);
    m_pick_using_float_input_command.set_host(this);
    m_toggle_brush_preview_command  .set_host(this);
}

void Brush_tool::set_active_brush(const std::shared_ptr<Brush>& brush)
{
    leave_kit_mode();
    m_active_brush.adopt(*m_context.asset_manager, brush);
}

void Brush_tool::set_kit_brush(const Kit_parameter_values& values, const std::shared_ptr<Brush>& brush, const std::span<const std::string> slot_materials)
{
    const Kit_shape_definition* const shape = values.get_shape();
    if (!brush || (shape == nullptr)) {
        return;
    }
    if (get_kit_shape() != shape) {
        m_kit_yaw_steps = 0;
    }
    m_kit_values = values;
    m_kit_brush  = brush;
    m_kit_join_flow.valid = false;
    get_shape_connectors(m_kit_values, m_kit_connectors);
    Level_kits& level_kits = *m_context.level_kits;
    level_kits.get_default_slot_materials(*shape, m_kit_slot_materials);
    m_kit_preview_materials.clear();
    for (std::size_t slot = 0, end = m_kit_slot_materials.size(); slot < end; ++slot) {
        if ((slot < slot_materials.size()) && !slot_materials[slot].empty()) {
            m_kit_slot_materials[slot] = slot_materials[slot];
        }
        m_kit_preview_materials.push_back(level_kits.get_preview_material(m_kit_slot_materials[slot]));
    }
    // The preview takes the new brush's primitive and material when it is made again.
    remove_preview_mesh();
    on_motion();
}

void Brush_tool::leave_kit_mode()
{
    if (!m_kit_brush) {
        return;
    }
    m_kit_brush.reset();
    m_kit_slot_materials.clear();
    m_kit_preview_materials.clear();
    m_kit_connectors.clear();
    m_kit_pose.reset();
    m_kit_join_flow = Kit_join_flow{};
    remove_preview_mesh();
}

auto Brush_tool::is_kit_mode() const -> bool
{
    return static_cast<bool>(m_kit_brush);
}

auto Brush_tool::get_kit_shape() const -> const Kit_shape_definition*
{
    return m_kit_brush ? m_kit_values.get_shape() : nullptr;
}

auto Brush_tool::get_kit_values() const -> const Kit_parameter_values&
{
    return m_kit_values;
}

auto Brush_tool::get_kit_brush() const -> const std::shared_ptr<Brush>&
{
    return m_kit_brush;
}

auto Brush_tool::get_kit_slot_materials() const -> const std::vector<std::string>&
{
    return m_kit_slot_materials;
}

auto Brush_tool::get_kit_yaw_steps() const -> int
{
    return m_kit_yaw_steps;
}

auto Brush_tool::get_kit_pose() const -> std::optional<Kit_placement_pose>
{
    return (m_kit_brush && m_preview_node) ? m_kit_pose : std::optional<Kit_placement_pose>{};
}

auto Brush_tool::get_kit_join() const -> std::optional<Kit_brush_join>
{
    return (m_kit_brush && m_preview_node) ? m_kit_join : std::optional<Kit_brush_join>{};
}

auto Brush_tool::is_kit_join_suspended() const -> bool
{
    return m_kit_join_suspended;
}

void Brush_tool::on_connector_snap_suspend_key_edge()
{
    // A key repeat or a key outside kit mode changes nothing.
    if (!m_kit_brush || !m_preview_node || (m_context.transform_tool == nullptr)) {
        return;
    }
    if (m_context.transform_tool->is_connector_snap_suspend_held() != m_kit_join_suspended) {
        update_preview_mesh();
    }
}

auto Brush_tool::get_hover_world_position() const -> std::optional<glm::vec3>
{
    return m_hover_world_position;
}

auto Brush_tool::get_hover_world_normal() const -> std::optional<glm::vec3>
{
    return m_hover_world_normal;
}

void Brush_tool::on_selection(const Selection_message& message)
{
    if (!m_kit_brush) {
        return;
    }
    for (const std::shared_ptr<erhe::Item_base>& item : message.selection_change.newly_selected) {
        if (std::dynamic_pointer_cast<Brush>(item)) {
            leave_kit_mode();
            return;
        }
    }
}

void Brush_tool::clear_active_brush()
{
    m_active_brush.set_key(Asset_key{});
}

auto Brush_tool::get_preview_world_position() const -> std::optional<glm::vec3>
{
    if (!m_preview_node || !m_preview_mesh || (m_preview_node->get_parent_node() == nullptr)) {
        return {};
    }
    return glm::vec3{m_preview_node->position_in_world()};
}

auto Brush_tool::get_effective_brush() const -> std::shared_ptr<Brush>
{
    if (m_drag_and_drop_brush) {
        return m_drag_and_drop_brush;
    }
    if (m_kit_brush) {
        return m_kit_brush;
    }
    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    if (active_brush) {
        return active_brush;
    }
    return m_context.selection->get_last_selected<Brush>();
}

auto Brush_tool::get_active_brush() const -> std::shared_ptr<Brush>
{
    return m_active_brush.get_as<Brush>();
}

auto Brush_tool::get_drag_and_drop_brush() const -> const std::shared_ptr<Brush>&
{
    return m_drag_and_drop_brush;
}

void Brush_tool::on_items_removed(const Removed_items& removed)
{
    if (m_kit_join.has_value()) {
        const std::shared_ptr<erhe::scene::Node> target = m_kit_join.value().target.lock();
        if (!target || removed.lookup.contains(target.get())) {
            on_kit_join_target_changed(nullptr);
        }
    }
    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    if (active_brush && removed.lookup.contains(active_brush.get())) {
        clear_active_brush();
    }
    // Only ever set while an ImGui drag is in flight - Viewport_window cancels
    // it on any frame without a drag payload - so this can only fire when the
    // removal lands mid-drag. Kept for that case; it is not a cross-frame pin,
    // which is why the smoke test does not cover it.
    if (m_drag_and_drop_brush && removed.lookup.contains(m_drag_and_drop_brush.get())) {
        m_drag_and_drop_brush.reset();
    }
    if (m_preview_node && removed.lookup.contains(m_preview_node.get())) {
        remove_preview_mesh();
    }
}

void Brush_tool::on_close_scene(erhe::Item_host* const closing_host)
{
    // R5.6: brushes are not hosted; ask the manager whether the closing
    // scene's container record defines them (transient tool state drops the
    // asset when its defining scene closes - the R3 decision stands). The
    // preview node is scene content and keeps the host comparison.
    Asset_manager* const asset_manager = m_context.asset_manager;
    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    if (active_brush && (asset_manager != nullptr) && asset_manager->is_hosted_or_defined_by(*active_brush, closing_host)) {
        clear_active_brush();
    }
    if (m_drag_and_drop_brush && (asset_manager != nullptr) && asset_manager->is_hosted_or_defined_by(*m_drag_and_drop_brush, closing_host)) {
        m_drag_and_drop_brush.reset();
    }
    if (m_preview_node && (m_preview_node->get_item_host() == closing_host)) {
        remove_preview_mesh();
    }
}

void Brush_tool::on_hover_scene_view(Hover_scene_view_message& message)
{
    Scene_view* const old_scene_view = get_hover_scene_view();
    Tool::on_message(message);
    if (message.scene_view != old_scene_view) {
        bool visible = message.scene_view && (get_hover_scene_view() == message.scene_view);
        if (!visible) {
            remove_preview_mesh();
        }
    }
}

void Brush_tool::on_hover_mesh(Hover_mesh_message&)
{
    on_motion();
}

void Brush_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (new_priority < old_priority) {
        disable_command_host();
        m_preview_command.set_inactive();
        m_hover = Hover_entry{};
        m_hover_world_position.reset();
        m_hover_world_normal.reset();
        leave_kit_mode();
        remove_preview_mesh();
        clear_active_brush();
    }
    if (new_priority > old_priority) {
        enable_command_host();
        m_preview_command.set_active();
        on_motion();
    }
}

void Brush_tool::remove_preview_mesh()
{
    // Release the mesh prim *before* removing the node
    if (m_preview_mesh) {
        m_preview_mesh.reset();
    }
    if (m_preview_node) {
        // Do not remove this. It is not possible to rely on reset calling destructor,
        // because parent will have shared_ptr to the child.
        m_preview_node->set_parent({});
        m_preview_node.reset();
    }

    m_hover_frame.reset();
    m_kit_preview_shown.reset();
    clear_kit_join();
}

auto Brush_tool::try_rotate(int direction) -> bool
{
    if (m_kit_brush) {
        // Kit mode turns the next piece about its up axis (the surface normal with orient); seen from above, rotate_cw turns clockwise.
        // A connector join takes the steps as quarter twists about the target connector's outward axis.
        const Kit_placement_settings settings  = m_context.level_kits->get_placement_settings();
        const int                    per_turn  = std::max(1, static_cast<int>(std::lround(360.0f / std::max(1.0f, settings.rotation_step_degrees))));
        m_kit_yaw_steps = (((m_kit_yaw_steps - direction) % per_turn) + per_turn) % per_turn;
        update_preview_mesh();
        return true;
    }
    //remove_preview_mesh();
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (!hover_scene_mesh) {
        return false;
    }

    erhe::scene::Node* node = hover_scene_mesh.get();
    if (node == nullptr) {
        return false;
    }
    std::shared_ptr<erhe::Item_base>   node_item_base = node->shared_from_this();
    std::shared_ptr<erhe::scene::Node> node_shared    = std::dynamic_pointer_cast<erhe::scene::Node>(node_item_base);

    const std::optional<Brush_placement_data> brush_placement = read_brush_placement(*node);
    if (!brush_placement.has_value()) {
        return false;
    }

    std::shared_ptr<Brush> brush = brush_placement.value().brush;
    if (!brush) {
        return false;
    }

    if (!m_hover.geometry) {
        return false;
    }

    m_context.time->finish_all_transform_animations(*m_context.app_message_bus);

    erhe::geometry::Geometry& geometry = *m_hover.geometry.get();
    const GEO::Mesh& geo_mesh           = geometry.get_mesh();
    GEO::index_t     facet              = brush_placement.value().facet;
    if (facet == GEO::NO_INDEX) {
        return false; // TODO node/mesh was created by Scene_builder
    }
    ERHE_VERIFY(facet < geo_mesh.facets.nb());
    GEO::index_t     facet_corner_count = geo_mesh.facets.nb_corners(facet);
    GEO::index_t     old_corner         = brush_placement.value().corner;
    GEO::index_t     new_corner         = (old_corner + facet_corner_count + direction) % facet_corner_count;

    Reference_frame initial_frame{geo_mesh, facet, 0, old_corner, Frame_orientation::in};
    Reference_frame updated_frame{geo_mesh, facet, 0, new_corner, Frame_orientation::in};

    GEO::mat4f initial_brush_transform_ = initial_frame.transform(0.0f);
    GEO::mat4f updated_brush_transform_ = updated_frame.transform(0.0f);
    glm::mat4  initial_brush_transform  = to_glm_mat4(initial_brush_transform_);
    glm::mat4  updated_brush_transform  = to_glm_mat4(updated_brush_transform_);
    glm::mat4  brush_update_transform   = updated_brush_transform * glm::inverse(initial_brush_transform);
    glm::mat4  initial_node_transform   = node->parent_from_node();
    glm::mat4  updated_node_transform   = initial_node_transform * brush_update_transform;

    // Step 41: in a scene with attachments the turn is immediate, so the follow operation joins the followers to its end pose.
    Scene_root* const scene_root     = dynamic_cast<Scene_root*>(node_shared->get_item_host());
    const bool        follows        = (scene_root != nullptr) && (m_context.level_kits != nullptr) && Level_kits::has_attachments(*scene_root);
    auto              node_operation = std::make_shared<Node_transform_operation>(
        Node_transform_operation::Parameters{
            .node                    = node_shared,
            .parent_from_node_before = node_shared->parent_from_node_transform(),
            .parent_from_node_after  = erhe::scene::Trs_transform{updated_node_transform},
            .xform_op_stack_before   = node_shared->copy_xform_op_stack(),
            .time_duration           = follows ? 0.0f : 0.25f
        }
    );
    if (follows) {
        Compound_operation::Parameters compound{};
        compound.operations.push_back(node_operation);
        m_context.level_kits->append_attachment_follow_ups(std::span<const std::shared_ptr<erhe::scene::Node>>{&node_shared, 1}, compound.operations);
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(compound)));
    } else {
        m_context.operation_stack->queue(node_operation);
    }

    set_brush_placement_corner(*node, new_corner);
    return true;
}

auto Brush_tool::try_insert_ready() -> bool
{
    return is_enabled() &&
        (
            !m_hover.scene_mesh_weak.expired() ||
            !m_hover.grid_weak.expired()
        );
}

auto Brush_tool::get_hover_brush() const -> std::shared_ptr<Brush>
{
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (!hover_scene_mesh) {
        return {};
    }

    erhe::scene::Node* node = hover_scene_mesh.get();
    if (node == nullptr) {
        return {};
    }

    const std::optional<Brush_placement_data> brush_placement = read_brush_placement(*node);
    if (!brush_placement.has_value()) {
        return {};
    }

    return brush_placement.value().brush;
}

auto Brush_tool::try_pick() -> bool
{
    std::shared_ptr<Brush> brush = get_hover_brush();
    if (!brush) {
        return false;
    }
    m_context.selection->set_selection({brush});
    return true;
}

auto Brush_tool::try_insert(Brush* brush) -> bool
{
    if ((brush == nullptr) && m_kit_brush) {
        return try_insert_kit_piece();
    }
    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    if (brush == nullptr) {
        if (active_brush) {
            brush = active_brush.get();
        } else {
            brush = m_context.selection->get_last_selected<Brush>().get();
        }
        if (brush == nullptr) {
            return false;
        }
    }

    //update_preview_mesh_node_transform();

    if (!m_hover.position.has_value() || !brush) {
        return false;
    }

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    do_insert_operation(*brush);

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    remove_preview_mesh();

#if !defined(NDEBUG)
        m_context.app_scenes->sanity_check();
#endif

    return true;
}

auto Brush_tool::try_insert_kit_piece() -> bool
{
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root || !m_preview_node || !m_kit_pose.has_value()) {
        return false;
    }
    // The pose is final: the preview already applied the module snap and the surface options.
    Kit_placement placement;
    placement.shape    = m_kit_values.get_shape();
    placement.values   = m_kit_values;
    placement.position = m_kit_pose.value().position;
    placement.rotation = m_kit_pose.value().rotation;
    // S2.1: the snap runs again from the rested pose (it stays bit for bit) so the insert finds its support itself.
    if (m_kit_pose.value().surface_found && !m_kit_join.has_value()) {
        placement.surface           = m_context.level_kits->get_placement_settings().surface;
        placement.surface.direction = m_kit_pose.value().surface_direction;
    }
    if (m_kit_join.has_value()) {
        // Step 40: the same join level_place_pieces connect makes; a target removed since the hover is refused by place().
        const Kit_brush_join& join = m_kit_join.value();
        placement.connect = Kit_connect{
            .connector        = join.connector,
            .target           = join.target.lock(),
            .target_connector = join.target_connector,
            .twist_steps      = join.twist_steps,
            .gap              = 0.0f
        };
    }
    for (const std::string& name : m_kit_slot_materials) {
        placement.materials.push_back(Kit_slot_material{.material = {}, .palette_name = name});
    }
    Level_kits& level_kits = *m_context.level_kits;
    // R5.12.9 Place As Layout: a kit layout node of the initial values, element 0 at the preview's pose, selected.
    if (level_kits.get_layout_initial_values().place_as == Kit_place_as::layout) {
        Kit_layout_item item;
        item.shape      = placement.shape->id;
        item.parameters = m_kit_values.to_canonical_text();
        item.materials.assign(m_kit_slot_materials.begin(), m_kit_slot_materials.end());
        // A joined pose has no surface: the layout searches down, the default (step 40).
        const glm::vec3 surface_direction = m_kit_join.has_value() ? c_default_surface_direction : m_kit_pose.value().surface_direction;
        const Kit_layout_result layout = level_kits.add_layout_initial(
            *scene_root, item, placement.position, placement.rotation, surface_direction
        );
        if (!layout.error.empty()) {
            log_scene->warn("Brush tool: kit layout placement failed: {}", layout.error);
            return false;
        }
        m_context.selection->set_selection({layout.node}, layout.node);
    } else {
        const Kit_place_result result = level_kits.place(*scene_root, std::span<const Kit_placement>{&placement, 1}, Kit_snap::none);
        if (!result.error.empty()) {
            log_scene->warn("Brush tool: kit piece placement failed: {}", result.error);
            return false;
        }
    }
    if (level_kits.get_placement_settings().seed_per_piece == Kit_seed_per_piece::next) {
        take_next_kit_seed(*scene_root);
    }
    return true;
}

void Brush_tool::take_next_kit_seed(const Scene_root& scene_root)
{
    // The next piece derives its masked values from the next seed at the scene's lengths (D4.5).
    Level_kits&          level_kits  = *m_context.level_kits;
    Kit_parameter_values next_values = m_kit_values;
    static_cast<void>(next_values.set_int("seed", kit_next_seed(next_values.get_seed())));
    const Kit_lengths lengths = level_kits.get_lengths(scene_root);
    next_values.resolve_derived(lengths.module, lengths.wall_height);
    const std::shared_ptr<Brush> brush = level_kits.get_brush(next_values);
    if (!brush) {
        log_scene->warn("Brush tool: the kit shape made no geometry for {}", next_values.to_canonical_text());
        return;
    }
    // A copy: set_kit_brush() rewrites the member it would read.
    const std::vector<std::string> slot_materials = m_kit_slot_materials;
    set_kit_brush(next_values, brush, slot_materials);
    m_context.app_message_bus->kit_values_changed.send_message(Kit_values_changed_message{});
}

void Brush_tool::on_motion()
{
    auto* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return;
    }

    const Hover_entry* nearest_hover = scene_view->get_nearest_hover(
        Hover_entry::content_bit |
        Hover_entry::grid_bit    |
        Hover_entry::rendertarget_bit
    );
    if ((nearest_hover == nullptr) || (nearest_hover->slot == Hover_entry::rendertarget_slot)) {
        // Nothing to place on: the ray is on a rendertarget quad (HUD /
        // Hotbar) or points at nothing (sky). Treat like leaving the scene
        // viewport instead of freezing the last preview. Clearing m_hover
        // also takes try_insert_ready() (and pick / rotate) out of play, so
        // the insert command cannot consume the XR button events the quad's
        // ImGui host needs for its click handling.
        m_hover = Hover_entry{};
        m_hover_world_position.reset();
        m_hover_world_normal.reset();
        remove_preview_mesh();
        return;
    }
    m_hover = *nearest_hover;
    // F1.3, G8: the Brush tool places on the surface behind a hovered foliage mesh; the hover reported stays the pointer's.
    const std::shared_ptr<erhe::scene::Mesh> nearest_mesh = nearest_hover->scene_mesh_weak.lock();
    const bool over_foliage =
        (nearest_hover->slot == Hover_entry::content_slot) && nearest_mesh &&
        ((nearest_mesh->get_flag_bits() & Editor_item_flags::foliage) != 0) && (m_context.level_kits != nullptr);
    // Over foliage the pass-through and the kit preview read this one hit; elsewhere the kit preview finds its own.
    std::optional<Hover_placement_hit> placement;
    if (over_foliage) {
        placement = m_context.level_kits->get_placement_hover(*scene_view);
        m_hover   = make_placement_hover_entry(*scene_view, placement);
    }
    m_hover_world_position = nearest_hover->position;
    m_hover_world_normal   = nearest_hover->normal;
    // A back-face hit (a double-sided plane seen from behind) faces the viewer, so the search goes into the surface (U51).
    const std::optional<glm::vec3> ray_direction = scene_view->get_control_ray_direction_in_world();
    if (m_hover_world_normal.has_value() && ray_direction.has_value() && (glm::dot(m_hover_world_normal.value(), ray_direction.value()) > 0.0f)) {
        m_hover_world_normal = -m_hover_world_normal.value();
    }

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (hover_scene_mesh) {
        erhe::scene::Node* hover_node = hover_scene_mesh.get();
        if (hover_node != nullptr) {
            if (m_hover.position.has_value()) {
                m_hover.position = hover_node->transform_direction_from_world_to_local(m_hover.position.value());
            }
        }
    }

    update_preview_mesh(over_foliage ? &placement : nullptr);
}

auto Brush_tool::make_placement_hover_entry(const Scene_view& scene_view, const std::optional<Hover_placement_hit>& placement) const -> Hover_entry
{
    if (!placement.has_value()) {
        return Hover_entry{};
    }
    if (placement->source == Hover_placement_source::grid) {
        return scene_view.get_hover(Hover_entry::grid_slot);
    }
    if (!placement->mesh) {
        return Hover_entry{};
    }
    // The surface hit as a content hover: the face frame of the non-kit placement reads its primitive and facet.
    Hover_entry entry{};
    entry.slot                       = Hover_entry::content_slot;
    entry.mask                       = Hover_entry::content_bit;
    entry.valid                      = true;
    entry.scene_mesh_weak            = placement->mesh;
    entry.scene_mesh_primitive_index = placement->primitive_index;
    entry.position                   = placement->position;
    entry.normal                     = placement->normal;
    entry.facet                      = placement->facet;
    const std::vector<erhe::scene::Mesh_primitive>& primitives = placement->mesh->get_primitives();
    if (placement->primitive_index < primitives.size()) {
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitives[placement->primitive_index].primitive->get_shape_for_raytrace();
        if (shape) {
            entry.geometry = shape->get_geometry_const();
        }
    }
    if (!entry.geometry || (entry.facet == GEO::NO_INDEX)) {
        entry.scene_mesh_primitive_index = std::numeric_limits<std::size_t>::max();
        entry.geometry.reset();
    }
    return entry;
}

void Brush_tool::preview_drag_and_drop(std::shared_ptr<Brush> brush)
{
    if (brush) {
        leave_kit_mode();
        m_show_preview = true;
    }
    m_drag_and_drop_brush = brush;
    on_motion();
}

auto Brush_tool::get_placement_facet() const -> GEO::index_t
{
    return m_brush_placement_frame.has_value() ? m_brush_placement_frame.value().m_facet : GEO::NO_INDEX;
}

auto Brush_tool::get_placement_corner0() const -> GEO::index_t
{
    return m_brush_placement_frame.has_value() ? m_brush_placement_frame.value().m_corner0 : GEO::NO_INDEX;
}

auto Brush_tool::update_hover_frame_from_mesh() -> bool
{
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    if (
        !hover_scene_mesh ||
        (m_hover.scene_mesh_primitive_index == std::numeric_limits<std::size_t>::max()) ||
        (!m_hover.geometry)
    ) {
        return false;
    }

    const erhe::scene::Node* node = hover_scene_mesh.get();
    if (node == nullptr) {
        return false;
    }

    auto* geometry = m_hover.geometry.get();
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    ERHE_VERIFY(m_hover.facet < geo_mesh.facets.nb());

    GEO::index_t corner = 0;
    Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, corner, Frame_orientation::out};

    if (!m_snap_to_hover_polygon && m_hover.position.has_value()) {
        hover_frame.m_centroid = to_geo_vec3f(m_hover.position.value());
    }

    m_world_from_hover         = node->world_from_node();
    m_hover_frame              = hover_frame;
    m_hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);
    return true;
}

auto Brush_tool::update_brush_frame(Brush& brush) -> bool
{
    if (!m_hover_frame.has_value()) {
        return false;
    }

    const Reference_frame& hover_frame = m_hover_frame.value();

    if (m_hover_facet_corner_count.has_value() && m_use_matching_face) {
        m_brush_placement_frame = brush.get_reference_frame(
            m_hover_facet_corner_count.value(),
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
    }

    if (!m_brush_placement_frame.has_value()) {
        const size_t selected_corner_count = m_selected_corner_count.has_value() ? m_selected_corner_count.value() : brush.get_max_corner_count();
        m_brush_placement_frame = brush.get_reference_frame(
            static_cast<uint32_t>(selected_corner_count),
            static_cast<uint32_t>(m_polygon_offset),
            static_cast<uint32_t>(m_corner_offset)
        );
    }

    // A brush whose geometry preparation failed yields a default frame; the
    // placement is refused rather than continuing with a zero scale.
    if (!m_brush_placement_frame.value().is_valid()) {
        m_brush_placement_frame.reset();
        return false;
    }

    Reference_frame& brush_frame = m_brush_placement_frame.value();

    ERHE_VERIFY(brush_frame.scale() != 0.0f);
    
    if (m_scale_to_match) {
        if (m_hover.slot == Hover_entry::grid_slot) {
            m_transform_scale = m_grid_scale;
        } else {
            const float hover_scale = hover_frame.scale();
            const float brush_scale = brush_frame.scale();
            const float scale       = (brush_scale != 0.0f) ? static_cast<float>(hover_scale / brush_scale) : 1.0f;
    
            m_transform_scale = m_scale * scale;
        }
        if (m_transform_scale != 1.0f) {
            const glm::mat4 scale_transform = erhe::math::create_scale(m_transform_scale);
            brush_frame.transform_by(to_geo_mat4f(scale_transform));
        }
    }

    const GEO::mat4f hover_transform = hover_frame.transform(0.0f);
    const GEO::mat4f brush_transform = brush_frame.transform(0.0f); // hover distance
    const GEO::mat4f inverse_brush   = brush_transform.inverse();
    const GEO::mat4f align           = hover_transform * inverse_brush;
    m_hover_transform = to_glm_mat4(hover_transform);
    m_align_transform = to_glm_mat4(align);
    return true;
}

auto Brush_tool::update_hover_frame_from_grid() -> bool
{
    std::shared_ptr<const Grid> hover_grid = m_hover.grid_weak.lock();
    if (!hover_grid || !m_hover.position.has_value()) {
        return false;
    }

    const Grid& grid = *hover_grid.get();
    Reference_frame hover_frame{grid, m_hover.grid_frame, to_geo_vec3f(m_hover.position.value())};

    m_world_from_hover = m_hover.grid_frame.world_from_grid;
    m_hover_frame      = hover_frame;
    return true;
}

void Brush_tool::update_preview_mesh_node_transform(const std::optional<Hover_placement_hit>* const motion_placement)
{
    m_hover_facet_corner_count.reset();
    m_brush_placement_frame.reset();
    m_world_from_hover.reset();
    m_hover_transform.reset();
    m_align_transform.reset();

    const std::shared_ptr<Brush> shared_brush = get_effective_brush();
    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();
    if (
        !shared_brush ||
        !m_hover.position.has_value() ||
        !m_preview_mesh ||
        !m_preview_node ||
        (!hover_scene_mesh && !hover_grid)
    ) {
        return;
    }

    const bool got_hover_frame_from_mesh = update_hover_frame_from_mesh();
    const bool got_hover_frame_from_grid = !got_hover_frame_from_mesh && update_hover_frame_from_grid();
    if (!got_hover_frame_from_mesh && !got_hover_frame_from_grid) {
        return;
    }

    Brush& brush = *shared_brush.get();
    if (m_kit_brush && (shared_brush == m_kit_brush)) {
        update_kit_preview_transform(brush, motion_placement);
        return;
    }
    const bool brush_frame_ok = update_brush_frame(brush);
    if (!brush_frame_ok || !m_align_transform.has_value()) {
        return;
    }

    const Brush::Scaled* brush_scaled_pointer = brush.get_scaled(m_transform_scale);
    if (brush_scaled_pointer == nullptr) {
        return;
    }
    const Brush::Scaled& brush_scaled = *brush_scaled_pointer;
    const glm::mat4&     transform    = m_align_transform.value();

    // TODO Unparent, to remove raytrace primitives to raytrace scene.
    //      This is a workaround for clear_primitives() issue below
    m_preview_node->set_parent({});

    const std::shared_ptr<erhe::primitive::Primitive_render_shape> shape = m_preview_mesh->get_primitives().front().primitive->render_shape;
    ERHE_VERIFY(shape);

    // TODO Brush mesh does not really need RT primitives at all

    const std::shared_ptr<erhe::primitive::Material>& material = m_preview_mesh->get_primitives().front().material;
    m_preview_mesh->clear_primitives(); // TODO This is dangerous, as primitives *must* first be removed from rt_scene
    m_preview_mesh->add_primitive(brush_scaled.primitive, material);

    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (hover_scene_mesh) {
        m_preview_node->set_parent(hover_scene_mesh.get());
        m_preview_node->set_parent_from_node(transform);
    } else if (hover_grid) {
        ERHE_VERIFY(scene_root);
        m_preview_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
        m_preview_node->set_parent_from_node(transform);
    }
    m_preview_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
}

void Brush_tool::update_kit_preview_transform(Brush& brush, const std::optional<Hover_placement_hit>* const motion_placement)
{
    m_kit_pose.reset();
    clear_kit_join();
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    Scene_view* const scene_view = get_hover_scene_view();
    if (!scene_root || (scene_view == nullptr)) {
        return;
    }
    // FD2.4: everything about the placement comes from this one hit, foliage passed through.
    Level_kits& level_kits = *m_context.level_kits;
    const std::optional<Hover_placement_hit> placement = (motion_placement != nullptr) ? *motion_placement : level_kits.get_placement_hover(*scene_view);
    if (!placement.has_value()) {
        return;
    }
    // On content the surface point; on the grid the grid-snapped point, as the face-aligned placement.
    const glm::vec3 point = (placement.value().source == Hover_placement_source::grid)
        ? get_grid_point(placement.value())
        : placement.value().position;
    // U51: placed along the normal of the surface under the pointer (the grid's normal on the grid).
    m_kit_pose = level_kits.make_placement_pose(
        *scene_root, brush.get_bounding_box(), point, placement.value().normal, m_kit_yaw_steps, level_kits.get_placement_settings()
    );
    update_kit_join(*scene_root, placement.value());
    // add_preview_mesh() gave the preview the kit brush's primitive at scale 1 under the scene root;
    // a new kit brush makes a new preview, so only the transform changes here.
    m_preview_node->set_parent_from_node(
        erhe::math::create_translation<float>(m_kit_pose.value().position) * glm::mat4_cast(m_kit_pose.value().rotation)
    );
    sync_kit_preview_primitives();
}

void Brush_tool::clear_kit_join()
{
    m_kit_join.reset();
    if (m_context.level_kits != nullptr) {
        m_context.level_kits->get_connector_indicators().clear_brush_join();
    }
}

auto Brush_tool::get_grid_point(const Hover_placement_hit& placement) const -> glm::vec3
{
    if (!placement.grid) {
        return placement.position;
    }
    const glm::vec3 position_in_grid0 = glm::vec3{placement.grid_frame.grid_from_world * glm::vec4{placement.position, 1.0f}};
    const glm::vec3 position_in_grid  = m_snap_to_grid ? placement.grid->snap_grid_position(position_in_grid0) : position_in_grid0;
    return glm::vec3{placement.grid_frame.world_from_grid * glm::vec4{position_in_grid, 1.0f}};
}

void Brush_tool::update_kit_join(Scene_root& scene_root, const Hover_placement_hit& placement)
{
    ERHE_PROFILE_FUNCTION();

    clear_kit_join();
    m_kit_join_suspended = (m_context.transform_tool != nullptr) && m_context.transform_tool->is_connector_snap_suspend_held();
    Level_kits& level_kits = *m_context.level_kits;
    const float reach      = level_kits.get_connector_snap_distance();
    if (
        m_kit_join_suspended || !(reach > 0.0f) || m_kit_connectors.empty() || !m_kit_pose.has_value() ||
        (placement.source != Hover_placement_source::surface)
    ) {
        return;
    }
    // The hovered mesh is the piece's node (Brush::make_instance()), or a floor layout's slab, whose parent is the target (K6);
    // the indicator cache knows a floor without parsing its layout value again.
    Kit_connector_indicators& indicators = level_kits.get_connector_indicators();
    std::shared_ptr<erhe::scene::Node> piece = placement.mesh;
    bool floor = false;
    if (piece && !carries_kit_piece(*piece)) {
        std::shared_ptr<erhe::scene::Node> parent = piece->get_parent_node();
        if (parent && indicators.is_floor_layout(*parent)) {
            piece = std::move(parent);
            floor = true;
        }
    }
    if (!piece || (piece->get_item_host() != &scene_root) || !(floor || carries_kit_piece(*piece))) {
        return;
    }
    // Enabled frames only (U33, U39): a switched-off or layout-hidden connector is never a target. The cache reads a
    // floor's frames again when its outline or values change.
    const std::vector<Kit_connector>& local = indicators.get_local_connectors(*piece);
    if (local.empty() || !transform_connectors(local, piece->world_from_node(), m_kit_target_connectors)) {
        return;
    }
    const glm::vec3 hover_point = placement.position;
    // The nearest free one within reach (inclusive); the first in the shape's order on a tie.
    std::optional<std::size_t> nearest{};
    float nearest_distance = 0.0f;
    for (std::size_t i = 0, end = m_kit_target_connectors.size(); i < end; ++i) {
        // C1.9: a target no connector of the new piece is compatible with is skipped silently.
        const bool compatible = std::any_of(
            m_kit_connectors.begin(), m_kit_connectors.end(),
            [&](const Kit_connector& connector) -> bool { return is_compatible(connector, m_kit_target_connectors[i]); }
        );
        if (!compatible) {
            continue;
        }
        const float distance = glm::distance(m_kit_target_connectors[i].position, hover_point);
        if (
            (distance <= reach) && (!nearest.has_value() || (distance < nearest_distance)) &&
            !is_kit_connector_joined(scene_root, *piece, m_kit_target_connectors[i])
        ) {
            nearest          = i;
            nearest_distance = distance;
        }
    }
    if (!nearest.has_value()) {
        return;
    }
    // The new piece's connector whose outward opposes the target's most, both in their pieces' own frames; the first on a tie.
    const Kit_connector& target_local = local[nearest.value()];
    const Kit_connector* moved        = nullptr;
    float                moved_dot    = 2.0f;
    for (const Kit_connector& connector : m_kit_connectors) {
        if (!is_compatible(connector, target_local)) {
            continue;
        }
        const float facing = glm::dot(connector.outward, target_local.outward);
        if (facing < (moved_dot - 1.0e-4f)) {
            moved     = &connector;
            moved_dot = facing;
        }
    }
    // The quarter twist nearest the rotation steps' yaw angle.
    const float yaw_degrees = static_cast<float>(m_kit_yaw_steps) * level_kits.get_placement_settings().rotation_step_degrees;
    // K13: a rider runs along its host, so a host and rider pair takes the half turn nearest the yaw.
    const bool  rail_pair   = is_rail_host_of(target_local, *moved) || is_rail_host_of(*moved, target_local);
    const int   twist_steps = rail_pair
        ? ((static_cast<int>(std::lround(yaw_degrees / 180.0f)) * 2) % 4)
        : (static_cast<int>(std::lround(yaw_degrees / 90.0f)) % 4);
    // K9.2: the new piece joins at the values the target supplies, as the placement's flow gives them.
    const Kit_connector& joined = get_kit_join_moved_connector(*piece, target_local.name, *moved);
    static_cast<void>(apply_kit_join(scene_root, piece, target_local.name, m_kit_target_connectors[nearest.value()], joined, twist_steps, nearest_distance));
}

auto Brush_tool::get_kit_join_moved_connector(const erhe::scene::Node& target, const char* const target_connector, const Kit_connector& moved) -> const Kit_connector&
{
    Kit_join_flow&      flow       = m_kit_join_flow;
    Level_kits&         level_kits = *m_context.level_kits;
    const std::uint64_t target_id  = static_cast<std::uint64_t>(target.get_id());
    // Read from the setting, not get_layout_initial_values(), which parses the layout pattern.
    const bool          piece_mode = (m_context.editor_settings == nullptr) || !m_context.editor_settings->level_kit.place_as_layout;
    const bool same = flow.valid && (flow.piece_mode == piece_mode) && (flow.target_id == target_id) &&
                      (flow.target_connector == target_connector) && (flow.connector == moved.name);
    if (!same) {
        flow.valid            = true;
        flow.piece_mode       = piece_mode;
        flow.target_id        = target_id;
        flow.target_connector = target_connector;
        flow.connector        = moved.name;
        flow.values           = m_kit_values;
        flow.slot_materials   = m_kit_slot_materials;
        flow.brush.reset();
        flow.connectors.clear();
        flow.materials.clear();
        flow.flowed = piece_mode && level_kits.get_join_supplied_values(target, target_connector, moved.name, flow.values, flow.slot_materials);
        if (flow.flowed) {
            flow.brush = level_kits.get_brush(flow.values);
            if (flow.brush) {
                flow.brush->late_initialize();
            }
            const Brush::Scaled* const scaled = flow.brush ? flow.brush->get_scaled(1.0f) : nullptr;
            if ((scaled == nullptr) || (scaled->part_primitives.size() != m_kit_preview_materials.size())) {
                flow.flowed = false;
                flow.brush.reset();
            } else {
                get_shape_connectors(flow.values, flow.connectors);
                const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
                for (std::size_t slot = 0, end = flow.slot_materials.size(); slot < end; ++slot) {
                    std::shared_ptr<erhe::primitive::Material> material = level_kits.get_preview_material(flow.slot_materials[slot]);
                    if (!material && scene_root) {
                        material = Level_kits::find_scene_material(*scene_root, flow.slot_materials[slot]);
                    }
                    flow.materials.push_back(material ? material : m_kit_preview_materials[slot]);
                }
            }
        }
    }
    if (flow.flowed) {
        const Kit_connector* const flowed = find_connector(flow.connectors, moved.name);
        if (flowed != nullptr) {
            return *flowed;
        }
    }
    return moved;
}

void Brush_tool::sync_kit_preview_primitives()
{
    if (!m_kit_brush || !m_preview_mesh || !m_preview_node) {
        return;
    }
    const bool flowed = m_kit_join.has_value() && m_kit_join_flow.valid && m_kit_join_flow.flowed && m_kit_join_flow.brush;
    const std::shared_ptr<Brush>& shown = flowed ? m_kit_join_flow.brush : m_kit_brush;
    // Owner identity: a brush made later at a reused address is another brush.
    if (!m_kit_preview_shown.owner_before(shown) && !shown.owner_before(m_kit_preview_shown) && !m_kit_preview_shown.expired()) {
        return;
    }
    const Brush::Scaled* const scaled = shown->get_scaled(1.0f);
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if ((scaled == nullptr) || !scene_root) {
        return;
    }
    const std::shared_ptr<erhe::primitive::Material> fallback = m_preview_mesh->get_primitives().empty()
        ? std::shared_ptr<erhe::primitive::Material>{}
        : m_preview_mesh->get_primitives().front().material;
    // Out of the scene while its primitives change, as update_preview_mesh_node_transform() swaps them.
    m_preview_node->set_parent({});
    m_preview_mesh->clear_primitives();
    for (std::size_t part = 0, end = scaled->part_primitives.size(); part < end; ++part) {
        const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = flowed ? m_kit_join_flow.materials : m_kit_preview_materials;
        const bool has_own = (part < materials.size()) && materials[part];
        m_preview_mesh->add_primitive(scaled->part_primitives[part], has_own ? materials[part] : fallback);
    }
    m_preview_node->set_parent(scene_root->get_scene().get_root_node());
    m_kit_preview_shown = shown;
}

auto Brush_tool::get_kit_join_values() const -> const Kit_parameter_values*
{
    return (m_kit_join.has_value() && m_kit_join_flow.valid && m_kit_join_flow.flowed) ? &m_kit_join_flow.values : nullptr;
}

auto Brush_tool::get_kit_join_slot_materials() const -> const std::vector<std::string>&
{
    return (get_kit_join_values() != nullptr) ? m_kit_join_flow.slot_materials : m_kit_slot_materials;
}

auto Brush_tool::apply_kit_join(
    Scene_root&                               scene_root,
    const std::shared_ptr<erhe::scene::Node>& piece,
    const char* const                         target_connector_name,
    const Kit_connector&                      target_world,
    const Kit_connector&                      moved,
    const int                                 twist_steps,
    const float                               distance
) -> bool
{
    // As resolve_connect() computes it for a new piece (world_from_node identity), so a click lands on this pose.
    const std::optional<Kit_join_pose> pose = join_motion(moved, glm::mat4{1.0f}, target_world, twist_steps, 0.0f);
    if (!pose.has_value() || !m_kit_pose.has_value()) {
        return false;
    }
    m_kit_pose.value().position      = pose.value().position;
    m_kit_pose.value().rotation      = pose.value().rotation;
    m_kit_pose.value().surface_found = false;
    m_kit_join = Kit_brush_join{
        .target           = piece,
        .target_id        = static_cast<std::uint64_t>(piece->get_id()),
        .target_connector = target_connector_name,
        .connector        = moved.name,
        .twist_steps      = twist_steps,
        .distance         = distance
    };
    const std::optional<Kit_connector> moved_world = transform_connector(moved, apply_motion(pose.value(), glm::mat4{1.0f}));
    m_context.level_kits->get_connector_indicators().set_brush_join(
        scene_root, piece, target_connector_name, moved_world.value_or(target_world),
        m_preview_node ? static_cast<std::uint64_t>(m_preview_node->get_id()) : 0
    );
    return true;
}

void Brush_tool::refresh_kit_join()
{
    // The same pair at the target's new frame: the pointer has not moved, so the hover point is not searched again.
    const Kit_brush_join                     join       = m_kit_join.value();
    const std::shared_ptr<erhe::scene::Node> target     = join.target.lock();
    const std::shared_ptr<Scene_root>&       scene_root = get_scene_root();
    if (target && scene_root && m_preview_node && (target->get_item_host() == scene_root.get())) {
        const std::vector<Kit_connector>& local        = m_context.level_kits->get_connector_indicators().get_local_connectors(*target);
        const Kit_connector* const        target_local = find_connector(local, join.target_connector);
        const Kit_connector* const        moved        = find_connector(m_kit_connectors, join.connector);
        if ((target_local != nullptr) && (moved != nullptr)) {
            const std::optional<Kit_connector> target_world = transform_connector(*target_local, target->world_from_node());
            // K9.2: the changed target may supply other values (a floor edge's new length).
            m_kit_join_flow.valid = false;
            const Kit_connector& joined = get_kit_join_moved_connector(*target, join.target_connector, *moved);
            if (
                target_world.has_value() &&
                apply_kit_join(*scene_root, target, join.target_connector, target_world.value(), joined, join.twist_steps, join.distance)
            ) {
                m_preview_node->set_parent_from_node(
                    erhe::math::create_translation<float>(m_kit_pose.value().position) * glm::mat4_cast(m_kit_pose.value().rotation)
                );
                sync_kit_preview_primitives();
                return;
            }
        }
    }
    // The target is gone or its connector switched off: the preview places as without it.
    update_preview_mesh();
}

auto Brush_tool::is_kit_connector_joined(const Scene_root& scene_root, const erhe::scene::Node& piece, const Kit_connector& connector) -> bool
{
    constexpr float c_position_tolerance = 1.0e-3f;
    constexpr float c_facing_dot         = -0.999f;
    Kit_connector_indicators& indicators = m_context.level_kits->get_connector_indicators();
    bool joined = false;
    scene_root.get_scene().for_each_node(
        [&](const std::shared_ptr<erhe::scene::Node>& node) -> bool {
            if (!node || (node.get() == &piece) || !carries_kit_piece(*node)) {
                return true;
            }
            const erhe::scene::Mesh* const mesh = dynamic_cast<const erhe::scene::Mesh*>(node.get());
            if (mesh == nullptr) {
                return true;
            }
            const erhe::math::Aabb bounds = mesh->get_aabb_world();
            const glm::vec3        p      = connector.position;
            if (
                !bounds.is_valid_3d() ||
                glm::any(glm::lessThan(p, bounds.min - glm::vec3{c_position_tolerance})) ||
                glm::any(glm::greaterThan(p, bounds.max + glm::vec3{c_position_tolerance}))
            ) {
                return true;
            }
            const glm::mat4 world_from_node = node->world_from_node();
            for (const Kit_connector& local : indicators.get_local_connectors(*node)) {
                const std::optional<Kit_connector> other = transform_connector(local, world_from_node);
                if (
                    other.has_value() && (glm::distance(other.value().position, p) <= c_position_tolerance) &&
                    (glm::dot(other.value().outward, connector.outward) <= c_facing_dot)
                ) {
                    joined = true;
                    return false;
                }
            }
            return true;
        }
    );
    return joined;
}

void Brush_tool::on_kit_join_target_changed(const erhe::scene::Node* const node)
{
    if (!m_kit_join.has_value() || !m_kit_brush || !m_preview_node) {
        return;
    }
    if (node != nullptr) {
        // Only a move of the target or a node above it moves the target connector.
        bool above_target = false;
        for (std::shared_ptr<erhe::scene::Node> n = m_kit_join.value().target.lock(); n; n = n->get_parent_node()) {
            if (n.get() == node) {
                above_target = true;
                break;
            }
        }
        if (!above_target) {
            return;
        }
    }
    refresh_kit_join();
}

void Brush_tool::do_insert_operation(Brush& brush)
{
    const std::shared_ptr<Scene_root>& scene_root = get_scene_root();
    if (!scene_root) {
        return;
    }

    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    const std::shared_ptr<erhe::primitive::Material>& material =
        (active_brush && active_brush->get_material()) ? active_brush->get_material() : get_material();
    if (!m_hover.position.has_value() || !material) {
        return;
    }

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();

    if (!m_align_transform.has_value()) {
        return;
    }
    const auto hover_from_brush = m_align_transform.value();

    auto* const hover_node = hover_scene_mesh ? hover_scene_mesh.get() : nullptr;
    const glm::mat4 world_from_node = (hover_node != nullptr)
        ? hover_node->world_from_node() * hover_from_brush
        : hover_from_brush;

    // Determine parent node based on tool settings
    std::shared_ptr<erhe::scene::Node> parent;
    // doc/editor/active_item.md D6: the reference node is the active item.
    const std::shared_ptr<erhe::scene::Node> active_node = m_context.selection->get_active_item_as<erhe::scene::Node>();
    if (m_parent_to_active && active_node && (active_node->get_item_host() != nullptr)) {
        parent = active_node;
    }
    erhe::scene::Scene&                       scene     = scene_root->get_scene();
    const std::shared_ptr<erhe::scene::Node>& root_node = scene.get_root_node();
    if (!parent && m_parent_to_scene_root) {
        if (root_node) {
            parent = root_node;
        }
    }
    if (!parent && m_parent_to_hovered) {
        if (hover_node) {
            parent = std::static_pointer_cast<erhe::scene::Node>(hover_node->shared_from_this());
        } else if (root_node) {
            parent = root_node;
        }
    }

    place_brush_in_scene(
        m_context,
        brush,
        *scene_root,
        world_from_node,
        material,
        m_transform_scale,
        erhe::physics::Motion_mode::e_dynamic,
        parent
    );
}

void Brush_tool::add_preview_mesh(Brush& brush, const std::optional<Hover_placement_hit>* const motion_placement)
{
    const std::shared_ptr<Brush> active_brush = m_active_brush.get_as<Brush>();
    const bool kit_preview = m_kit_brush && (&brush == m_kit_brush.get());
    const std::shared_ptr<erhe::primitive::Material> material =
        (kit_preview && !m_kit_preview_materials.empty() && m_kit_preview_materials.front()) ? m_kit_preview_materials.front() :
        (active_brush && active_brush->get_material())     ? active_brush->get_material() :
                                                             get_material();
    auto* scene_view = get_hover_scene_view();

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    if (!material || !m_hover.position.has_value() || (!hover_scene_mesh && !hover_grid) || (scene_view == nullptr)) {
        return;
    }

    const auto& scene_root = scene_view->get_scene_root();
    ERHE_VERIFY(scene_root);

    brush.late_initialize();
    const Brush::Scaled* brush_scaled_pointer = brush.get_scaled(kit_preview ? 1.0f : m_transform_scale);
    if (brush_scaled_pointer == nullptr) {
        // get_scaled() has already named the brush in the log.
        return;
    }
    const Brush::Scaled& brush_scaled = *brush_scaled_pointer;
    const std::string name = fmt::format("brush-{}", brush.get_name());
    m_preview_node = std::make_shared<erhe::scene::Xform>(name);
    m_preview_mesh = std::make_shared<erhe::scene::Mesh>(name);
    if (kit_preview) {
        // One primitive per slot with that slot's material, as the placed piece gets them.
        for (std::size_t part = 0, end = brush_scaled.part_primitives.size(); part < end; ++part) {
            const bool has_own = (part < m_kit_preview_materials.size()) && m_kit_preview_materials[part];
            m_preview_mesh->add_primitive(brush_scaled.part_primitives[part], has_own ? m_kit_preview_materials[part] : material);
        }
        m_kit_preview_shown = m_kit_brush;
    } else {
        m_preview_mesh->add_primitive(brush_scaled.primitive, material);
    }
    m_preview_node->enable_flag_bits(
        editor::Editor_item_flags::brush      |
        erhe::Item_flags::no_message |
        editor::Editor_item_flags::show_in_developer_ui
    );
    m_preview_mesh->enable_flag_bits(
        editor::Editor_item_flags::brush       |
        erhe::Item_flags::no_message  |
        editor::Editor_item_flags::show_in_developer_ui
    );

    m_preview_mesh->layer_id = scene_root->layers().brush()->id;

    erhe::scene::set_mesh_parent(m_preview_mesh, m_preview_node);
    m_preview_node->set_parent(scene_root->get_scene().get_root_node());

    update_preview_mesh_node_transform(motion_placement);
}

void Brush_tool::update_preview_mesh(const std::optional<Hover_placement_hit>* const motion_placement)
{
    const std::shared_ptr<Brush> brush = get_effective_brush();

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    if (!m_preview_mesh && (is_enabled() || m_drag_and_drop_brush) && m_show_preview) {
        if (!brush || !m_hover.position.has_value() || (!hover_scene_mesh && !hover_grid)) {
            return;
        }

        add_preview_mesh(*brush.get(), motion_placement);
        return;
    }

    if (
        (!m_drag_and_drop_brush && !is_enabled()) ||
        !brush ||
        !m_hover.position.has_value() ||
        !m_show_preview ||
        (!hover_scene_mesh && !hover_grid)
    ) {
        remove_preview_mesh();
    }

    update_preview_mesh_node_transform(motion_placement);
}

void Brush_tool::tool_properties(erhe::imgui::Imgui_window& /*imgui_window*/)
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Brush> last_selected_brush = m_context.selection->get_last_selected<Brush>();
    const std::shared_ptr<Brush> hover_brush = get_hover_brush();

    const Kit_shape_definition* const kit_shape = get_kit_shape();
    if (kit_shape != nullptr) {
        ImGui::Text("Kit mode: %s (Level Kit window)", kit_shape->display_name.c_str());
    }

    ImGui::Checkbox("Parent to Active",   &m_parent_to_active        );
    ImGui::Checkbox("Parent to Scene",    &m_parent_to_scene_root    );
    ImGui::Checkbox("Parent to Hovered",  &m_parent_to_hovered       );

    //ImGui::Text("Brush: %s", last_selected_brush ? last_selected_brush->get_name().c_str() : "");
    //ImGui::Text("Hover Brush: %s", hover_brush ? hover_brush->get_name().c_str() : "");
    //ImGui::Text("Drag and Drop Brush: %s", m_drag_and_drop_brush ? m_drag_and_drop_brush->get_name().c_str() : "");
    //ImGui::SliderFloat("Preview Hover", &m_preview_hover_distance, -0.1f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    //ImGui::Text("Enabled: %s", is_enabled() ? "yes" : "no");
    //if (m_hover.position.has_value()) {
    //    const glm::vec3& p = m_hover.position.value();
    //    ImGui::Text("Hover position: %.2f, %.2f, %.2f", p.x, p.y, p.z);
    //} else {
    //    ImGui::Text("Hover position: -");
    //}
    //ImGui::Text("Hover mesh: %s", m_hover.mesh ? m_hover.mesh->get_name().c_str() : "");
    //ImGui::Text("Hover grid: %s", m_hover.grid ? m_hover.grid->get_name().c_str() : "");

    const std::shared_ptr<Brush> brush = get_effective_brush();
    const std::string brush_label = (brush && !brush->get_name().empty()) ? brush->get_name() : "(unnamed brush)";
    if (brush && ImGui::TreeNodeEx(brush_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        std::shared_ptr<erhe::geometry::Geometry> geometry = brush->get_geometry();
        if (geometry) {
            const std::map<GEO::index_t, std::vector<GEO::index_t>>& facets = brush->get_corner_count_to_facets();
            for (const auto& i : facets) {
                bool selected = m_selected_corner_count.has_value() && m_selected_corner_count.value() == i.first;
                std::string label = fmt::format("{}-gons: {}", i.first, i.second.size());
                if (ImGui::Selectable(label.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (selected) {
                        m_selected_corner_count = i.first;
                    } else {
                        m_selected_corner_count.reset();
                    }
                }
            }
        }
        ImGui::TreePop();
    }
    const auto material = m_context.selection->get_last_selected<erhe::primitive::Material>();
    if (material) {
        ImGui::Text("Material: %s", material->get_name().c_str());
    }

    if (ImGui::Checkbox("Scale to Match", &m_scale_to_match) && !m_scale_to_match) {
        m_transform_scale = 1.0f;
    }

    if (ImGui::RadioButton("Use Matching Face", m_use_matching_face)) {
        m_use_matching_face = true;
        m_use_selected_face = false;
    }
    if (ImGui::RadioButton("Use Selected Face", m_use_selected_face)) {
        m_use_matching_face = false;
        m_use_selected_face = true;
    }

    ImGui::Checkbox   ("Snap to Polygon", &m_snap_to_hover_polygon);
    ImGui::Checkbox   ("Snap To Grid",    &m_snap_to_grid);
    ImGui::SliderFloat("Scale",           &m_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Grid Scale",      &m_grid_scale, 0.0001f, 32.0f, "%.3f", ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox   ("Physics",         &m_with_physics);
    //ImGui::DragInt    ("Face Offset",     &m_polygon_offset, 0.1f, 0, INT_MAX);
    //ImGui::DragInt    ("Corner Offset",   &m_corner_offset,  0.1f, 0, INT_MAX);

    ImGui::NewLine    ();
    //ImGui::Checkbox   ("Debug Visualization", &m_debug_visualization);
    ImGui::Checkbox   ("Show Receiver Frame", &m_show_receiver_frame);
    ImGui::Checkbox   ("Show Brush Frame",    &m_show_brush_frame);
    ImGui::Checkbox   ("Show Preview",        &m_show_preview);

    if (ImGui::IsItemEdited()) {
        if (m_show_preview) {
            update_preview_mesh();
        }
        else {
            remove_preview_mesh();
        }
    }

    ///if (!m_brush_item_tree) {
    ///    std::shared_ptr<Content_library> content_library = get_content_library();
    ///    if (content_library) {
    ///        m_brush_item_tree = std::make_unique<Item_tree>(m_context);
    ///        m_brush_item_tree->set_root(content_library->brushes);
    ///        m_brush_item_tree->set_item_filter(
    ///            erhe::Item_filter{
    ///                .require_all_bits_set           = 0,
    ///                .require_at_least_one_bit_set   = 0,
    ///                .require_all_bits_clear         = 0,
    ///                .require_at_least_one_bit_clear = 0
    ///            }
    ///        );
    ///    }
    ///}
    ///if (m_brush_item_tree) {
    ///    m_brush_item_tree->imgui_tree(imgui_window.get_scale_value());
    ///}
}

void Brush_tool::tool_render(const Render_context& render_context)
{
    ERHE_PROFILE_FUNCTION();

    if (!m_debug_visualization) {
        return;
    }

    std::shared_ptr<erhe::scene::Mesh> hover_scene_mesh = m_hover.scene_mesh_weak.lock();
    std::shared_ptr<const Grid>        hover_grid       = m_hover.grid_weak.lock();

    const std::shared_ptr<Brush> shared_brush = get_effective_brush();
    //if (
    //    !shared_brush ||
    //    !m_hover.position.has_value() ||
    //    (!hover_scene_mesh && !hover_grid)
    //) {
    //    return;
    //}

    if (m_hover_frame.has_value()) {
        const Reference_frame& hover_frame      = m_hover_frame.value();
        const GEO::mat4f       hover_transform_ = hover_frame.transform(0.0f);
        const glm::mat4        hover_transform  = to_glm_mat4(hover_transform_);
        const glm::mat4        world_from_hover = m_world_from_hover.has_value() ? m_world_from_hover.value() : glm::mat4{1.0f};
        const glm::mat4        world_from_align = world_from_hover * hover_transform;
        constexpr glm::vec3 C    {0.0f, 0.0f, 0.0f};
        constexpr glm::vec3 C_t  {1.0f, 0.0f, 0.0f};
        constexpr glm::vec3 C_b  {0.0f, 1.0f, 0.0f};
        constexpr glm::vec3 C_n  {0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines( world_from_align, red,   { { C, C_t }});
        line_renderer.add_lines( world_from_align, green, { { C, C_b }});
        line_renderer.add_lines( world_from_align, blue,  { { C, C_n }});
    }

#if 0
    if (m_show_receiver_frame) {
        //if (!hover_scene_mesh && hover_grid) {
        //    constexpr glm::vec3 C    {0.0f, 0.0f, 0.0f};
        //    constexpr glm::vec3 C_t  {1.0f, 0.0f, 0.0f};
        //    constexpr glm::vec3 C_b  {0.0f, 1.0f, 0.0f};
        //    constexpr glm::vec3 C_n  {0.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        //    constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        //    erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
        //    line_renderer.set_thickness(10.0f);
        //    line_renderer.add_lines( world_from_hover, red,   { { C, C_t }});
        //    line_renderer.add_lines( world_from_hover, green, { { C, C_b }});
        //    line_renderer.add_lines( world_from_hover, blue,  { { C, C_n }});
        //    return;
        //}
        //
        //erhe::scene::Node* hover_node = hover_scene_mesh.get();
        //if (hover_node == nullptr) {
        //    return;
        //}
        //
        //const glm::mat4 world_from_hover_node = hover_node->world_from_node();
        //
        //Brush& brush = *shared_brush.get();
        //auto* geometry = m_hover.geometry.get();
        //const GEO::Mesh& geo_mesh = geometry->get_mesh();
        //GEO::index_t corner = 0;
        //Reference_frame hover_frame{geo_mesh, m_hover.facet, 0, corner, Frame_orientation::out};
        //const GEO::index_t hover_facet_corner_count = geo_mesh.facets.nb_corners(m_hover.facet);
        //m_brush_placement_frame = brush.get_reference_frame(
        //    hover_facet_corner_count,
        //    static_cast<uint32_t>(m_polygon_offset),
        //    static_cast<uint32_t>(m_corner_offset)
        //);

        const GEO::vec3f C_   = hover_frame.m_centroid;
        const GEO::vec3f C_t_ = hover_frame.m_centroid + hover_frame.m_T;
        const GEO::vec3f C_b_ = hover_frame.m_centroid + hover_frame.m_B;
        const GEO::vec3f C_n_ = hover_frame.m_centroid + hover_frame.m_N;
        const glm::vec3  C    = to_glm_vec3(C_  );
        const glm::vec3  C_t  = to_glm_vec3(C_t_);
        const glm::vec3  C_b  = to_glm_vec3(C_b_);
        const glm::vec3  C_n  = to_glm_vec3(C_n_);
        constexpr glm::vec4 red  {1.0f, 0.0f, 0.0f, 1.0f};
        constexpr glm::vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
        constexpr glm::vec4 blue {0.0f, 0.0f, 1.0f, 1.0f};
        erhe::renderer::Primitive_renderer line_renderer = render_context.get({erhe::graphics::Primitive_type::line, 2, true, true});
        line_renderer.set_thickness(10.0f);
        line_renderer.add_lines( world_from_hover_node, red,   { { C, C_t }});
        line_renderer.add_lines( world_from_hover_node, green, { { C, C_b }});
        line_renderer.add_lines( world_from_hover_node, blue,  { { C, C_n }});
    }
#endif
}

}
