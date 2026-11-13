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
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <optional>
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
        }
    );
    m_active_item_subscription = app_message_bus.active_item.subscribe(
        [this](Active_item_changed_message&) {
            m_piece_editor.mark_dirty();
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
    queue_kit_brush();
}

void Level_kit_window::queue_kit_brush()
{
    if ((m_shape == nullptr) || !m_place_message.empty()) {
        return;
    }
    // Next frame's prepare phase: a new brush uploads its mesh.
    m_context.imgui_windows->queue(
        [this, values = m_place_values]() {
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
            brush_tool->set_kit_brush(values, brush);
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
    if (ImGui::Button("Reset To Defaults")) {
        select_shape(*m_shape, scene_root);
    }
    tooltip("The shape's defaults at the scene's module and wall height.");
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

} // namespace editor
