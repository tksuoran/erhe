#include "windows/config_ui.hpp"

#include "erhe_codegen/field_info.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace editor {

namespace {

// The ImGui data type matching the field's ACTUAL storage.
//
// Reading an integer field as `int*` because it is "an integer" is what this
// exists to prevent: the reflected `size` is the width the codegen laid out, so
// an 8-byte field driven through a 4-byte widget would read half a value and
// write into whatever member follows it. `int_` / `unsigned_int` carry no fixed
// width of their own, so every case resolves through size.
[[nodiscard]] auto imgui_integer_data_type(const std::size_t size, const bool is_signed) -> std::optional<ImGuiDataType>
{
    switch (size) {
        case 1: return is_signed ? ImGuiDataType_S8  : ImGuiDataType_U8;
        case 2: return is_signed ? ImGuiDataType_S16 : ImGuiDataType_U16;
        case 4: return is_signed ? ImGuiDataType_S32 : ImGuiDataType_U32;
        case 8: return is_signed ? ImGuiDataType_S64 : ImGuiDataType_U64;
        default: return {};
    }
}

// Writes `value` into `dst` as the given data type, clamped to that type's
// range: a definition's ui_min / ui_max is a double and need not fit the field
// it belongs to, and casting an out-of-range double to an integer is undefined.
template <typename T>
void store_clamped(void* const dst, const double value)
{
    const double lowest  = static_cast<double>(std::numeric_limits<T>::lowest());
    const double highest = static_cast<double>(std::numeric_limits<T>::max());
    *static_cast<T*>(dst) = static_cast<T>(std::clamp(value, lowest, highest));
}

void store_as(void* const dst, const ImGuiDataType data_type, const double value)
{
    switch (data_type) {
        case ImGuiDataType_S8:  store_clamped<std::int8_t  >(dst, value); break;
        case ImGuiDataType_U8:  store_clamped<std::uint8_t >(dst, value); break;
        case ImGuiDataType_S16: store_clamped<std::int16_t >(dst, value); break;
        case ImGuiDataType_U16: store_clamped<std::uint16_t>(dst, value); break;
        case ImGuiDataType_S32: store_clamped<std::int32_t >(dst, value); break;
        case ImGuiDataType_U32: store_clamped<std::uint32_t>(dst, value); break;
        case ImGuiDataType_S64: store_clamped<std::int64_t >(dst, value); break;
        case ImGuiDataType_U64: store_clamped<std::uint64_t>(dst, value); break;
        default: break;
    }
}

// Half the data type's range, as a double.
//
// ImGui::SliderBehavior ASSERTS that a slider's bounds fit within half the
// natural range of types 32 bits and wider (imgui_widgets.cpp: "It would be
// possible to lift that limitation with some work but it doesn't seem to be
// worth it for sliders"), because its position math would otherwise overflow.
// A definition is free to ask for a wider range - Load_config's
// max_decoded_bytes_in_flight asks for 1 GiB, one above IM_S32_MAX / 2 - and
// that assert is a CRASH in a debug build, so the range decides slider vs drag
// rather than being trusted blindly.
[[nodiscard]] auto data_type_half_range(const ImGuiDataType data_type, double& out_lowest, double& out_highest) -> bool
{
    const auto half = [&out_lowest, &out_highest](const double lowest, const double highest) {
        out_lowest  = lowest  / 2.0;
        out_highest = highest / 2.0;
        return true;
    };
    switch (data_type) {
        // The narrow types are widened to 32 bits inside SliderBehavior, so
        // their whole range is usable.
        case ImGuiDataType_S8:  return half(static_cast<double>(std::numeric_limits<std::int8_t  >::lowest()) * 2.0, static_cast<double>(std::numeric_limits<std::int8_t  >::max()) * 2.0);
        case ImGuiDataType_U8:  return half(0.0, static_cast<double>(std::numeric_limits<std::uint8_t >::max()) * 2.0);
        case ImGuiDataType_S16: return half(static_cast<double>(std::numeric_limits<std::int16_t >::lowest()) * 2.0, static_cast<double>(std::numeric_limits<std::int16_t >::max()) * 2.0);
        case ImGuiDataType_U16: return half(0.0, static_cast<double>(std::numeric_limits<std::uint16_t>::max()) * 2.0);
        case ImGuiDataType_S32: return half(static_cast<double>(std::numeric_limits<std::int32_t >::lowest()), static_cast<double>(std::numeric_limits<std::int32_t >::max()));
        case ImGuiDataType_U32: return half(0.0, static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
        case ImGuiDataType_S64: return half(static_cast<double>(std::numeric_limits<std::int64_t >::lowest()), static_cast<double>(std::numeric_limits<std::int64_t >::max()));
        case ImGuiDataType_U64: return half(0.0, static_cast<double>(std::numeric_limits<std::uint64_t>::max()));
        default: return false;
    }
}

// One integer field of any width, as a slider when the definition gives ui
// bounds a slider can represent, and as a drag otherwise. DragBehavior has no
// range restriction, so it is the honest fallback for a wide range - the field
// stays editable instead of asserting.
void imgui_integer_field(void* const ptr, const erhe::codegen::Field_info& field, const bool is_signed)
{
    const std::optional<ImGuiDataType> data_type = imgui_integer_data_type(field.size, is_signed);
    if (!data_type.has_value()) {
        // An integer width the codegen should never emit; showing the type name
        // beats writing through a wrongly-sized pointer.
        ImGui::TextUnformatted(field.type_name);
        return;
    }

    if (field.numeric_limits.has_ui_min && field.numeric_limits.has_ui_max) {
        double slider_lowest  = 0.0;
        double slider_highest = 0.0;
        const bool have_half_range = data_type_half_range(data_type.value(), slider_lowest, slider_highest);
        const bool slider_ok =
            have_half_range &&
            (field.numeric_limits.ui_min >= slider_lowest) &&
            (field.numeric_limits.ui_max <= slider_highest);
        alignas(std::uint64_t) std::array<std::byte, 8> min_storage{};
        alignas(std::uint64_t) std::array<std::byte, 8> max_storage{};
        store_as(min_storage.data(), data_type.value(), field.numeric_limits.ui_min);
        store_as(max_storage.data(), data_type.value(), field.numeric_limits.ui_max);
        if (slider_ok) {
            ImGui::SliderScalar("##", data_type.value(), ptr, min_storage.data(), max_storage.data());
        } else {
            ImGui::DragScalar("##", data_type.value(), ptr, 1.0f, min_storage.data(), max_storage.data());
        }
        return;
    }
    ImGui::DragScalar("##", data_type.value(), ptr, 1.0f);
}

} // anonymous namespace

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
            imgui_integer_field(ptr, field, true);
            break;
        case Field_type::unsigned_int:
        case Field_type::uint8:
        case Field_type::uint16:
        case Field_type::uint32:
        case Field_type::uint64:
            imgui_integer_field(ptr, field, false);
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

            // Show the human-readable short_desc when the definition has one;
            // the identifier-style value name is the fallback.
            const auto value_label = [](const erhe::codegen::Enum_value_info& v) -> const char* {
                return (v.short_desc != nullptr && v.short_desc[0] != '\0') ? v.short_desc : v.name;
            };
            const char* preview = (current_index >= 0)
                ? value_label(field.enum_info->values[current_index])
                : "(unknown)";

            if (ImGui::BeginCombo("##", preview)) {
                for (std::size_t i = 0; i < field.enum_info->values.size(); ++i) {
                    const erhe::codegen::Enum_value_info& v = field.enum_info->values[i];
                    const bool selected = (current_index == static_cast<int>(i));
                    if (ImGui::Selectable(value_label(v), selected)) {
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
