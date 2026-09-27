#pragma once

#include "windows/property_editor.hpp"

#include "erhe_codegen/field_info.hpp"

#include <imgui/imgui.h>

#include <span>
#include <cstdint>
#include <string>
#include <type_traits>

namespace editor {

// Renders a single reflected config field as its matching ImGui widget. Shared
// by the Settings window and the Properties window (issue #240). Defined in
// config_ui.cpp.
void imgui_field(void* base, const erhe::codegen::Field_info& field);

// Renders a codegen enum value as a combo of its reflected values (the
// short_desc labels, the value names as fallback), like imgui_field() does
// for an enum_ref field. For an enum held outside a reflected struct (e.g.
// Editor_settings_config::indirect_diffuse_source). Returns true when the
// value was changed.
template <typename E>
auto imgui_enum_combo(const char* label, E& value) -> bool
{
    const erhe::codegen::Enum_info& enum_info = get_enum_info(static_cast<const E*>(nullptr));
    const auto value_label = [](const erhe::codegen::Enum_value_info& v) -> const char* {
        return ((v.short_desc != nullptr) && (v.short_desc[0] != '\0')) ? v.short_desc : v.name;
    };
    const int64_t current = static_cast<int64_t>(value);
    const char*   preview = "(unknown)";
    for (const erhe::codegen::Enum_value_info& v : enum_info.values) {
        if (v.value == current) {
            preview = value_label(v);
        }
    }
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const erhe::codegen::Enum_value_info& v : enum_info.values) {
            const bool selected = (v.value == current);
            if (ImGui::Selectable(value_label(v), selected) && !selected) {
                value   = static_cast<E>(v.value);
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Renders a reflected config struct as a Property_editor group of entries.
// Nested struct_ref fields are skipped here (they are rendered by explicit
// add_config_section calls on the sub-struct). label_override gives a distinct
// group header when the same struct type is shown more than once (e.g. the
// Default vs Selection render-style appearance, or a per-scene override copy).
//
// get_struct_info / get_fields are the codegen-generated reflection helpers for
// the concrete config struct type; they are resolved at instantiation (the
// including translation unit must include the struct's generated header).
template <typename T>
void add_config_section(Property_editor& editor, bool show_developer, T& section, const char* label_override = nullptr)
{
    const erhe::codegen::Struct_info& struct_info = get_struct_info(static_cast<const std::remove_reference_t<T>*>(nullptr));
    if (struct_info.developer && !show_developer) {
        return;
    }
    const char* label = (label_override != nullptr)
        ? label_override
        : (struct_info.short_desc != nullptr && struct_info.short_desc[0] != '\0')
            ? struct_info.short_desc
            : struct_info.name;
    editor.push_group(label, ImGuiTreeNodeFlags_Framed);
    const std::span<const erhe::codegen::Field_info> fields = get_fields(static_cast<const std::remove_reference_t<T>*>(nullptr));
    for (const erhe::codegen::Field_info& field : fields) {
        if (field.removed_in != 0) {
            continue;
        }
        if (field.developer && !show_developer) {
            continue;
        }
        if (field.field_type == erhe::codegen::Field_type::struct_ref) {
            continue;
        }
        const char* entry_label = (field.short_desc != nullptr && field.short_desc[0] != '\0')
            ? field.short_desc
            : field.name;
        std::string tooltip = (field.long_desc != nullptr && field.long_desc[0] != '\0')
            ? std::string{field.long_desc}
            : std::string{};
        editor.add_entry(std::string{entry_label}, [&section, &field]() {
            imgui_field(&section, field);
        }, std::move(tooltip));
    }
    editor.pop_group();
}

} // namespace editor
