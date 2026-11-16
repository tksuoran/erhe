#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace editor {

void get_shape_connectors(const Kit_parameter_values& values, std::vector<Kit_connector>& out_connectors)
{
    out_connectors.clear();
    const Kit_shape_definition* const shape = values.get_shape();
    if ((shape != nullptr) && shape->get_connectors) {
        shape->get_connectors(values, out_connectors);
    }
}

auto find_connector(const std::span<const Kit_connector> connectors, const std::string_view name) -> const Kit_connector*
{
    for (const Kit_connector& connector : connectors) {
        if (std::string_view{connector.name} == name) {
            return &connector;
        }
    }
    return nullptr;
}

auto connector_names_text(const std::span<const Kit_connector> connectors) -> std::string
{
    if (connectors.empty()) {
        return "(none)";
    }
    std::string text;
    for (const Kit_connector& connector : connectors) {
        text += text.empty() ? std::string{} : std::string{", "};
        text += connector.name;
    }
    return text;
}

namespace {

// Calls visit with each trimmed, non-empty name of a comma separated list until it returns true.
template <typename Visit>
auto for_each_listed_name(const std::string_view text, Visit&& visit) -> bool
{
    std::size_t begin = 0;
    while (begin <= text.size()) {
        std::size_t end = text.find(',', begin);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view name = text.substr(begin, end - begin);
        while (!name.empty() && (name.front() == ' ')) {
            name.remove_prefix(1);
        }
        while (!name.empty() && (name.back() == ' ')) {
            name.remove_suffix(1);
        }
        if (!name.empty() && visit(name)) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

} // anonymous namespace

auto Kit_connector_switches::is_disabled(const std::string_view connector_name) const -> bool
{
    return for_each_listed_name(disabled_connectors, [connector_name](const std::string_view name) { return name == connector_name; });
}

auto Kit_connector_switches::is_enabled(const std::string_view connector_name) const -> bool
{
    return connectors_enabled && !is_disabled(connector_name);
}

auto parse_connector_names(const std::string_view text) -> std::vector<std::string>
{
    std::vector<std::string> names;
    static_cast<void>(for_each_listed_name(text, [&names](const std::string_view name) { names.emplace_back(name); return false; }));
    return names;
}

auto make_connector_names_text(std::vector<std::string> names) -> std::string
{
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    std::string text;
    for (const std::string& name : names) {
        if (!text.empty()) {
            text += ',';
        }
        text += name;
    }
    return text;
}

auto check_disabled_connectors_text(const std::string_view text, const std::span<const Kit_connector> connectors) -> std::string
{
    const std::vector<std::string> names = parse_connector_names(text);
    std::string unknown;
    for (const std::string& name : names) {
        if (find_connector(connectors, name) == nullptr) {
            unknown += unknown.empty() ? std::string{} : std::string{", "};
            unknown += "'" + name + "'";
        }
    }
    if (!unknown.empty()) {
        return "no connector " + unknown + " (the shape's connectors: " + connector_names_text(connectors) + ")";
    }
    const std::string canonical = make_connector_names_text(names);
    if (canonical != text) {
        return "the list '" + std::string{text} + "' is not canonical (sorted, unique, comma separated, no spaces): write '" + canonical + "'";
    }
    return {};
}

auto set_connector_state(const std::string_view disabled_connectors, const std::string_view connector_name, const Kit_connector_state state) -> std::string
{
    std::vector<std::string> names = parse_connector_names(disabled_connectors);
    std::erase(names, std::string{connector_name});
    if (state == Kit_connector_state::disabled) {
        names.emplace_back(connector_name);
    }
    return make_connector_names_text(std::move(names));
}

void remove_disabled_connectors(const Kit_connector_switches& switches, std::vector<Kit_connector>& connectors)
{
    std::erase_if(connectors, [&switches](const Kit_connector& connector) { return !switches.is_enabled(connector.name); });
}

auto transform_connector(const Kit_connector& connector, const glm::vec3& position, const glm::quat& rotation) -> Kit_connector
{
    const glm::mat3 basis = glm::mat3_cast(rotation);
    return Kit_connector{
        .name     = connector.name,
        .position = position + (basis * connector.position),
        .outward  = basis * connector.outward,
        .up       = basis * connector.up
    };
}

void transform_connectors(
    const std::span<const Kit_connector> connectors,
    const glm::vec3&                     position,
    const glm::quat&                     rotation,
    std::vector<Kit_connector>&          out_connectors
)
{
    out_connectors.clear();
    for (const Kit_connector& connector : connectors) {
        out_connectors.push_back(transform_connector(connector, position, rotation));
    }
}

auto join_connectors(const Kit_connector& piece_connector, const Kit_connector& target_connector, const int twist_steps, const float gap) -> Kit_join_pose
{
    // A quarter turn about the target's outward axis maps up to outward x up (both unit and normal to each other).
    const glm::vec3 axis    = target_connector.outward;
    const glm::vec3 side    = glm::cross(axis, target_connector.up);
    const int       quarter = ((twist_steps % 4) + 4) % 4;
    const glm::vec3 up =
        (quarter == 0) ? target_connector.up :
        (quarter == 1) ? side :
        (quarter == 2) ? -target_connector.up :
        -side;

    // Both bases are right-handed orthonormal, so the rotation between them is proper.
    const glm::vec3 to_outward = -axis;
    const glm::mat3 from_basis{piece_connector.outward, piece_connector.up, glm::cross(piece_connector.outward, piece_connector.up)};
    const glm::mat3 to_basis  {to_outward, up, glm::cross(to_outward, up)};
    const glm::mat3 rotation_matrix = to_basis * glm::transpose(from_basis);
    const glm::quat rotation        = glm::normalize(glm::quat_cast(rotation_matrix));

    // The translation takes the rotation the node will have, so its connector lands on the target point.
    const glm::vec3 target_point = target_connector.position + (gap * axis);
    const glm::vec3 position     = target_point - (glm::mat3_cast(rotation) * piece_connector.position);
    return Kit_join_pose{.position = position, .rotation = rotation};
}

namespace {

constexpr float c_orthogonal_tolerance = 1.0e-4f;
constexpr float c_singular_ratio    = 1.0e-6f;
constexpr float c_joined_distance      = 1.0e-4f;
constexpr float c_joined_angle         = 1.0e-4f;

} // anonymous namespace

auto has_orthogonal_axes(const glm::mat3& m) -> bool
{
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            const float limit = c_orthogonal_tolerance * glm::length(m[i]) * glm::length(m[j]);
            if (!(std::abs(glm::dot(m[i], m[j])) <= limit)) {
                return false;
            }
        }
    }
    return true;
}

auto is_rotation_matrix(const glm::mat3& m) -> bool
{
    constexpr float tolerance = 1.0e-5f;
    for (int i = 0; i < 3; ++i) {
        if (!(std::abs(glm::length(m[i]) - 1.0f) <= tolerance)) {
            return false;
        }
        for (int j = i + 1; j < 3; ++j) {
            if (!(std::abs(glm::dot(m[i], m[j])) <= tolerance)) {
                return false;
            }
        }
    }
    return glm::determinant(m) > 0.0f;
}

auto rotation_for_scale(const glm::mat3& linear, const glm::vec3& scale) -> std::optional<glm::quat>
{
    if ((scale.x == 0.0f) || (scale.y == 0.0f) || (scale.z == 0.0f)) {
        return std::nullopt;
    }
    const glm::mat3 rotation{linear[0] / scale.x, linear[1] / scale.y, linear[2] / scale.z};
    if (!is_rotation_matrix(rotation)) {
        return std::nullopt;
    }
    return glm::normalize(glm::quat_cast(rotation));
}

auto is_degenerate_frame(const glm::mat3& linear) -> bool
{
    const float determinant = glm::determinant(linear);
    const float volume      = glm::length(linear[0]) * glm::length(linear[1]) * glm::length(linear[2]);
    return !(std::abs(determinant) > (c_singular_ratio * volume)) || !std::isfinite(determinant);
}

auto connector_in_root(const Kit_connector& connector, const glm::mat4& root_from_node) -> Kit_connector
{
    const glm::mat3 linear{root_from_node};
    return Kit_connector{
        .name     = connector.name,
        .position = glm::vec3{root_from_node * glm::vec4{connector.position, 1.0f}},
        .outward  = glm::inverse(glm::transpose(linear)) * connector.outward,
        .up       = linear * connector.up
    };
}

auto carry_connector_position(const glm::vec3& position_in_root, const glm::mat4& world_from_root) -> std::optional<glm::vec3>
{
    if (is_degenerate_frame(glm::mat3{world_from_root})) {
        return std::nullopt;
    }
    return glm::vec3{world_from_root * glm::vec4{position_in_root, 1.0f}};
}

auto transform_connector(const Kit_connector& connector, const glm::mat4& world_from_node) -> std::optional<Kit_connector>
{
    const glm::mat3 linear{world_from_node};
    if (is_degenerate_frame(linear)) {
        return std::nullopt;
    }
    // The outward axis is a face normal: it stays normal to the transformed face, so joined faces are flush under shear.
    const glm::vec3 outward = glm::inverse(glm::transpose(linear)) * connector.outward;
    const glm::vec3 up      = linear * connector.up;
    return Kit_connector{
        .name     = connector.name,
        .position = glm::vec3{world_from_node * glm::vec4{connector.position, 1.0f}},
        .outward  = glm::normalize(outward),
        .up       = glm::normalize(up)
    };
}

auto transform_connectors(
    const std::span<const Kit_connector> connectors,
    const glm::mat4&                     world_from_node,
    std::vector<Kit_connector>&          out_connectors
) -> bool
{
    out_connectors.clear();
    for (const Kit_connector& connector : connectors) {
        const std::optional<Kit_connector> transformed = transform_connector(connector, world_from_node);
        if (!transformed.has_value()) {
            out_connectors.clear();
            return false;
        }
        out_connectors.push_back(transformed.value());
    }
    return true;
}

auto join_motion(
    const Kit_connector& local_connector,
    const glm::mat4&     world_from_node,
    const Kit_connector& target_connector,
    const int            twist_steps,
    const float          gap
) -> std::optional<Kit_join_pose>
{
    const std::optional<Kit_connector> frame = transform_connector(local_connector, world_from_node);
    if (!frame.has_value()) {
        return std::nullopt;
    }
    return join_connectors(frame.value(), target_connector, twist_steps, gap);
}

auto apply_motion(const Kit_join_pose& motion, const glm::mat4& m) -> glm::mat4
{
    glm::mat4 world_from_world = glm::mat4_cast(motion.rotation);
    world_from_world[3] = glm::vec4{motion.position, 1.0f};
    return world_from_world * m;
}

auto is_identity_motion(const Kit_join_pose& motion) -> bool
{
    // The rotation angle is about twice the length of the quaternion's vector part.
    const glm::vec3 axis_part{motion.rotation.x, motion.rotation.y, motion.rotation.z};
    return (glm::length(motion.position) <= c_joined_distance) && ((2.0f * glm::length(axis_part)) <= c_joined_angle);
}

auto c_str(const Kit_connector_indicator_role role) -> const char*
{
    switch (role) {
        case Kit_connector_indicator_role::piece:   return "piece";
        case Kit_connector_indicator_role::chosen:  return "chosen";
        case Kit_connector_indicator_role::moved:   return "moved";
        case Kit_connector_indicator_role::target:  return "target";
        case Kit_connector_indicator_role::snapped: return "snapped";
        default:                                    return "?";
    }
}

auto constrain_connector_snap_offset(const glm::vec3& offset, const Kit_connector_snap_constraint& constraint) -> glm::vec3
{
    const float length_squared = glm::dot(constraint.direction, constraint.direction);
    if ((constraint.kind == Kit_connector_snap_constraint_kind::free) || !(length_squared > 0.0f) || !std::isfinite(length_squared)) {
        return offset;
    }
    const glm::vec3 along = constraint.direction * (glm::dot(offset, constraint.direction) / length_squared);
    return (constraint.kind == Kit_connector_snap_constraint_kind::axis) ? along : (offset - along);
}

auto find_connector_snap(
    const std::span<const Kit_connector_point> moved,
    const std::span<const Kit_connector_point> others,
    const float                                max_distance
) -> Kit_connector_snap_match
{
    Kit_connector_snap_match match{};
    if (!(max_distance > 0.0f) || !std::isfinite(max_distance)) {
        return match;
    }
    const float limit_squared = max_distance * max_distance;
    float best_squared = 0.0f;
    for (std::size_t i = 0, i_end = moved.size(); i < i_end; ++i) {
        const Kit_connector_point& a = moved[i];
        for (std::size_t j = 0, j_end = others.size(); j < j_end; ++j) {
            const Kit_connector_point& b = others[j];
            // Scalar arithmetic: the search runs every drag step, and glm calls are not inlined in Debug builds.
            const float dx        = b.position.x - a.position.x;
            const float dy        = b.position.y - a.position.y;
            const float dz        = b.position.z - a.position.z;
            const float d_squared = (dx * dx) + (dy * dy) + (dz * dz);
            if (d_squared > limit_squared) {
                continue;
            }
            if (match.found) {
                // Equal distances: the smaller (moved piece, moved connector, other piece, other connector) key wins.
                const Kit_connector_point& best_a = moved[match.moved];
                const Kit_connector_point& best_b = others[match.other];
                const bool better =
                    (d_squared < best_squared) ||
                    (
                        (d_squared == best_squared) &&
                        (
                            std::tie(a.piece_id, a.connector_index, b.piece_id, b.connector_index) <
                            std::tie(best_a.piece_id, best_a.connector_index, best_b.piece_id, best_b.connector_index)
                        )
                    );
                if (!better) {
                    continue;
                }
            }
            match.found  = true;
            match.moved  = i;
            match.other  = j;
            best_squared = d_squared;
        }
    }
    if (match.found) {
        match.distance = std::sqrt(best_squared);
    }
    return match;
}

} // namespace editor
