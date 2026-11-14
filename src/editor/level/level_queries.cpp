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

constexpr float        c_max_distance      = 10000.0f;
// An excluded hit restarts the ray this far past it.
constexpr float        c_pass_through_step = 1.0e-4f;
constexpr unsigned int c_max_pass_throughs = 64;
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

[[nodiscard]] auto is_excluded(const erhe::scene::Mesh* mesh, const std::span<const erhe::scene::Mesh* const> excluded) -> bool
{
    return (mesh != nullptr) && (std::find(excluded.begin(), excluded.end(), mesh) != excluded.end());
}

// The nearest content surface along the ray; hits on excluded meshes are passed through.
[[nodiscard]] auto cast_surface_ray(
    erhe::raytrace::IScene&                         rt_scene,
    const glm::vec3                                 origin,
    const glm::vec3                                 direction,
    const std::span<const erhe::scene::Mesh* const> excluded
) -> Surface_hit
{
    float     travelled = 0.0f;
    bool      passed    = false;
    float     passed_distance{0.0f};
    glm::vec3 passed_normal  {0.0f};
    for (unsigned int pass = 0; pass < c_max_pass_throughs; ++pass) {
        erhe::raytrace::Ray ray{
            .origin    = origin + travelled * direction,
            .t_near    = 0.0f,
            .direction = direction,
            .time      = 0.0f,
            .t_far     = c_max_distance - travelled,
            .mask      = Raytrace_node_mask::content,
            .id        = 0,
            .flags     = 0
        };
        erhe::raytrace::Hit hit;
        rt_scene.intersect(ray, hit);
        if (hit.instance == nullptr) {
            return Surface_hit{};
        }
        const erhe::scene::Raytrace_primitive* const raytrace_primitive =
            static_cast<const erhe::scene::Raytrace_primitive*>(hit.instance->get_user_data());
        const erhe::scene::Mesh* const mesh = (raytrace_primitive != nullptr) ? raytrace_primitive->mesh : nullptr;
        const bool leaves_solid = glm::dot(hit.normal, direction) > 0.0f;
        if (is_excluded(mesh, excluded)) {
            // Only a face where the ray leaves the moved piece can lie on a surface.
            passed          = leaves_solid;
            passed_distance = travelled + ray.t_far;
            passed_normal   = hit.normal;
            travelled       = passed_distance + c_pass_through_step;
            continue;
        }
        if (leaves_solid && !passed) {
            // Leaving a solid not entered through a surface: e.g. the bottom of a piece resting on the start plane.
            travelled = travelled + ray.t_far + c_pass_through_step;
            continue;
        }
        // Leaving a solid right after leaving a moved piece: its entry face was stepped over, so it
        // lies on the passed face (a moved piece resting on it); that face's plane is the surface.
        const bool  exits    = leaves_solid;
        // The hit normal is the unnormalized geometric normal; a surface faces the ray.
        glm::vec3   normal   = exits ? passed_normal : hit.normal;
        const float length   = glm::length(normal);
        normal = (length > 0.0f) ? (normal / length) : -direction;
        if (glm::dot(normal, direction) > 0.0f) {
            normal = -normal;
        }
        const float distance = exits ? passed_distance : (travelled + ray.t_far);
        return Surface_hit{
            .found    = true,
            .distance = distance,
            .position = origin + distance * direction,
            .normal   = normal
        };
    }
    return Surface_hit{};
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
        // Each ray starts at the top plane above its base point, so a sunk piece rises.
        const glm::vec3   base = start.position + (start.rotation * (start.scale * point));
        const glm::vec3   top  = start.position + (start.rotation * (start.scale * glm::vec3{point.x, local_bounds.max.y, point.z}));
        const Surface_hit hit  = cast_surface_ray(rt_scene, top, direction, excluded);
        if (!hit.found) {
            continue;
        }
        const float distance = hit.distance - glm::dot(base - top, direction);
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
    const glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float travelled = 0.0f;
    for (unsigned int pass = 0; pass < c_max_pass_throughs; ++pass) {
        if (!(travelled < max_distance)) {
            return Surface_hit{};
        }
        erhe::raytrace::Ray ray{
            .origin    = origin + travelled * direction,
            .t_near    = 0.0f,
            .direction = direction,
            .time      = 0.0f,
            .t_far     = max_distance - travelled,
            .mask      = Raytrace_node_mask::content,
            .id        = 0,
            .flags     = 0
        };
        erhe::raytrace::Hit hit;
        rt_scene.intersect(ray, hit);
        if (hit.instance == nullptr) {
            return Surface_hit{};
        }
        const float distance = travelled + ray.t_far;
        if (glm::dot(hit.normal, direction) >= 0.0f) {
            travelled = distance + c_pass_through_step;
            continue;
        }
        const erhe::scene::Raytrace_primitive* const raytrace_primitive =
            static_cast<const erhe::scene::Raytrace_primitive*>(hit.instance->get_user_data());
        return Surface_hit{
            .found    = true,
            .distance = distance,
            .position = origin + distance * direction,
            .normal   = glm::normalize(hit.normal),
            .mesh     = (raytrace_primitive != nullptr) ? raytrace_primitive->mesh : nullptr
        };
    }
    return Surface_hit{};
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

    const glm::vec3 center            = local_bounds.center();
    const glm::vec3 base_center_local{center.x, local_bounds.min.y, center.z};
    const auto to_world = [&start](const glm::vec3 local) -> glm::vec3 {
        return start.position + (start.rotation * (start.scale * local));
    };

    if (options.orient == Surface_orient::on) {
        // Orient rests the base center on the center hit; it implies snapping (D7.3).
        const glm::vec3   base_center = to_world(base_center_local);
        const glm::vec3   top_center  = to_world(glm::vec3{center.x, local_bounds.max.y, center.z});
        const Surface_hit hit         = cast_surface_ray(rt_scene, top_center, options.direction, excluded);
        if (!hit.found) {
            return result;
        }
        const glm::vec3 up       = glm::normalize(start.rotation * glm::vec3{0.0f, 1.0f, 0.0f});
        const glm::quat rotation = glm::normalize(shortest_rotation(up, hit.normal) * start.rotation);
        result.pose.rotation = rotation;
        result.pose.position = hit.position - (rotation * (start.scale * base_center_local));
        result.distance      = hit.distance - glm::dot(base_center - top_center, options.direction);
        result.surface_found = true;
        return result;
    }

    const Surface_distance snap = find_snap_distance(rt_scene, local_bounds, start, options.direction, excluded);
    result.distance      = snap.distance;
    result.surface_found = snap.found;
    if (result.surface_found) {
        result.pose.position = start.position + (result.distance * options.direction);
    }
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
