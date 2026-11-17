#include "level/surface_base_frame.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

// The base corners lie this far (meters) inside the footprint.
constexpr float c_footprint_inset = 1.0e-3f;

class Face_candidate
{
public:
    int         axis;
    Bounds_side side;
};

// The tie order of get_surface_base_frame().
constexpr std::array<Face_candidate, 6> c_face_candidates{
    Face_candidate{.axis = 1, .side = Bounds_side::min},
    Face_candidate{.axis = 1, .side = Bounds_side::max},
    Face_candidate{.axis = 0, .side = Bounds_side::min},
    Face_candidate{.axis = 0, .side = Bounds_side::max},
    Face_candidate{.axis = 2, .side = Bounds_side::min},
    Face_candidate{.axis = 2, .side = Bounds_side::max}
};

// A zero scale flattens the axis without turning it over.
[[nodiscard]] auto scale_sign(const float scale) -> float
{
    return (scale < 0.0f) ? -1.0f : 1.0f;
}

// The face's outward normal in node space, with the scale's sign folded in (the inverse transpose's direction).
[[nodiscard]] auto get_outward_normal(const Face_candidate& face, const glm::vec3& scale) -> glm::vec3
{
    glm::vec3 normal{0.0f};
    normal[face.axis] = ((face.side == Bounds_side::min) ? -1.0f : 1.0f) * scale_sign(scale[face.axis]);
    return normal;
}

} // anonymous namespace

auto get_surface_base_frame(
    const Surface_base_rule rule,
    const glm::quat&        rotation,
    const glm::vec3&        scale,
    const glm::vec3&        direction
) -> Surface_base_frame
{
    // A kit piece chooses between its two Y faces only (the first two candidates), so a mirror of any axis keeps it upright.
    const std::size_t     count      = (rule == Surface_base_rule::authored_y_up) ? std::size_t{2} : c_face_candidates.size();
    const Face_candidate* best       = &c_face_candidates[0];
    float                 best_score = glm::dot(rotation * get_outward_normal(*best, scale), direction);
    for (std::size_t i = 1; i < count; ++i) {
        const float score = glm::dot(rotation * get_outward_normal(c_face_candidates[i], scale), direction);
        if (score > best_score) {
            best       = &c_face_candidates[i];
            best_score = score;
        }
    }
    Surface_base_frame frame{};
    frame.axis     = best->axis;
    frame.side     = best->side;
    frame.u        = (best->axis == 0) ? 1 : 0;
    frame.v        = (best->axis == 2) ? 1 : 2;
    frame.up_local = -get_outward_normal(*best, scale);
    return frame;
}

auto get_orient_frame(
    const Surface_base_rule   rule,
    const glm::quat&          rotation,
    const glm::vec3&          scale,
    const Surface_base_frame& line_frame,
    const glm::vec3&          normal
) -> Surface_base_frame
{
    return (rule == Surface_base_rule::from_pose) ? get_surface_base_frame(rule, rotation, scale, -normal) : line_frame;
}

auto get_base_coordinate(const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame) -> float
{
    return (frame.side == Bounds_side::min) ? local_bounds.min[frame.axis] : local_bounds.max[frame.axis];
}

auto get_top_coordinate(const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame) -> float
{
    return (frame.side == Bounds_side::min) ? local_bounds.max[frame.axis] : local_bounds.min[frame.axis];
}

auto get_footprint_bounds(const erhe::math::Aabb& local_bounds, const glm::vec3 scale, const Surface_base_frame& frame) -> erhe::math::Aabb
{
    // The inset is in meters: a node scale divides it into node units.
    const auto local_inset = [](const float extent, const float axis_scale) -> float {
        const float scale_length = std::abs(axis_scale);
        return (scale_length > 0.0f) ? std::min(c_footprint_inset / scale_length, 0.5f * extent) : 0.0f;
    };
    glm::vec3 inset{0.0f};
    inset[frame.u] = local_inset(local_bounds.max[frame.u] - local_bounds.min[frame.u], scale[frame.u]);
    inset[frame.v] = local_inset(local_bounds.max[frame.v] - local_bounds.min[frame.v], scale[frame.v]);
    return erhe::math::Aabb{.min = local_bounds.min + inset, .max = local_bounds.max - inset};
}

auto get_base_points(const erhe::math::Aabb& bounds, const Surface_base_frame& frame) -> std::array<glm::vec3, 5>
{
    const float base = get_base_coordinate(bounds, frame);
    const auto  make = [&frame, base](const float u, const float v) -> glm::vec3 {
        glm::vec3 point{0.0f};
        point[frame.axis] = base;
        point[frame.u]    = u;
        point[frame.v]    = v;
        return point;
    };
    const glm::vec3 center = bounds.center();
    const int       u      = frame.u;
    const int       v      = frame.v;
    return std::array<glm::vec3, 5>{
        make(center[u],     center[v]),
        make(bounds.min[u], bounds.min[v]),
        make(bounds.max[u], bounds.min[v]),
        make(bounds.min[u], bounds.max[v]),
        make(bounds.max[u], bounds.max[v])
    };
}

auto get_base_center(const erhe::math::Aabb& bounds, const Surface_base_frame& frame) -> glm::vec3
{
    glm::vec3 point = bounds.center();
    point[frame.axis] = get_base_coordinate(bounds, frame);
    return point;
}

auto get_top_point(const erhe::math::Aabb& local_bounds, const Surface_base_frame& frame, const glm::vec3 base_point) -> glm::vec3
{
    glm::vec3 point = base_point;
    point[frame.axis] = get_top_coordinate(local_bounds, frame);
    return point;
}

auto get_oriented_pose(
    const Surface_pose&       start,
    const erhe::math::Aabb&   local_bounds,
    const Surface_base_frame& frame,
    const glm::vec3           direction,
    const float               height,
    const glm::vec3           normal
) -> Surface_pose
{
    const glm::vec3 base_center_local = get_base_center(local_bounds, frame);
    const glm::vec3 base_center       = get_world_point(start, base_center_local);
    const glm::vec3 up                = glm::normalize(start.rotation * frame.up_local);
    const glm::quat rotation          = glm::normalize(shortest_rotation(up, normal) * start.rotation);
    return Surface_pose{
        .position = (base_center - (height * direction)) - (rotation * (start.scale * base_center_local)),
        .rotation = rotation,
        .scale    = start.scale
    };
}

auto shortest_rotation(const glm::vec3 from, const glm::vec3 to) -> glm::quat
{
    const float cos_angle = glm::dot(from, to);
    if (cos_angle >= 1.0f - 1.0e-6f) {
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    if (cos_angle <= -1.0f + 1.0e-6f) {
        const glm::vec3 helper = (std::abs(from.x) < 0.9f) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
        return glm::angleAxis(glm::pi<float>(), glm::normalize(glm::cross(from, helper)));
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float     s    = std::sqrt((1.0f + cos_angle) * 2.0f);
    return glm::normalize(glm::quat{0.5f * s, axis.x / s, axis.y / s, axis.z / s});
}

} // namespace editor
