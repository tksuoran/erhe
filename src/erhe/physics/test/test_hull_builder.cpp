#include "erhe_physics/box3d/box3d_hull_builder.hpp"

#include <gtest/gtest.h>

#include <box3d/box3d.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace {

class Bounds
{
public:
    glm::vec3 min{ std::numeric_limits<float>::max()};
    glm::vec3 max{-std::numeric_limits<float>::max()};
};

[[nodiscard]] auto bounds_of(const std::vector<b3Vec3>& points) -> Bounds
{
    Bounds bounds{};
    for (const b3Vec3& point : points) {
        bounds.min = glm::min(bounds.min, glm::vec3{point.x, point.y, point.z});
        bounds.max = glm::max(bounds.max, glm::vec3{point.x, point.y, point.z});
    }
    return bounds;
}

[[nodiscard]] auto contains_point(const std::vector<b3Vec3>& points, const glm::vec3& target, const float tolerance) -> bool
{
    return std::any_of(
        points.begin(),
        points.end(),
        [&target, tolerance](const b3Vec3& point) {
            return glm::distance(glm::vec3{point.x, point.y, point.z}, target) <= tolerance;
        }
    );
}

} // anonymous namespace

TEST(hull_builder, box_points)
{
    std::vector<b3Vec3> points;
    erhe::physics::make_box_hull_points(glm::vec3{1.0f, 2.0f, 3.0f}, points);
    EXPECT_EQ(points.size(), 8u);
    const Bounds bounds = bounds_of(points);
    EXPECT_FLOAT_EQ(bounds.min.x, -1.0f);
    EXPECT_FLOAT_EQ(bounds.max.x,  1.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, -2.0f);
    EXPECT_FLOAT_EQ(bounds.max.y,  2.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, -3.0f);
    EXPECT_FLOAT_EQ(bounds.max.z,  3.0f);
}

TEST(hull_builder, cylinder_points_are_centered_on_the_origin)
{
    std::vector<b3Vec3> points;
    erhe::physics::make_tapered_cylinder_hull_points(2.0f, 2.0f, 5.0f, points);
    EXPECT_EQ(points.size(), 2u * static_cast<std::size_t>(erhe::physics::hull_slice_count));
    const Bounds bounds = bounds_of(points);
    EXPECT_NEAR(bounds.min.y, -5.0f, 1e-5f);
    EXPECT_NEAR(bounds.max.y,  5.0f, 1e-5f);
    EXPECT_NEAR(bounds.max.x,  2.0f, 1e-5f);
}

TEST(hull_builder, cone_collapses_the_zero_radius_end_to_an_apex)
{
    // erhe has no cone shape type; a cone is a tapered cylinder with one radius
    // at zero. Box3D's own b3CreateCone asserts both radii are positive, which
    // is why the points are generated here instead.
    std::vector<b3Vec3> points;
    erhe::physics::make_tapered_cylinder_hull_points(3.0f, 0.0f, 4.0f, points);
    EXPECT_EQ(points.size(), static_cast<std::size_t>(erhe::physics::hull_slice_count) + 1u);
    EXPECT_TRUE(contains_point(points, glm::vec3{0.0f, 4.0f, 0.0f}, 1e-5f));
}

TEST(hull_builder, sphere_points_stay_on_the_sphere)
{
    std::vector<b3Vec3> points;
    erhe::physics::append_sphere_hull_points(
        glm::vec3{0.0f, 0.0f, 0.0f},
        2.0f,
        erhe::physics::hull_sphere_longitude,
        erhe::physics::hull_sphere_latitude,
        points
    );
    const std::size_t expected =
        static_cast<std::size_t>(erhe::physics::hull_sphere_longitude * erhe::physics::hull_sphere_latitude) + 2u;
    EXPECT_EQ(points.size(), expected);
    for (const b3Vec3& point : points) {
        EXPECT_NEAR(glm::length(glm::vec3{point.x, point.y, point.z}), 2.0f, 1e-5f);
    }
}

TEST(hull_builder, tapered_capsule_contains_both_cap_extremes)
{
    // The silhouette must reach the far pole of each cap sphere.
    std::vector<b3Vec3> points;
    erhe::physics::make_tapered_capsule_hull_points(2.0f, 0.5f, 3.0f, points);
    EXPECT_TRUE(contains_point(points, glm::vec3{0.0f, -3.0f - 2.0f, 0.0f}, 1e-5f)) << "bottom pole";
    EXPECT_TRUE(contains_point(points, glm::vec3{0.0f,  3.0f + 0.5f, 0.0f}, 1e-5f)) << "top pole";
    const Bounds bounds = bounds_of(points);
    EXPECT_NEAR(bounds.max.x, 2.0f, 1e-5f);
}

TEST(hull_builder, sphere_tessellation_respects_the_box3d_edge_budget)
{
    // The binding limit is edges, not vertices: for a UV sphere
    // E = L * (2A + 1), and b3CreateHull rejects hulls above B3_MAX_HULL_EDGES.
    // Latitude counts must be odd so a ring lands on the equator.
    EXPECT_LE(
        erhe::physics::hull_sphere_longitude * ((2 * erhe::physics::hull_sphere_latitude) + 1),
        B3_MAX_HULL_EDGES
    );
    EXPECT_LE(
        erhe::physics::hull_capsule_longitude * ((2 * erhe::physics::hull_capsule_latitude) + 1),
        B3_MAX_HULL_EDGES
    );
    EXPECT_EQ(erhe::physics::hull_sphere_latitude  % 2, 1);
    EXPECT_EQ(erhe::physics::hull_capsule_latitude % 2, 1);
}

TEST(hull_builder, generated_hulls_are_valid)
{
    EXPECT_TRUE(erhe::physics::create_box_hull(glm::vec3{1.0f, 1.0f, 1.0f}).is_valid());
    EXPECT_TRUE(erhe::physics::create_cylinder_hull(1.0f, 2.0f).is_valid());
    EXPECT_TRUE(erhe::physics::create_tapered_cylinder_hull(2.0f, 1.0f, 2.0f).is_valid());
    EXPECT_TRUE(erhe::physics::create_tapered_cylinder_hull(2.0f, 0.0f, 2.0f).is_valid()) << "cone";
    EXPECT_TRUE(erhe::physics::create_sphere_hull(1.0f).is_valid());
    EXPECT_TRUE(erhe::physics::create_capsule_hull(1.0f, 2.0f).is_valid());
    EXPECT_TRUE(erhe::physics::create_tapered_capsule_hull(2.0f, 0.5f, 1.0f).is_valid());
}

TEST(hull_builder, generated_hulls_stay_within_the_box3d_budgets)
{
    const erhe::physics::Box3d_hull hull = erhe::physics::create_tapered_capsule_hull(2.0f, 0.5f, 1.0f);
    ASSERT_TRUE(hull.is_valid());
    EXPECT_LE(hull.get()->vertexCount, B3_MAX_HULL_VERTICES);
    EXPECT_LE(hull.get()->edgeCount, 2 * B3_MAX_HULL_EDGES) << "edgeCount is the half-edge count";
}

TEST(hull_builder, convex_hull_honors_a_padded_stride)
{
    // Callers pass interleaved vertex buffers, so the stride is not always 12.
    class Padded_point
    {
    public:
        float position[3]{};
        float padding [5]{};
    };
    Padded_point points[8]{};
    for (int corner = 0; corner < 8; ++corner) {
        points[corner].position[0] = ((corner & 1) != 0) ? 1.0f : -1.0f;
        points[corner].position[1] = ((corner & 2) != 0) ? 1.0f : -1.0f;
        points[corner].position[2] = ((corner & 4) != 0) ? 1.0f : -1.0f;
    }
    const erhe::physics::Box3d_hull hull = erhe::physics::create_convex_hull(
        &points[0].position[0],
        8,
        static_cast<int>(sizeof(Padded_point))
    );
    ASSERT_TRUE(hull.is_valid());
    EXPECT_EQ(hull.get()->vertexCount, 8);
    EXPECT_NEAR(hull.get()->volume, 8.0f, 1e-3f);
}

TEST(hull_builder, degenerate_input_yields_an_invalid_hull)
{
    // Fewer than four points cannot bound a volume; the failure is reported
    // rather than producing a shape that silently collides with nothing.
    std::vector<b3Vec3> points;
    points.push_back(b3Vec3{0.0f, 0.0f, 0.0f});
    points.push_back(b3Vec3{1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(erhe::physics::create_hull_from_points(points, "degenerate").is_valid());
    EXPECT_FALSE(erhe::physics::create_convex_hull(nullptr, 0, 12).is_valid());
}
