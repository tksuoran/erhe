#include "erhe_physics/box3d/box3d_hull_builder.hpp"
#include "erhe_physics/physics_log.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <utility>

namespace erhe::physics {

Box3d_hull::Box3d_hull(b3HullData* hull)
    : m_hull{hull}
{
}

Box3d_hull::~Box3d_hull() noexcept
{
    if (m_hull != nullptr) {
        b3DestroyHull(m_hull);
        m_hull = nullptr;
    }
}

Box3d_hull::Box3d_hull(Box3d_hull&& other) noexcept
    : m_hull{std::exchange(other.m_hull, nullptr)}
{
}

auto Box3d_hull::operator=(Box3d_hull&& other) noexcept -> Box3d_hull&
{
    if (this != &other) {
        if (m_hull != nullptr) {
            b3DestroyHull(m_hull);
        }
        m_hull = std::exchange(other.m_hull, nullptr);
    }
    return *this;
}

void make_box_hull_points(const glm::vec3& half_extents, std::vector<b3Vec3>& points)
{
    points.clear();
    points.reserve(8);
    for (int corner = 0; corner < 8; ++corner) {
        const float x = ((corner & 1) != 0) ? half_extents.x : -half_extents.x;
        const float y = ((corner & 2) != 0) ? half_extents.y : -half_extents.y;
        const float z = ((corner & 4) != 0) ? half_extents.z : -half_extents.z;
        points.push_back(b3Vec3{x, y, z});
    }
}

void make_tapered_cylinder_hull_points(
    const float          bottom_radius,
    const float          top_radius,
    const float          half_height,
    std::vector<b3Vec3>& points
)
{
    points.clear();
    points.reserve(2 * static_cast<std::size_t>(hull_slice_count));

    const float delta_alpha = glm::two_pi<float>() / static_cast<float>(hull_slice_count);
    const float radii[2]    = {bottom_radius, top_radius};
    const float heights[2]  = {-half_height, half_height};

    for (int end = 0; end < 2; ++end) {
        if (radii[end] <= 0.0f) {
            // Degenerate end: a single apex point (a cone tip).
            points.push_back(b3Vec3{0.0f, heights[end], 0.0f});
            continue;
        }
        for (int slice = 0; slice < hull_slice_count; ++slice) {
            const float alpha = delta_alpha * static_cast<float>(slice);
            points.push_back(
                b3Vec3{
                    radii[end] * std::cos(alpha),
                    heights[end],
                    radii[end] * std::sin(alpha)
                }
            );
        }
    }
}

void append_sphere_hull_points(
    const glm::vec3&     center,
    const float          radius,
    const int            longitude_count,
    const int            latitude_count,
    std::vector<b3Vec3>& points
)
{
    points.push_back(b3Vec3{center.x, center.y + radius, center.z});
    points.push_back(b3Vec3{center.x, center.y - radius, center.z});

    const float delta_alpha = glm::two_pi<float>() / static_cast<float>(longitude_count);
    for (int ring = 0; ring < latitude_count; ++ring) {
        // Rings strictly between the poles.
        const float theta     = glm::pi<float>() * static_cast<float>(ring + 1) / static_cast<float>(latitude_count + 1);
        const float ring_y    = center.y + (radius * std::cos(theta));
        const float ring_r    = radius * std::sin(theta);
        for (int longitude = 0; longitude < longitude_count; ++longitude) {
            const float alpha = delta_alpha * static_cast<float>(longitude);
            points.push_back(
                b3Vec3{
                    center.x + (ring_r * std::cos(alpha)),
                    ring_y,
                    center.z + (ring_r * std::sin(alpha))
                }
            );
        }
    }
}

void make_tapered_capsule_hull_points(
    const float          bottom_radius,
    const float          top_radius,
    const float          half_length,
    std::vector<b3Vec3>& points
)
{
    points.clear();
    points.reserve(2 * static_cast<std::size_t>((hull_capsule_longitude * hull_capsule_latitude) + 2));
    // Full spheres, not hemispheres: when the two radii differ the common
    // tangent touches each sphere away from its equator, on the side facing the
    // other cap, so a hemisphere would clip the true silhouette.
    append_sphere_hull_points(glm::vec3{0.0f, -half_length, 0.0f}, bottom_radius, hull_capsule_longitude, hull_capsule_latitude, points);
    append_sphere_hull_points(glm::vec3{0.0f,  half_length, 0.0f}, top_radius,    hull_capsule_longitude, hull_capsule_latitude, points);
}

auto create_hull_from_points(const std::vector<b3Vec3>& points, const char* debug_label) -> Box3d_hull
{
    if (points.size() < 4) {
        log_physics->error("box3d hull '{}': {} points is below the minimum of 4", debug_label, points.size());
        return Box3d_hull{};
    }
    b3HullData* hull = b3CreateHull(points.data(), static_cast<int>(points.size()), B3_MAX_HULL_VERTICES);
    if (hull == nullptr) {
        log_physics->error("box3d hull '{}': b3CreateHull failed for {} points (degenerate point set?)", debug_label, points.size());
        return Box3d_hull{};
    }
    return Box3d_hull{hull};
}

auto create_box_hull(const glm::vec3& half_extents) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    make_box_hull_points(half_extents, points);
    return create_hull_from_points(points, "box");
}

auto create_cylinder_hull(const float radius, const float half_height) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    make_tapered_cylinder_hull_points(radius, radius, half_height, points);
    return create_hull_from_points(points, "cylinder");
}

auto create_tapered_cylinder_hull(const float bottom_radius, const float top_radius, const float half_height) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    make_tapered_cylinder_hull_points(bottom_radius, top_radius, half_height, points);
    return create_hull_from_points(points, "tapered cylinder");
}

auto create_sphere_hull(const float radius) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    append_sphere_hull_points(glm::vec3{0.0f, 0.0f, 0.0f}, radius, hull_sphere_longitude, hull_sphere_latitude, points);
    return create_hull_from_points(points, "sphere");
}

auto create_capsule_hull(const float radius, const float half_length) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    make_tapered_capsule_hull_points(radius, radius, half_length, points);
    return create_hull_from_points(points, "capsule");
}

auto create_tapered_capsule_hull(const float bottom_radius, const float top_radius, const float half_length) -> Box3d_hull
{
    std::vector<b3Vec3> points;
    make_tapered_capsule_hull_points(bottom_radius, top_radius, half_length, points);
    return create_hull_from_points(points, "tapered capsule");
}

auto create_convex_hull(const float* points, const int point_count, const int stride) -> Box3d_hull
{
    std::vector<b3Vec3> hull_points;
    if ((points == nullptr) || (point_count <= 0)) {
        log_physics->error("box3d hull 'convex hull': no input points");
        return Box3d_hull{};
    }
    hull_points.reserve(static_cast<std::size_t>(point_count));
    const char* base = reinterpret_cast<const char*>(points);
    for (int i = 0; i < point_count; ++i) {
        const float* point = reinterpret_cast<const float*>(base + (static_cast<std::size_t>(i) * static_cast<std::size_t>(stride)));
        hull_points.push_back(b3Vec3{point[0], point[1], point[2]});
    }
    return create_hull_from_points(hull_points, "convex hull");
}

} // namespace erhe::physics
