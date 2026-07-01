#include "windows/config_ui.hpp"

#include "erhe_codegen/field_info.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor {

void imgui_field(void* base, const erhe::codegen::Field_info& field)
{
    using erhe::codegen::Field_type;
    void* ptr = static_cast<char*>(base) + field.offset;

    switch (field.field_type) {
        case Field_type::bool_:
            ImGui::Checkbox("##", static_cast<bool*>(ptr));
            break;
        case Field_type::int_:
        case Field_type::int8:
        case Field_type::int16:
        case Field_type::int32:
        case Field_type::int64:
            if (field.numeric_limits.has_ui_min && field.numeric_limits.has_ui_max) {
                ImGui::SliderInt("##", static_cast<int*>(ptr),
                    static_cast<int>(field.numeric_limits.ui_min),
                    static_cast<int>(field.numeric_limits.ui_max));
            } else {
                ImGui::DragInt("##", static_cast<int*>(ptr));
            }
            break;
        case Field_type::unsigned_int:
        case Field_type::uint8:
        case Field_type::uint16:
        case Field_type::uint32:
        case Field_type::uint64:
            ImGui::DragInt("##", static_cast<int*>(ptr));
            break;
        case Field_type::float_:
            if (field.numeric_limits.has_ui_min && field.numeric_limits.has_ui_max) {
                ImGui::SliderFloat("##", static_cast<float*>(ptr),
                    static_cast<float>(field.numeric_limits.ui_min),
                    static_cast<float>(field.numeric_limits.ui_max));
            } else {
                ImGui::DragFloat("##", static_cast<float*>(ptr), 0.01f);
            }
            break;
        case Field_type::double_:
            {
                float v = static_cast<float>(*static_cast<double*>(ptr));
                if (ImGui::DragFloat("##", &v, 0.01f)) {
                    *static_cast<double*>(ptr) = static_cast<double>(v);
                }
            }
            break;
        case Field_type::string:
            ImGui::InputText("##", static_cast<std::string*>(ptr));
            break;
        case Field_type::vec2:
            ImGui::DragFloat2("##", static_cast<float*>(ptr), 0.01f);
            break;
        case Field_type::vec3:
            ImGui::ColorEdit3("##", static_cast<float*>(ptr));
            break;
        case Field_type::vec4:
            ImGui::ColorEdit4("##", static_cast<float*>(ptr));
            break;
        case Field_type::ivec2:
            ImGui::DragInt2("##", static_cast<int*>(ptr));
            break;
        case Field_type::ivec3:
            ImGui::DragInt3("##", static_cast<int*>(ptr));
            break;
        case Field_type::mat4:
        case Field_type::vector:
        case Field_type::array:
        case Field_type::optional:
        case Field_type::map:
        case Field_type::struct_ref:
            ImGui::TextUnformatted(field.type_name);
            break;
        case Field_type::enum_ref: {
            if ((field.enum_info == nullptr) || field.enum_info->values.empty()) {
                ImGui::TextUnformatted(field.type_name);
                break;
            }
            // The codegen emits enums as `enum class <Name> : <underlying>` and
            // generates field offsets that match the underlying integer width.
            // size carries that width so we can read/write the right type.
            int64_t current_value = 0;
            switch (field.size) {
                case 1: current_value = static_cast<int64_t>(*static_cast<int8_t* >(ptr)); break;
                case 2: current_value = static_cast<int64_t>(*static_cast<int16_t*>(ptr)); break;
                case 4: current_value = static_cast<int64_t>(*static_cast<int32_t*>(ptr)); break;
                case 8: current_value = *static_cast<int64_t*>(ptr); break;
                default: ImGui::TextUnformatted(field.type_name); return;
            }

            int current_index = -1;
            for (std::size_t i = 0; i < field.enum_info->values.size(); ++i) {
                if (field.enum_info->values[i].value == current_value) {
                    current_index = static_cast<int>(i);
                    break;
                }
            }

            const char* preview = (current_index >= 0)
                ? field.enum_info->values[current_index].name
                : "(unknown)";

            if (ImGui::BeginCombo("##", preview)) {
                for (std::size_t i = 0; i < field.enum_info->values.size(); ++i) {
                    const erhe::codegen::Enum_value_info& v = field.enum_info->values[i];
                    const bool selected = (current_index == static_cast<int>(i));
                    if (ImGui::Selectable(v.name, selected)) {
                        const int64_t new_value = v.value;
                        switch (field.size) {
                            case 1: *static_cast<int8_t* >(ptr) = static_cast<int8_t >(new_value); break;
                            case 2: *static_cast<int16_t*>(ptr) = static_cast<int16_t>(new_value); break;
                            case 4: *static_cast<int32_t*>(ptr) = static_cast<int32_t>(new_value); break;
                            case 8: *static_cast<int64_t*>(ptr) = new_value; break;
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
    }
}

} // namespace editor
