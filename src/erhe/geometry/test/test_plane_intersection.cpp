#include "erhe_geometry/plane_intersection.hpp"

#include <gtest/gtest.h>

#include <cmath>

using erhe::geometry::Planef;
using erhe::geometry::intersect_three_planes_cramer;
using erhe::geometry::intersect_three_planes_least_squares;

namespace {

void expect_vec3f_near(
    const GEO::vec3f& actual,
    const GEO::vec3f& expected,
    float             tolerance,
    const char*       context = ""
)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance) << context;
    EXPECT_NEAR(actual.y, expected.y, tolerance) << context;
    EXPECT_NEAR(actual.z, expected.z, tolerance) << context;
}

auto signed_distance_to_plane(const Planef& plane, const GEO::vec3f& point) -> float
{
    return GEO::dot(plane.normal(), point) + plane.d;
}

auto sum_squared_distances(
    const Planef&    a,
    const Planef&    b,
    const Planef&    c,
    const GEO::vec3f& point
) -> float
{
    const float da = signed_distance_to_plane(a, point);
    const float db = signed_distance_to_plane(b, point);
    const float dc = signed_distance_to_plane(c, point);
    return (da * da) + (db * db) + (dc * dc);
}

} // anonymous namespace

//
// === Planef construction tests ===
//

TEST(Planef_Construction, PointNormal_AtOrigin)
{
    Planef plane{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    EXPECT_FLOAT_EQ(plane.a, 1.0f);
    EXPECT_FLOAT_EQ(plane.b, 0.0f);
    EXPECT_FLOAT_EQ(plane.c, 0.0f);
    EXPECT_FLOAT_EQ(plane.d, 0.0f);
}

TEST(Planef_Construction, PointNormal_Offset)
{
    // Plane x=5
    Planef plane{GEO::vec3f{5.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    EXPECT_FLOAT_EQ(plane.a,  1.0f);
    EXPECT_FLOAT_EQ(plane.d, -5.0f);
    EXPECT_NEAR(signed_distance_to_plane(plane, GEO::vec3f{0.0f, 0.0f, 0.0f}), -5.0f, 1e-6f);
    EXPECT_NEAR(signed_distance_to_plane(plane, GEO::vec3f{5.0f, 0.0f, 0.0f}),  0.0f, 1e-6f);
    EXPECT_NEAR(signed_distance_to_plane(plane, GEO::vec3f{7.0f, 0.0f, 0.0f}),  2.0f, 1e-6f);
}

TEST(Planef_Construction, ThreePoints_Normalized)
{
    // Triangle in XY plane -> normal should be unit +Z
    Planef plane{
        GEO::vec3f{0.0f, 0.0f, 0.0f},
        GEO::vec3f{1.0f, 0.0f, 0.0f},
        GEO::vec3f{0.0f, 1.0f, 0.0f}
    };
    EXPECT_NEAR(plane.a, 0.0f, 1e-6f);
    EXPECT_NEAR(plane.b, 0.0f, 1e-6f);
    EXPECT_NEAR(std::abs(plane.c), 1.0f, 1e-6f);
    EXPECT_NEAR(plane.d, 0.0f, 1e-6f);
}

TEST(Planef_Construction, ThreePoints_LargeTriangle)
{
    // Large triangle should still produce a unit normal
    Planef plane{
        GEO::vec3f{0.0f,    0.0f, 0.0f},
        GEO::vec3f{1000.0f, 0.0f, 0.0f},
        GEO::vec3f{0.0f, 1000.0f, 0.0f}
    };
    const float normal_length = GEO::length(plane.normal());
    EXPECT_NEAR(normal_length, 1.0f, 1e-5f);
}

TEST(Planef_Construction, DefaultConstructor_ZeroInitialized)
{
    Planef plane;
    EXPECT_FLOAT_EQ(plane.a, 0.0f);
    EXPECT_FLOAT_EQ(plane.b, 0.0f);
    EXPECT_FLOAT_EQ(plane.c, 0.0f);
    EXPECT_FLOAT_EQ(plane.d, 0.0f);
}

//
// === Well-conditioned intersection: coordinate axis planes ===
//

TEST(IntersectThreePlanes, CoordinatePlanes_AtOrigin)
{
    // x=0, y=0, z=0 -> intersection at origin
    Planef px{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f expected{0.0f, 0.0f, 0.0f};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(px, py, pz);
    ASSERT_TRUE(cramer.has_value());
    expect_vec3f_near(cramer.value(), expected, 1e-6f);

    GEO::vec3f ls = intersect_three_planes_least_squares(px, py, pz, ref);
    expect_vec3f_near(ls, expected, 1e-6f);
}

TEST(IntersectThreePlanes, CoordinatePlanes_Offset)
{
    // x=1, y=2, z=3 -> intersection at (1,2,3)
    Planef px{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 3.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f expected{1.0f, 2.0f, 3.0f};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(px, py, pz);
    ASSERT_TRUE(cramer.has_value());
    expect_vec3f_near(cramer.value(), expected, 1e-5f);

    GEO::vec3f ls = intersect_three_planes_least_squares(px, py, pz, ref);
    expect_vec3f_near(ls, expected, 1e-5f);
}

//
// === Well-conditioned: angled planes ===
//

TEST(IntersectThreePlanes, DiagonalPlanes)
{
    // Three planes with 45-degree normals, intersecting at (1,1,1)
    const float s = 1.0f / std::sqrt(2.0f);
    Planef p1{GEO::vec3f{1.0f, 1.0f, 0.0f}, GEO::vec3f{s, s, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 1.0f, 1.0f}, GEO::vec3f{0.0f, s, s}};
    Planef p3{GEO::vec3f{1.0f, 0.0f, 1.0f}, GEO::vec3f{s, 0.0f, s}};
    const GEO::vec3f expected{1.0f, 1.0f, 1.0f};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    ASSERT_TRUE(cramer.has_value());
    expect_vec3f_near(cramer.value(), expected, 1e-4f);

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    expect_vec3f_near(ls, expected, 1e-4f);
}

TEST(IntersectThreePlanes, ChamferLikeCorner)
{
    // Simulates a cube face corner being chamfered:
    // Face plane: z=0 (top face of unit cube)
    // Edge plane 1: x=0.5 (inset from edge)
    // Edge plane 2: y=0.5 (inset from edge)
    Planef face {GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    Planef edge1{GEO::vec3f{0.5f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef edge2{GEO::vec3f{0.0f, 0.5f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    const GEO::vec3f expected{0.5f, 0.5f, 0.0f};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(face, edge1, edge2);
    ASSERT_TRUE(cramer.has_value());
    expect_vec3f_near(cramer.value(), expected, 1e-6f);

    GEO::vec3f ls = intersect_three_planes_least_squares(face, edge1, edge2, ref);
    expect_vec3f_near(ls, expected, 1e-6f);
}

//
// === Large coordinates (the case that triggered ERHE_VERIFY(magnitude < 100)) ===
//

TEST(IntersectThreePlanes, LargeCoordinates)
{
    Planef px{GEO::vec3f{1000.0f, 0.0f,    0.0f},    GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f,    1000.0f, 0.0f},    GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f,    0.0f,    1000.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f expected{1000.0f, 1000.0f, 1000.0f};
    const GEO::vec3f ref{1000.0f, 1000.0f, 1000.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(px, py, pz);
    ASSERT_TRUE(cramer.has_value());
    expect_vec3f_near(cramer.value(), expected, 0.01f);

    GEO::vec3f ls = intersect_three_planes_least_squares(px, py, pz, ref);
    expect_vec3f_near(ls, expected, 0.01f);
}

//
// === Methods agree on well-conditioned systems ===
//

TEST(IntersectThreePlanes, MethodsAgree_AxisAligned)
{
    Planef px{GEO::vec3f{2.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 3.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 4.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(px, py, pz);
    ASSERT_TRUE(cramer.has_value());
    GEO::vec3f ls = intersect_three_planes_least_squares(px, py, pz, ref);
    expect_vec3f_near(cramer.value(), ls, 1e-5f);
}

TEST(IntersectThreePlanes, MethodsAgree_Angled)
{
    const float s = 1.0f / std::sqrt(3.0f);
    Planef p1{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef p3{GEO::vec3f{s, s, s},           GEO::vec3f{s, s, s}};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    ASSERT_TRUE(cramer.has_value());
    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    expect_vec3f_near(cramer.value(), ls, 1e-4f);
}

//
// === Order independence ===
//

TEST(IntersectThreePlanes, OrderIndependence_Cramer)
{
    Planef px{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 3.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};

    std::optional<GEO::vec3f> r1 = intersect_three_planes_cramer(px, py, pz);
    std::optional<GEO::vec3f> r2 = intersect_three_planes_cramer(py, pz, px);
    std::optional<GEO::vec3f> r3 = intersect_three_planes_cramer(pz, px, py);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    ASSERT_TRUE(r3.has_value());
    expect_vec3f_near(r1.value(), r2.value(), 1e-6f);
    expect_vec3f_near(r1.value(), r3.value(), 1e-6f);
}

TEST(IntersectThreePlanes, OrderIndependence_LeastSquares)
{
    Planef px{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 3.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    GEO::vec3f r1 = intersect_three_planes_least_squares(px, py, pz, ref);
    GEO::vec3f r2 = intersect_three_planes_least_squares(py, pz, px, ref);
    GEO::vec3f r3 = intersect_three_planes_least_squares(pz, px, py, ref);
    GEO::vec3f r4 = intersect_three_planes_least_squares(px, pz, py, ref);
    GEO::vec3f r5 = intersect_three_planes_least_squares(py, px, pz, ref);
    GEO::vec3f r6 = intersect_three_planes_least_squares(pz, py, px, ref);
    expect_vec3f_near(r1, r2, 1e-6f);
    expect_vec3f_near(r1, r3, 1e-6f);
    expect_vec3f_near(r1, r4, 1e-6f);
    expect_vec3f_near(r1, r5, 1e-6f);
    expect_vec3f_near(r1, r6, 1e-6f);
}

//
// === Degenerate: two parallel planes ===
//

TEST(IntersectThreePlanes, TwoParallelPlanes)
{
    // x=0, x=1 (parallel), z=0 (perpendicular)
    // No unique intersection. Least-squares minimizes (x)^2 + (x-1)^2 + (z)^2:
    //   optimal x = 0.5, z = 0, y = reference_y
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.5f, 7.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    EXPECT_FALSE(cramer.has_value());

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    EXPECT_NEAR(ls.x, 0.5f, 1e-4f);
    EXPECT_NEAR(ls.z, 0.0f, 1e-4f);
    EXPECT_NEAR(ls.y, ref.y, 1e-4f); // unconstrained -> stays at reference
}

//
// === Degenerate: all three parallel ===
//

TEST(IntersectThreePlanes, AllThreeParallel)
{
    // All normals +X, at x=0, x=1, x=2
    // Minimizes (x)^2 + (x-1)^2 + (x-2)^2 = 3x^2 - 6x + 5
    //   optimal x = 1
    // Y, Z unconstrained -> stay at reference
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p3{GEO::vec3f{2.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    const GEO::vec3f ref{5.0f, 3.0f, 7.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    EXPECT_FALSE(cramer.has_value());

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    EXPECT_NEAR(ls.x, 1.0f, 1e-4f);
    EXPECT_NEAR(ls.y, ref.y, 1e-4f);
    EXPECT_NEAR(ls.z, ref.z, 1e-4f);
}

//
// === Degenerate: two identical planes ===
//

TEST(IntersectThreePlanes, TwoIdenticalPlanes)
{
    // Two copies of x=0, plus z=0
    // Intersection is the Y axis (x=0, z=0, y free)
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.0f, 5.0f, 0.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    EXPECT_FALSE(cramer.has_value());

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    EXPECT_NEAR(ls.x, 0.0f, 1e-5f);
    EXPECT_NEAR(ls.z, 0.0f, 1e-5f);
    EXPECT_NEAR(ls.y, 5.0f, 1e-4f); // kept from reference
}

//
// === Degenerate: coplanar normals (prismatic arrangement) ===
//

TEST(IntersectThreePlanes, CoplanarNormals)
{
    // All three normals lie in the XY plane (no Z component)
    // The planes form a "prism" along Z -> no finite intersection point
    Planef p1{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 1.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    const float s = 1.0f / std::sqrt(2.0f);
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{s, s, 0.0f}};
    const GEO::vec3f ref{1.0f, 1.0f, 42.0f};

    std::optional<GEO::vec3f> cramer = intersect_three_planes_cramer(p1, p2, p3);
    EXPECT_FALSE(cramer.has_value());

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    // Z is unconstrained -> should stay at reference
    EXPECT_NEAR(ls.z, ref.z, 1e-4f);
    // X and Y are constrained by the three planes in the XY plane
    // The result should be finite and reasonable
    EXPECT_LT(GEO::length(ls), 1000.0f);
}

//
// === Nearly degenerate (chamfer-like: nearly collinear edges) ===
//

TEST(IntersectThreePlanes, NearlyParallelEdgePlanes)
{
    // Chamfer scenario: face plane + two nearly-parallel edge planes
    Planef face{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    Planef edge1{GEO::vec3f{0.5f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    // Edge2 normal is nearly identical to edge1 normal (0.01 rad rotation)
    const float angle = 0.01f;
    const GEO::vec3f n2 = GEO::normalize(GEO::vec3f{std::cos(angle), std::sin(angle), 0.0f});
    Planef edge2{GEO::vec3f{0.5f, 0.0f, 0.0f}, n2};
    const GEO::vec3f ref{0.5f, 0.0f, 0.0f};

    GEO::vec3f ls = intersect_three_planes_least_squares(face, edge1, edge2, ref);

    // Should be on the face plane (z=0)
    EXPECT_NEAR(ls.z, 0.0f, 1e-3f);
    // Should be near x=0.5 (both edge planes pass through x~0.5)
    EXPECT_NEAR(ls.x, 0.5f, 0.1f);
    // Must be finite and bounded
    EXPECT_LT(GEO::length(ls), 100.0f);
}

TEST(IntersectThreePlanes, NearlyParallelThroughOrigin)
{
    // All three planes pass through origin, two nearly parallel
    const float angle = 0.001f;
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{std::cos(angle), std::sin(angle), 0.0f}};
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    // All planes pass through origin, so (0,0,0) is a valid intersection
    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    EXPECT_NEAR(GEO::length(ls), 0.0f, 1e-3f);
}

//
// === Least-squares actually minimizes residual ===
//

TEST(IntersectThreePlanes, LeastSquaresMinimizesResidual_Degenerate)
{
    // Two parallel planes: x=0 and x=2, plus z=0
    // Optimal: x=1 (midpoint minimizes squared distances), z=0, y=reference
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{2.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{1.0f, 0.0f, 0.0f};

    GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
    const float ssd_result = sum_squared_distances(p1, p2, p3, ls);

    // Verify that nearby points have equal or higher sum of squared distances
    const float step = 0.1f;
    for (float dx = -0.5f; dx <= 0.5f; dx += step) {
        for (float dz = -0.5f; dz <= 0.5f; dz += step) {
            GEO::vec3f test_point = ls + GEO::vec3f{dx, 0.0f, dz};
            const float ssd_test = sum_squared_distances(p1, p2, p3, test_point);
            EXPECT_GE(ssd_test + 1e-5f, ssd_result)
                << "Point (" << test_point.x << ", " << test_point.y << ", " << test_point.z
                << ") has lower SSD " << ssd_test << " than result SSD " << ssd_result;
        }
    }
}

TEST(IntersectThreePlanes, LeastSquaresMinimizesResidual_WellConditioned)
{
    // Well-conditioned case: residual should be essentially zero
    Planef px{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 3.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};
    const GEO::vec3f ref{0.0f, 0.0f, 0.0f};

    GEO::vec3f ls = intersect_three_planes_least_squares(px, py, pz, ref);
    const float ssd = sum_squared_distances(px, py, pz, ls);
    EXPECT_NEAR(ssd, 0.0f, 1e-8f);
}

//
// === Reference point behavior in degenerate case ===
//

TEST(IntersectThreePlanes, ReferencePointAffectsUnconstrainedDirections)
{
    // Two identical x=0 planes + z=0 plane
    // Y axis is unconstrained -> result.y should track reference.y
    Planef p1{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p2{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef p3{GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};

    for (float ref_y : {-10.0f, 0.0f, 5.0f, 100.0f}) {
        GEO::vec3f ref{0.0f, ref_y, 0.0f};
        GEO::vec3f ls = intersect_three_planes_least_squares(p1, p2, p3, ref);
        EXPECT_NEAR(ls.x, 0.0f,  1e-4f);
        EXPECT_NEAR(ls.z, 0.0f,  1e-4f);
        EXPECT_NEAR(ls.y, ref_y, 1e-4f) << "ref_y = " << ref_y;
    }
}

TEST(IntersectThreePlanes, ReferencePointDoesNotAffectWellConditioned)
{
    // Well-conditioned: result should be the same regardless of reference
    Planef px{GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}};
    Planef py{GEO::vec3f{0.0f, 2.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}};
    Planef pz{GEO::vec3f{0.0f, 0.0f, 3.0f}, GEO::vec3f{0.0f, 0.0f, 1.0f}};

    GEO::vec3f ref1{0.0f, 0.0f, 0.0f};
    GEO::vec3f ref2{100.0f, 100.0f, 100.0f};
    GEO::vec3f ref3{-50.0f, 200.0f, -30.0f};

    GEO::vec3f ls1 = intersect_three_planes_least_squares(px, py, pz, ref1);
    GEO::vec3f ls2 = intersect_three_planes_least_squares(px, py, pz, ref2);
    GEO::vec3f ls3 = intersect_three_planes_least_squares(px, py, pz, ref3);
    expect_vec3f_near(ls1, ls2, 1e-3f);
    expect_vec3f_near(ls1, ls3, 1e-3f);
}
