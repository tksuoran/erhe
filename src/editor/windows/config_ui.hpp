#pragma once

#include "windows/property_editor.hpp"

#include "erhe_codegen/field_info.hpp"

#include <imgui/imgui.h>

#include <span>
#include <string>
#include <type_traits>

namespace editor {

// Renders a single reflected config field as its matching ImGui widget. Shared
// by the Settings window and the Properties window (issue #240). Defined in
// config_ui.cpp.
void imgui_field(void* base, const erhe::codegen::Field_info& field);

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
