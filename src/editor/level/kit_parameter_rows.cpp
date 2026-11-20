#include "level/kit_parameter_rows.hpp"

#include "level/kit_piece.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"
#include "level/level_kits.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>

#include <array>
#include <imgui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <optional>

namespace editor {

namespace {

[[nodiscard]] auto float_format(const Kit_unit unit) -> const char*
{
    switch (unit) {
        case Kit_unit::meters:  return "%.3f m";
        case Kit_unit::degrees: return "%.1f deg";
        case Kit_unit::count:   return "%.0f";
        case Kit_unit::ratio:   return "%.2f";
        case Kit_unit::none:
        default:                return "%.3f";
    }
}

[[nodiscard]] auto item_edit() -> Kit_row_edit
{
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        return Kit_row_edit::committed;
    }
    if (ImGui::IsItemDeactivated()) {
        return Kit_row_edit::cancelled;
    }
    return Kit_row_edit::none;
}

} // anonymous namespace

auto merge(const Kit_row_edit lhs, const Kit_row_edit rhs) -> Kit_row_edit
{
    return (static_cast<unsigned int>(lhs) > static_cast<unsigned int>(rhs)) ? lhs : rhs;
}

namespace {

void from_seed_tooltip(const Kit_parameter& parameter, const Kit_lengths& lengths)
{
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    switch (parameter.type) {
        case Kit_parameter_type::float_value: {
            const Kit_derive_range range = parameter.resolve_derive_range(lengths.module, lengths.wall_height);
            ImGui::SetTooltip("Derive from seed (range %g - %g %s)", range.min_value, range.max_value, c_str(parameter.unit));
            break;
        }
        case Kit_parameter_type::int_value: {
            ImGui::SetTooltip("Derive from seed (range %d - %d)", parameter.derive_int_min, parameter.derive_int_max);
            break;
        }
        case Kit_parameter_type::enum_value:
        default: {
            ImGui::SetTooltip("Derive from seed (any of its values)");
            break;
        }
    }
}

// Names an item of the row: by role inside a recorded row (Properties), else '<name>.<role>'.
void name_row_item(const Kit_parameter& parameter, const Kit_row_label label, const char* role)
{
    if (label == Kit_row_label::recorded) {
        erhe::imgui::set_item_debug_role(role);
    } else if (erhe::imgui::is_item_recording()) {
        erhe::imgui::set_item_debug_label(fmt::format("{}.{}", parameter.name, role));
    }
}

} // anonymous namespace

auto kit_parameter_row(Kit_parameter_values& values, const std::size_t index, const Kit_row_label label, const Kit_lengths& lengths) -> Kit_row_edit
{
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || (index >= shape->parameters.size())) {
        return Kit_row_edit::none;
    }
    const Kit_parameter& parameter = shape->parameters[index];
    Kit_parameter_value  value     = values.get_values()[index];
    const bool  beside      = (label == Kit_row_label::beside);
    const bool  is_seed     = (parameter.name == "seed");
    const bool  masked      = (values.get_value_source(index) == Kit_value_source::seed);
    const char* widget_name = beside ? parameter.name.c_str() : "##value";
    ImGui::PushID(parameter.name.c_str());
    Kit_row_edit edit    = Kit_row_edit::none;
    bool         changed = false;

    // The toggle is one edit of its own (D4.5): a set_parameters() with the new mask.
    Kit_row_edit toggle_edit = Kit_row_edit::none;
    if (parameter.derive == Kit_derive::derivable) {
        bool from_seed = masked;
        if (ImGui::Checkbox("##from_seed", &from_seed)) {
            values.set_value_source(index, from_seed ? Kit_value_source::seed : Kit_value_source::explicit_value);
            values.resolve_derived(lengths.module, lengths.wall_height);
            toggle_edit = Kit_row_edit::committed;
        }
        name_row_item(parameter, label, "from_seed");
        from_seed_tooltip(parameter, lengths);
        ImGui::SameLine();
    }
    if (!beside) {
        ImGui::SetNextItemWidth(is_seed ? -(ImGui::CalcTextSize("New").x + (3.0f * ImGui::GetStyle().FramePadding.x) + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
    }
    ImGui::BeginDisabled(masked);
    switch (parameter.type) {
        case Kit_parameter_type::float_value: {
            const float speed = std::max(0.001f, (parameter.float_max - parameter.float_min) / 1000.0f);
            // No AlwaysClamp: a drag stays in range, a typed value outside it shows validate()'s message.
            changed = ImGui::DragFloat(
                widget_name, &value.float_value, speed, parameter.float_min, parameter.float_max,
                float_format(parameter.unit)
            );
            edit = item_edit();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s\nRange %g - %g %s", parameter.description.c_str(), parameter.float_min, parameter.float_max, c_str(parameter.unit));
            }
            break;
        }
        case Kit_parameter_type::int_value: {
            changed = ImGui::DragInt(
                widget_name, &value.int_value, 0.1f, parameter.int_min, parameter.int_max, "%d"
            );
            edit = item_edit();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s\nRange %d - %d", parameter.description.c_str(), parameter.int_min, parameter.int_max);
            }
            break;
        }
        case Kit_parameter_type::enum_value: {
            const std::size_t count   = parameter.enum_values.size();
            const bool        valid   = (value.int_value >= 0) && (static_cast<std::size_t>(value.int_value) < count);
            const char*       preview = valid ? parameter.enum_values[static_cast<std::size_t>(value.int_value)].c_str() : "";
            const bool        open    = ImGui::BeginCombo(widget_name, preview);
            if (!open) {
                // Dear ImGui reports no label for a combo (doc/agents/mcp_ui_driving.md "How an item gets a label").
                erhe::imgui::set_item_debug_label(parameter.name);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", parameter.description.c_str());
                }
            } else {
                for (std::size_t j = 0; j < count; ++j) {
                    const bool is_selected = (static_cast<std::size_t>(value.int_value) == j);
                    if (ImGui::Selectable(parameter.enum_values[j].c_str(), is_selected) && !is_selected) {
                        value.int_value = static_cast<int>(j);
                        changed = true;
                        edit    = Kit_row_edit::committed;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        default: {
            break;
        }
    }
    if (!beside && (parameter.type != Kit_parameter_type::enum_value)) {
        erhe::imgui::set_item_debug_label(parameter.name);
    }
    ImGui::EndDisabled();
    if (changed) {
        values.set_value(index, value);
        edit = merge(edit, Kit_row_edit::changed);
    }
    if (is_seed) {
        ImGui::SameLine();
        if (ImGui::SmallButton("New")) {
            value.int_value = kit_next_seed(values.get_seed());
            values.set_value(index, value);
            changed = true;
            edit    = merge(edit, Kit_row_edit::committed);
        }
        name_row_item(parameter, label, "new");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Take the next seed: the values derived from the seed change with it");
        }
    }
    if (changed && is_seed) {
        // Every masked value follows the seed, live during a drag like any other row (D3.6).
        values.resolve_derived(lengths.module, lengths.wall_height);
    }
    ImGui::PopID();
    return merge(edit, toggle_edit);
}

auto kit_parameter_rows(Kit_parameter_values& values, const Kit_lengths& lengths) -> Kit_row_edit
{
    const Kit_shape_definition* const shape = values.get_shape();
    if (shape == nullptr) {
        return Kit_row_edit::none;
    }
    Kit_row_edit edit = Kit_row_edit::none;
    for (std::size_t i = 0, end = shape->parameters.size(); i < end; ++i) {
        edit = merge(edit, kit_parameter_row(values, i, Kit_row_label::beside, lengths));
    }
    return edit;
}

void kit_stairs_line(const Kit_parameter_values& values)
{
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || (shape->id != "basic.stairs")) {
        return;
    }
    const Kit_stairs_layout layout = get_stairs_layout(values);
    ImGui::Text("%d steps, step rise %.3f m, tread depth %.3f m", layout.step_count, layout.step_rise, layout.tread_depth);
}

void kit_message_line(const std::string& message)
{
    if (!message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.35f, 0.35f, 1.0f});
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::PopStyleColor();
    }
}

Kit_piece_editor::Kit_piece_editor(App_context& context)
    : m_context        {context}
    , m_connection_rows{context}
{
}

void Kit_piece_editor::mark_dirty()
{
    m_dirty = true;
}

void Kit_piece_editor::update(const std::shared_ptr<erhe::scene::Mesh>& piece)
{
    const bool same_piece = (piece != nullptr) && (piece->get_id() == m_piece_id);
    if (!piece || (same_piece && (!m_dirty || m_context.level_kits->is_previewing(*piece)))) {
        return;
    }
    m_piece    = piece;
    m_piece_id = piece->get_id();
    m_dirty    = false;
    m_message.clear();
    const std::optional<Kit_piece_data> record = read_kit_piece(*piece);
    m_problem = record.has_value()
        ? m_context.level_kits->read_piece_values(record.value(), m_values)
        : std::string{"not a kit piece"};
    const Kit_shape_definition* const shape = record.has_value() ? m_context.level_kits->get_library().find(record.value().shape) : nullptr;
    // Scene items are hosted by their Scene_root.
    const Scene_root* const scene_root = static_cast<const Scene_root*>(piece->get_item_host());
    m_lengths = (scene_root != nullptr) ? m_context.level_kits->get_lengths(*scene_root) : Kit_lengths{};
    m_shape_text = (shape != nullptr) ? shape->display_name : (record.has_value() ? record.value().shape : std::string{});
    get_shape_connectors(m_values, m_connectors);
    if (!m_problem.empty()) {
        m_connectors.clear();
    }
    m_connector_switches = read_piece_connector_switches(*piece);
    // C6.3: the connections, both ends' records, each with its status (read when the piece is read; a cold path).
    m_connection_rows.read(piece);
}

Kit_connection_rows::Kit_connection_rows(App_context& context)
    : m_context{context}
{
}

void Kit_connection_rows::update(const std::shared_ptr<erhe::scene::Node>& node)
{
    if (node && (node->get_id() == m_node_id) && !m_dirty) {
        return;
    }
    read(node);
}

void Kit_connection_rows::mark_dirty()
{
    m_dirty = true;
}

void Kit_connection_rows::read(const std::shared_ptr<erhe::scene::Node>& node)
{
    m_node    = node;
    m_node_id = node ? node->get_id() : 0;
    m_dirty   = false;
    m_rows.clear();
    // Scene items are hosted by their Scene_root.
    const Scene_root* const scene_root = node ? static_cast<const Scene_root*>(node->get_item_host()) : nullptr;
    if (scene_root == nullptr) {
        return;
    }
    std::vector<Kit_attachment_status> statuses;
    const auto add_row = [this](const std::string& connector, const std::string& other_name, const Kit_attachment_status& status, const std::string& other_connector) {
        const Kit_attachment& record = status.attachment;
        std::string text = fmt::format(
            "'{}' {}{}{}", other_name, other_connector,
            (record.twist_steps != 0) ? fmt::format(", twist {}", record.twist_steps) : std::string{},
            (record.gap != 0.0f) ? fmt::format(", gap {:.3f} m", record.gap) : std::string{}
        );
        if (status.state == Kit_attachment_state::dormant) {
            text += fmt::format(" - not followed: {}", status.reason);
        }
        m_rows.push_back(Row{connector, std::move(text)});
    };
    m_context.level_kits->get_attachment_statuses(*node, statuses);
    for (const Kit_attachment_status& own : statuses) {
        add_row(own.attachment.connector, own.target ? own.target->get_name() : std::string{"(missing)"}, own, own.attachment.target_connector);
    }
    const std::string uid = node->get_gltf_uid();
    scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& other) -> bool {
        if ((other.get() != node.get()) && other->has_local_value(Kit_piece::attachment_property.get())) {
            m_context.level_kits->get_attachment_statuses(*other, statuses);
            for (const Kit_attachment_status& record : statuses) {
                if (record.attachment.target == uid) {
                    add_row(record.attachment.target_connector, other->get_name(), record, record.attachment.connector);
                }
            }
        }
        return true;
    });
}

auto Kit_connection_rows::get_rows() const -> const std::vector<Row>&
{
    return m_rows;
}

void Kit_piece_editor::on_edit(const Kit_row_edit edit)
{
    if (edit == Kit_row_edit::none) {
        return;
    }
    enum class Step : unsigned int { preview, set, cancel };
    Step step = Step::cancel;
    if (edit == Kit_row_edit::cancelled) {
        m_dirty = true; // a cancelled typed edit reverts its row; read the record again
    } else {
        m_message = m_values.validate();
        if (edit == Kit_row_edit::changed) {
            if (!m_message.empty()) {
                return; // refused values: the piece keeps the last valid preview
            }
            step = Step::preview;
        } else if (m_message.empty()) {
            step = Step::set;
        }
    }
    // Next frame's prepare phase: the brush of new values uploads its mesh.
    m_context.imgui_windows->queue(
        [this, step, weak_piece = m_piece, values = m_values]() {
            const std::shared_ptr<erhe::scene::Mesh> mesh = weak_piece.lock();
            Level_kits& level_kits = *m_context.level_kits;
            if (step == Step::cancel) {
                if (mesh && level_kits.is_previewing(*mesh)) {
                    level_kits.cancel_parameter_preview();
                }
                return;
            }
            if (!mesh || !m_context.app_scenes->is_host_registered(mesh->get_item_host())) {
                return;
            }
            if (step == Step::preview) {
                const std::string error = level_kits.preview_parameters(mesh, values);
                if (!error.empty()) {
                    log_scene->warn("Kit piece: parameter preview of '{}' failed: {}", mesh->get_name(), error);
                }
                return;
            }
            const Kit_set_parameters_result result = level_kits.set_parameters(mesh, values);
            if (!result.error.empty()) {
                log_scene->warn("Kit piece: parameter change of '{}' failed: {}", mesh->get_name(), result.error);
                m_dirty = true;
            }
        }
    );
}

namespace {

void queue_connector_switch_edit(App_context& context, const std::weak_ptr<erhe::scene::Mesh>& weak_piece, Kit_connector_switch_edit edit)
{
    context.imgui_windows->queue(
        [&context, weak_piece, edit = std::move(edit)]() {
            const std::shared_ptr<erhe::scene::Mesh> mesh = weak_piece.lock();
            if (!mesh || !context.app_scenes->is_host_registered(mesh->get_item_host())) {
                return;
            }
            // Scene items are hosted by their Scene_root.
            Scene_root& scene_root = *static_cast<Scene_root*>(mesh->get_item_host());
            const Kit_connector_switch_result result = context.level_kits->set_connector_switches(scene_root, mesh, edit);
            if (!result.error.empty()) {
                log_scene->warn("Kit piece: connector switch of '{}' failed: {}", mesh->get_name(), result.error);
            }
        }
    );
}

} // anonymous namespace

void Kit_piece_editor::set_piece_connectors(const Kit_connector_state state)
{
    queue_connector_switch_edit(m_context, m_piece, Kit_connector_switch_edit{.piece = state, .connectors = {}});
}

void Kit_piece_editor::set_connector(const char* const connector_name, const Kit_connector_state state)
{
    queue_connector_switch_edit(
        m_context, m_piece,
        Kit_connector_switch_edit{.piece = {}, .connectors = {Kit_connector_switch_edit::Connector{.name = connector_name, .state = state}}}
    );
}

auto Kit_piece_editor::get_connection_rows() -> Kit_connection_rows&
{
    return m_connection_rows;
}

void Kit_connection_rows::remove_connection(const std::string& connector)
{
    m_context.imgui_windows->queue(
        [&context = m_context, weak_node = m_node, connector]() {
            const std::shared_ptr<erhe::scene::Node> node = weak_node.lock();
            if (!node || !context.app_scenes->is_host_registered(node->get_item_host())) {
                return;
            }
            Scene_root& scene_root = *static_cast<Scene_root*>(node->get_item_host());
            const Kit_remove_connection_result result = context.level_kits->remove_connection(scene_root, node, connector);
            if (!result.error.empty()) {
                log_scene->warn("Kit piece: removing the connection of '{}' {} failed: {}", node->get_name(), connector, result.error);
            }
        }
    );
}

void Kit_connection_rows::detach()
{
    m_context.imgui_windows->queue(
        [&context = m_context, weak_node = m_node]() {
            const std::shared_ptr<erhe::scene::Node> node = weak_node.lock();
            if (!node || !context.app_scenes->is_host_registered(node->get_item_host())) {
                return;
            }
            // Scene items are hosted by their Scene_root.
            Scene_root& scene_root = *static_cast<Scene_root*>(node->get_item_host());
            const std::array<std::shared_ptr<erhe::scene::Node>, 1> nodes{node};
            const Kit_detach_result result = context.level_kits->detach_pieces(scene_root, nodes);
            if (!result.error.empty()) {
                log_scene->warn("Kit piece: detach of '{}' failed: {}", node->get_name(), result.error);
            }
        }
    );
}

auto Kit_piece_editor::get_connectors() const -> const std::vector<Kit_connector>&
{
    return m_connectors;
}

auto Kit_piece_editor::get_connector_switches() const -> const Kit_connector_switches&
{
    return m_connector_switches;
}

auto Kit_piece_editor::get_shape() const -> const Kit_shape_definition*
{
    return m_problem.empty() ? m_values.get_shape() : nullptr;
}

auto Kit_piece_editor::get_shape_text() const -> const std::string&
{
    return m_shape_text;
}

auto Kit_piece_editor::get_problem() const -> const std::string&
{
    return m_problem;
}

auto Kit_piece_editor::get_message() const -> const std::string&
{
    return m_message;
}

auto Kit_piece_editor::get_values() -> Kit_parameter_values&
{
    return m_values;
}

auto Kit_piece_editor::get_lengths() const -> const Kit_lengths&
{
    return m_lengths;
}

} // namespace editor
