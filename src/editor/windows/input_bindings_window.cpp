#include "windows/input_bindings_window.hpp"

#include "app_context.hpp"
#include "input_bindings_store.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <optional>

namespace editor {

namespace {

constexpr const char* c_edit_popup_name = "Edit Binding";

[[nodiscard]] auto is_modifier_key(const ImGuiKey key) -> bool
{
    return
        (key == ImGuiKey_LeftCtrl)  || (key == ImGuiKey_RightCtrl)  ||
        (key == ImGuiKey_LeftShift) || (key == ImGuiKey_RightShift) ||
        (key == ImGuiKey_LeftAlt)   || (key == ImGuiKey_RightAlt)   ||
        (key == ImGuiKey_LeftSuper) || (key == ImGuiKey_RightSuper);
}

[[nodiscard]] auto current_modifier_mask() -> uint32_t
{
    const ImGuiIO& io = ImGui::GetIO();
    uint32_t mask = 0u;
    if (io.KeyCtrl ) { mask = mask | erhe::window::Key_modifier_bit_ctrl;  }
    if (io.KeyShift) { mask = mask | erhe::window::Key_modifier_bit_shift; }
    if (io.KeyAlt  ) { mask = mask | erhe::window::Key_modifier_bit_menu;  }
    if (io.KeySuper) { mask = mask | erhe::window::Key_modifier_bit_super; }
    return mask;
}

[[nodiscard]] auto has_trigger(const erhe::commands::Binding_kind kind) -> bool
{
    return
        (kind == erhe::commands::Binding_kind::key) ||
        (kind == erhe::commands::Binding_kind::mouse_button) ||
        (kind == erhe::commands::Binding_kind::controller_button);
}

// A fresh binding of the command's input kind, the starting point for "+".
[[nodiscard]] auto make_new_binding(const erhe::commands::Input_kind input_kind) -> erhe::commands::Binding_desc
{
    using erhe::commands::Binding_kind;
    using erhe::commands::Input_kind;
    switch (input_kind) {
        case Input_kind::drag:   return {.kind = Binding_kind::mouse_drag,      .code = static_cast<int>(erhe::window::Mouse_button_left), .modifier_mask = 0u};
        case Input_kind::wheel:  return {.kind = Binding_kind::mouse_wheel,     .modifier_mask = 0u};
        case Input_kind::motion: return {.kind = Binding_kind::mouse_motion,    .modifier_mask = 0u};
        case Input_kind::axis:   return {.kind = Binding_kind::controller_axis, .code = 0, .modifier_mask = 0u};
        default:                 return {.kind = Binding_kind::key,             .code = erhe::window::Key_unknown, .modifier_mask = 0u};
    }
}

} // anonymous namespace

Input_bindings_window::Input_bindings_window(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Input Bindings", "input_bindings"}
    , m_context                {app_context}
{
    // Runs inside Commands::tick() when the dispatch tables were rebuilt;
    // the rows are refreshed on the next imgui().
    commands.add_bindings_changed_callback(
        [this]() {
            m_rows_dirty = true;
        }
    );
}

void Input_bindings_window::rebuild_rows()
{
    erhe::commands::Commands& commands = *m_context.commands;
    m_rows.clear();
    for (erhe::commands::Command* command : commands.get_commands()) {
        const erhe::commands::Input_kind input_kind = commands.get_input_kind(*command);
        if (input_kind == erhe::commands::Input_kind::internal) {
            continue;
        }
        Row& row = m_rows.emplace_back();
        row.command    = command;
        row.name       = command->get_name();
        const std::size_t dot = row.name.find('.');
        row.group      = (dot == std::string::npos) ? std::string{"General"} : row.name.substr(0, dot);
        row.input_kind = input_kind;
        row.modified   = commands.has_binding_override(*command);
        commands.get_effective_bindings(*command, row.bindings);
        for (const erhe::commands::Binding_desc& desc : row.bindings) {
            row.labels     .push_back(desc.to_display_string());
            row.search_keys.push_back(desc.to_string());
        }
    }
    for (const erhe::commands::Binding_conflict& conflict : commands.get_binding_conflicts()) {
        for (Row& row : m_rows) {
            if (row.command != conflict.command) {
                continue;
            }
            if (!row.conflicts.empty()) {
                row.conflicts += '\n';
            }
            row.conflicts += conflict.binding.to_display_string();
            row.conflicts += " is also bound to ";
            row.conflicts += conflict.other_command->get_name();
        }
    }
    std::sort(
        m_rows.begin(),
        m_rows.end(),
        [](const Row& lhs, const Row& rhs) -> bool {
            if (lhs.group != rhs.group) {
                return lhs.group < rhs.group;
            }
            return lhs.name < rhs.name;
        }
    );
    m_rows_dirty = false;
}

auto Input_bindings_window::row_matches_filter(const Row& row) const -> bool
{
    if (m_show_modified_only && !row.modified) {
        return false;
    }
    if (m_filter.empty()) {
        return true;
    }
    if (row.name.find(m_filter) != std::string::npos) {
        return true;
    }
    for (std::size_t i = 0, end = row.labels.size(); i < end; ++i) {
        if ((row.labels[i].find(m_filter) != std::string::npos) || (row.search_keys[i].find(m_filter) != std::string::npos)) {
            return true;
        }
    }
    return false;
}

void Input_bindings_window::imgui()
{
    if (m_rows_dirty) {
        rebuild_rows();
    }

    ImGui::SetNextItemWidth(-220.0f);
    ImGui::InputTextWithHint("##filter", "Filter commands or bindings...", &m_filter);
    ImGui::SameLine();
    ImGui::Checkbox("Modified only", &m_show_modified_only);
    ImGui::SameLine();
    if (ImGui::Button("Reset All")) {
        reset_all();
    }

    // Each group's table lives inside PushID(group) so the tables of the
    // groups get distinct ids; the id is popped after EndTable().
    const std::string* current_group = nullptr;
    bool group_open  = false;
    bool table_begun = false;
    bool id_pushed   = false;
    for (std::size_t row_index = 0, end = m_rows.size(); row_index < end; ++row_index) {
        const Row& row = m_rows[row_index];
        if (!row_matches_filter(row)) {
            continue;
        }
        if ((current_group == nullptr) || (*current_group != row.group)) {
            if (table_begun) {
                ImGui::EndTable();
                table_begun = false;
            }
            if (id_pushed) {
                ImGui::PopID();
                id_pushed = false;
            }
            current_group = &row.group;
            group_open = ImGui::CollapsingHeader(row.group.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (group_open) {
                ImGui::PushID(row.group.c_str());
                id_pushed = true;
                table_begun = ImGui::BeginTable("##bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp);
                if (table_begun) {
                    ImGui::TableSetupColumn("Command",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                    ImGui::TableSetupColumn("##reset",  ImGuiTableColumnFlags_WidthFixed);
                }
            }
        }
        if (group_open && table_begun) {
            row_imgui(row_index);
        }
    }
    if (table_begun) {
        ImGui::EndTable();
    }
    if (id_pushed) {
        ImGui::PopID();
    }

    if (m_open_edit_popup) {
        ImGui::OpenPopup(c_edit_popup_name);
        m_open_edit_popup = false;
    }
    edit_popup_imgui();
}

void Input_bindings_window::row_imgui(const std::size_t row_index)
{
    Row& row = m_rows[row_index];
    ImGui::PushID(row.name.c_str());
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    if (row.modified) {
        ImGui::TextColored(ImVec4{1.0f, 0.8f, 0.3f, 1.0f}, "%s *", row.name.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Edited: the default bindings of this command are replaced");
        }
    } else {
        ImGui::TextUnformatted(row.name.c_str());
    }
    if (!row.conflicts.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.3f, 1.0f}, "(!)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", row.conflicts.c_str());
        }
    }

    ImGui::TableSetColumnIndex(1);
    std::optional<std::size_t> remove_index;
    for (std::size_t i = 0, end = row.labels.size(); i < end; ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (i > 0) {
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(row.labels[i].c_str())) {
            begin_edit(row_index, i);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nClick to edit", row.search_keys[i].c_str());
        }
        ImGui::SameLine(0.0f, 1.0f);
        if (ImGui::SmallButton("x")) {
            remove_index = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove this binding");
        }
        ImGui::PopID();
    }
    if (!row.labels.empty()) {
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("+")) {
        begin_edit(row_index, row.bindings.size());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add a binding");
    }

    ImGui::TableSetColumnIndex(2);
    if (row.modified) {
        if (ImGui::SmallButton("Reset")) {
            reset(row);
        }
    }

    ImGui::PopID();

    if (remove_index.has_value()) {
        m_edit_bindings = row.bindings;
        m_edit_bindings.erase(m_edit_bindings.begin() + static_cast<std::ptrdiff_t>(remove_index.value()));
        commit(row, m_edit_bindings);
    }
}

void Input_bindings_window::begin_edit(const std::size_t row_index, const std::size_t binding_index)
{
    const Row& row = m_rows[row_index];
    m_edit_row     = row_index;
    m_edit_binding = binding_index;
    m_edit_desc    = (binding_index < row.bindings.size()) ? row.bindings[binding_index] : make_new_binding(row.input_kind);
    m_edit_text    = m_edit_desc.to_string();
    m_edit_error.clear();
    const bool capturable =
        (row.input_kind == erhe::commands::Input_kind::button) ||
        (row.input_kind == erhe::commands::Input_kind::drag);
    m_edit_mode = (capturable && (binding_index >= row.bindings.size())) ? Edit_mode::capturing : Edit_mode::editing;
    m_open_edit_popup = true;
}

void Input_bindings_window::capture_imgui()
{
    const Row& row = m_rows[m_edit_row];
    const bool accept_keys = (row.input_kind == erhe::commands::Input_kind::button);

    ImGui::TextUnformatted(
        accept_keys
            ? "Press a key (with modifiers), or click the box with a mouse button.\nEsc cancels."
            : "Click the box with the mouse button to drag with (hold modifiers as needed).\nEsc cancels."
    );
    ImGui::Button("Click here with a mouse button", ImVec2{-1.0f, 3.0f * ImGui::GetFrameHeight()});
    if (ImGui::IsItemHovered()) {
        for (int imgui_button = 0; imgui_button < ImGuiMouseButton_COUNT; ++imgui_button) {
            if (!ImGui::IsMouseClicked(imgui_button)) {
                continue;
            }
            const erhe::window::Mouse_button button = erhe::imgui::to_erhe_mouse_button(imgui_button);
            if (button == erhe::window::Mouse_button_none) {
                continue;
            }
            m_edit_desc.kind          = accept_keys ? erhe::commands::Binding_kind::mouse_button : erhe::commands::Binding_kind::mouse_drag;
            m_edit_desc.code          = static_cast<int>(button);
            m_edit_desc.modifier_mask = current_modifier_mask();
            m_edit_desc.trigger       = erhe::commands::Button_trigger::Button_pressed;
            m_edit_text               = m_edit_desc.to_string();
            m_edit_mode               = Edit_mode::editing;
            return;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_edit_mode = Edit_mode::none;
        ImGui::CloseCurrentPopup();
        return;
    }
    if (!accept_keys) {
        return;
    }
    for (int key_index = ImGuiKey_NamedKey_BEGIN; key_index < ImGuiKey_NamedKey_END; ++key_index) {
        const ImGuiKey key = static_cast<ImGuiKey>(key_index);
        if (is_modifier_key(key) || !ImGui::IsKeyPressed(key, false)) {
            continue;
        }
        const erhe::window::Keycode keycode = erhe::imgui::to_erhe_keycode(key);
        if (keycode == erhe::window::Key_unknown) {
            continue;
        }
        m_edit_desc.kind          = erhe::commands::Binding_kind::key;
        m_edit_desc.code          = keycode;
        m_edit_desc.modifier_mask = current_modifier_mask();
        if (m_edit_desc.trigger == erhe::commands::Button_trigger::Any) {
            m_edit_desc.trigger = erhe::commands::Button_trigger::Button_pressed;
        }
        m_edit_text = m_edit_desc.to_string();
        m_edit_mode = Edit_mode::editing;
        return;
    }
}

void Input_bindings_window::modifiers_imgui()
{
    bool edited = false;
    bool any_modifiers = !m_edit_desc.modifier_mask.has_value();
    if (ImGui::Checkbox("Any modifiers", &any_modifiers)) {
        if (any_modifiers) {
            m_edit_desc.modifier_mask.reset();
        } else {
            m_edit_desc.modifier_mask = 0u;
        }
        edited = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Fire regardless of which modifier keys are held");
    }
    if (m_edit_desc.modifier_mask.has_value()) {
        class Modifier_checkbox
        {
        public:
            const char* label;
            uint32_t    bit;
        };
        static constexpr std::array<Modifier_checkbox, 4> c_modifiers{
            Modifier_checkbox{"Ctrl",  erhe::window::Key_modifier_bit_ctrl },
            Modifier_checkbox{"Shift", erhe::window::Key_modifier_bit_shift},
            Modifier_checkbox{"Alt",   erhe::window::Key_modifier_bit_menu },
            Modifier_checkbox{"Super", erhe::window::Key_modifier_bit_super}
        };
        for (const Modifier_checkbox& modifier : c_modifiers) {
            ImGui::SameLine();
            bool set = (m_edit_desc.modifier_mask.value() & modifier.bit) != 0u;
            if (ImGui::Checkbox(modifier.label, &set)) {
                m_edit_desc.modifier_mask = set
                    ? (m_edit_desc.modifier_mask.value() | modifier.bit)
                    : (m_edit_desc.modifier_mask.value() & ~modifier.bit);
                edited = true;
            }
        }
    }
    if (edited) {
        m_edit_text = m_edit_desc.to_string();
    }
}

void Input_bindings_window::edit_popup_imgui()
{
    if (!ImGui::BeginPopupModal(c_edit_popup_name, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if ((m_edit_mode == Edit_mode::none) || (m_edit_row >= m_rows.size())) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    Row& row = m_rows[m_edit_row];
    ImGui::Text("Command: %s", row.name.c_str());
    ImGui::Separator();

    if (m_edit_mode == Edit_mode::capturing) {
        capture_imgui();
        ImGui::EndPopup();
        return;
    }

    using erhe::commands::Binding_kind;
    using erhe::commands::Button_trigger;

    const std::string label = m_edit_desc.to_display_string();
    ImGui::Text("Binding: %s", label.c_str());
    if ((row.input_kind == erhe::commands::Input_kind::button) || (row.input_kind == erhe::commands::Input_kind::drag)) {
        ImGui::SameLine();
        if (ImGui::Button("Capture...")) {
            m_edit_mode = Edit_mode::capturing;
        }
    }

    bool edited = false;
    if (m_edit_desc.kind == Binding_kind::mouse_drag) {
        static constexpr std::array<erhe::window::Mouse_button, 5> c_buttons{
            erhe::window::Mouse_button_left, erhe::window::Mouse_button_right, erhe::window::Mouse_button_middle,
            erhe::window::Mouse_button_x1,   erhe::window::Mouse_button_x2
        };
        if (ImGui::BeginCombo("Mouse button", erhe::window::c_str(static_cast<erhe::window::Mouse_button>(m_edit_desc.code)))) {
            for (const erhe::window::Mouse_button button : c_buttons) {
                if (ImGui::Selectable(erhe::window::c_str(button), m_edit_desc.code == static_cast<int>(button))) {
                    m_edit_desc.code = static_cast<int>(button);
                    edited = true;
                }
            }
            ImGui::EndCombo();
        }
    }
    if ((m_edit_desc.kind == Binding_kind::controller_axis) || (m_edit_desc.kind == Binding_kind::controller_button)) {
        if (ImGui::InputInt((m_edit_desc.kind == Binding_kind::controller_axis) ? "Axis" : "Button", &m_edit_desc.code)) {
            m_edit_desc.code = std::max(m_edit_desc.code, 0);
            edited = true;
        }
    }
    if (has_trigger(m_edit_desc.kind)) {
        const bool allow_any = (m_edit_desc.kind != Binding_kind::mouse_button);
        const char* const trigger_names[] = {"Pressed", "Released", "Pressed and released"};
        int trigger = static_cast<int>(m_edit_desc.trigger);
        if (ImGui::Combo("Trigger", &trigger, trigger_names, allow_any ? 3 : 2)) {
            m_edit_desc.trigger = static_cast<Button_trigger>(trigger);
            edited = true;
        }
    }
    if (edited) {
        m_edit_text = m_edit_desc.to_string();
    }
    modifiers_imgui();

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("Text form", &m_edit_text);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const std::optional<erhe::commands::Binding_desc> parsed = erhe::commands::Binding_desc::parse(m_edit_text);
        if (parsed.has_value()) {
            m_edit_desc = parsed.value();
            m_edit_error.clear();
        } else {
            m_edit_error = "Not a valid binding: " + m_edit_text;
        }
    }

    if (!m_edit_error.empty()) {
        ImGui::TextColored(ImVec4{1.0f, 0.4f, 0.3f, 1.0f}, "%s", m_edit_error.c_str());
    }

    ImGui::Separator();
    const bool valid_key = (m_edit_desc.kind != Binding_kind::key) || (m_edit_desc.code != erhe::window::Key_unknown);
    ImGui::BeginDisabled(!valid_key);
    if (ImGui::Button("OK")) {
        m_edit_bindings = row.bindings;
        if (m_edit_binding < m_edit_bindings.size()) {
            m_edit_bindings[m_edit_binding] = m_edit_desc;
        } else {
            m_edit_bindings.push_back(m_edit_desc);
        }
        if (commit(row, m_edit_bindings)) {
            m_edit_mode = Edit_mode::none;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        m_edit_mode = Edit_mode::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

auto Input_bindings_window::commit(Row& row, const std::vector<erhe::commands::Binding_desc>& bindings) -> bool
{
    erhe::commands::Commands& commands = *m_context.commands;

    // A list equal to the defaults is no override: the command then follows
    // future changes of its defaults again.
    std::vector<erhe::commands::Binding_desc> defaults;
    commands.get_default_bindings(*row.command, defaults);
    if (bindings == defaults) {
        commands.clear_binding_override(*row.command);
    } else {
        std::string error;
        if (!commands.set_binding_override(*row.command, bindings, &error)) {
            m_edit_error = error;
            return false;
        }
    }
    m_context.input_bindings_store->save();
    return true;
}

void Input_bindings_window::reset(Row& row)
{
    m_context.commands->clear_binding_override(*row.command);
    m_context.input_bindings_store->save();
}

void Input_bindings_window::reset_all()
{
    m_context.commands->clear_all_binding_overrides();
    m_context.input_bindings_store->save();
}

} // namespace editor
