#include "level/kit_shape.hpp"
#include "level/kit_mesh_builder.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace editor {

auto c_str(const Kit_parameter_type type) -> const char*
{
    switch (type) {
        case Kit_parameter_type::float_value: return "float";
        case Kit_parameter_type::int_value:   return "int";
        case Kit_parameter_type::enum_value:  return "enum";
        default:                              return "?";
    }
}

auto c_str(const Kit_unit unit) -> const char*
{
    switch (unit) {
        case Kit_unit::meters:  return "m";
        case Kit_unit::degrees: return "degrees";
        case Kit_unit::count:   return "count";
        case Kit_unit::ratio:   return "ratio";
        case Kit_unit::none:    return "";
        default:                return "?";
    }
}

auto c_str(const Kit_default_scale scale) -> const char*
{
    switch (scale) {
        case Kit_default_scale::meters:       return "meters";
        case Kit_default_scale::modules:      return "modules";
        case Kit_default_scale::wall_heights: return "wall_heights";
        default:                              return "?";
    }
}

auto c_str(const Kit_normal_style style) -> const char*
{
    switch (style) {
        case Kit_normal_style::faceted:                return "faceted";
        case Kit_normal_style::smooth:                 return "smooth";
        case Kit_normal_style::smooth_with_hard_edges: return "smooth_with_hard_edges";
        default:                                       return "?";
    }
}

auto c_str(const Kit_shading shading) -> const char*
{
    switch (shading) {
        case Kit_shading::faceted: return "faceted";
        case Kit_shading::smooth:  return "smooth";
        default:                   return "?";
    }
}

auto c_str(const Kit_side side) -> const char*
{
    switch (side) {
        case Kit_side::x_neg: return "-x";
        case Kit_side::x_pos: return "+x";
        case Kit_side::y_neg: return "-y";
        case Kit_side::y_pos: return "+y";
        case Kit_side::z_neg: return "-z";
        case Kit_side::z_pos: return "+z";
        default:              return "?";
    }
}

auto c_str(const Kit_collision collision) -> const char*
{
    switch (collision) {
        case Kit_collision::convex_hull:   return "convex_hull";
        case Kit_collision::triangle_mesh: return "triangle_mesh";
        case Kit_collision::none:          return "none";
        case Kit_collision::box:           return "box";
        default:                           return "?";
    }
}

auto c_str(const Kit_mesh_kind mesh_kind) -> const char*
{
    switch (mesh_kind) {
        case Kit_mesh_kind::solid:   return "solid";
        case Kit_mesh_kind::surface: return "surface";
        default:                     return "?";
    }
}

auto c_str(const Kit_derive derive) -> const char*
{
    switch (derive) {
        case Kit_derive::fixed:     return "fixed";
        case Kit_derive::derivable: return "derivable";
        default:                    return "?";
    }
}

auto c_str(const Kit_mirror_plane plane) -> const char*
{
    switch (plane) {
        case Kit_mirror_plane::x: return "x";
        case Kit_mirror_plane::z: return "z";
        default:                  return "?";
    }
}

auto kit_next_seed(const int seed) -> int
{
    Kit_random random{seed, "next_seed"};
    const int next = static_cast<int>(random.next_u32() % static_cast<uint32_t>(kit_seed_max + 1));
    return (next != seed) ? next : ((next + 1) % (kit_seed_max + 1));
}

namespace {

[[nodiscard]] auto scale_factor(const Kit_default_scale scale, const float module, const float wall_height) -> float
{
    switch (scale) {
        case Kit_default_scale::modules:      return module;
        case Kit_default_scale::wall_heights: return wall_height;
        case Kit_default_scale::meters:
        default:                              return 1.0f;
    }
}

} // anonymous namespace

auto Kit_parameter::resolve_float_default(const float module, const float wall_height) const -> float
{
    return float_default * scale_factor(default_scale, module, wall_height);
}

auto Kit_parameter::resolve_derive_range(const float module, const float wall_height) const -> Kit_derive_range
{
    const float factor = scale_factor(default_scale, module, wall_height);
    const float lo     = std::clamp(derive_float_min * factor, float_min, float_max);
    const float hi     = std::clamp(derive_float_max * factor, lo, float_max);
    return Kit_derive_range{.min_value = lo, .max_value = hi, .step_value = derive_float_step * factor};
}

auto derive_parameter_value(const Kit_parameter& parameter, const int seed, const float module, const float wall_height) -> Kit_parameter_value
{
    // The stream depends on the seed and the name only, so adding a parameter changes no other derived value.
    Kit_random random{seed, parameter.name};
    const uint32_t draw = random.next_u32();
    Kit_parameter_value value{};
    switch (parameter.type) {
        case Kit_parameter_type::float_value: {
            const Kit_derive_range range = parameter.resolve_derive_range(module, wall_height);
            if (!(range.step_value > 0.0f) || !(range.max_value > range.min_value)) {
                value.float_value = range.min_value;
                break;
            }
            // Quantized to the step, so mesh keys repeat and values read cleanly.
            const uint32_t steps = static_cast<uint32_t>(std::floor(((range.max_value - range.min_value) / range.step_value) + 1.0e-3f));
            const uint32_t k     = draw % (steps + 1u);
            value.float_value = std::min(range.max_value, range.min_value + (static_cast<float>(k) * range.step_value));
            break;
        }
        case Kit_parameter_type::int_value: {
            const int lo    = std::clamp(parameter.derive_int_min, parameter.int_min, parameter.int_max);
            const int hi    = std::clamp(parameter.derive_int_max, lo, parameter.int_max);
            const uint32_t count = static_cast<uint32_t>(hi - lo) + 1u;
            value.int_value = lo + static_cast<int>(draw % count);
            break;
        }
        case Kit_parameter_type::enum_value: {
            const uint32_t count = static_cast<uint32_t>(std::max<std::size_t>(1, parameter.enum_values.size()));
            value.int_value = static_cast<int>(draw % count);
            break;
        }
        default: {
            break;
        }
    }
    return value;
}

auto operator==(const Kit_parameter_value& lhs, const Kit_parameter_value& rhs) -> bool
{
    // Bitwise, so -0.0 and 0.0 differ exactly as their canonical texts do.
    uint32_t lhs_bits = 0;
    uint32_t rhs_bits = 0;
    std::memcpy(&lhs_bits, &lhs.float_value, sizeof(float));
    std::memcpy(&rhs_bits, &rhs.float_value, sizeof(float));
    return (lhs_bits == rhs_bits) && (lhs.int_value == rhs.int_value);
}

auto Kit_shape_definition::find_parameter(const std::string_view name) const -> std::optional<std::size_t>
{
    for (std::size_t i = 0, end = parameters.size(); i < end; ++i) {
        if (parameters[i].name == name) {
            return i;
        }
    }
    return {};
}

auto Kit_shape_definition::has_tag(const std::string_view tag) const -> bool
{
    for (const std::string& t : tags) {
        if (t == tag) {
            return true;
        }
    }
    return false;
}

Kit_parameter_values::Kit_parameter_values(const Kit_shape_definition& shape)
    : m_shape {&shape}
    , m_values{shape.parameters.size()}
{
}

auto Kit_parameter_values::get_shape() const -> const Kit_shape_definition*
{
    return m_shape;
}

auto Kit_parameter_values::get_values() const -> const std::vector<Kit_parameter_value>&
{
    return m_values;
}

auto Kit_parameter_values::find_index(const std::string_view name, const Kit_parameter_type type) const -> std::optional<std::size_t>
{
    if (m_shape == nullptr) {
        return {};
    }
    const std::optional<std::size_t> index = m_shape->find_parameter(name);
    if (!index.has_value() || (m_shape->parameters[index.value()].type != type)) {
        return {};
    }
    return index;
}

auto Kit_parameter_values::get_float(const std::string_view name) const -> float
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::float_value);
    return index.has_value() ? m_values[index.value()].float_value : 0.0f;
}

auto Kit_parameter_values::get_int(const std::string_view name) const -> int
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::int_value);
    return index.has_value() ? m_values[index.value()].int_value : 0;
}

auto Kit_parameter_values::get_enum(const std::string_view name) const -> std::string_view
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::enum_value);
    if (!index.has_value()) {
        return {};
    }
    const Kit_parameter& parameter = m_shape->parameters[index.value()];
    const int            value     = m_values[index.value()].int_value;
    if ((value < 0) || (static_cast<std::size_t>(value) >= parameter.enum_values.size())) {
        return {};
    }
    return parameter.enum_values[static_cast<std::size_t>(value)];
}

auto Kit_parameter_values::set_float(const std::string_view name, const float value) -> bool
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::float_value);
    if (!index.has_value()) {
        return false;
    }
    m_values[index.value()].float_value = value;
    return true;
}

auto Kit_parameter_values::set_int(const std::string_view name, const int value) -> bool
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::int_value);
    if (!index.has_value()) {
        return false;
    }
    m_values[index.value()].int_value = value;
    return true;
}

auto Kit_parameter_values::set_enum(const std::string_view name, const std::string_view value_name) -> bool
{
    const std::optional<std::size_t> index = find_index(name, Kit_parameter_type::enum_value);
    if (!index.has_value()) {
        return false;
    }
    const std::vector<std::string>& names = m_shape->parameters[index.value()].enum_values;
    for (std::size_t i = 0, end = names.size(); i < end; ++i) {
        if (names[i] == value_name) {
            m_values[index.value()].int_value = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

void Kit_parameter_values::set_value(const std::size_t index, const Kit_parameter_value& value)
{
    if (index < m_values.size()) {
        m_values[index] = value;
    }
}

auto Kit_parameter_values::get_value_source(const std::size_t index) const -> Kit_value_source
{
    return ((index < 64) && ((m_derive_mask & (uint64_t{1} << index)) != 0)) ? Kit_value_source::seed : Kit_value_source::explicit_value;
}

void Kit_parameter_values::set_value_source(const std::size_t index, const Kit_value_source source)
{
    if (index >= 64) {
        return;
    }
    const uint64_t bit = uint64_t{1} << index;
    m_derive_mask = (source == Kit_value_source::seed) ? (m_derive_mask | bit) : (m_derive_mask & ~bit);
}

auto Kit_parameter_values::get_derive_mask() const -> uint64_t
{
    return m_derive_mask;
}

auto Kit_parameter_values::get_seed() const -> int
{
    return get_int("seed");
}

void Kit_parameter_values::resolve_derived(const float module, const float wall_height)
{
    if (m_shape == nullptr) {
        return;
    }
    const int seed = get_seed();
    for (std::size_t i = 0, end = std::min(m_values.size(), m_shape->parameters.size()); i < end; ++i) {
        const Kit_parameter& parameter = m_shape->parameters[i];
        if ((get_value_source(i) == Kit_value_source::seed) && (parameter.derive == Kit_derive::derivable)) {
            m_values[i] = derive_parameter_value(parameter, seed, module, wall_height);
        }
    }
}

namespace {

// The value a text that leaves the parameter out reads as: an unless_default float or enum parameter's default.
[[nodiscard]] auto get_omitted_value(const Kit_parameter& parameter) -> std::optional<Kit_parameter_value>
{
    if (parameter.text_presence != Kit_text_presence::unless_default) {
        return {};
    }
    switch (parameter.type) {
        case Kit_parameter_type::float_value: return Kit_parameter_value{.float_value = parameter.float_default};
        case Kit_parameter_type::enum_value:  return Kit_parameter_value{.int_value = parameter.int_default};
        default:                              return {};
    }
}

auto format_number(const float value) -> std::string
{
    return fmt::format("{}", value);
}

auto unit_suffix(const Kit_unit unit) -> std::string
{
    const char* const text = c_str(unit);
    return (text[0] == '\0') ? std::string{} : fmt::format(" {}", text);
}

} // anonymous namespace

auto Kit_parameter_values::validate() const -> std::string
{
    if (m_shape == nullptr) {
        return "Parameter values have no shape";
    }
    if (m_values.size() != m_shape->parameters.size()) {
        return fmt::format("{}: expected {} parameter values, got {}", m_shape->id, m_shape->parameters.size(), m_values.size());
    }
    for (std::size_t i = 0, end = m_values.size(); i < end; ++i) {
        const Kit_parameter&       parameter = m_shape->parameters[i];
        const Kit_parameter_value& value     = m_values[i];
        switch (parameter.type) {
            case Kit_parameter_type::float_value: {
                // L23: the declared range is where the UI's drags run, not a limit: any finite value of the range's sign is
                // taken (a range starting above zero takes any positive value, one starting at zero any non-negative one).
                const float v = value.float_value;
                const bool  positive     = (parameter.float_min > 0.0f);
                const bool  non_negative = (parameter.float_min >= 0.0f);
                if (!std::isfinite(v) || (positive && !(v > 0.0f)) || (non_negative && (v < 0.0f))) {
                    return fmt::format(
                        "{}: parameter '{}' is {}{}; it must be a finite number{}",
                        m_shape->id, parameter.name, format_number(v), unit_suffix(parameter.unit),
                        positive ? " above zero" : (non_negative ? " of zero or more" : "")
                    );
                }
                break;
            }
            case Kit_parameter_type::int_value: {
                if ((value.int_value < parameter.int_min) || (value.int_value > parameter.int_max)) {
                    return fmt::format(
                        "{}: parameter '{}' is {}, outside its range {} - {}{}",
                        m_shape->id, parameter.name, value.int_value,
                        parameter.int_min, parameter.int_max, unit_suffix(parameter.unit)
                    );
                }
                break;
            }
            case Kit_parameter_type::enum_value: {
                if ((value.int_value < 0) || (static_cast<std::size_t>(value.int_value) >= parameter.enum_values.size())) {
                    return fmt::format(
                        "{}: parameter '{}' has value index {}, outside its values {}",
                        m_shape->id, parameter.name, value.int_value, fmt::join(parameter.enum_values, ", ")
                    );
                }
                break;
            }
            default: {
                return fmt::format("{}: parameter '{}' has an unknown type", m_shape->id, parameter.name);
            }
        }
    }
    for (std::size_t i = 0; i < 64; ++i) {
        if (get_value_source(i) != Kit_value_source::seed) {
            continue;
        }
        if (i >= m_shape->parameters.size()) {
            return fmt::format("{}: the derive mask names parameter index {}, past the {} parameters", m_shape->id, i, m_shape->parameters.size());
        }
        if (m_shape->parameters[i].derive != Kit_derive::derivable) {
            return fmt::format("{}: parameter '{}' cannot be derived from the seed", m_shape->id, m_shape->parameters[i].name);
        }
    }
    if (m_shape->check_combination) {
        return m_shape->check_combination(*this);
    }
    return {};
}

auto format_hexfloat(const float value) -> std::string
{
    if (!std::isfinite(value)) {
        return std::isnan(value) ? std::string{"nan"} : (value < 0.0f) ? std::string{"-inf"} : std::string{"inf"};
    }
    std::string text;
    if (std::signbit(value)) {
        text.push_back('-');
    }
    text.append("0x");
    std::array<char, 64> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::fabs(value), std::chars_format::hex);
    text.append(buffer.data(), result.ptr);
    return text;
}

auto parse_hexfloat(const std::string_view text, float& out_value) -> bool
{
    std::string_view rest = text;
    bool negative = false;
    if (!rest.empty() && (rest.front() == '-')) {
        negative = true;
        rest.remove_prefix(1);
    }
    if ((rest.size() < 3) || (rest[0] != '0') || ((rest[1] != 'x') && (rest[1] != 'X'))) {
        return false;
    }
    // Floating point std::from_chars is missing from the Android NDK libc++;
    // strtof on a null-terminated copy parses the same "0x" literal.
    const std::string copy{rest};
    char* end = nullptr;
    const float value = std::strtof(copy.c_str(), &end);
    if ((end != (copy.c_str() + copy.size())) || !std::isfinite(value)) {
        return false;
    }
    out_value = negative ? -value : value;
    return true;
}

auto Kit_parameter_values::to_canonical_text() const -> std::string
{
    if (m_shape == nullptr) {
        return "{}";
    }
    std::string text{"{"};
    bool        first_value = true;
    for (std::size_t i = 0, end = m_values.size(); i < end; ++i) {
        const Kit_parameter&       parameter = m_shape->parameters[i];
        const Kit_parameter_value& value     = m_values[i];
        const std::optional<Kit_parameter_value> omitted = get_omitted_value(parameter);
        const bool is_omitted_value = omitted.has_value() && (
            (parameter.type == Kit_parameter_type::float_value) ? (value.float_value == omitted->float_value) : (value.int_value == omitted->int_value)
        );
        if (is_omitted_value && (get_value_source(i) == Kit_value_source::explicit_value)) {
            continue;
        }
        if (!first_value) {
            text.push_back(',');
        }
        first_value = false;
        text.append(fmt::format("\"{}\":", parameter.name));
        switch (parameter.type) {
            case Kit_parameter_type::float_value: {
                text.append(fmt::format("\"{}\"", format_hexfloat(value.float_value)));
                break;
            }
            case Kit_parameter_type::int_value: {
                text.append(fmt::format("{}", value.int_value));
                break;
            }
            case Kit_parameter_type::enum_value: {
                const bool in_range = (value.int_value >= 0) && (static_cast<std::size_t>(value.int_value) < parameter.enum_values.size());
                text.append(fmt::format("\"{}\"", in_range ? parameter.enum_values[static_cast<std::size_t>(value.int_value)] : std::string{}));
                break;
            }
            default: {
                text.append("null");
                break;
            }
        }
    }
    if (m_derive_mask != 0) {
        text.append(",\"derive\":[");
        bool first = true;
        for (std::size_t i = 0, end = m_values.size(); i < end; ++i) {
            if (get_value_source(i) == Kit_value_source::seed) {
                text.append(fmt::format("{}\"{}\"", first ? "" : ",", m_shape->parameters[i].name));
                first = false;
            }
        }
        text.push_back(']');
    }
    text.push_back('}');
    return text;
}

auto Kit_parameter_values::operator==(const Kit_parameter_values& other) const -> bool
{
    return (m_shape == other.m_shape) && (m_values == other.m_values) && (m_derive_mask == other.m_derive_mask);
}

auto make_default_values(const Kit_shape_definition& shape, const float module, const float wall_height) -> Kit_parameter_values
{
    Kit_parameter_values values{shape};
    for (std::size_t i = 0, end = shape.parameters.size(); i < end; ++i) {
        const Kit_parameter& parameter = shape.parameters[i];
        Kit_parameter_value  value{};
        if (parameter.type == Kit_parameter_type::float_value) {
            value.float_value = parameter.resolve_float_default(module, wall_height);
        } else {
            value.int_value = parameter.int_default;
        }
        values.set_value(i, value);
        if ((parameter.derive == Kit_derive::derivable) && (parameter.default_source == Kit_value_source::seed)) {
            values.set_value_source(i, Kit_value_source::seed);
        }
    }
    values.resolve_derived(module, wall_height);
    return values;
}

namespace {

// Reader for the flat JSON object of to_canonical_text(): string and integer values only.
class Canonical_reader
{
public:
    explicit Canonical_reader(const std::string_view text) : m_text{text} {}

    void skip_space()
    {
        while ((m_position < m_text.size()) && ((m_text[m_position] == ' ') || (m_text[m_position] == '\t') || (m_text[m_position] == '\n') || (m_text[m_position] == '\r'))) {
            ++m_position;
        }
    }

    auto expect(const char c) -> bool
    {
        skip_space();
        if ((m_position < m_text.size()) && (m_text[m_position] == c)) {
            ++m_position;
            return true;
        }
        return false;
    }

    auto peek() -> char
    {
        skip_space();
        return (m_position < m_text.size()) ? m_text[m_position] : '\0';
    }

    auto read_string(std::string_view& out) -> bool
    {
        if (!expect('"')) {
            return false;
        }
        const std::size_t start = m_position;
        while ((m_position < m_text.size()) && (m_text[m_position] != '"')) {
            if (m_text[m_position] == '\\') {
                return false;
            }
            ++m_position;
        }
        if (m_position >= m_text.size()) {
            return false;
        }
        out = m_text.substr(start, m_position - start);
        ++m_position;
        return true;
    }

    auto read_int(int& out) -> bool
    {
        skip_space();
        const char* const first = m_text.data() + m_position;
        const char* const last  = m_text.data() + m_text.size();
        const std::from_chars_result result = std::from_chars(first, last, out);
        if (result.ec != std::errc{}) {
            return false;
        }
        m_position += static_cast<std::size_t>(result.ptr - first);
        return true;
    }

    [[nodiscard]] auto at_end() -> bool
    {
        skip_space();
        return m_position == m_text.size();
    }

private:
    std::string_view m_text;
    std::size_t      m_position{0};
};

// The "derive" list: masked parameter names, each once.
[[nodiscard]] auto read_derive_list(const Kit_shape_definition& shape, Canonical_reader& reader, Kit_parameter_values& values) -> std::string
{
    if (!reader.expect('[')) {
        return fmt::format("{}: 'derive' is not a list of parameter names", shape.id);
    }
    if (reader.expect(']')) {
        return {};
    }
    for (;;) {
        std::string_view name;
        if (!reader.read_string(name)) {
            return fmt::format("{}: 'derive' is not a list of parameter names", shape.id);
        }
        const std::optional<std::size_t> index = shape.find_parameter(name);
        if (!index.has_value()) {
            return fmt::format("{}: 'derive' names unknown parameter '{}'", shape.id, name);
        }
        if (values.get_value_source(index.value()) == Kit_value_source::seed) {
            return fmt::format("{}: 'derive' names parameter '{}' twice", shape.id, name);
        }
        values.set_value_source(index.value(), Kit_value_source::seed);
        if (reader.expect(',')) {
            continue;
        }
        if (reader.expect(']')) {
            return {};
        }
        return fmt::format("{}: 'derive' is not a list of parameter names", shape.id);
    }
}

} // anonymous namespace

auto parse_canonical_text(const Kit_shape_definition& shape, const std::string_view text, Kit_parameter_values& out_values) -> std::string
{
    Kit_parameter_values values{shape};
    std::vector<bool>    seen(shape.parameters.size(), false);
    bool                 seen_derive{false};
    Canonical_reader     reader{text};
    if (!reader.expect('{')) {
        return fmt::format("{}: parameter text does not start with '{{'", shape.id);
    }
    if (reader.peek() != '}') {
        for (;;) {
            std::string_view key;
            if (!reader.read_string(key) || !reader.expect(':')) {
                return fmt::format("{}: malformed parameter text", shape.id);
            }
            if (key == "derive") {
                if (seen_derive) {
                    return fmt::format("{}: 'derive' appears twice", shape.id);
                }
                seen_derive = true;
                const std::string error = read_derive_list(shape, reader, values);
                if (!error.empty()) {
                    return error;
                }
                if (reader.expect(',')) {
                    continue;
                }
                break;
            }
            const std::optional<std::size_t> index = shape.find_parameter(key);
            if (!index.has_value()) {
                return fmt::format("{}: unknown parameter '{}'", shape.id, key);
            }
            if (seen[index.value()]) {
                return fmt::format("{}: parameter '{}' appears twice", shape.id, key);
            }
            seen[index.value()] = true;
            const Kit_parameter& parameter = shape.parameters[index.value()];
            Kit_parameter_value  value{};
            switch (parameter.type) {
                case Kit_parameter_type::float_value: {
                    std::string_view number_text;
                    if (!reader.read_string(number_text) || !parse_hexfloat(number_text, value.float_value)) {
                        return fmt::format("{}: parameter '{}' is not a hexadecimal float string", shape.id, key);
                    }
                    break;
                }
                case Kit_parameter_type::int_value: {
                    if (!reader.read_int(value.int_value)) {
                        return fmt::format("{}: parameter '{}' is not an integer", shape.id, key);
                    }
                    break;
                }
                case Kit_parameter_type::enum_value: {
                    std::string_view value_name;
                    if (!reader.read_string(value_name)) {
                        return fmt::format("{}: parameter '{}' is not a string", shape.id, key);
                    }
                    values.set_value(index.value(), value);
                    if (!values.set_enum(key, value_name)) {
                        return fmt::format("{}: parameter '{}' has no value '{}'", shape.id, key, value_name);
                    }
                    value = values.get_values()[index.value()];
                    break;
                }
                default: {
                    return fmt::format("{}: parameter '{}' has an unknown type", shape.id, key);
                }
            }
            values.set_value(index.value(), value);
            if (reader.expect(',')) {
                continue;
            }
            break;
        }
    }
    if (!reader.expect('}') || !reader.at_end()) {
        return fmt::format("{}: malformed parameter text", shape.id);
    }
    for (std::size_t i = 0, end = seen.size(); i < end; ++i) {
        if (seen[i]) {
            continue;
        }
        const Kit_parameter&                     parameter = shape.parameters[i];
        const std::optional<Kit_parameter_value> omitted   = get_omitted_value(parameter);
        if (!omitted.has_value()) {
            return fmt::format("{}: parameter '{}' is missing", shape.id, parameter.name);
        }
        values.set_value(i, omitted.value());
    }
    out_values = values;
    return {};
}

auto get_kit_detail_factor(const Kit_parameter_values& values) -> float
{
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || (shape->detail_response != Kit_detail_response::scaled)) {
        return 1.0f;
    }
    return std::clamp(values.get_float(c_kit_detail_parameter), c_kit_detail_min, 1.0f) / c_kit_detail_default;
}

auto kit_detail_count(const Kit_parameter_values& values, const int own, const int least, const int most, const int dimensions, const int step) -> int
{
    return kit_detail_count(get_kit_detail_factor(values), own, least, most, dimensions, step);
}

auto kit_detail_count(const float detail_factor, const int own, const int least, const int most, const int dimensions, const int step) -> int
{
    const float factor = std::pow(detail_factor, 1.0f / static_cast<float>(std::max(dimensions, 1)));
    const int   unit   = std::max(step, 1);
    int count = own + (unit * static_cast<int>(std::lround((factor - 1.0f) * static_cast<float>(own) / static_cast<float>(unit))));
    while ((count > most) && ((count - unit) >= own)) {
        count -= unit;
    }
    while ((count < least) && ((count + unit) <= own)) {
        count += unit;
    }
    return std::clamp(count, std::min(least, own), std::max(most, own));
}

auto get_kit_detail_values(const Kit_parameter_values& values) -> Kit_parameter_values
{
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || shape->detail_counts.empty() || (get_kit_detail_factor(values) == 1.0f)) {
        return values;
    }
    Kit_parameter_values scaled = values;
    for (const Kit_detail_count& count : shape->detail_counts) {
        const std::optional<std::size_t> index = shape->find_parameter(count.parameter);
        ERHE_VERIFY(index.has_value() && (shape->parameters[index.value()].type == Kit_parameter_type::int_value));
        const Kit_parameter& parameter = shape->parameters[index.value()];
        const int            own       = values.get_int(count.parameter);
        ERHE_VERIFY(scaled.set_int(count.parameter, kit_detail_count(values, own, std::max(parameter.int_min, count.least), parameter.int_max, count.dimensions, count.step)));
    }
    return scaled;
}

auto get_kit_collision(const Kit_parameter_values& values) -> Kit_collision
{
    const Kit_shape_definition* const shape = values.get_shape();
    if (shape == nullptr) {
        return Kit_collision::none;
    }
    return shape->get_collision ? shape->get_collision(values) : shape->collision;
}

auto get_kit_shading(const Kit_parameter_values& values) -> Kit_normal_style
{
    const Kit_shape_definition* const shape = values.get_shape();
    ERHE_VERIFY(shape != nullptr);
    if (shape->find_parameter(c_kit_shading_parameter).has_value() && (values.get_enum(c_kit_shading_parameter) == c_str(Kit_shading::faceted))) {
        return Kit_normal_style::faceted;
    }
    return shape->normal_style;
}

auto get_kit_geometry_bounds(const Kit_parameter_values& values) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape == nullptr) || !shape->generator) {
        return bounds;
    }
    GEO::Mesh mesh{3, true};
    shape->generator(values, mesh);
    for (const GEO::index_t facet : mesh.facets) {
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
            bounds.include(glm::vec3{p.x, p.y, p.z});
        }
    }
    return bounds;
}

} // namespace editor
