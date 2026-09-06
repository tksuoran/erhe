#include "erhe_property/property_string.hpp"
#include "erhe_property/dependency_object.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/enum_info.hpp"

#include <charconv>
#include <cstdlib>
#include <string>
#include <vector>

namespace erhe::property {

namespace {

auto float_to_string(const float value) -> std::string
{
    char buffer[64];
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string{buffer, result.ptr};
}

auto double_to_string(const double value) -> std::string
{
    char buffer[64];
    const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string{buffer, result.ptr};
}

auto trim(std::string_view text) -> std::string_view
{
    while (!text.empty() && ((text.front() == ' ') || (text.front() == '\t') || (text.front() == '\n') || (text.front() == '\r'))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && ((text.back() == ' ') || (text.back() == '\t') || (text.back() == '\n') || (text.back() == '\r'))) {
        text.remove_suffix(1);
    }
    return text;
}

auto split_components(std::string_view text) -> std::vector<std::string_view>
{
    std::vector<std::string_view> parts;
    text = trim(text);
    while (!text.empty()) {
        std::size_t end = 0;
        while ((end < text.size()) && (text[end] != ' ') && (text[end] != ',') && (text[end] != '\t')) {
            ++end;
        }
        if (end > 0) {
            parts.push_back(text.substr(0, end));
        }
        text.remove_prefix(end);
        while (!text.empty() && ((text.front() == ' ') || (text.front() == ',') || (text.front() == '\t'))) {
            text.remove_prefix(1);
        }
    }
    return parts;
}

auto parse_int(const std::string_view text) -> std::optional<int>
{
    int value = 0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if ((result.ec != std::errc{}) || (result.ptr != text.data() + text.size())) {
        return std::nullopt;
    }
    return value;
}

auto parse_float(const std::string_view text) -> std::optional<float>
{
    // std::from_chars for floating point is not available on every libc++
    // erhe builds against; strtof on a null-terminated copy is.
    const std::string copy{text};
    char* end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    if ((end == copy.c_str()) || (end != copy.c_str() + copy.size())) {
        return std::nullopt;
    }
    return value;
}

auto parse_double(const std::string_view text) -> std::optional<double>
{
    // Same reason as parse_float: std::from_chars for floating point is not
    // available on every libc++ erhe builds against; strtod on a
    // null-terminated copy is.
    const std::string copy{text};
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if ((end == copy.c_str()) || (end != copy.c_str() + copy.size())) {
        return std::nullopt;
    }
    return value;
}

auto parse_floats(const std::string_view text, const std::size_t count) -> std::optional<std::vector<float>>
{
    const std::vector<std::string_view> parts = split_components(text);
    if (parts.size() != count) {
        return std::nullopt;
    }
    std::vector<float> values;
    values.reserve(count);
    for (const std::string_view part : parts) {
        const std::optional<float> value = parse_float(part);
        if (!value.has_value()) {
            return std::nullopt;
        }
        values.push_back(value.value());
    }
    return values;
}

auto parse_ints(const std::string_view text, const std::size_t count) -> std::optional<std::vector<int>>
{
    const std::vector<std::string_view> parts = split_components(text);
    if (parts.size() != count) {
        return std::nullopt;
    }
    std::vector<int> values;
    values.reserve(count);
    for (const std::string_view part : parts) {
        const std::optional<int> value = parse_int(part);
        if (!value.has_value()) {
            return std::nullopt;
        }
        values.push_back(value.value());
    }
    return values;
}

} // anonymous namespace

auto to_string(const Property_value& value, const Enum_info* enum_info) -> std::string
{
    switch (type_of(value)) {
        case Property_type::boolean:  return std::get<bool>(value) ? "true" : "false";
        case Property_type::integer:  return std::to_string(std::get<int>(value));
        case Property_type::floating: return float_to_string(std::get<float>(value));
        case Property_type::vec2: {
            const glm::vec2 v = std::get<glm::vec2>(value);
            return float_to_string(v.x) + " " + float_to_string(v.y);
        }
        case Property_type::vec3: {
            const glm::vec3 v = std::get<glm::vec3>(value);
            return float_to_string(v.x) + " " + float_to_string(v.y) + " " + float_to_string(v.z);
        }
        case Property_type::vec4: {
            const glm::vec4 v = std::get<glm::vec4>(value);
            return float_to_string(v.x) + " " + float_to_string(v.y) + " " + float_to_string(v.z) + " " + float_to_string(v.w);
        }
        case Property_type::quat: {
            const glm::quat q = std::get<glm::quat>(value);
            return float_to_string(q.x) + " " + float_to_string(q.y) + " " + float_to_string(q.z) + " " + float_to_string(q.w);
        }
        case Property_type::string: return std::get<std::string>(value);
        case Property_type::enumeration: {
            const int32_t raw = std::get<Enum_value>(value).value;
            if (enum_info != nullptr) {
                const std::string_view label = enum_info->label_for(raw);
                if (!label.empty()) {
                    return std::string{label};
                }
            }
            return std::to_string(raw);
        }
        case Property_type::ivec2: {
            const glm::ivec2 v = std::get<glm::ivec2>(value);
            return std::to_string(v.x) + " " + std::to_string(v.y);
        }
        case Property_type::ivec3: {
            const glm::ivec3 v = std::get<glm::ivec3>(value);
            return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
        }
        case Property_type::ivec4: {
            const glm::ivec4 v = std::get<glm::ivec4>(value);
            return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z) + " " + std::to_string(v.w);
        }
        case Property_type::object: {
            const Object_reference& reference = std::get<Object_reference>(value);
            return reference.object ? reference.object->get_reference_path() : std::string{};
        }
        case Property_type::double_floating: return double_to_string(std::get<double>(value));
        case Property_type::asset_path: return std::get<Asset_path>(value).path;
        case Property_type::float_array: {
            // Space-separated components, as the vectors are; an empty array
            // is the empty string.
            const std::vector<float>& v = std::get<std::vector<float>>(value);
            std::string text;
            for (std::size_t i = 0, end = v.size(); i < end; ++i) {
                if (i > 0) {
                    text += ' ';
                }
                text += float_to_string(v[i]);
            }
            return text;
        }
        case Property_type::int_array: {
            const std::vector<int>& v = std::get<std::vector<int>>(value);
            std::string text;
            for (std::size_t i = 0, end = v.size(); i < end; ++i) {
                if (i > 0) {
                    text += ' ';
                }
                text += std::to_string(v[i]);
            }
            return text;
        }
        case Property_type::mat4: {
            // 16 floats in glm's own storage order: column 0 first, each
            // column x y z w.
            const glm::mat4& m = std::get<glm::mat4>(value);
            const float* const f = &m[0].x;
            std::string text;
            for (int i = 0; i < 16; ++i) {
                if (i > 0) {
                    text += ' ';
                }
                text += float_to_string(f[i]);
            }
            return text;
        }
    }
    return {};
}

auto to_string(const Dependency_property& property, const Property_value& value) -> std::string
{
    return to_string(value, property.get_enum_info());
}

auto parse_value(const Property_type type, const std::string_view text_in, const Enum_info* enum_info) -> std::optional<Property_value>
{
    const std::string_view text = trim(text_in);
    switch (type) {
        case Property_type::boolean: {
            if ((text == "true") || (text == "1")) {
                return true;
            }
            if ((text == "false") || (text == "0")) {
                return false;
            }
            return std::nullopt;
        }
        case Property_type::integer: {
            const std::optional<int> value = parse_int(text);
            if (!value.has_value()) {
                return std::nullopt;
            }
            return value.value();
        }
        case Property_type::floating: {
            const std::optional<float> value = parse_float(text);
            if (!value.has_value()) {
                return std::nullopt;
            }
            return value.value();
        }
        case Property_type::vec2: {
            const std::optional<std::vector<float>> v = parse_floats(text, 2);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::vec2{v->at(0), v->at(1)};
        }
        case Property_type::vec3: {
            const std::optional<std::vector<float>> v = parse_floats(text, 3);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::vec3{v->at(0), v->at(1), v->at(2)};
        }
        case Property_type::vec4: {
            const std::optional<std::vector<float>> v = parse_floats(text, 4);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::vec4{v->at(0), v->at(1), v->at(2), v->at(3)};
        }
        case Property_type::quat: {
            const std::optional<std::vector<float>> v = parse_floats(text, 4);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::quat{v->at(3), v->at(0), v->at(1), v->at(2)}; // glm ctor is (w, x, y, z)
        }
        case Property_type::string: {
            return std::string{text_in};
        }
        case Property_type::enumeration: {
            if (enum_info != nullptr) {
                const std::optional<int32_t> by_label = enum_info->value_for(text);
                if (by_label.has_value()) {
                    return Enum_value{by_label.value()};
                }
            }
            const std::optional<int> raw = parse_int(text);
            if (!raw.has_value()) {
                return std::nullopt;
            }
            if ((enum_info != nullptr) && !enum_info->contains(raw.value())) {
                return std::nullopt;
            }
            return Enum_value{raw.value()};
        }
        case Property_type::ivec2: {
            const std::optional<std::vector<int>> v = parse_ints(text, 2);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::ivec2{v->at(0), v->at(1)};
        }
        case Property_type::ivec3: {
            const std::optional<std::vector<int>> v = parse_ints(text, 3);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::ivec3{v->at(0), v->at(1), v->at(2)};
        }
        case Property_type::ivec4: {
            const std::optional<std::vector<int>> v = parse_ints(text, 4);
            if (!v.has_value()) {
                return std::nullopt;
            }
            return glm::ivec4{v->at(0), v->at(1), v->at(2), v->at(3)};
        }
        case Property_type::object: {
            // Needs the referencing object; see the context overload.
            return std::nullopt;
        }
        case Property_type::double_floating: {
            const std::optional<double> value = parse_double(text);
            if (!value.has_value()) {
                return std::nullopt;
            }
            return value.value();
        }
        case Property_type::asset_path: {
            return Asset_path{std::string{text_in}};
        }
        case Property_type::float_array: {
            // Any component count, separated by spaces or commas the way the
            // vectors accept them; the empty text is the empty array.
            const std::vector<std::string_view> parts = split_components(text);
            std::vector<float> values;
            values.reserve(parts.size());
            for (const std::string_view part : parts) {
                const std::optional<float> component = parse_float(part);
                if (!component.has_value()) {
                    return std::nullopt;
                }
                values.push_back(component.value());
            }
            return values;
        }
        case Property_type::int_array: {
            const std::vector<std::string_view> parts = split_components(text);
            std::vector<int> values;
            values.reserve(parts.size());
            for (const std::string_view part : parts) {
                const std::optional<int> component = parse_int(part);
                if (!component.has_value()) {
                    return std::nullopt;
                }
                values.push_back(component.value());
            }
            return values;
        }
        case Property_type::mat4: {
            const std::optional<std::vector<float>> v = parse_floats(text, 16);
            if (!v.has_value()) {
                return std::nullopt;
            }
            glm::mat4 m{1.0f};
            float* const f = &m[0].x;
            for (std::size_t i = 0; i < 16; ++i) {
                f[i] = v->at(i);
            }
            return m;
        }
    }
    return std::nullopt;
}

auto parse_value(const Dependency_property& property, const std::string_view text) -> std::optional<Property_value>
{
    return parse_value(property.get_type(), text, property.get_enum_info());
}

auto parse_value(const Dependency_object& context, const Dependency_property& property, const std::string_view text_in) -> std::optional<Property_value>
{
    if (property.get_type() != Property_type::object) {
        return parse_value(property.get_type(), text_in, property.get_enum_info());
    }
    const std::string_view text = trim(text_in);
    if (text.empty()) {
        return Object_reference{};
    }
    const Dependency_object* const resolved = context.resolve_expression_object(text);
    if (resolved == nullptr) {
        return std::nullopt;
    }
    std::shared_ptr<Dependency_object> shared = resolved->get_shared_reference();
    if (!shared) {
        return std::nullopt;
    }
    return Object_reference{std::move(shared)};
}

} // namespace erhe::property
