#include "level/level_queries.hpp"

#include "scene/node_raytrace_mask.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/primitive.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/iscene.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/mesh_raytrace.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace editor {

namespace {

constexpr float        c_max_distance      = 10000.0f;
// An excluded hit restarts the ray this far past it.
constexpr float        c_pass_through_step = 1.0e-4f;
constexpr unsigned int c_max_pass_throughs = 64;

class Surface_hit
{
public:
    bool      found   {false};
    float     distance{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 normal  {0.0f, 1.0f, 0.0f};
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
        if (is_excluded(mesh, excluded)) {
            passed          = true;
            passed_distance = travelled + ray.t_far;
            passed_normal   = hit.normal;
            travelled       = passed_distance + c_pass_through_step;
            continue;
        }
        // Leaving a solid right after a pass-through: its entry face was stepped over, so it lies
        // on the passed face (a moved piece resting on it); that face's plane is the surface.
        const bool  exits    = passed && (glm::dot(hit.normal, direction) > 0.0f);
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

    const std::array<glm::vec3, 5> base_points{
        base_center_local,
        glm::vec3{local_bounds.min.x, local_bounds.min.y, local_bounds.min.z},
        glm::vec3{local_bounds.max.x, local_bounds.min.y, local_bounds.min.z},
        glm::vec3{local_bounds.min.x, local_bounds.min.y, local_bounds.max.z},
        glm::vec3{local_bounds.max.x, local_bounds.min.y, local_bounds.max.z}
    };
    for (const glm::vec3& point : base_points) {
        // Each ray starts at the top plane above its base point, so a sunk piece rises.
        const glm::vec3   base = to_world(point);
        const glm::vec3   top  = to_world(glm::vec3{point.x, local_bounds.max.y, point.z});
        const Surface_hit hit  = cast_surface_ray(rt_scene, top, options.direction, excluded);
        if (!hit.found) {
            continue;
        }
        const float distance = hit.distance - glm::dot(base - top, options.direction);
        if (!result.surface_found || (distance < result.distance)) {
            result.distance      = distance;
            result.surface_found = true;
        }
    }
    if (result.surface_found) {
        result.pose.position = start.position + (result.distance * options.direction);
    }
    return result;
}

} // namespace editor
