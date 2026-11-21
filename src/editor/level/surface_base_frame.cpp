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

constexpr Face_candidate c_min_y_face{.axis = 1, .side = Bounds_side::min};

// Snap's candidates in tie order: an exact tie keeps the earlier face, -Y first.
constexpr std::array<Face_candidate, 6> c_snap_faces{
    c_min_y_face,
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

[[nodiscard]] auto make_frame(const Face_candidate& face, const glm::vec3& scale) -> Surface_base_frame
{
    Surface_base_frame frame{};
    frame.axis     = face.axis;
    frame.side     = face.side;
    frame.u        = (face.axis == 0) ? 1 : 0;
    frame.v        = (face.axis == 2) ? 1 : 2;
    frame.up_local = -get_outward_normal(face, scale);
    return frame;
}

} // anonymous namespace

auto get_surface_base_frame(const glm::quat& rotation, const glm::vec3& scale, const glm::vec3& direction) -> Surface_base_frame
{
    const Face_candidate* best       = &c_snap_faces.front();
    float                 best_score = glm::dot(rotation * get_outward_normal(*best, scale), direction);
    for (const Face_candidate& face : c_snap_faces) {
        const float score = glm::dot(rotation * get_outward_normal(face, scale), direction);
        if (score > best_score) {
            best       = &face;
            best_score = score;
        }
    }
    return make_frame(*best, scale);
}

namespace {

// Orient's window reference: of the two Y faces the one more along the direction (a tie keeps -Y).
[[nodiscard]] auto get_leading_y_frame(const glm::quat& rotation, const glm::vec3& scale, const glm::vec3& direction) -> Surface_base_frame
{
    const Face_candidate& max_y_face = c_snap_faces[1];
    const float min_score = glm::dot(rotation * get_outward_normal(c_min_y_face, scale), direction);
    const float max_score = glm::dot(rotation * get_outward_normal(max_y_face, scale), direction);
    return make_frame((max_score > min_score) ? max_y_face : c_min_y_face, scale);
}

} // anonymous namespace

auto get_orient_base_frame(const glm::vec3& scale) -> Surface_base_frame
{
    return make_frame(c_min_y_face, scale);
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
    const glm::quat rotation          = glm::normalize(get_orient_turn(start.rotation, frame, get_orient_half_turn(start.rotation, frame, direction), normal) * start.rotation);
    return Surface_pose{
        .position = (base_center - (height * direction)) - (rotation * (start.scale * base_center_local)),
        .rotation = rotation,
        .scale    = start.scale
    };
}

auto get_orient_half_turn(const glm::quat& rotation, const Surface_base_frame& frame, const glm::vec3 direction) -> Orient_half_turn
{
    // From the start pose and the direction only, so the turn is the same for every element of a layout whatever normal each
    // one finds.
    return (glm::dot(rotation * frame.up_local, direction) > 0.0f) ? Orient_half_turn::on : Orient_half_turn::off;
}

auto get_orient_turn(
    const glm::quat&          rotation,
    const Surface_base_frame& frame,
    const Orient_half_turn    half_turn,
    const glm::vec3           normal
) -> glm::quat
{
    glm::vec3 up = glm::normalize(rotation * frame.up_local);
    glm::quat turn{1.0f, 0.0f, 0.0f, 0.0f};
    if (half_turn == Orient_half_turn::on) {
        glm::vec3 u_axis{0.0f};
        u_axis[frame.u] = 1.0f;
        turn = glm::angleAxis(glm::pi<float>(), glm::normalize(rotation * u_axis));
        up   = -up;
    }
    return shortest_rotation(up, normal) * turn;
}

auto get_anchor_rest(
    const Surface_pose&         start,
    const glm::vec3             direction,
    const Surface_line&         line,
    const Surface_search_window window
) -> Anchor_rest
{
    const Point_rest rest = find_point_rest(start.position, direction, line, window);
    if (!rest.found) {
        return Anchor_rest{.found = false, .pose = start, .distance = 0.0f};
    }
    if (rest.distance == 0.0f) {
        return Anchor_rest{.found = true, .pose = start, .distance = 0.0f, .hit = rest.hit};
    }
    return Anchor_rest{
        .found    = true,
        .pose     = Surface_pose{.position = rest.position, .rotation = start.rotation, .scale = start.scale},
        .distance = rest.distance,
        .hit      = rest.hit
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

auto get_orient_search(
    const erhe::math::Aabb&     local_bounds,
    const Surface_pose&         start,
    const glm::vec3             direction,
    const Surface_search_window window
) -> Orient_search
{
    Orient_search search{};
    search.frame = get_orient_base_frame(start.scale);
    const glm::vec3 base_center_local = get_base_center(local_bounds, search.frame);
    search.base_center = get_world_point(start, base_center_local);
    search.span        = glm::length(get_world_point(start, get_top_point(local_bounds, search.frame, base_center_local)) - search.base_center);
    // 0 when the -Y face leads; the top face's distance along the direction for a piece turned over.
    const Surface_base_frame lead_frame = get_leading_y_frame(start.rotation, start.scale, direction);
    const float lead = glm::dot(get_world_point(start, get_base_center(local_bounds, lead_frame)) - search.base_center, direction);
    search.window = Surface_search_window{
        .forward_distance  = window.forward_distance + lead,
        .backward_distance = window.backward_distance - lead
    };
    return search;
}

} // namespace editor
