#include "level/foliage_palette.hpp"

#include "level/kit_layout_values.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_repeat.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace editor {

namespace {

using json = nlohmann::json;

constexpr const char* c_keys[] = {
    "materials", "parameters", "radius", "seed", "shape", "variants", "weight", "yaw_max", "yaw_min"
};

[[nodiscard]] auto is_known_key(const std::string& key) -> bool
{
    for (const char* known : c_keys) {
        if (key == known) {
            return true;
        }
    }
    return false;
}

// A pass over the text before the DOM parse: the position of a syntax error and the first key an object repeats.
class Text_checker : public nlohmann::json_sax<json>
{
public:
    auto null           () -> bool override                                 { return true; }
    auto boolean        (bool) -> bool override                             { return true; }
    auto number_integer (number_integer_t) -> bool override                 { return true; }
    auto number_unsigned(number_unsigned_t) -> bool override                { return true; }
    auto number_float   (number_float_t, const string_t&) -> bool override  { return true; }
    auto string         (string_t&) -> bool override                        { return true; }
    auto binary         (binary_t&) -> bool override                        { return true; }
    auto start_array    (std::size_t) -> bool override                      { return true; }
    auto end_array      () -> bool override                                 { return true; }

    auto start_object(std::size_t) -> bool override
    {
        if (m_depth == m_object_keys.size()) {
            m_object_keys.emplace_back();
        }
        m_object_keys[m_depth].clear();
        ++m_depth;
        return true;
    }

    auto key(string_t& name) -> bool override
    {
        std::vector<std::string>& keys = m_object_keys[m_depth - 1];
        if (std::find(keys.begin(), keys.end(), name) != keys.end()) {
            m_error = fmt::format("the palette text repeats the key '{}' in one object", name);
            return false;
        }
        keys.push_back(name);
        return true;
    }

    auto end_object() -> bool override
    {
        --m_depth;
        return true;
    }

    auto parse_error(std::size_t position, const std::string& last_token, const nlohmann::detail::exception& error) -> bool override
    {
        // nlohmann::json's id 406: a number the parser cannot hold (1e999).
        if (error.id == 406) {
            m_error = fmt::format("the palette text has a number out of range at byte {}: '{}'", position, last_token);
            return false;
        }
        m_error = fmt::format("the palette text is not JSON: a syntax error at byte {} after '{}'", position, last_token);
        return false;
    }

    [[nodiscard]] auto get_error() const -> const std::string& { return m_error; }

private:
    std::vector<std::vector<std::string>> m_object_keys;
    std::size_t                           m_depth{0};
    std::string                           m_error;
};

// Reads the keys of one entry object; missing keys keep the entry's defaults; the first problem is kept.
class Entry_reader
{
public:
    explicit Entry_reader(const json& object) : m_object{object} {}

    void read_float(const char* key, float& out_value)
    {
        const json* value = find(key);
        if ((value != nullptr) && (!value->is_string() || !parse_hexfloat(value->get<std::string>(), out_value))) {
            fail(fmt::format("'{}' is not a hexadecimal float string", key));
        }
    }

    void read_int(const char* key, long long min_value, long long max_value, long long& out_value)
    {
        const json* value = find(key);
        if (value == nullptr) {
            return;
        }
        if (!value->is_number_integer()) {
            fail(fmt::format("'{}' is not an integer", key));
            return;
        }
        // An unsigned number above the signed range would wrap in get<long long>().
        const bool too_large = value->is_number_unsigned() &&
            (value->get<unsigned long long>() > static_cast<unsigned long long>(std::numeric_limits<long long>::max()));
        const long long number = too_large ? max_value : value->get<long long>();
        if (too_large || (number < min_value) || (number > max_value)) {
            fail(fmt::format("'{}' is {}, outside {} - {}", key, value->dump(), min_value, max_value));
            return;
        }
        out_value = number;
    }

    void read_string(const char* key, std::string& out_value)
    {
        const json* value = find(key);
        if ((value != nullptr) && !value->is_string()) {
            fail(fmt::format("'{}' is not a string", key));
            return;
        }
        if (value != nullptr) {
            out_value = value->get<std::string>();
        }
    }

    [[nodiscard]] auto find(const char* key) const -> const json*
    {
        return m_object.contains(key) ? &m_object.at(key) : nullptr;
    }

    void fail(std::string message)
    {
        if (m_error.empty()) {
            m_error = std::move(message);
        }
    }

    [[nodiscard]] auto get_error() const -> const std::string& { return m_error; }

private:
    const json& m_object;
    std::string m_error;
};

[[nodiscard]] auto read_entry(const json& object, Foliage_palette_entry& out_entry) -> std::string
{
    if (!object.is_object()) {
        return "it is not an object";
    }
    for (json::const_iterator it = object.begin(), end = object.end(); it != end; ++it) {
        if (!is_known_key(it.key())) {
            return fmt::format("'{}' is not an entry key", it.key());
        }
    }
    Foliage_palette_entry entry;
    Entry_reader          reader{object};
    if (reader.find("shape") == nullptr) {
        return "'shape' is missing";
    }
    reader.read_string("shape", entry.shape);
    const json* parameters = reader.find("parameters");
    if (parameters != nullptr) {
        if (parameters->is_string()) {
            entry.parameters = parameters->get<std::string>();
        } else {
            reader.fail("'parameters' is not a string (the parameters' canonical text)");
        }
    }
    const json* materials = reader.find("materials");
    if (materials != nullptr) {
        if (!materials->is_array()) {
            reader.fail("'materials' is not a list");
        } else {
            for (const json& name : *materials) {
                if (!name.is_string()) {
                    reader.fail("'materials' is not a list of names");
                    break;
                }
                entry.materials.push_back(name.get<std::string>());
            }
        }
    }
    long long weight   = entry.weight;
    long long variants = entry.variants;
    long long seed     = entry.seed;
    reader.read_int("weight",   0, std::numeric_limits<std::uint32_t>::max(), weight);
    reader.read_int("variants", std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), variants);
    reader.read_int("seed",     std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), seed);
    reader.read_float("radius",  entry.radius);
    reader.read_float("yaw_min", entry.yaw_min_degrees);
    reader.read_float("yaw_max", entry.yaw_max_degrees);
    if (!reader.get_error().empty()) {
        return reader.get_error();
    }
    entry.weight   = static_cast<std::uint32_t>(weight);
    entry.variants = static_cast<int>(variants);
    entry.seed     = static_cast<int>(seed);
    out_entry = std::move(entry);
    return {};
}

[[nodiscard]] auto check_entry(const Kit_shape_library& library, const Foliage_palette_entry& entry) -> std::string
{
    const Kit_shape_definition* const shape = library.find(entry.shape);
    if (shape == nullptr) {
        return fmt::format("the kits have no shape '{}'", entry.shape);
    }
    // The default level lengths: begin() checks the variants again at the painted scene's lengths.
    const Kit_lengths    lengths{};
    Kit_parameter_values values = make_default_values(*shape, lengths.module, lengths.wall_height);
    if (!entry.parameters.empty()) {
        const std::string error = read_kit_piece_record(library, entry.shape, entry.parameters, values);
        if (!error.empty()) {
            return error;
        }
    }
    if (entry.weight > c_max_scatter_weight) {
        return fmt::format("the weight is {}, outside 0 - {}", entry.weight, c_max_scatter_weight);
    }
    if (!std::isfinite(entry.radius) || (entry.radius < 0.0f)) {
        return fmt::format("the radius is {}, not a length of 0 m or more", entry.radius);
    }
    if (!std::isfinite(entry.yaw_min_degrees) || !std::isfinite(entry.yaw_max_degrees)) {
        return "the yaw limits are not finite";
    }
    if (entry.yaw_min_degrees > entry.yaw_max_degrees) {
        return fmt::format("the yaw limits are out of order: {} > {}", entry.yaw_min_degrees, entry.yaw_max_degrees);
    }
    if ((entry.variants < c_foliage_min_variants) || (entry.variants > c_foliage_max_variants)) {
        return fmt::format("the variants are {}, outside {} - {}", entry.variants, c_foliage_min_variants, c_foliage_max_variants);
    }
    if (entry.materials.size() > shape->material_slots.size()) {
        return fmt::format("{} materials for the shape's {} slots", entry.materials.size(), shape->material_slots.size());
    }
    // FD7.2: each variant's values, as Foliage_stroke::begin() derives them at the painted scene's lengths.
    std::vector<Kit_parameter_values> variants;
    return derive_foliage_variants(entry, values, lengths, variants);
}

} // namespace

auto make_foliage_palette_text(const std::span<const Foliage_palette_entry> entries) -> std::string
{
    json list = json::array();
    for (const Foliage_palette_entry& entry : entries) {
        // nlohmann::json objects keep their keys sorted.
        json object{
            {"shape",      entry.shape},
            {"parameters", entry.parameters},
            {"materials",  entry.materials},
            {"weight",     entry.weight},
            {"radius",     format_hexfloat(entry.radius)},
            {"yaw_min",    format_hexfloat(entry.yaw_min_degrees)},
            {"yaw_max",    format_hexfloat(entry.yaw_max_degrees)},
            {"variants",   entry.variants},
            {"seed",       entry.seed}
        };
        list.push_back(std::move(object));
    }
    return list.dump();
}

auto parse_foliage_palette_text(const std::string_view text, std::vector<Foliage_palette_entry>& out_entries) -> std::string
{
    Text_checker checker;
    if (!json::sax_parse(text, &checker)) {
        return checker.get_error();
    }
    const json list = json::parse(text, nullptr, false);
    if (!list.is_array()) {
        return "the palette text is not a JSON list";
    }
    std::vector<Foliage_palette_entry> entries;
    for (std::size_t i = 0, end = list.size(); i < end; ++i) {
        Foliage_palette_entry entry;
        const std::string error = read_entry(list[i], entry);
        if (!error.empty()) {
            return fmt::format("entry {}: {}", i, error);
        }
        entries.push_back(std::move(entry));
    }
    out_entries = std::move(entries);
    return {};
}

auto check_foliage_palette(const Kit_shape_library& library, const std::span<const Foliage_palette_entry> entries) -> std::string
{
    for (std::size_t i = 0, end = entries.size(); i < end; ++i) {
        const std::string error = check_entry(library, entries[i]);
        if (!error.empty()) {
            return fmt::format("entry {} ('{}'): {}", i, entries[i].shape, error);
        }
    }
    return {};
}

auto get_foliage_variant_seed(const int entry_seed, const std::uint32_t variant) -> int
{
    return derive_layout_element_seed(entry_seed, static_cast<std::size_t>(variant));
}

auto is_foliage_seed_derived(const Kit_parameter_values& values) -> bool
{
    const Kit_shape_definition* const shape = values.get_shape();
    return (values.get_derive_mask() != 0) || ((shape != nullptr) && (shape->randomness == Kit_randomness::seeded));
}

auto get_foliage_variant_count(const Foliage_palette_entry& entry, const Kit_parameter_values& values) -> std::uint32_t
{
    if (!is_foliage_seed_derived(values)) {
        return 1;
    }
    return static_cast<std::uint32_t>(std::clamp(entry.variants, c_foliage_min_variants, c_foliage_max_variants));
}

auto derive_foliage_variants(
    const Foliage_palette_entry&       entry,
    const Kit_parameter_values&        base,
    const Kit_lengths&                 lengths,
    std::vector<Kit_parameter_values>& out_variants
) -> std::string
{
    out_variants.clear();
    const Kit_shape_definition* const shape   = base.get_shape();
    const bool                        derives = is_foliage_seed_derived(base);
    const std::uint32_t               count   = get_foliage_variant_count(entry, base);
    for (std::uint32_t variant = 0; variant < count; ++variant) {
        // The variant's seed goes through the shape's derive-from-seed as a layout's Per Piece Seed does.
        Kit_parameter_values values = base;
        if (derives) {
            static_cast<void>(values.set_int("seed", get_foliage_variant_seed(entry.seed, variant)));
            values.resolve_derived(lengths.module, lengths.wall_height);
        }
        std::string error = values.validate();
        if (error.empty() && (shape != nullptr) && shape->check_combination) {
            error = shape->check_combination(values);
        }
        if (!error.empty()) {
            out_variants.clear();
            return fmt::format("variant {}: {}", variant, error);
        }
        out_variants.push_back(std::move(values));
    }
    return {};
}

} // namespace editor
