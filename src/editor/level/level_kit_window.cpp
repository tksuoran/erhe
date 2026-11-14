#include "level/level_kit_window.hpp"

#include "level/kit_parameter_rows.hpp"
#include "level/kit_piece.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"
#include "level/surface_settings_ui.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "brushes/brush_tool.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/binding_desc.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/curve.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

namespace {

void tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

constexpr std::array<int, 3> c_rotation_steps{90, 45, 15};

// The effective bindings of a command as display text, else the command's name.
[[nodiscard]] auto binding_text(const erhe::commands::Commands& commands, const std::string_view command_name) -> std::string
{
    const erhe::commands::Command* const command = commands.find_command(command_name);
    if (command == nullptr) {
        return std::string{command_name};
    }
    std::vector<erhe::commands::Binding_desc> bindings;
    commands.get_effective_bindings(*command, bindings);
    std::string text;
    for (const erhe::commands::Binding_desc& binding : bindings) {
        text += text.empty() ? std::string{} : std::string{" or "};
        text += binding.to_display_string();
    }
    return text.empty() ? std::string{command_name} : text;
}

} // anonymous namespace

Level_kit_window::Level_kit_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Level Kit", "level_kit"}
    , m_context     {app_context}
    , m_piece_editor{app_context}
    , m_layout_editor{app_context}
{
    set_min_size(360.0f, 480.0f);
    set_default_visibility(erhe::imgui::Window_visibility::hidden);
    // Runs inside Commands::tick() when the dispatch tables were rebuilt.
    commands.add_bindings_changed_callback(
        [this]() {
            m_place_hint_dirty = true;
        }
    );
    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message&) {
            m_piece_editor.mark_dirty();
            m_group_selection_changed  = true;
        }
    );
    m_active_item_subscription = app_message_bus.active_item.subscribe(
        [this](Active_item_changed_message&) {
            m_piece_editor.mark_dirty();
            m_group_selection_changed  = true;
        }
    );
    // The Brush tool took the next seed after a placed piece: the rows show its values.
    m_kit_values_changed_subscription = app_message_bus.kit_values_changed.subscribe(
        [this](Kit_values_changed_message&) {
            on_kit_values_changed();
        }
    );
    // A parameter change, its undo and redo all send it (Kit_piece_parameters_operation).
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message&) {
            m_piece_editor.mark_dirty();
            m_duplicate_source.reset();
            m_group_selection_changed = true;
        }
    );
}

void Level_kit_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Viewport_scene_view> scene_view =
        (m_context.scene_views != nullptr) ? m_context.scene_views->last_scene_view() : std::shared_ptr<Viewport_scene_view>{};
    const std::shared_ptr<Scene_root> scene_root = scene_view ? scene_view->get_scene_root() : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        ImGui::TextUnformatted("Scene: none");
        ImGui::TextWrapped("The window acts on the scene of the last hovered viewport: hover a viewport.");
        return;
    }
    ImGui::Text("Scene: %s", scene_root->get_name().c_str());
    tooltip("The scene of the last hovered viewport; the shape defaults follow its module and wall height.");

    level_settings_imgui(*scene_root);
    shapes_imgui(*scene_root);
    place_parameters_imgui(*scene_root);
    placement_settings_imgui();
    selection_imgui(scene_root);
    layout_imgui(scene_root);
    group_imgui(scene_root);
    spatial_queries_imgui(scene_root);
}

void Level_kit_window::level_settings_imgui(Scene_root& scene_root)
{
    if (!ImGui::CollapsingHeader("Level Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (!m_lengths_editing) {
        const Kit_lengths lengths = m_context.level_kits->get_lengths(scene_root);
        m_module      = lengths.module;
        m_wall_height = lengths.wall_height;
    }
    bool commit = false;
    ImGui::DragFloat("Module", &m_module, 0.01f, 0.05f, 64.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
    tooltip("The level's unit of length: snap positions and modular default sizes follow it.");
    m_lengths_editing = ImGui::IsItemActive();
    commit = commit || ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat("Wall Height", &m_wall_height, 0.01f, 0.05f, 64.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
    tooltip("The default wall height; pieces placed later take their defaults from it.");
    m_lengths_editing = m_lengths_editing || ImGui::IsItemActive();
    commit = commit || ImGui::IsItemDeactivatedAfterEdit();
    if (commit) {
        // As the Properties window and MCP set_scene_settings write Scene_settings: in place, not undoable.
        scene_root.get_scene_settings().level = Level_settings{.module = m_module, .wall_height = m_wall_height};
    }
}

void Level_kit_window::on_kit_values_changed()
{
    const Brush_tool* const brush_tool = m_context.brush_tool;
    if ((brush_tool == nullptr) || (m_shape == nullptr) || (brush_tool->get_kit_shape() != m_shape)) {
        return;
    }
    m_place_values  = brush_tool->get_kit_values();
    m_place_message = m_place_values.validate();
}

void Level_kit_window::select_shape(const Kit_shape_definition& shape, const Scene_root& scene_root)
{
    m_shape         = &shape;
    m_place_values  = m_context.level_kits->make_default_values(scene_root, shape);
    m_place_message = m_place_values.validate();
    m_context.level_kits->get_default_slot_materials(shape, m_place_slot_materials);
    queue_kit_brush();
}

void Level_kit_window::queue_kit_brush()
{
    if ((m_shape == nullptr) || !m_place_message.empty()) {
        return;
    }
    // Next frame's prepare phase: a new brush uploads its mesh.
    m_context.imgui_windows->queue(
        [this, values = m_place_values, slot_materials = m_place_slot_materials]() {
            Brush_tool* const brush_tool = m_context.brush_tool;
            if (brush_tool == nullptr) {
                return;
            }
            const std::shared_ptr<Brush> brush = m_context.level_kits->get_brush(values);
            if (!brush) {
                log_scene->warn("Level Kit: the shape made no geometry for {}", values.to_canonical_text());
                return;
            }
            m_context.tools->set_priority_tool(brush_tool);
            brush_tool->set_kit_brush(values, brush, slot_materials);
        }
    );
}

void Level_kit_window::update_place_hint()
{
    m_place_hint_dirty = false;
    if (m_context.commands == nullptr) {
        m_place_hint.clear();
        return;
    }
    const erhe::commands::Commands& commands = *m_context.commands;
    m_place_hint = fmt::format(
        "{} in a viewport places the piece; {} and {} turn it by the rotation step.",
        binding_text(commands, "Brush_tool.insert"),
        binding_text(commands, "Brush_tool.rotate_cw"),
        binding_text(commands, "Brush_tool.rotate_ccw")
    );
}

void Level_kit_window::shapes_imgui(const Scene_root& scene_root)
{
    const Kit_shape_library&          library    = m_context.level_kits->get_library();
    const Kit_shape_definition* const kit_shape  = (m_context.brush_tool != nullptr) ? m_context.brush_tool->get_kit_shape() : nullptr;
    for (const Kit_definition& kit : library.get_kits()) {
        if (!ImGui::CollapsingHeader(kit.display_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        ImGui::PushID(kit.name.c_str());
        for (const Kit_shape_definition& shape : library.get_shapes()) {
            if (shape.kit != kit.name) {
                continue;
            }
            // Shown chosen while the Brush tool places this shape.
            if (ImGui::Selectable(shape.display_name.c_str(), kit_shape == &shape)) {
                select_shape(shape, scene_root);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", shape.id.c_str(), shape.description.c_str());
            }
        }
        ImGui::PopID();
    }
    if (m_place_hint_dirty) {
        update_place_hint();
    }
    ImGui::TextWrapped("%s", m_place_hint.c_str());
}

void Level_kit_window::place_parameters_imgui(const Scene_root& scene_root)
{
    if (m_shape == nullptr) {
        ImGui::TextUnformatted("Choose a shape to place.");
        return;
    }
    ImGui::SeparatorText(m_shape->display_name.c_str());
    ImGui::PushID("place");
    const Kit_row_edit edit = kit_parameter_rows(m_place_values, m_context.level_kits->get_lengths(scene_root));
    if ((edit == Kit_row_edit::changed) || (edit == Kit_row_edit::committed)) {
        // Every valid change reaches the Brush tool at once (D3.6); refused values leave it the brush it has.
        m_place_message = m_place_values.validate();
        queue_kit_brush();
    } else if (edit == Kit_row_edit::cancelled) {
        // A cancelled typed edit reverts its row: the tool shows the reverted values again.
        m_place_message = m_place_values.validate();
        const Brush_tool* const brush_tool = m_context.brush_tool;
        if ((brush_tool != nullptr) && brush_tool->is_kit_mode() && !(brush_tool->get_kit_values() == m_place_values)) {
            queue_kit_brush();
        }
    }
    std::string chosen;
    const std::optional<std::size_t> slot = slot_material_combos(
        m_context.level_kits->get_library(), *m_shape,
        [this](const std::size_t i) -> const std::string& { return m_place_slot_materials[i]; },
        chosen
    );
    if (slot.has_value()) {
        m_place_slot_materials[slot.value()] = chosen;
        queue_kit_brush();
    }
    if (ImGui::Button("Reset To Defaults")) {
        select_shape(*m_shape, scene_root);
    }
    tooltip("The shape's defaults at the scene's module and wall height, and its slots' default materials.");
    ImGui::PopID();
    kit_stairs_line(m_place_values);
    kit_message_line(m_place_message);
}

void Level_kit_window::placement_settings_imgui()
{
    if (m_context.editor_settings == nullptr) {
        return;
    }
    Level_kit_config& config = m_context.editor_settings->level_kit;
    bool changed = false;
    changed = ImGui::Checkbox("Snap To Module", &config.snap_to_module) || changed;
    tooltip("Round the placement position to multiples of the scene's module.");
    surface_settings_checkboxes(*m_context.level_kits);
    changed = ImGui::Checkbox("New Seed Per Piece", &config.new_seed_per_piece) || changed;
    tooltip("After each placed piece the next piece takes the next seed: the values derived from the seed vary.");
    std::array<char, 16> label{};
    const auto write_label = [&label](const int degrees) -> const char* {
        const fmt::format_to_n_result<char*> end = fmt::format_to_n(label.data(), label.size() - 1, "{} deg", degrees);
        *end.out = '\0';
        return label.data();
    };
    const bool step_open = ImGui::BeginCombo("Rotation Step", write_label(config.rotation_step_degrees));
    if (!step_open) {
        // Dear ImGui reports no label for a combo (doc/agents/mcp_ui_driving.md "How an item gets a label").
        erhe::imgui::set_item_debug_label("Rotation Step");
    }
    if (step_open) {
        for (const int step : c_rotation_steps) {
            if (ImGui::Selectable(write_label(step), config.rotation_step_degrees == step) && (config.rotation_step_degrees != step)) {
                config.rotation_step_degrees = step;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    tooltip("The angle the Brush tool's rotate commands turn the piece about the up axis.");
    if (changed && (m_context.app_settings != nullptr)) {
        m_context.app_settings->settings_store().touch();
    }
}

auto Level_kit_window::find_selected_piece(const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Mesh>
{
    const auto is_piece = [&scene_root](const std::shared_ptr<erhe::Item_base>& item) -> bool {
        return item && (item->get_item_host() == &scene_root) &&
            erhe::utility::test_all_rhs_bits_set(item->get_type(), erhe::scene::Mesh::get_static_type()) &&
            carries_kit_piece(static_cast<const erhe::scene::Mesh&>(*item));
    };
    const std::shared_ptr<erhe::Item_base> active = m_context.selection->get_active_item();
    if (is_piece(active) && active->is_selected()) {
        return std::static_pointer_cast<erhe::scene::Mesh>(active);
    }
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (is_piece(item)) {
            return std::static_pointer_cast<erhe::scene::Mesh>(item);
        }
    }
    return {};
}

auto Level_kit_window::count_selected_meshes(const Scene_root& scene_root) const -> std::size_t
{
    std::size_t count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (item && (item->get_item_host() == &scene_root) && erhe::utility::test_all_rhs_bits_set(item->get_type(), erhe::scene::Mesh::get_static_type())) {
            ++count;
        }
    }
    return count;
}

void Level_kit_window::selection_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const std::shared_ptr<erhe::scene::Mesh> piece = find_selected_piece(*scene_root);
    if (piece) {
        // Read again only when the selection or a mesh changed (messages) or another piece is chosen.
        m_piece_editor.update(piece);
        ImGui::Text("Piece: %s (%s)", piece->get_name().c_str(), m_piece_editor.get_shape_text().c_str());
        if (!m_piece_editor.get_problem().empty()) {
            kit_message_line(m_piece_editor.get_problem());
        } else {
            ImGui::PushID("piece");
            m_piece_editor.on_edit(kit_parameter_rows(m_piece_editor.get_values(), m_piece_editor.get_lengths()));
            const Kit_shape_definition* const shape = m_piece_editor.get_shape();
            if ((shape != nullptr) && (piece->get_primitives().size() == shape->material_slots.size())) {
                static const std::string no_material{"(none)"};
                std::string chosen;
                const std::optional<std::size_t> slot = slot_material_combos(
                    m_context.level_kits->get_library(), *shape,
                    [&piece](const std::size_t i) -> const std::string& {
                        const std::shared_ptr<erhe::primitive::Material>& material = piece->get_primitives()[i].material;
                        return material ? material->get_name() : no_material;
                    },
                    chosen
                );
                if (slot.has_value()) {
                    queue_slot_material(scene_root, piece, slot.value(), chosen);
                }
            }
            ImGui::PopID();
            kit_stairs_line(m_piece_editor.get_values());
            kit_message_line(m_piece_editor.get_message());
        }
    } else {
        ImGui::TextUnformatted("No kit piece selected.");
    }

    const std::size_t mesh_count = count_selected_meshes(*scene_root);
    ImGui::BeginDisabled(mesh_count == 0);
    if (ImGui::Button("Snap Selected To Surface")) {
        queue_snap_selected(scene_root, Surface_orient::off);
    }
    tooltip("Moves the selected meshes of this scene down onto the surface under them, one undo step.");
    if (ImGui::Button("Orient Selected To Surface")) {
        queue_snap_selected(scene_root, Surface_orient::on);
    }
    tooltip("Rests the selected meshes of this scene on the surface under them with their up axis on its normal, one undo step.");
    ImGui::EndDisabled();
}

void Level_kit_window::queue_slot_material(
    const std::shared_ptr<Scene_root>&        scene_root,
    const std::shared_ptr<erhe::scene::Mesh>& piece,
    const std::size_t                         slot,
    const std::string&                        palette_name
)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_piece = std::weak_ptr<erhe::scene::Mesh>{piece}, slot, palette_name]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Mesh> mesh   = weak_piece.lock();
            if (!target || !mesh || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_set_material_result result = m_context.level_kits->set_slot_material(
                *target, mesh, slot, Kit_slot_material{.material = {}, .palette_name = palette_name}
            );
            if (!result.error.empty()) {
                log_scene->warn("Level Kit: material of '{}' slot {} not set: {}", mesh->get_name(), slot, result.error);
            }
        }
    );
}

void Level_kit_window::queue_snap_selected(const std::shared_ptr<Scene_root>& scene_root, const Surface_orient orient)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, orient]() {
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            std::vector<std::shared_ptr<erhe::scene::Mesh>> meshes;
            for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
                if (item && (item->get_item_host() == target.get()) && erhe::utility::test_all_rhs_bits_set(item->get_type(), erhe::scene::Mesh::get_static_type())) {
                    meshes.push_back(std::static_pointer_cast<erhe::scene::Mesh>(item));
                }
            }
            Surface_options options;
            options.snap   = Surface_snap::on;
            options.orient = orient;
            std::vector<Kit_snap_result> results;
            static_cast<void>(m_context.level_kits->snap_to_surface(*target, meshes, options, Kit_snap_apply::move, results));
        }
    );
}

void Level_kit_window::layout_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!ImGui::CollapsingHeader("Layout", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    m_layout_editor.imgui(scene_root, find_selected_piece(*scene_root));
}

auto Level_kit_window::count_selected_nodes(const Scene_root& scene_root) const -> std::size_t
{
    std::size_t count = 0;
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (item && (item->get_item_host() == &scene_root) && (dynamic_cast<const erhe::scene::Node*>(item.get()) != nullptr)) {
            ++count;
        }
    }
    return count;
}

auto Level_kit_window::find_selected_group(const Scene_root& scene_root) const -> std::shared_ptr<erhe::scene::Node>
{
    const auto is_group = [&scene_root](const std::shared_ptr<erhe::Item_base>& item) -> bool {
        return item && (item->get_item_host() == &scene_root) && (dynamic_cast<const erhe::scene::Xform*>(item.get()) != nullptr);
    };
    const std::shared_ptr<erhe::Item_base> active = m_context.selection->get_active_item();
    if (is_group(active) && active->is_selected()) {
        return std::dynamic_pointer_cast<erhe::scene::Node>(active);
    }
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (is_group(item)) {
            return std::dynamic_pointer_cast<erhe::scene::Node>(item);
        }
    }
    return {};
}

void Level_kit_window::group_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!ImGui::CollapsingHeader("Group", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::PushID("group");
    ImGui::InputText("Group Name", &m_group_name);
    tooltip("The name of the next group node Group Selected makes.");
    const std::size_t node_count = count_selected_nodes(*scene_root);
    ImGui::BeginDisabled(node_count == 0);
    if (ImGui::Button("Group Selected")) {
        queue_group_action(scene_root, Group_action::group, {});
    }
    ImGui::EndDisabled();
    tooltip("Puts the selected nodes of this scene under a new group node at the base center of their bounds, world transforms kept, one undo step.");

    const std::shared_ptr<erhe::scene::Node> group = find_selected_group(*scene_root);
    if (!group) {
        ImGui::TextUnformatted("Select a group node to ungroup, duplicate or mirror it.");
    } else {
        // The offset takes the group's width when another group becomes the selected one or a mesh changes (messages).
        if (m_group_selection_changed) {
            m_group_selection_changed = false;
            if (m_duplicate_source.lock() != group) {
                m_duplicate_source = group;
                const erhe::math::Aabb bounds = Level_kits::get_subtree_bounds(*group);
                if (bounds.is_valid_3d() && ((bounds.max.x - bounds.min.x) > 0.0f)) {
                    m_duplicate_offset = glm::vec3{bounds.max.x - bounds.min.x, 0.0f, 0.0f};
                }
            }
        }
        ImGui::Text("Group: %s (%zu children)", group->get_name().c_str(), group->get_child_count());
        if (ImGui::Button("Ungroup")) {
            queue_group_action(scene_root, Group_action::ungroup, group);
        }
        tooltip("Moves the group's children to its parent, world transforms kept, and removes the group node, one undo step.");
        ImGui::DragFloat3("Duplicate Offset", &m_duplicate_offset.x, 0.01f, -1000.0f, 1000.0f, "%.3f m");
        tooltip("The world offset of the copy; by default the group's width along x.");
        if (ImGui::Button("Duplicate")) {
            queue_group_action(scene_root, Group_action::duplicate, group);
        }
        tooltip("Copies the group with everything below it, every piece a full kit piece, one undo step; selects the copy.");
        if (ImGui::Button("Mirror X")) {
            queue_group_action(scene_root, Group_action::mirror_x, group);
        }
        tooltip("Reflects the group's content across the plane through its origin normal to its x axis, one undo step.");
        ImGui::SameLine();
        if (ImGui::Button("Mirror Z")) {
            queue_group_action(scene_root, Group_action::mirror_z, group);
        }
        tooltip("Reflects the group's content across the plane through its origin normal to its z axis, one undo step.");
    }
    ImGui::PopID();
    if (m_group_failed) {
        kit_message_line(m_group_message);
    } else if (!m_group_message.empty()) {
        ImGui::TextWrapped("%s", m_group_message.c_str());
    }
}

void Level_kit_window::queue_group_action(const std::shared_ptr<Scene_root>& scene_root, const Group_action action, const std::shared_ptr<erhe::scene::Node>& group)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_group = std::weak_ptr<erhe::scene::Node>{group}, action,
         name = m_group_name, offset = m_duplicate_offset]() {
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            Level_kits& level_kits = *m_context.level_kits;
            const std::shared_ptr<erhe::scene::Node> node = weak_group.lock();
            if ((action != Group_action::group) && !node) {
                return;
            }
            switch (action) {
                case Group_action::group: {
                    std::vector<std::shared_ptr<erhe::scene::Node>> nodes;
                    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
                        if (item && (item->get_item_host() == target.get()) && (dynamic_cast<const erhe::scene::Node*>(item.get()) != nullptr)) {
                            nodes.push_back(std::dynamic_pointer_cast<erhe::scene::Node>(item));
                        }
                    }
                    const Kit_group_result result = level_kits.group(*target, nodes, name);
                    m_group_failed  = !result.error.empty();
                    m_group_message = m_group_failed ? result.error : fmt::format("Grouped {} nodes into '{}'.", result.node->get_child_count(), result.node->get_name());
                    if (!m_group_failed) {
                        m_context.selection->set_selection({result.node}, result.node);
                    }
                    break;
                }
                case Group_action::ungroup: {
                    const Kit_ungroup_result result = level_kits.ungroup(*target, node);
                    m_group_failed  = !result.error.empty();
                    m_group_message = m_group_failed ? result.error : fmt::format("Moved {} children to '{}'.", result.children.size(), result.parent->get_name());
                    break;
                }
                case Group_action::duplicate: {
                    const Kit_group_result result = level_kits.duplicate(*target, node, Kit_group_copy{.placement = Kit_copy_placement::offset, .value = offset, .name = {}});
                    m_group_failed  = !result.error.empty();
                    m_group_message = m_group_failed ? result.error : fmt::format("Copied '{}' as '{}'.", node->get_name(), result.node->get_name());
                    if (!m_group_failed) {
                        m_context.selection->set_selection({result.node}, result.node);
                    }
                    break;
                }
                case Group_action::mirror_x:
                case Group_action::mirror_z: {
                    const Mirror_axis axis = (action == Group_action::mirror_x) ? Mirror_axis::x : Mirror_axis::z;
                    const Kit_mirror_result result = level_kits.mirror(*target, node, axis);
                    m_group_failed = !result.error.empty();
                    if (m_group_failed) {
                        m_group_message = result.error;
                        break;
                    }
                    m_group_message = fmt::format("Mirrored {} nodes across {}.", result.nodes.size(), c_str(axis));
                    if (result.not_exact_count > 0) {
                        m_group_message += fmt::format(" Not an exact mirror image ({} without a symmetry plane):", result.not_exact_count);
                        for (const Kit_mirrored_node& mirrored : result.nodes) {
                            if (mirrored.image == Mirror_image::not_exact) {
                                m_group_message += " " + mirrored.node->get_name();
                            }
                        }
                    }
                    break;
                }
                default: break;
            }
        }
    );
}

void Level_kit_window::collect_selected_meshes(const Scene_root& scene_root, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes) const
{
    out_meshes.clear();
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        if (item && (item->get_item_host() == &scene_root) && erhe::utility::test_all_rhs_bits_set(item->get_type(), erhe::scene::Mesh::get_static_type())) {
            out_meshes.push_back(std::static_pointer_cast<erhe::scene::Mesh>(item));
        }
    }
}

void Level_kit_window::spatial_queries_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    if (!ImGui::CollapsingHeader("Spatial Queries", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const bool has_selection = count_selected_meshes(*scene_root) > 0;
    if (ImGui::Button("Support Report")) {
        queue_spatial_query(scene_root, Spatial_query::support_report);
    }
    tooltip("Lists the selected meshes, or every kit piece without a selection, that rest on nothing (no surface within 1 cm under the base).");
    if (!m_support_summary.empty()) {
        ImGui::TextUnformatted(m_support_summary.c_str());
        if (!m_unsupported.empty()) {
            if (ImGui::Button("Select Unsupported")) {
                select_meshes(m_unsupported);
            }
            query_entries_imgui("unsupported", m_unsupported);
        }
    }

    ImGui::BeginDisabled(!has_selection);
    if (ImGui::Button("Region Contents")) {
        queue_spatial_query(scene_root, Spatial_query::region_contents);
    }
    tooltip("Lists the meshes whose bounds intersect the bounds of the selected meshes.");
    ImGui::SameLine();
    if (ImGui::Button("Height Map")) {
        queue_spatial_query(scene_root, Spatial_query::height_map);
    }
    tooltip("Top surface heights over the selected meshes' bounds and one module around them, one cell per module: digits 0 (lowest) to 9 (highest), '.' without a surface.");
    ImGui::EndDisabled();
    if (!m_region_summary.empty()) {
        ImGui::TextUnformatted(m_region_summary.c_str());
        query_entries_imgui("region", m_region_meshes);
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Exclude Tags", &m_exclude_tags_text);
    tooltip("Comma separated tags: pieces carrying one take no part in the overlap report and in the level check's support and overlap parts.");
    if (ImGui::Button("Overlap Report")) {
        queue_spatial_query(scene_root, Spatial_query::overlap_report);
    }
    tooltip("Lists pairs of the selected meshes, or of every kit piece without a selection, that interpenetrate by more than 1 cm, with the depth.");
    ImGui::SameLine();
    if (ImGui::Button("Level Check")) {
        queue_spatial_query(scene_root, Spatial_query::level_check);
    }
    tooltip("Over every kit piece: unsupported pieces, overlaps, unreachable navigation islands over 1 m2 and walkable shapes whose parameter values the agent cannot walk.");
    if (!m_overlap_summary.empty()) {
        ImGui::TextUnformatted(m_overlap_summary.c_str());
        query_rows_imgui("overlaps", m_overlap_rows);
    }
    if (!m_check_summary.empty()) {
        ImGui::TextWrapped("%s", m_check_summary.c_str());
        query_rows_imgui("findings", m_check_rows);
    }
    if (!m_height_map_summary.empty()) {
        ImGui::TextWrapped("%s", m_height_map_summary.c_str());
        // The pictures are character grids: monospace keeps the columns aligned.
        ImFont* const mono_font = m_imgui_renderer.mono_font();
        if (!m_height_map_text.empty()) {
            if (mono_font != nullptr) {
                ImGui::PushFont(mono_font, 0.0f);
            }
            ImGui::TextUnformatted(m_height_map_text.c_str());
            if (mono_font != nullptr) {
                ImGui::PopFont();
            }
        }
        if (!m_island_text.empty()) {
            ImGui::TextUnformatted("Islands (# not walkable):");
            if (mono_font != nullptr) {
                ImGui::PushFont(mono_font, 0.0f);
            }
            ImGui::TextUnformatted(m_island_text.c_str());
            if (mono_font != nullptr) {
                ImGui::PopFont();
            }
        }
    }
}

void Level_kit_window::query_entries_imgui(const char* id, const std::vector<std::weak_ptr<erhe::scene::Mesh>>& meshes)
{
    ImGui::PushID(id);
    for (std::size_t i = 0, end = meshes.size(); i < end; ++i) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = meshes[i].lock();
        ImGui::PushID(static_cast<int>(i));
        if (!mesh) {
            ImGui::TextDisabled("(removed)");
        } else if (ImGui::Selectable(mesh->get_name().c_str(), mesh->is_selected())) {
            m_context.selection->set_selection({mesh});
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

void Level_kit_window::query_rows_imgui(const char* id, const std::vector<Spatial_query_row>& rows)
{
    ImGui::PushID(id);
    for (std::size_t i = 0, end = rows.size(); i < end; ++i) {
        const Spatial_query_row& row = rows[i];
        bool removed  = false;
        bool selected = row.mesh_count > 0;
        for (std::size_t j = 0; j < row.mesh_count; ++j) {
            const std::shared_ptr<erhe::scene::Mesh> mesh = row.meshes[j].lock();
            removed  = removed || !mesh;
            selected = selected && mesh && mesh->is_selected();
        }
        ImGui::PushID(static_cast<int>(i));
        if (removed) {
            ImGui::TextDisabled("(removed) %s", row.label.c_str());
        } else if (ImGui::Selectable(row.label.c_str(), selected) && (row.mesh_count > 0)) {
            std::vector<std::shared_ptr<erhe::Item_base>> items;
            for (std::size_t j = 0; j < row.mesh_count; ++j) {
                items.push_back(row.meshes[j].lock());
            }
            m_context.selection->set_selection(items);
        }
        ImGui::PopID();
    }
    ImGui::PopID();
}

void Level_kit_window::select_meshes(const std::vector<std::weak_ptr<erhe::scene::Mesh>>& meshes)
{
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    for (const std::weak_ptr<erhe::scene::Mesh>& weak_mesh : meshes) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = weak_mesh.lock();
        if (mesh) {
            items.push_back(mesh);
        }
    }
    m_context.selection->set_selection(items);
}

void Level_kit_window::queue_spatial_query(const std::shared_ptr<Scene_root>& scene_root, const Spatial_query query)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, query]() {
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            switch (query) {
                case Spatial_query::support_report:  run_support_report (*target); break;
                case Spatial_query::region_contents: run_region_contents(*target); break;
                case Spatial_query::height_map:      run_height_map     (*target); break;
                case Spatial_query::overlap_report:  run_overlap_report (*target); break;
                case Spatial_query::level_check:     run_level_check    (*target); break;
                default: break;
            }
            // The results hold the meshes weakly only.
            m_query_meshes.clear();
            m_support_results.clear();
            m_overlap_report.overlaps.clear();
            m_level_check.findings.clear();
            m_level_check.pieces.clear();
        }
    );
}

void Level_kit_window::run_support_report(Scene_root& scene_root)
{
    collect_selected_meshes(scene_root, m_query_meshes);
    const bool selected = !m_query_meshes.empty();
    if (!selected) {
        get_kit_piece_meshes(scene_root, m_query_meshes);
    }
    find_support(scene_root, m_query_meshes, c_default_support_tolerance, m_support_results);
    m_unsupported.clear();
    for (const Support_result& result : m_support_results) {
        if (!result.supported) {
            m_unsupported.push_back(result.mesh);
        }
    }
    m_support_summary = fmt::format(
        "{} checked: {}, unsupported: {}", selected ? "Selected meshes" : "Kit pieces", m_support_results.size(), m_unsupported.size()
    );
}

void Level_kit_window::run_region_contents(Scene_root& scene_root)
{
    collect_selected_meshes(scene_root, m_query_meshes);
    erhe::math::Aabb bounds{};
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_query_meshes) {
        const erhe::math::Aabb mesh_bounds = mesh->get_aabb_world();
        if (mesh_bounds.is_valid_3d()) {
            bounds.include(mesh_bounds);
        }
    }
    m_region_meshes.clear();
    if (!bounds.is_valid_3d()) {
        m_region_summary = "The selected meshes have no bounds.";
        return;
    }
    const Level_region region{.shape = Level_region_shape::box, .min = bounds.min, .max = bounds.max};
    find_region_contents(scene_root, region, m_query_meshes);
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_query_meshes) {
        m_region_meshes.push_back(mesh);
    }
    m_region_summary = fmt::format(
        "Box ({:.2f}, {:.2f}, {:.2f}) - ({:.2f}, {:.2f}, {:.2f}): {} meshes",
        bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z, m_region_meshes.size()
    );
}

void Level_kit_window::run_height_map(Scene_root& scene_root)
{
    collect_selected_meshes(scene_root, m_query_meshes);
    erhe::math::Aabb bounds{};
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_query_meshes) {
        const erhe::math::Aabb mesh_bounds = mesh->get_aabb_world();
        if (mesh_bounds.is_valid_3d()) {
            bounds.include(mesh_bounds);
        }
    }
    m_height_map_text.clear();
    m_island_text.clear();
    if (!bounds.is_valid_3d()) {
        m_height_map_summary = "The selected meshes have no bounds.";
        return;
    }
    // The bounds grown by one module on every side: the rays start above them and end below.
    const float           module = m_context.level_kits->get_lengths(scene_root).module;
    const glm::vec3       margin{module};
    const Height_map_grid grid   = make_height_map_grid(bounds.min - margin, bounds.max + margin, module);
    if ((grid.columns * grid.rows) > c_max_height_map_cells) {
        m_height_map_summary = fmt::format(
            "{} x {} cells exceed the limit of {}: select a smaller area.", grid.columns, grid.rows, c_max_height_map_cells
        );
        return;
    }
    make_height_map(scene_root, grid, m_height_map);
    const Height_range range = get_height_range(m_height_map.cells);
    write_height_text(grid, m_height_map.cells, m_height_map_text);
    if (m_height_map.navigation_available) {
        write_island_text(grid, m_height_map.cells, m_island_text);
    }
    m_height_map_summary = range.valid
        ? fmt::format(
            "{} x {} cells of {:.2f} m, rows from z {:.2f}, columns from x {:.2f}; 0 = {:.2f} m, 9 = {:.2f} m{}",
            grid.columns, grid.rows, module, grid.min.z, grid.min.x, range.min, range.max,
            m_height_map.navigation_stale ? "; navigation mesh stale" : ""
        )
        : fmt::format("{} x {} cells of {:.2f} m: no surface", grid.columns, grid.rows, module);
}

namespace {

void read_exclude_tags(const std::string& text, std::vector<std::string>& out_tags)
{
    std::set<std::string> tags;
    erhe::Item_base::tags_from_string(text, tags);
    out_tags.assign(tags.begin(), tags.end());
}

} // anonymous namespace

void Level_kit_window::run_overlap_report(Scene_root& scene_root)
{
    collect_selected_meshes(scene_root, m_query_meshes);
    const bool selected = !m_query_meshes.empty();
    if (!selected) {
        get_kit_piece_meshes(scene_root, m_query_meshes);
    }
    read_exclude_tags(m_exclude_tags_text, m_exclude_tags);
    find_overlaps(m_query_meshes, c_default_overlap_tolerance, m_exclude_tags, m_overlap_report);
    m_overlap_rows.clear();
    for (const Overlap_result& overlap : m_overlap_report.overlaps) {
        m_overlap_rows.push_back(Spatial_query_row{
            .label      = fmt::format(
                "{} - {}: {:.3f} m{}", overlap.mesh_a->get_name(), overlap.mesh_b->get_name(), overlap.depth,
                (overlap.measure == Overlap_measure::exact) ? "" : fmt::format(" ({})", c_str(overlap.measure))
            ),
            .meshes     = {overlap.mesh_a, overlap.mesh_b},
            .mesh_count = 2
        });
    }
    m_overlap_summary = fmt::format(
        "{} tested: {} of {}, pairs by bounds: {}, overlapping: {}",
        selected ? "Selected meshes" : "Kit pieces", m_overlap_report.tested_count, m_query_meshes.size(),
        m_overlap_report.bounds_pair_count, m_overlap_rows.size()
    );
}

void Level_kit_window::run_level_check(Scene_root& scene_root)
{
    read_exclude_tags(m_exclude_tags_text, m_exclude_tags);
    const Level_check_options options{.exclude_tags = m_exclude_tags};
    editor::run_level_check(scene_root, m_context.level_kits->get_library(), options, m_level_check);
    m_check_rows.clear();
    for (const Level_finding& finding : m_level_check.findings) {
        m_check_rows.push_back(Spatial_query_row{
            .label      = fmt::format("{}: {}", c_str(finding.kind), finding.detail),
            .meshes     = {finding.meshes[0], finding.meshes[1]},
            .mesh_count = finding.mesh_count
        });
    }
    m_check_summary = fmt::format(
        "Kit pieces: {} ({} excluded), findings: {}; {}",
        m_level_check.piece_count, m_level_check.excluded_count, m_check_rows.size(),
        !m_level_check.navigation_available
            ? "no navigation mesh: islands not checked"
            : (m_level_check.navigation_stale ? "navigation mesh stale" : "navigation mesh current")
    );
}

} // namespace editor
