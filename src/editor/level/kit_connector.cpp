#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"

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

auto transform_connector(const Kit_connector& connector, const glm::mat4& world_from_node) -> std::optional<Kit_connector>
{
    const glm::mat3 linear{world_from_node};
    // Singular within 1e-6 of the product of the column lengths: an axis collapsed.
    const float determinant = glm::determinant(linear);
    const float volume      = glm::length(linear[0]) * glm::length(linear[1]) * glm::length(linear[2]);
    if (!(std::abs(determinant) > (c_singular_ratio * volume)) || !std::isfinite(determinant)) {
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
