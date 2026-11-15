#include "level/level_queries.hpp"

#include "level/kit_piece.hpp"
#include "level/kit_shape_library.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_root.hpp"
#if defined(ERHE_USE_DETOUR)
#   include "navigation/navigation_mesh.hpp"
#   include "scene/scene_navigation.hpp"
#endif

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/transform.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <utility>

namespace editor {

namespace {

constexpr float        c_max_distance  = 10000.0f;
// A rise smaller than this is a rest; a surface this close to a ray's start is out of reach.
constexpr float        c_rise_tolerance = 1.0e-4f;
// A rest casts again after each rise; each rise reaches one more surface layer under the piece.
constexpr unsigned int c_max_rests      = 16;
// A ray starts at least this far back from its base point, so a flat piece reaches the surface it rests on.
constexpr float        c_min_ray_height = 1.0e-3f;
// Surfaces this close to the nearest one along a ray are at the same place; one total order picks the normal.
constexpr float        c_tie_distance   = 1.0e-4f;
// A rest turning the piece less than this (1 - cosine of the turn) leaves its rotation as it is.
constexpr float        c_turn_tolerance = 1.0e-6f;
// Half width of the navigation query box around a height map hit.
constexpr float        c_walkable_half_width   = 0.05f;
// A nearest navigation point this close horizontally is over the polygon.
constexpr float        c_walkable_over_polygon = 1.0e-3f;

[[nodiscard]] auto by_id(const std::shared_ptr<erhe::scene::Mesh>& lhs, const std::shared_ptr<erhe::scene::Mesh>& rhs) -> bool
{
    return lhs->get_id() < rhs->get_id();
}

class Surface_hit
{
public:
    bool      found   {false};
    float     distance{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 normal  {0.0f, 1.0f, 0.0f};
    const erhe::scene::Mesh* mesh{nullptr};
};

[[nodiscard]] auto get_hit_mesh(const erhe::raytrace::Hit& hit) -> const erhe::scene::Mesh*
{
    const erhe::scene::Raytrace_primitive* const raytrace_primitive = (hit.instance != nullptr)
        ? static_cast<const erhe::scene::Raytrace_primitive*>(hit.instance->get_user_data())
        : nullptr;
    return (raytrace_primitive != nullptr) ? raytrace_primitive->mesh : nullptr;
}

// A surface is a front face (its normal faces the ray) of a content mesh that is not excluded.
class Surface_hit_filter : public erhe::raytrace::IHit_filter
{
public:
    Surface_hit_filter(const glm::vec3 direction, const std::span<const erhe::scene::Mesh* const> excluded)
        : m_direction{direction}
        , m_excluded {excluded}
    {
    }

    [[nodiscard]] auto accept(const erhe::raytrace::Hit& candidate) const -> bool override
    {
        if (!(glm::dot(candidate.normal, m_direction) < 0.0f)) {
            return false;
        }
        // A hit without a mesh is no content surface (and cast_surface_ray() reads the mesh from the instance).
        const erhe::scene::Mesh* const mesh = get_hit_mesh(candidate);
        return (mesh != nullptr) && (std::find(m_excluded.begin(), m_excluded.end(), mesh) == m_excluded.end());
    }

private:
    glm::vec3                                 m_direction;
    std::span<const erhe::scene::Mesh* const> m_excluded;
};

// The key of the D7.3 tie order: the normal most against the ray first, then the normal's components.
[[nodiscard]] auto tie_key(const glm::vec3 normal, const glm::vec3 direction) -> std::array<float, 4>
{
    // Quantized so normals equal up to rounding compare by their components alone.
    const float against = std::round(-glm::dot(normal, direction) * 1.0e5f);
    return std::array<float, 4>{against, normal.x, normal.y, normal.z};
}

// Sees every surface candidate in the ray's interval (each is rejected) and keeps the normal first in the tie order.
class Surface_tie_filter : public erhe::raytrace::IHit_filter
{
public:
    Surface_tie_filter(const glm::vec3 direction, const std::span<const erhe::scene::Mesh* const> excluded)
        : m_surface{direction, excluded}
        , m_direction{direction}
    {
    }

    [[nodiscard]] auto accept(const erhe::raytrace::Hit& candidate) const -> bool override
    {
        const float length = glm::length(candidate.normal);
        if (!m_surface.accept(candidate) || !(length > 0.0f)) {
            return false;
        }
        const glm::vec3            normal = candidate.normal / length;
        const std::array<float, 4> key    = tie_key(normal, m_direction);
        if (!m_found || (key > m_best_key)) {
            m_found    = true;
            m_best_key = key;
            m_normal   = normal;
        }
        return false;
    }

    [[nodiscard]] auto found () const -> bool      { return m_found; }
    [[nodiscard]] auto normal() const -> glm::vec3 { return m_normal; }

private:
    Surface_hit_filter           m_surface;
    glm::vec3                    m_direction;
    mutable bool                 m_found   {false};
    mutable std::array<float, 4> m_best_key{};
    mutable glm::vec3            m_normal  {0.0f, 1.0f, 0.0f};
};

// The nearest surface along the ray between min_distance and max_distance (D7.3).
[[nodiscard]] auto cast_surface_ray(
    erhe::raytrace::IScene&                         rt_scene,
    const glm::vec3                                 origin,
    const glm::vec3                                 direction,
    const std::span<const erhe::scene::Mesh* const> excluded,
    const float                                     min_distance,
    const float                                     max_distance
) -> Surface_hit
{
    const Surface_hit_filter filter{direction, excluded};
    erhe::raytrace::Ray ray{
        .origin    = origin,
        .t_near    = min_distance,
        .direction = direction,
        .time      = 0.0f,
        .t_far     = max_distance,
        .mask      = Raytrace_node_mask::content,
        .id        = 0,
        .flags     = 0,
        .filter    = &filter
    };
    erhe::raytrace::Hit hit;
    rt_scene.intersect(ray, hit);
    if (hit.instance == nullptr) {
        return Surface_hit{};
    }
    // The hit normal is the unnormalized geometric normal.
    const float length = glm::length(hit.normal);
    return Surface_hit{
        .found    = true,
        .distance = ray.t_far,
        .position = origin + ray.t_far * direction,
        .normal   = (length > 0.0f) ? (hit.normal / length) : -direction,
        .mesh     = get_hit_mesh(hit)
    };
}

// The normal of the surfaces within c_tie_distance of the nearest hit, by the tie order (D7.3); it does not
// depend on which of them the traversal meets first.
[[nodiscard]] auto resolve_tie_normal(
    erhe::raytrace::IScene&                         rt_scene,
    const glm::vec3                                 origin,
    const glm::vec3                                 direction,
    const std::span<const erhe::scene::Mesh* const> excluded,
    const Surface_hit&                              nearest
) -> glm::vec3
{
    const Surface_tie_filter filter{direction, excluded};
    erhe::raytrace::Ray ray{
        .origin    = origin,
        .t_near    = std::max(c_rise_tolerance, nearest.distance - c_tie_distance),
        .direction = direction,
        .time      = 0.0f,
        .t_far     = nearest.distance + c_tie_distance,
        .mask      = Raytrace_node_mask::content,
        .id        = 0,
        .flags     = 0,
        .filter    = &filter
    };
    erhe::raytrace::Hit hit;
    rt_scene.intersect(ray, hit);
    return filter.found() ? filter.normal() : nearest.normal;
}

class Surface_distance
{
public:
    bool  found   {false};
    float distance{0.0f}; // from the base along the direction; negative when the base is below the surface
};

// The snap rays of D7.3: from the top plane above the base center and the base corners, the
// smallest distance from the base to a hit.
[[nodiscard]] auto find_snap_distance(
    erhe::raytrace::IScene&                         rt_scene,
    const erhe::math::Aabb&                         local_bounds,
    const Surface_pose&                             start,
    const glm::vec3                                 direction,
    const std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_distance
{
    const glm::vec3 center = local_bounds.center();
    const std::array<glm::vec3, 5> base_points{
        glm::vec3{center.x, local_bounds.min.y, center.z},
        glm::vec3{local_bounds.min.x, local_bounds.min.y, local_bounds.min.z},
        glm::vec3{local_bounds.max.x, local_bounds.min.y, local_bounds.min.z},
        glm::vec3{local_bounds.min.x, local_bounds.min.y, local_bounds.max.z},
        glm::vec3{local_bounds.max.x, local_bounds.min.y, local_bounds.max.z}
    };
    Surface_distance result{};
    for (const glm::vec3& point : base_points) {
        // Each ray starts at the top face above its base point, so a sunk piece rises.
        const glm::vec3   base   = start.position + (start.rotation * (start.scale * point));
        const glm::vec3   top    = start.position + (start.rotation * (start.scale * glm::vec3{point.x, local_bounds.max.y, point.z}));
        const float       back   = glm::dot(base - top, direction);
        const glm::vec3   origin = top - (std::max(0.0f, c_min_ray_height - back) * direction);
        const Surface_hit hit    = cast_surface_ray(rt_scene, origin, direction, excluded, c_rise_tolerance, c_max_distance);
        if (!hit.found) {
            continue;
        }
        const float distance = hit.distance - glm::dot(base - origin, direction);
        if (!result.found || (distance < result.distance)) {
            result.distance = distance;
            result.found    = true;
        }
    }
    return result;
}

// The first face seen from the front along a downward ray, within max_distance.
[[nodiscard]] auto cast_height_ray(erhe::raytrace::IScene& rt_scene, const glm::vec3 origin, const float max_distance) -> Surface_hit
{
    return cast_surface_ray(rt_scene, origin, glm::vec3{0.0f, -1.0f, 0.0f}, {}, 0.0f, max_distance);
}

[[nodiscard]] auto shortest_rotation(const glm::vec3 from, const glm::vec3 to) -> glm::quat
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

} // anonymous namespace

auto any_surface_option(const Surface_options& options) -> bool
{
    return (options.snap == Surface_snap::on) || (options.orient == Surface_orient::on);
}

auto mesh_local_bounds(const erhe::scene::Mesh& mesh) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};
    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        const erhe::math::Aabb primitive_bounds = mesh_primitive.primitive->get_bounding_box();
        if (primitive_bounds.is_valid_3d()) {
            bounds.include(primitive_bounds.min);
            bounds.include(primitive_bounds.max);
        }
    }
    return bounds;
}

auto find_surface_placement(
    Scene_root&                                     scene_root,
    const erhe::math::Aabb&                         local_bounds,
    const Surface_pose&                             start,
    const Surface_options&                          options,
    const std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_placement
{
    if (!any_surface_option(options) || !local_bounds.is_valid_3d()) {
        return Surface_placement{.pose = start};
    }
    erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
    rt_scene.commit();
    return find_surface_placement(rt_scene, local_bounds, start, options, excluded);
}

auto find_surface_placement(
    erhe::raytrace::IScene&                         rt_scene,
    const erhe::math::Aabb&                         local_bounds,
    const Surface_pose&                             start,
    const Surface_options&                          options,
    const std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_placement
{
    Surface_placement result{.pose = start};
    if (!any_surface_option(options) || !local_bounds.is_valid_3d()) {
        return result;
    }

    if (options.orient == Surface_orient::on) {
        // Orient implies snapping (D7.3). The ray passes through the base center, the pivot of the
        // turn, so turning moves no ray and a rested pose casts onto its own hit.
        const glm::vec3 center = local_bounds.center();
        const glm::vec3 base_center_local{center.x, local_bounds.min.y, center.z};
        const glm::vec3 base_center = start.position + (start.rotation * (start.scale * base_center_local));
        const float     height      = std::max(c_min_ray_height, std::abs(start.scale.y) * (local_bounds.max.y - local_bounds.min.y));
        const glm::vec3 up          = glm::normalize(start.rotation * glm::vec3{0.0f, 1.0f, 0.0f});
        for (unsigned int rest = 0; rest < c_max_rests; ++rest) {
            const glm::vec3   origin = base_center + ((result.distance - height) * options.direction);
            const Surface_hit hit    = cast_surface_ray(rt_scene, origin, options.direction, excluded, c_rise_tolerance, c_max_distance);
            if (!hit.found) {
                return result;
            }
            const glm::vec3 normal   = resolve_tie_normal(rt_scene, origin, options.direction, excluded, hit);
            const float     rise     = hit.distance - height;
            const glm::quat rotation = glm::normalize(shortest_rotation(up, normal) * start.rotation);
            result.distance      += rise;
            result.surface_found  = true;
            result.pose.rotation  = rotation;
            result.pose.position  = hit.position - (rotation * (start.scale * base_center_local));
            if (!(rise < -c_rise_tolerance)) {
                // A start pose that already rests is the rest itself, bit for bit.
                if ((std::abs(result.distance) <= c_rise_tolerance) && (glm::dot(up, normal) >= (1.0f - c_turn_tolerance))) {
                    result.pose = start;
                }
                return result;
            }
        }
        result.settled = false;
        return result;
    }

    // A rise brings new surfaces within reach of the top plane; the rest is where the rays answer zero.
    for (unsigned int rest = 0; rest < c_max_rests; ++rest) {
        const Surface_distance snap = find_snap_distance(rt_scene, local_bounds, result.pose, options.direction, excluded);
        if (!snap.found) {
            return result;
        }
        result.distance      += snap.distance;
        result.surface_found  = true;
        result.pose.position  = start.position + (result.distance * options.direction);
        if (!(snap.distance < -c_rise_tolerance)) {
            // A start pose that already rests is the rest itself, bit for bit.
            if (std::abs(result.distance) <= c_rise_tolerance) {
                result.pose = start;
            }
            return result;
        }
    }
    result.settled = false;
    return result;
}

void get_kit_piece_meshes(Scene_root& scene_root, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes)
{
    out_meshes.clear();
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root.item_host_mutex};
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            if (mesh && carries_kit_piece(*mesh)) {
                out_meshes.push_back(mesh);
            }
        }
    }
    std::sort(out_meshes.begin(), out_meshes.end(), by_id);
}

void find_region_contents(Scene_root& scene_root, const Level_region& region, std::vector<std::shared_ptr<erhe::scene::Mesh>>& out_meshes)
{
    out_meshes.clear();
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root.item_host_mutex};
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            if (mesh && region_intersects(region, mesh->get_aabb_world())) {
                out_meshes.push_back(mesh);
            }
        }
    }
    std::sort(out_meshes.begin(), out_meshes.end(), by_id);
}

void make_height_map(Scene_root& scene_root, const Height_map_grid& grid, Height_map& out_height_map)
{
    out_height_map.grid = grid;
    out_height_map.cells.clear();
    out_height_map.navigation_available = false;
    out_height_map.navigation_stale     = false;
    const std::size_t cell_count = grid.columns * grid.rows;
    if ((cell_count == 0) || (cell_count > c_max_height_map_cells)) {
        return;
    }
    out_height_map.cells.resize(cell_count);

#if defined(ERHE_USE_DETOUR)
    Scene_navigation&                      navigation = scene_root.get_navigation();
    const std::shared_ptr<Navigation_mesh> nav_mesh   = navigation.get_mesh();
    out_height_map.navigation_available = static_cast<bool>(nav_mesh);
    out_height_map.navigation_stale     = nav_mesh && navigation.get_status().staleness.is_stale();
    // A hit is walkable when a polygon lies over or under it within the agent's climb.
    const glm::vec3 nav_half_extents = nav_mesh
        ? glm::vec3{c_walkable_half_width, std::max(nav_mesh->get_settings().agent_max_climb, c_walkable_half_width), c_walkable_half_width}
        : glm::vec3{0.0f};
#endif

    erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
    rt_scene.commit();
    const float range = grid.max.y - grid.min.y;
    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            Height_map_cell&  cell   = out_height_map.cells[(row * grid.columns) + column];
            const glm::vec2   xz     = get_cell_center(grid, column, row);
            const Surface_hit hit    = cast_height_ray(rt_scene, glm::vec3{xz.x, grid.max.y, xz.y}, range);
            cell = Height_map_cell{};
            if (!hit.found) {
                continue;
            }
            cell.hit    = true;
            cell.height = hit.position.y;
#if defined(ERHE_USE_DETOUR)
            if (nav_mesh) {
                const Navigation_point_result point = nav_mesh->find_nearest_point(hit.position, nav_half_extents);
                const glm::vec2 offset{point.point.x - hit.position.x, point.point.z - hit.position.z};
                if (point.found && (glm::length(offset) <= c_walkable_over_polygon)) {
                    cell.walkable = true;
                    cell.island   = static_cast<int32_t>(point.island);
                }
            }
#endif
        }
    }
}

void find_support(
    Scene_root&                                               scene_root,
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    const float                                               tolerance,
    std::vector<Support_result>&                              out_results
)
{
    out_results.clear();
    erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
    rt_scene.commit();
    const glm::vec3 down{0.0f, -1.0f, 0.0f};
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        if (!mesh) {
            continue;
        }
        const erhe::math::Aabb           local_bounds = mesh_local_bounds(*mesh);
        const erhe::scene::Trs_transform world        = mesh->world_from_node_transform();
        const erhe::scene::Mesh* const   self         = mesh.get();
        Support_result result{.mesh = mesh};
        if (local_bounds.is_valid_3d()) {
            const Surface_distance snap = find_snap_distance(
                rt_scene,
                local_bounds,
                Surface_pose{.position = world.get_translation(), .rotation = world.get_rotation(), .scale = world.get_scale()},
                down,
                std::span<const erhe::scene::Mesh* const>{&self, 1}
            );
            result.surface_found = snap.found;
            result.distance      = snap.distance;
            result.supported     = snap.found && (snap.distance <= tolerance);
        }
        out_results.push_back(std::move(result));
    }
}

namespace {

// The body's view of the node: world rotation and translation, no scale (node_physics_system.cpp).
[[nodiscard]] auto physics_transform(const erhe::scene::Mesh& mesh) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world = mesh.world_from_node_transform();
    return erhe::physics::Transform{glm::mat3_cast(world.get_rotation()), world.get_translation()};
}

[[nodiscard]] auto get_hull(Overlap_report& report, const std::size_t index) -> const erhe::physics::ICollision_shape*
{
    if (!report.hulls[index]) {
        report.hulls[index] = build_shape_from_mesh(report.tested[index].get(), true);
    }
    return report.hulls[index].get();
}

class Measured_depth
{
public:
    std::optional<float> depth;
    Overlap_measure      measure{Overlap_measure::not_measured};
};

[[nodiscard]] auto measure_pair(Overlap_report& report, const Bounds_pair& pair) -> Measured_depth
{
    using erhe::physics::ICollision_shape;
    const erhe::physics::Transform transform_a = physics_transform(*report.tested[pair.a]);
    const erhe::physics::Transform transform_b = physics_transform(*report.tested[pair.b]);
    const ICollision_shape* const  shape_a     = report.shapes[pair.a].get();
    const ICollision_shape* const  shape_b     = report.shapes[pair.b].get();
    if ((shape_a != nullptr) && (shape_b != nullptr)) {
        const std::optional<float> depth = ICollision_shape::get_penetration_depth(*shape_a, transform_a, *shape_b, transform_b);
        if (depth.has_value()) {
            return Measured_depth{.depth = depth, .measure = Overlap_measure::exact};
        }
    }
    // A hull for a missing shape; for two shapes the backend cannot collide (two triangle meshes)
    // each against the other's hull, the smaller depth: each bounds the depth of the two from above.
    const ICollision_shape* const hull_a = get_hull(report, pair.a);
    const ICollision_shape* const hull_b = get_hull(report, pair.b);
    std::optional<float> best{};
    const auto consider = [&best](const ICollision_shape* lhs, const erhe::physics::Transform& lhs_transform, const ICollision_shape* rhs, const erhe::physics::Transform& rhs_transform) {
        if ((lhs == nullptr) || (rhs == nullptr)) {
            return;
        }
        const std::optional<float> depth = ICollision_shape::get_penetration_depth(*lhs, lhs_transform, *rhs, rhs_transform);
        if (depth.has_value() && (!best.has_value() || (depth.value() < best.value()))) {
            best = depth;
        }
    };
    if ((shape_a != nullptr) && (shape_b != nullptr)) {
        consider(hull_a,  transform_a, shape_b, transform_b);
        consider(shape_a, transform_a, hull_b,  transform_b);
    } else {
        consider((shape_a != nullptr) ? shape_a : hull_a, transform_a, (shape_b != nullptr) ? shape_b : hull_b, transform_b);
    }
    if (best.has_value()) {
        return Measured_depth{.depth = best, .measure = Overlap_measure::convex_hull};
    }
    return Measured_depth{};
}

} // anonymous namespace

auto c_str(const Overlap_measure measure) -> const char*
{
    switch (measure) {
        case Overlap_measure::exact:        return "exact";
        case Overlap_measure::convex_hull:  return "convex_hull";
        case Overlap_measure::not_measured: return "not_measured";
        default:                            return "?";
    }
}

void find_overlaps(
    const std::span<const std::shared_ptr<erhe::scene::Mesh>> meshes,
    const float                                               tolerance,
    const std::span<const std::string>                        exclude_tags,
    Overlap_report&                                           out_report
)
{
    ERHE_PROFILE_FUNCTION();
    out_report.overlaps.clear();
    out_report.tested.clear();
    out_report.bounds.clear();
    out_report.shapes.clear();
    out_report.hulls.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : meshes) {
        if (mesh && !has_any_tag(mesh->get_tags(), exclude_tags)) {
            out_report.tested.push_back(mesh);
            out_report.bounds.push_back(mesh->get_aabb_world());
            out_report.shapes.push_back(get_node_collision_shape(*mesh));
            out_report.hulls.emplace_back();
        }
    }
    out_report.tested_count = out_report.tested.size();
    find_bounds_pairs(out_report.bounds, tolerance, out_report.order, out_report.pairs);
    out_report.bounds_pair_count = out_report.pairs.size();
    for (const Bounds_pair& pair : out_report.pairs) {
        const Measured_depth measured = measure_pair(out_report, pair);
        const float depth = measured.depth.has_value()
            ? measured.depth.value()
            : get_bounds_overlap(out_report.bounds[pair.a], out_report.bounds[pair.b]);
        if (!(depth > tolerance)) {
            continue;
        }
        std::shared_ptr<erhe::scene::Mesh> mesh_a = out_report.tested[pair.a];
        std::shared_ptr<erhe::scene::Mesh> mesh_b = out_report.tested[pair.b];
        if (mesh_b->get_id() < mesh_a->get_id()) {
            std::swap(mesh_a, mesh_b);
        }
        out_report.overlaps.push_back(Overlap_result{
            .mesh_a   = std::move(mesh_a),
            .mesh_b   = std::move(mesh_b),
            .depth    = depth,
            .measure  = measured.measure,
            .position = get_bounds_overlap_center(out_report.bounds[pair.a], out_report.bounds[pair.b])
        });
    }
    std::sort(
        out_report.overlaps.begin(), out_report.overlaps.end(),
        [](const Overlap_result& lhs, const Overlap_result& rhs) -> bool {
            return (lhs.mesh_a->get_id() < rhs.mesh_a->get_id()) ||
                ((lhs.mesh_a->get_id() == rhs.mesh_a->get_id()) && (lhs.mesh_b->get_id() < rhs.mesh_b->get_id()));
        }
    );
    // Only the results hold meshes after the call.
    out_report.tested.clear();
    out_report.shapes.clear();
    out_report.hulls.clear();
}

namespace {

void add_finding(Level_check& check, Level_finding&& finding)
{
    check.findings.push_back(std::move(finding));
}

[[nodiscard]] auto base_center(const erhe::math::Aabb& bounds) -> glm::vec3
{
    const glm::vec3 center = bounds.center();
    return glm::vec3{center.x, bounds.min.y, center.z};
}

} // anonymous namespace

void run_level_check(
    Scene_root&                scene_root,
    const Kit_shape_library&   library,
    const Level_check_options& options,
    Level_check&               out_check
)
{
    ERHE_PROFILE_FUNCTION();
    out_check.findings.clear();
    out_check.tested.clear();
    out_check.navigation_available = false;
    out_check.navigation_stale     = false;
    out_check.island_count         = 0;
    out_check.agent                = Kit_agent{};
    get_kit_piece_meshes(scene_root, out_check.pieces);
    out_check.piece_count = out_check.pieces.size();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : out_check.pieces) {
        if (!has_any_tag(mesh->get_tags(), options.exclude_tags)) {
            out_check.tested.push_back(mesh);
        }
    }
    out_check.excluded_count = out_check.piece_count - out_check.tested.size();

    // R10.4
    find_support(scene_root, out_check.tested, options.support_tolerance, out_check.support);
    for (const Support_result& result : out_check.support) {
        if (result.supported) {
            continue;
        }
        add_finding(out_check, Level_finding{
            .kind       = Level_finding_kind::unsupported,
            .position   = base_center(result.mesh->get_aabb_world()),
            .meshes     = {result.mesh, {}},
            .mesh_count = 1,
            .value      = result.distance,
            .has_value  = result.surface_found,
            .detail     = result.surface_found
                ? fmt::format("{}: the nearest surface is {:.3f} m under the base", result.mesh->get_name(), result.distance)
                : fmt::format("{}: no surface under the base", result.mesh->get_name())
        });
    }
    out_check.support.clear();

    // R10.3
    find_overlaps(out_check.tested, options.overlap_tolerance, std::span<const std::string>{}, out_check.overlap);
    for (const Overlap_result& overlap : out_check.overlap.overlaps) {
        add_finding(out_check, Level_finding{
            .kind       = Level_finding_kind::overlap,
            .position   = overlap.position,
            .meshes     = {overlap.mesh_a, overlap.mesh_b},
            .mesh_count = 2,
            .value      = overlap.depth,
            .has_value  = true,
            .measure    = overlap.measure,
            .detail     = fmt::format(
                "{} and {} interpenetrate by {:.3f} m{}", overlap.mesh_a->get_name(), overlap.mesh_b->get_name(), overlap.depth,
                (overlap.measure == Overlap_measure::exact) ? "" : fmt::format(" ({})", c_str(overlap.measure))
            )
        });
    }
    out_check.overlap.overlaps.clear();

    // R9.10
#if defined(ERHE_USE_DETOUR)
    Scene_navigation&                      navigation = scene_root.get_navigation();
    const Navigation_build_settings        settings   = navigation.get_settings();
    const std::shared_ptr<Navigation_mesh> nav_mesh   = navigation.get_mesh();
    out_check.agent = Kit_agent{
        .height            = settings.agent_height,
        .radius            = settings.agent_radius,
        .max_climb         = settings.agent_max_climb,
        .max_slope_degrees = settings.agent_max_slope
    };
    out_check.navigation_available = static_cast<bool>(nav_mesh);
    out_check.navigation_stale     = nav_mesh && navigation.get_status().staleness.is_stale();
    if (nav_mesh) {
        const std::span<const Navigation_island> islands = nav_mesh->get_islands();
        out_check.island_count = islands.size();
        erhe::raytrace::IScene& rt_scene = scene_root.get_raytrace_scene();
        rt_scene.commit();
        const float climb = std::max(nav_mesh->get_settings().agent_max_climb, c_walkable_half_width);
        for (std::size_t i = 0, end = islands.size(); i < end; ++i) {
            const Navigation_island& island = islands[i];
            if (island.reachable || !(island.area > static_cast<double>(options.min_island_area))) {
                continue;
            }
            // The piece under the island's point: the first upward face within the climb.
            const Surface_hit hit = cast_height_ray(rt_scene, island.point + glm::vec3{0.0f, climb, 0.0f}, 2.0f * climb);
            std::shared_ptr<erhe::scene::Mesh> mesh{};
            if (hit.found && (hit.mesh != nullptr)) {
                for (const std::shared_ptr<erhe::scene::Mesh>& piece : out_check.pieces) {
                    if (piece.get() == hit.mesh) {
                        mesh = piece;
                        break;
                    }
                }
            }
            add_finding(out_check, Level_finding{
                .kind       = Level_finding_kind::unreachable_island,
                .position   = island.point,
                .meshes     = {mesh, {}},
                .mesh_count = mesh ? std::size_t{1} : std::size_t{0},
                .value      = static_cast<float>(island.area),
                .has_value  = true,
                .island     = static_cast<uint32_t>(i),
                .detail     = fmt::format(
                    "island {} of {:.2f} m2 is unreachable{}", i, island.area, mesh ? fmt::format(", on {}", mesh->get_name()) : std::string{}
                )
            });
        }
    }
#endif

    // R2.10
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : out_check.pieces) {
        const std::optional<Kit_piece_data> record = read_kit_piece(*mesh);
        if (!record.has_value()) {
            continue;
        }
        const Kit_shape_definition* const shape = library.find(record.value().shape);
        std::string problem{};
        Level_finding_kind kind = Level_finding_kind::unwalkable_parameters;
        if (shape == nullptr) {
            kind    = Level_finding_kind::unreadable_piece;
            problem = fmt::format("{}: unknown shape '{}'", mesh->get_name(), record.value().shape);
        } else if (shape->check_walkability) {
            Kit_parameter_values values{*shape};
            const std::string error = parse_canonical_text(*shape, record.value().parameters, values);
            if (!error.empty()) {
                kind    = Level_finding_kind::unreadable_piece;
                problem = fmt::format("{}: {}", mesh->get_name(), error);
            } else {
                problem = shape->check_walkability(values, out_check.agent);
                if (!problem.empty()) {
                    problem = fmt::format("{}: {}", mesh->get_name(), problem);
                }
            }
        }
        if (problem.empty()) {
            continue;
        }
        add_finding(out_check, Level_finding{
            .kind       = kind,
            .position   = mesh->world_from_node_transform().get_translation(),
            .meshes     = {mesh, {}},
            .mesh_count = 1,
            .detail     = std::move(problem)
        });
    }
    out_check.tested.clear();
}

} // namespace editor
