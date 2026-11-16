#include "level/kit_layout_values.hpp"

#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace editor {

namespace {

using json = nlohmann::json;

// An exact fit of the space counts the last element (a fraction of a step in units of steps).
constexpr double c_fit_epsilon = 1.0e-4;

[[nodiscard]] auto hex_json(const float value) -> json
{
    return json(format_hexfloat(value));
}

[[nodiscard]] auto vec3_json(const glm::vec3& value) -> json
{
    return json::array({format_hexfloat(value.x), format_hexfloat(value.y), format_hexfloat(value.z)});
}

[[nodiscard]] auto parse_kind(const std::string& name, Repeat_pattern_kind& out_kind) -> bool
{
    for (const Repeat_pattern_kind kind : {Repeat_pattern_kind::line, Repeat_pattern_kind::grid, Repeat_pattern_kind::ring, Repeat_pattern_kind::path, Repeat_pattern_kind::scatter, Repeat_pattern_kind::segments, Repeat_pattern_kind::stairs}) {
        if (name == c_str(kind)) {
            out_kind = kind;
            return true;
        }
    }
    return false;
}

// Reads the object's keys in turn; the first problem is kept.
class Object_reader
{
public:
    explicit Object_reader(const json& object) : m_object{object} {}

    void read_float(const char* key, float& out_value)
    {
        const json* value = find(key);
        if ((value == nullptr) || !value->is_string() || !parse_hexfloat(value->get<std::string>(), out_value)) {
            fail(fmt::format("'{}' is not a hexadecimal float string", key));
        }
    }

    // A key a pattern text written before it existed lacks: missing keeps the default.
    void read_optional_float(const char* key, float& out_value)
    {
        if (m_object.contains(key)) {
            read_float(key, out_value);
        }
    }

    [[nodiscard]] auto find_optional_string(const char* key) -> std::string
    {
        return m_object.contains(key) ? find_string(key) : std::string{};
    }

    void read_int(const char* key, int& out_value)
    {
        const json* value = find(key);
        if ((value == nullptr) || !value->is_number_integer()) {
            fail(fmt::format("'{}' is not an integer", key));
            return;
        }
        const long long number = value->get<long long>();
        if ((number < std::numeric_limits<int>::min()) || (number > std::numeric_limits<int>::max())) {
            fail(fmt::format("'{}' is out of range", key));
            return;
        }
        out_value = static_cast<int>(number);
    }

    void read_vec3(const json& value, const char* key, glm::vec3& out_value)
    {
        if (!value.is_array() || (value.size() != 3)) {
            fail(fmt::format("'{}' is not 3 hexadecimal float strings", key));
            return;
        }
        for (glm::vec3::length_type i = 0; i < 3; ++i) {
            if (!value[i].is_string() || !parse_hexfloat(value[i].get<std::string>(), out_value[i])) {
                fail(fmt::format("'{}' is not 3 hexadecimal float strings", key));
                return;
            }
        }
    }

    void read_vec3(const char* key, glm::vec3& out_value)
    {
        const json* value = find(key);
        if (value != nullptr) {
            read_vec3(*value, key, out_value);
        }
    }

    [[nodiscard]] auto find_string(const char* key) -> std::string
    {
        const json* value = find(key);
        if ((value == nullptr) || !value->is_string()) {
            fail(fmt::format("'{}' is not a string", key));
            return {};
        }
        return value->get<std::string>();
    }

    [[nodiscard]] auto find(const char* key) -> const json*
    {
        if (!m_object.contains(key)) {
            fail(fmt::format("'{}' is missing", key));
            return nullptr;
        }
        ++m_read_count;
        return &m_object.at(key);
    }

    void fail(std::string message)
    {
        if (m_error.empty()) {
            m_error = std::move(message);
        }
    }

    [[nodiscard]] auto finish(const char* what) -> std::string
    {
        if (m_error.empty() && (m_read_count != m_object.size())) {
            m_error = fmt::format("the {} has keys it does not use", what);
        }
        return m_error;
    }

private:
    const json& m_object;
    std::size_t m_read_count{0};
    std::string m_error;
};

[[nodiscard]] auto count_limit_refusal(const char* kind, const double space, const double step, const double count) -> std::string
{
    return fmt::format(
        "the {} is {:.3f} m long: {:.0f} pieces at a {:.3f} m step, more than the {} pieces one layout places",
        kind, space, count, step, c_max_repeat_count
    );
}

[[nodiscard]] auto stated_count_refusal(const int count) -> std::string
{
    if ((count < 1) || (static_cast<std::size_t>(count) > c_max_repeat_count)) {
        return fmt::format("count is {}, outside 1 - {} (the pieces one layout places)", count, c_max_repeat_count);
    }
    return {};
}

// The pieces a scatter asks for and the ones its spacing lets in (doc/editor/level_tools.md "Scatter layouts").
[[nodiscard]] auto resolve_scatter(
    const Kit_layout_pattern&       pattern,
    const int                       seed,
    const Kit_layout_item_sizes&    items,
    Kit_layout_run&                 out_run,
    std::vector<Repeat_pose>* const out_poses
) -> std::string
{
    Repeat_pattern& run = out_run.pattern;
    run.scatter_items   = items.items;
    run.scatter_region  = pattern.region;
    run.scatter_size_x  = pattern.size_x;
    run.scatter_size_z  = pattern.size_z;
    run.scatter_radius  = pattern.region_radius;
    run.scatter_spacing = pattern.min_spacing;
    run.yaw_min_degrees = pattern.yaw_min;
    run.yaw_max_degrees = pattern.yaw_max;
    run.scatter_seed    = seed;
    const double area = (pattern.region == Repeat_scatter_region::disc)
        ? (std::numbers::pi * static_cast<double>(pattern.region_radius) * static_cast<double>(pattern.region_radius))
        : (static_cast<double>(pattern.size_x) * static_cast<double>(pattern.size_z));
    if (pattern.scatter_mode == Kit_scatter_mode::density) {
        if (!std::isfinite(pattern.density) || !(pattern.density > 0.0f)) {
            return fmt::format("density is {} pieces per square meter, it must be positive", pattern.density);
        }
        if (!std::isfinite(area)) {
            return "the scatter region's area is not finite";
        }
        // At least one piece, as a space shorter than one step holds element 0 alone.
        const double asked  = static_cast<double>(pattern.density) * area; // its own statement: no fused multiply-add
        const double wanted = std::max(1.0, std::floor(asked + 0.5));
        if (wanted > static_cast<double>(c_max_repeat_count)) {
            return fmt::format(
                "density {} over {:.3f} square meters asks for {:.0f} pieces, more than the {} pieces one layout places",
                pattern.density, area, wanted, c_max_repeat_count
            );
        }
        run.count = static_cast<int>(wanted);
    } else {
        const std::string refusal = stated_count_refusal(pattern.count);
        if (!refusal.empty()) {
            return refusal;
        }
        run.count = pattern.count;
    }
    // The count is what the sampling lets in; without out_poses the points go to leaf scratch.
    static thread_local std::vector<Repeat_pose> scratch;
    std::vector<Repeat_pose>& poses = (out_poses != nullptr) ? *out_poses : scratch;
    const std::string error = expand_repeat(run, Repeat_pose{}, poses);
    if (!error.empty()) {
        return error;
    }
    out_run.count     = poses.size();
    out_run.requested = static_cast<std::size_t>(run.count);
    out_run.space     = static_cast<float>(area);
    return {};
}

// Element centers spanning the space from the first to the last: floor(space / step) + 1.
[[nodiscard]] auto span_count(const double space, const double step) -> double
{
    return std::floor((space / step) + c_fit_epsilon) + 1.0;
}

} // anonymous namespace

auto c_str(const Kit_layout_count_mode mode) -> const char*
{
    switch (mode) {
        case Kit_layout_count_mode::count: return "count";
        case Kit_layout_count_mode::gap:   return "gap";
        default:                           return "?";
    }
}

auto c_str(const Kit_scatter_mode mode) -> const char*
{
    switch (mode) {
        case Kit_scatter_mode::count:   return "count";
        case Kit_scatter_mode::density: return "density";
        default:                        return "?";
    }
}

auto c_str(const Repeat_ring_facing facing) -> const char*
{
    switch (facing) {
        case Repeat_ring_facing::outward:        return "outward";
        case Repeat_ring_facing::start_rotation: return "start";
        default:                                 return "?";
    }
}

auto to_canonical_text(const Kit_layout_pattern& pattern) -> std::string
{
    json object{
        {"kind",        c_str(pattern.kind)},
        {"count_mode",  c_str(pattern.count_mode)},
        {"count",       pattern.count},
        {"gap",         hex_json(pattern.gap)},
        {"direction",   vec3_json(pattern.direction)},
        {"step",        hex_json(pattern.step)},
        {"length",      hex_json(pattern.length)},
        {"count_x",     pattern.count_x},
        {"count_z",     pattern.count_z},
        {"spacing_x",   hex_json(pattern.spacing_x)},
        {"spacing_z",   hex_json(pattern.spacing_z)},
        {"radius",      hex_json(pattern.radius)},
        {"arc_degrees", hex_json(pattern.arc_degrees)},
        {"facing",      c_str(pattern.facing)},
        {"orientation", c_str(pattern.orientation)},
        {"scatter_mode",  c_str(pattern.scatter_mode)},
        {"density",       hex_json(pattern.density)},
        {"region",        c_str(pattern.region)},
        {"size_x",        hex_json(pattern.size_x)},
        {"size_z",        hex_json(pattern.size_z)},
        {"region_radius", hex_json(pattern.region_radius)},
        {"min_spacing",   hex_json(pattern.min_spacing)},
        {"yaw_min",       hex_json(pattern.yaw_min)},
        {"yaw_max",       hex_json(pattern.yaw_max)}
    };
    // The stairs values at their defaults are left out, so a text written before stairs is the canonical text of its values.
    if ((pattern.stairs != Stairs_values{}) || !pattern.stairs_material.empty()) {
        object["stairs_width"]     = hex_json(pattern.stairs.width);
        object["stairs_rise"]      = hex_json(pattern.stairs.max_rise);
        object["stairs_tread"]     = hex_json(pattern.stairs.min_tread);
        object["stairs_thickness"] = hex_json(pattern.stairs.thickness);
        object["stairs_tolerance"] = hex_json(pattern.stairs.tolerance);
        object["stairs_material"]  = pattern.stairs_material;
    }
    return object.dump();
}

auto parse_layout_pattern(const std::string_view text, Kit_layout_pattern& out_pattern) -> std::string
{
    const json object = json::parse(text, nullptr, false);
    if (!object.is_object()) {
        return "the pattern text is not a JSON object";
    }
    Kit_layout_pattern pattern;
    Object_reader      reader{object};
    const std::string kind = reader.find_string("kind");
    if (!kind.empty() && !parse_kind(kind, pattern.kind)) {
        reader.fail(fmt::format("unknown pattern kind '{}'", kind));
    }
    const std::string count_mode = reader.find_string("count_mode");
    if (count_mode == "count") {
        pattern.count_mode = Kit_layout_count_mode::count;
    } else if (count_mode == "gap") {
        pattern.count_mode = Kit_layout_count_mode::gap;
    } else if (!count_mode.empty()) {
        reader.fail(fmt::format("unknown count mode '{}'", count_mode));
    }
    reader.read_int  ("count",       pattern.count);
    reader.read_float("gap",         pattern.gap);
    reader.read_vec3 ("direction",   pattern.direction);
    reader.read_float("step",        pattern.step);
    reader.read_float("length",      pattern.length);
    reader.read_int  ("count_x",     pattern.count_x);
    reader.read_int  ("count_z",     pattern.count_z);
    reader.read_float("spacing_x",   pattern.spacing_x);
    reader.read_float("spacing_z",   pattern.spacing_z);
    reader.read_float("radius",      pattern.radius);
    reader.read_float("arc_degrees", pattern.arc_degrees);
    const std::string facing = reader.find_string("facing");
    if (facing == "outward") {
        pattern.facing = Repeat_ring_facing::outward;
    } else if (facing == "start") {
        pattern.facing = Repeat_ring_facing::start_rotation;
    } else if (!facing.empty()) {
        reader.fail(fmt::format("unknown ring facing '{}'", facing));
    }
    const std::string orientation = reader.find_string("orientation");
    if (orientation == c_str(Repeat_path_orientation::upright)) {
        pattern.orientation = Repeat_path_orientation::upright;
    } else if (orientation == c_str(Repeat_path_orientation::tangent)) {
        pattern.orientation = Repeat_path_orientation::tangent;
    } else if (!orientation.empty()) {
        reader.fail(fmt::format("unknown path orientation '{}'", orientation));
    }
    const std::string scatter_mode = reader.find_optional_string("scatter_mode");
    if (scatter_mode == c_str(Kit_scatter_mode::count)) {
        pattern.scatter_mode = Kit_scatter_mode::count;
    } else if (scatter_mode == c_str(Kit_scatter_mode::density)) {
        pattern.scatter_mode = Kit_scatter_mode::density;
    } else if (!scatter_mode.empty()) {
        reader.fail(fmt::format("unknown scatter mode '{}'", scatter_mode));
    }
    const std::string region = reader.find_optional_string("region");
    if (region == c_str(Repeat_scatter_region::rectangle)) {
        pattern.region = Repeat_scatter_region::rectangle;
    } else if (region == c_str(Repeat_scatter_region::disc)) {
        pattern.region = Repeat_scatter_region::disc;
    } else if (!region.empty()) {
        reader.fail(fmt::format("unknown scatter region '{}'", region));
    }
    reader.read_optional_float("density",       pattern.density);
    reader.read_optional_float("size_x",        pattern.size_x);
    reader.read_optional_float("size_z",        pattern.size_z);
    reader.read_optional_float("region_radius", pattern.region_radius);
    reader.read_optional_float("min_spacing",   pattern.min_spacing);
    reader.read_optional_float("yaw_min",       pattern.yaw_min);
    reader.read_optional_float("yaw_max",       pattern.yaw_max);
    reader.read_optional_float("stairs_width",     pattern.stairs.width);
    reader.read_optional_float("stairs_rise",      pattern.stairs.max_rise);
    reader.read_optional_float("stairs_tread",     pattern.stairs.min_tread);
    reader.read_optional_float("stairs_thickness", pattern.stairs.thickness);
    reader.read_optional_float("stairs_tolerance", pattern.stairs.tolerance);
    pattern.stairs_material = reader.find_optional_string("stairs_material");
    const std::string error = reader.finish("pattern");
    if (!error.empty()) {
        return error;
    }
    out_pattern = std::move(pattern);
    return {};
}

auto to_canonical_text(const std::span<const Kit_layout_item> items) -> std::string
{
    json list = json::array();
    for (const Kit_layout_item& item : items) {
        // The parameters are the item's canonical text as an object; a text that is not JSON is kept as a string.
        json parameters = json::parse(item.parameters, nullptr, false);
        if (parameters.is_discarded()) {
            parameters = json(item.parameters);
        }
        json object{{"shape", item.shape}, {"parameters", parameters}, {"materials", item.materials}};
        // The default weight is left out, so a text written before weights is the canonical text of its values.
        if (item.weight != 1) {
            object["weight"] = item.weight;
        }
        list.push_back(std::move(object));
    }
    return list.dump();
}

auto parse_layout_items(const std::string_view text, std::vector<Kit_layout_item>& out_items) -> std::string
{
    const json list = json::parse(text, nullptr, false);
    if (!list.is_array()) {
        return "the items text is not a JSON list";
    }
    std::vector<Kit_layout_item> items;
    for (std::size_t i = 0, end = list.size(); i < end; ++i) {
        const json& object = list[i];
        if (!object.is_object()) {
            return fmt::format("item {} is not an object", i);
        }
        Kit_layout_item item;
        Object_reader   reader{object};
        item.shape = reader.find_string("shape");
        const json* parameters = reader.find("parameters");
        if (parameters != nullptr) {
            if (!parameters->is_object()) {
                reader.fail("'parameters' is not an object");
            } else {
                item.parameters = parameters->dump();
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
                    item.materials.push_back(name.get<std::string>());
                }
            }
        }
        if (object.contains("weight")) {
            int weight = 1;
            reader.read_int("weight", weight);
            if ((weight < 0) || (static_cast<uint32_t>(weight) > c_max_scatter_weight)) {
                reader.fail(fmt::format("'weight' is {}, outside 0 - {}", weight, c_max_scatter_weight));
            } else {
                item.weight = static_cast<uint32_t>(weight);
            }
        }
        const std::string error = reader.finish("item");
        if (!error.empty()) {
            return fmt::format("item {}: {}", i, error);
        }
        items.push_back(std::move(item));
    }
    out_items = std::move(items);
    return {};
}

void transform_curve_path(const erhe::math::Curve_path& path, const glm::mat4& transform, erhe::math::Curve_path& out_path)
{
    out_path.points.clear();
    out_path.closure = path.closure;
    const glm::mat3 linear{transform};
    for (const erhe::math::Curve_point& point : path.points) {
        out_path.points.push_back(
            erhe::math::Curve_point{
                .position   = glm::vec3{transform * glm::vec4{point.position, 1.0f}},
                .in_handle  = linear * point.in_handle,
                .out_handle = linear * point.out_handle,
                .kind       = point.kind
            }
        );
    }
}

namespace {

[[nodiscard]] auto resolve_pattern_run(
    const Kit_layout_pattern&                 pattern,
    const int                                 seed,
    const Kit_layout_item_sizes&              items,
    const erhe::math::Curve_path_table* const path_table,
    Kit_layout_run&                           out_run,
    std::vector<Repeat_pose>* const           out_poses
) -> std::string
{
    Repeat_pattern& run = out_run.pattern;
    run.kind = pattern.kind;
    const float item_extent = items.extent;
    const bool counted  = (pattern.kind == Repeat_pattern_kind::line) || (pattern.kind == Repeat_pattern_kind::ring) || (pattern.kind == Repeat_pattern_kind::path);
    const bool gap_mode = (pattern.count_mode == Kit_layout_count_mode::gap) && counted;
    double     step_min = 0.0;
    if (gap_mode) {
        if (!std::isfinite(pattern.gap)) {
            return "gap must be finite";
        }
        if (!std::isfinite(item_extent) || (item_extent < 0.0f)) {
            return "the item has no usable extent";
        }
        step_min = static_cast<double>(item_extent) + static_cast<double>(pattern.gap);
        if (!(step_min > 0.0)) {
            return fmt::format(
                "gap is {} m with an item extent of {} m: the step is not positive, the gap must be more than -{} m",
                pattern.gap, item_extent, item_extent
            );
        }
    } else if (counted) {
        const std::string refusal = stated_count_refusal(pattern.count);
        if (!refusal.empty()) {
            return refusal;
        }
    }

    switch (pattern.kind) {
        case Repeat_pattern_kind::line: {
            const float direction_length = glm::length(pattern.direction);
            if (!std::isfinite(direction_length) || !(direction_length > 0.0f)) {
                return "direction must be finite and not zero";
            }
            const glm::vec3 direction = pattern.direction / direction_length;
            double count = static_cast<double>(pattern.count);
            double step  = static_cast<double>(pattern.step);
            if (gap_mode) {
                if (!std::isfinite(pattern.length) || (pattern.length < 0.0f)) {
                    return fmt::format("length is {} m, it must be zero or more", pattern.length);
                }
                const double length = static_cast<double>(pattern.length);
                count = span_count(length, step_min);
                if (count > static_cast<double>(c_max_repeat_count)) {
                    return count_limit_refusal("line", length, step_min, count);
                }
                step = (count > 1.0) ? (length / (count - 1.0)) : 0.0;
            } else if (!std::isfinite(pattern.step) || ((pattern.count > 1) && (pattern.step == 0.0f))) {
                return fmt::format("step is {} m, it must be finite and not zero", pattern.step);
            }
            run.count       = static_cast<int>(count);
            run.spacing     = direction * static_cast<float>(step);
            out_run.count   = static_cast<std::size_t>(count);
            out_run.step    = (count > 1.0) ? static_cast<float>(step) : 0.0f;
            out_run.space   = static_cast<float>(step * (count - 1.0));
            return {};
        }
        case Repeat_pattern_kind::grid: {
            run.count_x   = pattern.count_x;
            run.count_z   = pattern.count_z;
            run.spacing_x = pattern.spacing_x;
            run.spacing_z = pattern.spacing_z;
            if ((pattern.count_x < 1) || (pattern.count_z < 1)) {
                return fmt::format("the grid counts are {} x {}, each must be at least 1", pattern.count_x, pattern.count_z);
            }
            const std::size_t count = static_cast<std::size_t>(pattern.count_x) * static_cast<std::size_t>(pattern.count_z);
            if (count > c_max_repeat_count) {
                return fmt::format(
                    "the grid is {} x {}: {} pieces, more than the {} pieces one layout places",
                    pattern.count_x, pattern.count_z, count, c_max_repeat_count
                );
            }
            out_run.count = count;
            return {};
        }
        case Repeat_pattern_kind::ring: {
            if (!std::isfinite(pattern.radius) || !(pattern.radius > 0.0f)) {
                return fmt::format("radius is {}, it must be positive", pattern.radius);
            }
            if (!std::isfinite(pattern.arc_degrees) || !(pattern.arc_degrees > 0.0f) || (pattern.arc_degrees > 360.0f)) {
                return fmt::format("arc is {} degrees, it must be more than 0 and at most 360", pattern.arc_degrees);
            }
            const bool   full = pattern.arc_degrees >= 360.0f;
            const double arc  = static_cast<double>(pattern.radius) * glm::radians(static_cast<double>(pattern.arc_degrees));
            double count = static_cast<double>(pattern.count);
            if (gap_mode) {
                // A full ring closes: its last gap is a gap like the others.
                count = full
                    ? std::max(1.0, std::floor((arc / step_min) + c_fit_epsilon))
                    : span_count(arc, step_min);
                if (count > static_cast<double>(c_max_repeat_count)) {
                    return count_limit_refusal("ring's arc", arc, step_min, count);
                }
            }
            run.count       = static_cast<int>(count);
            run.radius      = pattern.radius;
            run.arc_degrees = pattern.arc_degrees;
            run.facing      = pattern.facing;
            out_run.count   = static_cast<std::size_t>(count);
            out_run.space   = static_cast<float>(arc);
            out_run.step    = full
                ? static_cast<float>(arc / count)
                : ((count > 1.0) ? static_cast<float>(arc / (count - 1.0)) : 0.0f);
            return {};
        }
        case Repeat_pattern_kind::path: {
            if (path_table == nullptr) {
                return "a path layout needs a curve";
            }
            const double length = static_cast<double>(path_table->get_length());
            const bool   closed = (path_table->get_closure() == erhe::math::Curve_closure::closed);
            double count = static_cast<double>(pattern.count);
            if (gap_mode) {
                // A closed curve closes as a full ring does: its last gap is a gap like the others.
                count = closed
                    ? std::max(1.0, std::floor((length / step_min) + c_fit_epsilon))
                    : span_count(length, step_min);
                if (count > static_cast<double>(c_max_repeat_count)) {
                    return count_limit_refusal("curve", length, step_min, count);
                }
            }
            if ((count > 1.0) && !(length > 0.0)) {
                return fmt::format("the curve has no length: it holds one piece, not {:.0f}", count);
            }
            // An open curve holds a piece at each end; a closed one spreads them around without one at the seam twice.
            const double step = closed ? (length / count) : ((count > 1.0) ? (length / (count - 1.0)) : 0.0);
            run.count            = static_cast<int>(count);
            run.path             = path_table;
            run.path_step        = static_cast<float>(step);
            run.path_orientation = pattern.orientation;
            out_run.count        = static_cast<std::size_t>(count);
            out_run.space        = static_cast<float>(length);
            out_run.step         = (count > 1.0) ? static_cast<float>(step) : 0.0f;
            return {};
        }
        case Repeat_pattern_kind::scatter: {
            return resolve_scatter(pattern, seed, items, out_run, out_poses);
        }
        case Repeat_pattern_kind::segments: {
            if (path_table == nullptr) {
                return "a segments layout needs a curve";
            }
            run.path             = path_table;
            run.path_orientation = pattern.orientation;
            run.segment_items    = items.segment_items;
            // The poses are checked here, so a refusal names its segment before any expansion.
            static thread_local std::vector<Repeat_pose> scratch;
            std::vector<Repeat_pose>& poses = (out_poses != nullptr) ? *out_poses : scratch;
            const std::string error = expand_repeat(run, Repeat_pose{}, poses);
            if (!error.empty()) {
                poses.clear();
                return error;
            }
            double space = 0.0;
            for (std::size_t i = 0, end = path_table->get_span_count(); i < end; ++i) {
                const erhe::math::Curve_span& span  = path_table->get_span(i);
                const glm::vec3               chord = span.control[3] - span.control[0];
                const glm::vec3 spanned = (pattern.orientation == Repeat_path_orientation::upright) ? glm::vec3{chord.x, 0.0f, chord.z} : chord;
                space += static_cast<double>(glm::length(spanned));
            }
            out_run.count = poses.size();
            out_run.space = static_cast<float>(space);
            return {};
        }
        case Repeat_pattern_kind::stairs: {
            if (path_table == nullptr) {
                return "a stairs layout needs a curve";
            }
            // The fit is the check; the staircase is one generated child at the node's origin.
            static thread_local Stairs_report report;
            const std::string error = make_stairs(*path_table, pattern.stairs, report, nullptr);
            if (!error.empty()) {
                return error;
            }
            if (out_poses != nullptr) {
                out_poses->clear();
                out_poses->push_back(Repeat_pose{});
            }
            out_run.count = 1;
            out_run.space = report.fit.length;
            return {};
        }
        default: {
            return "unknown pattern kind";
        }
    }
}

} // anonymous namespace

auto resolve_layout_pattern(
    const Kit_layout_pattern&                 pattern,
    const int                                 seed,
    const Kit_layout_item_sizes&              items,
    const erhe::math::Curve_path_table* const path_table,
    Kit_layout_run&                           out_run,
    std::vector<Repeat_pose>* const           out_poses
) -> std::string
{
    out_run = Kit_layout_run{};
    if (out_poses != nullptr) {
        out_poses->clear();
    }
    std::string error = resolve_pattern_run(pattern, seed, items, path_table, out_run, out_poses);
    if (!error.empty()) {
        return error;
    }
    if (out_run.requested == 0) {
        out_run.requested = out_run.count;
    }
    // A scatter's, a segments run's and the stairs' poses are made while resolving it; the other kinds expand here.
    const bool expanded = (pattern.kind == Repeat_pattern_kind::scatter) || (pattern.kind == Repeat_pattern_kind::segments) || (pattern.kind == Repeat_pattern_kind::stairs);
    if ((out_poses != nullptr) && !expanded) {
        error = expand_repeat(out_run.pattern, Repeat_pose{}, *out_poses);
        for (std::size_t i = 0, end = out_poses->size(); i < end; ++i) {
            (*out_poses)[i].item = get_layout_item_index(i, items.items.size());
        }
    }
    return error;
}

void Kit_layout_item_sizes::clear()
{
    extent = 0.0f;
    items.clear();
    segment_items.clear();
}

void Kit_layout_item_sizes::add(const Kit_layout_item& item, const erhe::math::Aabb& bounds)
{
    if (bounds.is_valid_3d()) {
        extent = std::max(extent, bounds.max.x - bounds.min.x);
    }
    items.push_back(Repeat_scatter_item{.weight = item.weight, .radius = get_scatter_footprint_radius(bounds)});
    segment_items.push_back(bounds.is_valid_3d() ? Repeat_segment_item{.min_x = bounds.min.x, .max_x = bounds.max.x} : Repeat_segment_item{});
}

auto get_scatter_footprint_radius(const erhe::math::Aabb& bounds) -> float
{
    if (!bounds.is_valid_3d()) {
        return 0.0f;
    }
    // Each product and sum its own statement: unfused on every compiler (doc/cmake_conventions.md).
    const float x  = std::max(std::abs(bounds.min.x), std::abs(bounds.max.x));
    const float z  = std::max(std::abs(bounds.min.z), std::abs(bounds.max.z));
    const float xx = x * x;
    const float zz = z * z;
    const float rr = xx + zz;
    return std::sqrt(rr);
}

auto make_stairs_source_text(const erhe::math::Curve_path& path, const Stairs_values& values) -> std::string
{
    json points = json::array();
    for (const erhe::math::Curve_point& point : path.points) {
        points.push_back(json::array({vec3_json(point.position), vec3_json(point.in_handle), vec3_json(point.out_handle), static_cast<int>(point.kind)}));
    }
    const json object{
        {"generator", c_stairs_generator_version},
        {"closed",    path.closure == erhe::math::Curve_closure::closed},
        {"points",    points},
        {"width",     hex_json(values.width)},
        {"rise",      hex_json(values.max_rise)},
        {"tread",     hex_json(values.min_tread)},
        {"thickness", hex_json(values.thickness)},
        {"tolerance", hex_json(values.tolerance)}
    };
    return object.dump();
}

auto parse_stairs_source_text(const std::string_view text, erhe::math::Curve_path& out_path, Stairs_values& out_values) -> std::string
{
    const json object = json::parse(text, nullptr, false);
    if (!object.is_object() || !object.contains("generator") || !object.at("generator").is_number_integer()) {
        return "the stairs source text does not read";
    }
    if (object.at("generator").get<int>() != c_stairs_generator_version) {
        return fmt::format("the staircase was generated by generator version {}, the current one is {}", object.at("generator").get<int>(), c_stairs_generator_version);
    }
    Object_reader        reader{object};
    Stairs_values        values;
    erhe::math::Curve_path path;
    static_cast<void>(reader.find("generator"));
    const json* const closed = reader.find("closed");
    const json* const points = reader.find("points");
    if ((closed == nullptr) || !closed->is_boolean() || (points == nullptr) || !points->is_array()) {
        return "the stairs source text does not read";
    }
    path.closure = closed->get<bool>() ? erhe::math::Curve_closure::closed : erhe::math::Curve_closure::open;
    for (const json& entry : *points) {
        if (!entry.is_array() || (entry.size() != 4) || !entry[3].is_number_integer()) {
            return "the stairs source text does not read";
        }
        erhe::math::Curve_point point;
        reader.read_vec3(entry[0], "points", point.position);
        reader.read_vec3(entry[1], "points", point.in_handle);
        reader.read_vec3(entry[2], "points", point.out_handle);
        point.kind = static_cast<erhe::math::Curve_point_kind>(entry[3].get<int>());
        path.points.push_back(point);
    }
    reader.read_float("width",     values.width);
    reader.read_float("rise",      values.max_rise);
    reader.read_float("tread",     values.min_tread);
    reader.read_float("thickness", values.thickness);
    reader.read_float("tolerance", values.tolerance);
    const std::string error = reader.finish("stairs source text");
    if (!error.empty()) {
        return error;
    }
    out_path   = std::move(path);
    out_values = values;
    return {};
}

auto get_layout_item_index(const std::size_t element, const std::size_t item_count) -> std::size_t
{
    return (item_count == 0) ? 0 : (element % item_count);
}

auto derive_layout_element_seed(const int layout_seed, const std::size_t element) -> int
{
    Kit_random random{layout_seed, fmt::format("layout element {}", element)};
    return static_cast<int>(random.next_u32() % static_cast<uint32_t>(kit_seed_max + 1));
}

void set_layout_spacing_from_extent(Kit_layout_pattern& pattern, const glm::vec3& extent)
{
    pattern.step      = (extent.x > 0.0f) ? extent.x : 1.0f;
    pattern.spacing_x = (extent.x > 0.0f) ? extent.x : 1.0f;
    pattern.spacing_z = (extent.z > 0.0f) ? extent.z : 1.0f;
}

} // namespace editor
