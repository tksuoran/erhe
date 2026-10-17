// Tests for erhe::math::optimal_enclosing_sphere().
//
// Two properties are checked for every input:
//   enclosure: every input point passes the exact Sphere::contains(point) test;
//   tightness: the radius is at most Jung's bound (diameter * sqrt(3/8), the
//              largest minimal enclosing sphere any 3D point set of that
//              diameter can have) plus a slack for float rounding at the scale
//              of the input. Cases with a known minimal sphere check against it.
//
// The fixed cases cover trivial inputs, degenerate configurations, cases that
// are hard numerically, and all of them at several distances from the origin.
// The smoke test draws a case kind and its content from a seeded generator and
// runs for ERHE_SPHERE_SMOKE_SECONDS (default 1); on failure it prints the
// seed, iteration, kind and the points as hexadecimal floats for replay.

#include "erhe_math/sphere.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <ios>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

using erhe::math::Sphere;

constexpr float jung_factor = 1.2247449f; // sqrt(3/8) * 2: minimal radius <= half diameter * this

class Check_result
{
public:
    bool        ok{true};
    std::string message;
};

[[nodiscard]] auto farthest_from(const std::vector<glm::vec3>& points, const glm::vec3& origin) -> std::size_t
{
    std::size_t best  = 0;
    float       best2 = -1.0f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float d2 = glm::distance2(points[i], origin);
        if (d2 > best2) {
            best2 = d2;
            best  = i;
        }
    }
    return best;
}

// Half of the diameter of the set. Exact (all pairs) for small sets; for
// large sets a lower bound from the axis-extreme points, the farthest point
// from the centroid, and farthest-point iteration from each of them. A lower
// bound keeps Jung's bound a valid bound on the minimal radius only when the
// estimate reaches the true diameter, which these seeds do for every
// generated distribution; the exact path covers the rest.
[[nodiscard]] auto half_diameter(const std::vector<glm::vec3>& points) -> float
{
    const std::size_t n = points.size();
    float best2 = 0.0f;
    if (n <= 1024) {
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                best2 = std::max(best2, glm::distance2(points[i], points[j]));
            }
        }
        return 0.5f * std::sqrt(best2);
    }
    glm::vec3 centroid{0.0f};
    std::array<std::size_t, 6> extremes{0, 0, 0, 0, 0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        centroid += points[i] / static_cast<float>(n);
        for (int axis = 0; axis < 3; ++axis) {
            if (points[i][axis] < points[extremes[static_cast<std::size_t>(axis * 2)]][axis]) {
                extremes[static_cast<std::size_t>(axis * 2)] = i;
            }
            if (points[i][axis] > points[extremes[static_cast<std::size_t>(axis * 2 + 1)]][axis]) {
                extremes[static_cast<std::size_t>(axis * 2 + 1)] = i;
            }
        }
    }
    std::vector<std::size_t> seeds{extremes.begin(), extremes.end()};
    seeds.push_back(farthest_from(points, centroid));
    for (std::size_t seed : seeds) {
        for (int iteration = 0; iteration < 4; ++iteration) {
            const std::size_t far_index = farthest_from(points, points[seed]);
            best2 = std::max(best2, glm::distance2(points[seed], points[far_index]));
            seed = far_index;
        }
    }
    return 0.5f * std::sqrt(best2);
}

[[nodiscard]] auto magnitude(const std::vector<glm::vec3>& points) -> float
{
    float best2 = 0.0f;
    for (const glm::vec3& p : points) {
        best2 = std::max(best2, glm::length2(p));
    }
    return std::sqrt(best2);
}

// Rounding slack for a radius comparison: relative to the extent of the set,
// an absolute floor for the padding the fit adds near the origin, and the
// float resolution at the distance of the set from the origin.
[[nodiscard]] auto radius_slack(const float extent, const float magnitude_) -> float
{
    return (2e-3f * extent) + 1.5e-2f + (2e-5f * magnitude_);
}

[[nodiscard]] auto hex(const float value) -> std::string
{
    std::ostringstream stream;
    stream << std::hexfloat << value;
    return stream.str();
}

[[nodiscard]] auto describe(const std::vector<glm::vec3>& points, const Sphere& sphere) -> std::string
{
    std::ostringstream stream;
    stream << "sphere center (" << hex(sphere.center.x) << ", " << hex(sphere.center.y) << ", " << hex(sphere.center.z) << ") radius " << hex(sphere.radius) << " = " << sphere.radius << "\n";
    stream << "points " << points.size() << ":\n";
    for (const glm::vec3& p : points) {
        stream << "    {" << hex(p.x) << "f, " << hex(p.y) << "f, " << hex(p.z) << "f},\n";
    }
    return stream.str();
}

// Checks enclosure and tightness. expected_radius < 0 means unknown: only
// Jung's bound is applied; otherwise the radius must be within slack of it
// (never below it beyond rounding, since that would contradict enclosure).
[[nodiscard]] auto check_sphere(const std::vector<glm::vec3>& points, const Sphere& sphere, const float expected_radius = -1.0f) -> Check_result
{
    Check_result result;
    std::ostringstream stream;
    if (!std::isfinite(sphere.radius) || !std::isfinite(sphere.center.x) || !std::isfinite(sphere.center.y) || !std::isfinite(sphere.center.z)) {
        stream << "non-finite sphere\n";
        result.ok = false;
    }
    if (sphere.radius < 0.0f) {
        stream << "negative radius\n";
        result.ok = false;
    }
    float worst_overshoot = 0.0f;
    std::size_t outside = 0;
    for (const glm::vec3& p : points) {
        if (!sphere.contains(p)) {
            ++outside;
            worst_overshoot = std::max(worst_overshoot, glm::distance(p, sphere.center) - sphere.radius);
        }
    }
    if (outside > 0) {
        stream << outside << " point(s) outside, worst overshoot " << worst_overshoot << " (" << hex(worst_overshoot) << ")\n";
        result.ok = false;
    }
    if (!points.empty()) {
        const float half   = half_diameter(points);
        const float extent = 2.0f * half;
        const float slack  = radius_slack(extent, magnitude(points));
        const float bound  = (expected_radius >= 0.0f) ? expected_radius : (half * jung_factor);
        if (sphere.radius > bound + slack) {
            stream << "radius " << sphere.radius << " exceeds " << ((expected_radius >= 0.0f) ? "expected " : "Jung bound ") << bound << " by " << (sphere.radius - bound) << " (slack " << slack << ")\n";
            result.ok = false;
        }
        if ((expected_radius >= 0.0f) && (sphere.radius < expected_radius - slack)) {
            stream << "radius " << sphere.radius << " below expected minimum " << expected_radius << " (slack " << slack << ")\n";
            result.ok = false;
        }
    }
    if (!result.ok) {
        stream << describe(points, sphere);
    }
    result.message = stream.str();
    return result;
}

[[nodiscard]] auto run(const std::vector<glm::vec3>& points, const float expected_radius = -1.0f) -> Check_result
{
    const Sphere sphere = erhe::math::optimal_enclosing_sphere(points);
    return check_sphere(points, sphere, expected_radius);
}

#define EXPECT_SPHERE_OK(points, ...) \
    do { \
        const Check_result check_result_ = run((points), ##__VA_ARGS__); \
        EXPECT_TRUE(check_result_.ok) << check_result_.message; \
    } while (false)

// Offsets at which every fixed case is repeated; the extents of the cases are
// around 1, so the offsets span from near the origin to where a float holds
// about two decimals of the coordinates.
const std::vector<glm::vec3> offsets{
    glm::vec3{0.0f},
    glm::vec3{1.0f, -2.0f, 3.0f},
    glm::vec3{100.0f, 50.0f, -100.0f},
    glm::vec3{4400.0f, -24.0f, -3700.0f},
    glm::vec3{-30000.0f, 12000.0f, 20000.0f}
};

// Scales applied to a unit-sized case before the offset.
const std::vector<float> scales{1e-2f, 1.0f, 37.0f, 1000.0f};

[[nodiscard]] auto placed(const std::vector<glm::vec3>& unit_points, const float scale, const glm::vec3& offset) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> points;
    points.reserve(unit_points.size());
    for (const glm::vec3& p : unit_points) {
        points.push_back(offset + (scale * p));
    }
    return points;
}

// Runs a unit case at every scale and offset; expected_unit_radius < 0 means unknown.
void run_everywhere(const char* name, const std::vector<glm::vec3>& unit_points, const float expected_unit_radius = -1.0f)
{
    for (const float scale : scales) {
        for (const glm::vec3& offset : offsets) {
            const std::vector<glm::vec3> points = placed(unit_points, scale, offset);
            const float expected = (expected_unit_radius >= 0.0f) ? (expected_unit_radius * scale) : -1.0f;
            const Check_result check = run(points, expected);
            EXPECT_TRUE(check.ok) << name << " scale " << scale << " offset (" << offset.x << ", " << offset.y << ", " << offset.z << ")\n" << check.message;
        }
    }
}

[[nodiscard]] auto fibonacci_sphere(const std::size_t count, const float radius) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> points;
    points.reserve(count);
    const float golden = std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
    for (std::size_t i = 0; i < count; ++i) {
        const float y      = 1.0f - (2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(count));
        const float r_xz   = std::sqrt(std::max(0.0f, 1.0f - (y * y)));
        const float theta  = golden * static_cast<float>(i);
        points.push_back(radius * glm::vec3{r_xz * std::cos(theta), y, r_xz * std::sin(theta)});
    }
    return points;
}

[[nodiscard]] auto circle(const std::size_t count, const float radius, const glm::vec3& axis_a, const glm::vec3& axis_b) -> std::vector<glm::vec3>
{
    std::vector<glm::vec3> points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(count);
        points.push_back(radius * ((std::cos(angle) * axis_a) + (std::sin(angle) * axis_b)));
    }
    return points;
}

} // anonymous namespace

// ---- Trivial inputs -------------------------------------------------------

TEST(Sphere, empty_input_is_a_finite_degenerate_sphere)
{
    const Sphere sphere = erhe::math::optimal_enclosing_sphere({});
    EXPECT_TRUE(std::isfinite(sphere.radius));
    EXPECT_EQ(sphere.radius, 0.0f);
}

TEST(Sphere, single_point)
{
    for (const glm::vec3& offset : offsets) {
        const std::vector<glm::vec3> points{offset};
        const Sphere sphere = erhe::math::optimal_enclosing_sphere(points);
        EXPECT_EQ(sphere.center, offset);
        EXPECT_EQ(sphere.radius, 0.0f);
        EXPECT_TRUE(sphere.contains(offset));
    }
}

TEST(Sphere, two_points)
{
    run_everywhere("two points", {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}, 1.0f);
    run_everywhere("two points diagonal", {glm::vec3{-1.0f, -1.0f, -1.0f}, glm::vec3{1.0f, 1.0f, 1.0f}}, std::sqrt(3.0f));
    run_everywhere("two identical points", {glm::vec3{0.5f, 0.25f, -0.75f}, glm::vec3{0.5f, 0.25f, -0.75f}}, 0.0f);
}

TEST(Sphere, three_points)
{
    const float s = std::sqrt(3.0f) / 2.0f;
    run_everywhere("equilateral triangle",  {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{-0.5f, s, 0.0f}, glm::vec3{-0.5f, -s, 0.0f}}, 1.0f);
    run_everywhere("obtuse triangle",       {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.1f, 0.0f}}, 1.0f);
    run_everywhere("right triangle",        {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 2.0f, 0.0f}}, std::sqrt(2.0f));
    run_everywhere("collinear three",       {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}, 1.0f);
    run_everywhere("collinear three skew",  {glm::vec3{-1.0f, -1.0f, 0.0f}, glm::vec3{0.3f, 0.3f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f}}, std::sqrt(2.0f));
    run_everywhere("two identical of three", {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}, 1.0f);
    run_everywhere("three identical",       {glm::vec3{0.2f, 0.3f, 0.4f}, glm::vec3{0.2f, 0.3f, 0.4f}, glm::vec3{0.2f, 0.3f, 0.4f}}, 0.0f);
    run_everywhere("thin triangle",         {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1e-4f, 0.0f}}, 1.0f);
}

TEST(Sphere, four_points)
{
    // Regular tetrahedron inscribed in the unit sphere.
    const float k = 1.0f / std::sqrt(3.0f);
    run_everywhere("regular tetrahedron", {glm::vec3{k, k, k}, glm::vec3{k, -k, -k}, glm::vec3{-k, k, -k}, glm::vec3{-k, -k, k}}, 1.0f);
    // Unit square in a plane: minimal sphere is the circumscribed circle.
    run_everywhere("coplanar square", {glm::vec3{-1.0f, -1.0f, 0.0f}, glm::vec3{1.0f, -1.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f}, glm::vec3{-1.0f, 1.0f, 0.0f}}, std::sqrt(2.0f));
    // A point inside the sphere of the other three.
    run_everywhere("interior fourth", {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.1f, 0.1f, 0.1f}}, 1.0f);
    // Four collinear points.
    run_everywhere("collinear four", {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{-0.3f, 0.0f, 0.0f}, glm::vec3{0.6f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}, 1.0f);
    // Almost coplanar tetrahedron: the sphere through four points is huge, the minimal one is the circle.
    run_everywhere("near-coplanar tetrahedron", {glm::vec3{-1.0f, -1.0f, 0.0f}, glm::vec3{1.0f, -1.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f}, glm::vec3{-1.0f, 1.0f, 1e-4f}}, std::sqrt(2.0f));
    // All identical.
    run_everywhere("four identical", {glm::vec3{0.7f, -0.2f, 0.1f}, glm::vec3{0.7f, -0.2f, 0.1f}, glm::vec3{0.7f, -0.2f, 0.1f}, glm::vec3{0.7f, -0.2f, 0.1f}}, 0.0f);
    // Two points supporting, two inside.
    run_everywhere("antipodal plus interior", {glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{0.2f, 0.1f, 0.0f}, glm::vec3{-0.3f, 0.2f, 0.4f}}, 1.0f);
}

TEST(Sphere, five_points)
{
    const float k = 1.0f / std::sqrt(3.0f);
    run_everywhere("tetrahedron plus interior", {glm::vec3{k, k, k}, glm::vec3{k, -k, -k}, glm::vec3{-k, k, -k}, glm::vec3{-k, -k, k}, glm::vec3{0.05f, -0.02f, 0.1f}}, 1.0f);
    run_everywhere("tetrahedron plus exterior", {glm::vec3{k, k, k}, glm::vec3{k, -k, -k}, glm::vec3{-k, k, -k}, glm::vec3{-k, -k, k}, glm::vec3{0.0f, 0.0f, -3.0f}});
    run_everywhere("five on a circle", circle(5, 1.0f, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}), 1.0f);
    run_everywhere("five collinear", {glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{-0.5f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.5f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}}, 1.0f);
    run_everywhere("five identical", std::vector<glm::vec3>(5, glm::vec3{0.1f, 0.2f, 0.3f}), 0.0f);
}

// Five points of a Bistro mesh, roughly 6 km from the origin. The sphere
// through four of them did not contain the fifth within the former absolute
// tolerance, because float rounding at that magnitude exceeds it.
TEST(Sphere, bistro_points_far_from_origin)
{
    const std::vector<glm::vec3> points{
        glm::vec3{4462.23f, -24.2743f, -3769.47f},
        glm::vec3{4339.65f, -24.2743f, -3538.67f},
        glm::vec3{4386.4f,  -33.4032f, -3524.29f},
        glm::vec3{4339.98f, -32.3836f, -3538.6f},
        glm::vec3{4434.06f, -24.2313f, -3780.36f}
    };
    EXPECT_SPHERE_OK(points);
    // Every input order.
    std::vector<glm::vec3> permuted = points;
    std::sort(permuted.begin(), permuted.end(), [](const glm::vec3& l, const glm::vec3& r) { return l.x < r.x; });
    do {
        EXPECT_SPHERE_OK(permuted);
    } while (std::next_permutation(permuted.begin(), permuted.end(), [](const glm::vec3& l, const glm::vec3& r) { return l.x < r.x; }));
}

// Two clusters, from the smoke test (seed 5, iteration 6503): three points
// of one cluster are nearly collinear, the circumsphere of any four points
// is a sliver's, and the former fallback grew a cluster-sized sphere to the
// far cluster, doubling the radius.
TEST(Sphere, two_clusters_sliver_supports)
{
    const std::vector<glm::vec3> points{
        {0x1.69443ep+2f, 0x1.264edap+4f, 0x1.3580b2p+4f},
        {0x1.11535ep+2f, 0x1.db7a12p+3f, 0x1.207440p+4f},
        {0x1.8e97cap+0f, 0x1.78a570p+3f, 0x1.08e356p+4f},
        {-0x1.fe8c2ap+0f, -0x1.03e92cp+4f, -0x1.0aae74p+4f},
        {-0x1.3377e0p+1f, -0x1.043c7ep+4f, -0x1.1ae338p+4f},
        {-0x1.c90746p+1f, -0x1.04c29ap+4f, -0x1.13eb6ap+4f},
        {0x1.76ac16p+0f, -0x1.b00b40p+3f, -0x1.4d5922p+4f}
    };
    EXPECT_SPHERE_OK(points);
    for (const float scale : scales) {
        for (const glm::vec3& offset : offsets) {
            const std::vector<glm::vec3> placed_points = placed(points, scale, offset);
            const Check_result check = run(placed_points);
            EXPECT_TRUE(check.ok) << "scale " << scale << " offset " << offset.x << "\n" << check.message;
        }
    }
}

// ---- Structured sets --------------------------------------------------------

TEST(Sphere, cube_corners_with_interior_points)
{
    std::vector<glm::vec3> unit;
    for (int i = 0; i < 8; ++i) {
        unit.push_back(glm::vec3{((i & 1) != 0) ? 1.0f : -1.0f, ((i & 2) != 0) ? 1.0f : -1.0f, ((i & 4) != 0) ? 1.0f : -1.0f});
    }
    run_everywhere("cube corners", unit, std::sqrt(3.0f));
    for (int i = 0; i < 20; ++i) {
        const float t = static_cast<float>(i) / 20.0f;
        unit.push_back(glm::vec3{t - 0.5f, 0.5f - t, 0.25f * t});
    }
    run_everywhere("cube corners plus interior", unit, std::sqrt(3.0f));
}

TEST(Sphere, points_on_a_sphere_surface)
{
    run_everywhere("fibonacci 32",   fibonacci_sphere(32,   1.0f), 1.0f);
    run_everywhere("fibonacci 500",  fibonacci_sphere(500,  1.0f), 1.0f);
    run_everywhere("fibonacci 5000", fibonacci_sphere(5000, 1.0f), 1.0f);
}

TEST(Sphere, points_on_a_circle)
{
    const glm::vec3 a = glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f});
    const glm::vec3 b = glm::normalize(glm::vec3{-1.0f, 1.0f, 1.0f});
    run_everywhere("circle 7",   circle(7,   1.0f, a, b), 1.0f);
    run_everywhere("circle 100", circle(100, 1.0f, a, b), 1.0f);
}

TEST(Sphere, points_on_a_line)
{
    std::vector<glm::vec3> unit;
    for (int i = 0; i < 101; ++i) {
        const float t = (static_cast<float>(i) / 50.0f) - 1.0f;
        unit.push_back(t * glm::vec3{0.6f, 0.0f, 0.8f});
    }
    run_everywhere("line 101", unit, 1.0f);
    std::reverse(unit.begin(), unit.end());
    run_everywhere("line 101 reversed", unit, 1.0f);
}

TEST(Sphere, needle_and_pancake)
{
    // Needle: long in x, tiny in y and z.
    std::vector<glm::vec3> needle;
    for (int i = 0; i < 64; ++i) {
        const float t = (static_cast<float>(i) / 63.0f) * 2.0f - 1.0f;
        needle.push_back(glm::vec3{t, 1e-3f * std::sin(7.0f * t), 1e-3f * std::cos(5.0f * t)});
    }
    run_everywhere("needle", needle, 1.0f);
    // Pancake: a disc with tiny thickness.
    std::vector<glm::vec3> pancake = circle(64, 1.0f, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    for (std::size_t i = 0; i < pancake.size(); ++i) {
        pancake[i].z = 1e-3f * static_cast<float>(i % 3);
        pancake[i] *= (0.2f + 0.8f * static_cast<float>((i * 7) % 11) / 10.0f);
    }
    pancake.push_back(glm::vec3{1.0f, 0.0f, 0.0f});
    pancake.push_back(glm::vec3{-1.0f, 0.0f, 0.0f});
    run_everywhere("pancake", pancake, 1.0f);
}

TEST(Sphere, two_clusters_and_duplicates)
{
    std::vector<glm::vec3> unit;
    for (int i = 0; i < 30; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        unit.push_back(glm::vec3{-1.0f + (0.05f * t), 0.05f * std::sin(10.0f * t), 0.05f * std::cos(10.0f * t)});
        unit.push_back(glm::vec3{1.0f - (0.05f * t), 0.05f * std::cos(10.0f * t), 0.05f * std::sin(10.0f * t)});
    }
    run_everywhere("two clusters", unit);
    std::vector<glm::vec3> duplicated;
    for (const glm::vec3& p : unit) {
        duplicated.push_back(p);
        duplicated.push_back(p);
        duplicated.push_back(p);
    }
    run_everywhere("two clusters triplicated", duplicated);
}

TEST(Sphere, all_points_identical)
{
    run_everywhere("1000 identical", std::vector<glm::vec3>(1000, glm::vec3{-0.3f, 0.9f, 0.4f}), 0.0f);
}

// ---- Hard cases ----------------------------------------------------------------

// A mesh-like set: a sphere surface sampled coarsely with tangent jitter, far
// from the origin, so a candidate sphere through four support points misses
// the fifth by rounding.
TEST(Sphere, jittered_surface_far_from_origin)
{
    std::mt19937 rng{7u};
    std::uniform_real_distribution<float> jitter{-1e-3f, 1e-3f};
    for (const float radius : {0.05f, 1.0f, 120.0f}) {
        std::vector<glm::vec3> unit = fibonacci_sphere(200, 1.0f);
        for (glm::vec3& p : unit) {
            p += glm::vec3{jitter(rng), jitter(rng), jitter(rng)};
        }
        for (const glm::vec3& offset : offsets) {
            const std::vector<glm::vec3> points = placed(unit, radius, offset);
            const Check_result check = run(points, radius * (1.0f + 1e-3f));
            EXPECT_TRUE(check.ok) << "radius " << radius << " offset " << offset.x << "\n" << check.message;
        }
    }
}

// Input order determines which supports the search visits; every order must
// give an enclosing sphere of the same tightness.
TEST(Sphere, input_order_independence)
{
    std::mt19937 rng{99u};
    std::uniform_real_distribution<float> unit_range{-1.0f, 1.0f};
    std::vector<glm::vec3> unit;
    for (int i = 0; i < 300; ++i) {
        unit.push_back(glm::vec3{unit_range(rng), unit_range(rng), unit_range(rng)});
    }
    for (const glm::vec3& offset : offsets) {
        std::vector<glm::vec3> points = placed(unit, 10.0f, offset);
        const Sphere first = erhe::math::optimal_enclosing_sphere(points);
        for (int pass = 0; pass < 8; ++pass) {
            std::shuffle(points.begin(), points.end(), rng);
            const Sphere sphere = erhe::math::optimal_enclosing_sphere(points);
            const Check_result check = check_sphere(points, sphere, first.radius);
            EXPECT_TRUE(check.ok) << "pass " << pass << " offset " << offset.x << "\n" << check.message;
        }
    }
}

// The fit's absolute padding must not blow up a sphere that is tiny compared to it.
TEST(Sphere, tiny_extent_near_origin)
{
    const std::vector<glm::vec3> points{
        glm::vec3{0.0f, 0.0f, 0.0f},
        glm::vec3{1e-5f, 0.0f, 0.0f},
        glm::vec3{0.0f, 1e-5f, 0.0f},
        glm::vec3{0.0f, 0.0f, 1e-5f},
        glm::vec3{1e-5f, 1e-5f, 1e-5f}
    };
    const Sphere sphere = erhe::math::optimal_enclosing_sphere(points);
    const Check_result check = check_sphere(points, sphere);
    EXPECT_TRUE(check.ok) << check.message;
    EXPECT_LT(sphere.radius, 0.05f);
}

// Large extents: a kilometre-scale set near the origin and one far away.
TEST(Sphere, large_extents)
{
    run_everywhere("fibonacci 200 at scale 1e5", placed(fibonacci_sphere(200, 1.0f), 1e5f, glm::vec3{0.0f}), 1e5f);
    const std::vector<glm::vec3> far_points = placed(fibonacci_sphere(200, 1.0f), 1e4f, glm::vec3{2e6f, -1e6f, 5e5f});
    const Check_result check = run(far_points, 1e4f);
    EXPECT_TRUE(check.ok) << check.message;
}

// ---- Smoke test ----------------------------------------------------------------

namespace {

enum class Case_kind : int {
    few_points = 0,
    uniform_box,
    gaussian_cloud,
    sphere_surface,
    circle_points,
    line_points,
    plane_points,
    two_clusters,
    duplicates,
    needle,
    near_coplanar_tetrahedron,
    identical,
    integer_lattice,
    count
};

[[nodiscard]] auto case_name(const Case_kind kind) -> const char*
{
    switch (kind) {
        case Case_kind::few_points:                return "few_points";
        case Case_kind::uniform_box:               return "uniform_box";
        case Case_kind::gaussian_cloud:            return "gaussian_cloud";
        case Case_kind::sphere_surface:            return "sphere_surface";
        case Case_kind::circle_points:             return "circle_points";
        case Case_kind::line_points:               return "line_points";
        case Case_kind::plane_points:              return "plane_points";
        case Case_kind::two_clusters:              return "two_clusters";
        case Case_kind::duplicates:                return "duplicates";
        case Case_kind::needle:                    return "needle";
        case Case_kind::near_coplanar_tetrahedron: return "near_coplanar_tetrahedron";
        case Case_kind::identical:                 return "identical";
        case Case_kind::integer_lattice:           return "integer_lattice";
        default:                                   return "?";
    }
}

class Smoke_case
{
public:
    Case_kind              kind{Case_kind::few_points};
    float                  scale{1.0f};
    glm::vec3              offset{0.0f};
    std::vector<glm::vec3> points;
    float                  expected_radius{-1.0f}; // in placed units; < 0 = unknown, Jung bound applies
};

class Smoke_generator
{
public:
    explicit Smoke_generator(const std::uint64_t seed) : m_rng{seed} {}

    [[nodiscard]] auto next() -> Smoke_case
    {
        Smoke_case smoke;
        smoke.kind = static_cast<Case_kind>(uniform_int(0, static_cast<int>(Case_kind::count) - 1));
        // Scale: 10^U(-3, 4). Offset magnitude: 0 (one in four) or 10^U(-1, 6), direction random.
        smoke.scale = std::pow(10.0f, uniform(-3.0f, 4.0f));
        if (uniform_int(0, 3) != 0) {
            const float magnitude_ = std::pow(10.0f, uniform(-1.0f, 6.0f));
            // Float resolution at the offset must leave room for the extent.
            smoke.offset = magnitude_ * random_direction();
            smoke.scale  = std::max(smoke.scale, magnitude_ * 1e-4f);
        }
        std::vector<glm::vec3> unit;
        float expected_unit = -1.0f;
        generate(smoke.kind, unit, expected_unit);
        std::shuffle(unit.begin(), unit.end(), m_rng);
        smoke.points          = placed(unit, smoke.scale, smoke.offset);
        smoke.expected_radius = (expected_unit >= 0.0f) ? (expected_unit * smoke.scale) : -1.0f;
        return smoke;
    }

private:
    [[nodiscard]] auto uniform(const float lo, const float hi) -> float
    {
        return std::uniform_real_distribution<float>{lo, hi}(m_rng);
    }
    [[nodiscard]] auto uniform_int(const int lo, const int hi) -> int
    {
        return std::uniform_int_distribution<int>{lo, hi}(m_rng);
    }
    [[nodiscard]] auto random_direction() -> glm::vec3
    {
        for (;;) {
            const glm::vec3 v{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)};
            const float length2 = glm::length2(v);
            if ((length2 > 1e-4f) && (length2 <= 1.0f)) {
                return v / std::sqrt(length2);
            }
        }
    }
    [[nodiscard]] auto random_frame() -> std::array<glm::vec3, 3>
    {
        const glm::vec3 a = random_direction();
        glm::vec3 b = random_direction();
        b = b - (glm::dot(a, b) * a);
        while (glm::length2(b) < 1e-3f) {
            b = random_direction();
            b = b - (glm::dot(a, b) * a);
        }
        b = glm::normalize(b);
        return {a, b, glm::cross(a, b)};
    }
    [[nodiscard]] auto point_count(const int lo, const int hi) -> std::size_t
    {
        // Log-uniform so small and large sets are both common.
        const float f = std::pow(10.0f, uniform(std::log10(static_cast<float>(lo)), std::log10(static_cast<float>(hi))));
        return static_cast<std::size_t>(std::lround(f));
    }

    void generate(const Case_kind kind, std::vector<glm::vec3>& unit, float& expected_unit)
    {
        switch (kind) {
            case Case_kind::few_points: {
                const std::size_t count = static_cast<std::size_t>(uniform_int(0, 6));
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(glm::vec3{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)});
                }
                break;
            }
            case Case_kind::uniform_box: {
                const std::size_t count = point_count(5, 4096);
                const glm::vec3 half_size{uniform(0.1f, 1.0f), uniform(0.1f, 1.0f), uniform(0.1f, 1.0f)};
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(half_size * glm::vec3{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)});
                }
                break;
            }
            case Case_kind::gaussian_cloud: {
                const std::size_t count = point_count(5, 4096);
                std::normal_distribution<float> normal{0.0f, 0.4f};
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(glm::vec3{normal(m_rng), normal(m_rng), normal(m_rng)});
                }
                break;
            }
            case Case_kind::sphere_surface: {
                const std::size_t count = point_count(4, 4096);
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(random_direction());
                }
                // A dense sample has the unit sphere as its minimal sphere.
                if (count >= 200) {
                    expected_unit = 1.0f;
                }
                break;
            }
            case Case_kind::circle_points: {
                const std::size_t count = point_count(3, 1024);
                const std::array<glm::vec3, 3> frame = random_frame();
                for (std::size_t i = 0; i < count; ++i) {
                    const float angle = uniform(0.0f, 2.0f * std::numbers::pi_v<float>);
                    unit.push_back((std::cos(angle) * frame[0]) + (std::sin(angle) * frame[1]));
                }
                if (count >= 200) {
                    expected_unit = 1.0f;
                }
                break;
            }
            case Case_kind::line_points: {
                const std::size_t count = point_count(2, 1024);
                const glm::vec3 direction = random_direction();
                float t_min = 1.0f;
                float t_max = -1.0f;
                for (std::size_t i = 0; i < count; ++i) {
                    const float t = uniform(-1.0f, 1.0f);
                    t_min = std::min(t_min, t);
                    t_max = std::max(t_max, t);
                    unit.push_back(t * direction);
                }
                expected_unit = 0.5f * (t_max - t_min);
                break;
            }
            case Case_kind::plane_points: {
                const std::size_t count = point_count(3, 2048);
                const std::array<glm::vec3, 3> frame = random_frame();
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back((uniform(-1.0f, 1.0f) * frame[0]) + (uniform(-1.0f, 1.0f) * frame[1]));
                }
                break;
            }
            case Case_kind::two_clusters: {
                const std::size_t count = point_count(2, 1024);
                const glm::vec3 axis = random_direction();
                const float spread = uniform(0.001f, 0.2f);
                for (std::size_t i = 0; i < count; ++i) {
                    const float sign = ((i % 2) == 0) ? 1.0f : -1.0f;
                    unit.push_back((sign * axis) + (spread * glm::vec3{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)}));
                }
                break;
            }
            case Case_kind::duplicates: {
                const std::size_t distinct = point_count(1, 64);
                std::vector<glm::vec3> base;
                for (std::size_t i = 0; i < distinct; ++i) {
                    base.push_back(glm::vec3{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)});
                }
                const std::size_t count = point_count(static_cast<int>(distinct), 2048);
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(base[static_cast<std::size_t>(uniform_int(0, static_cast<int>(distinct) - 1))]);
                }
                break;
            }
            case Case_kind::needle: {
                const std::size_t count = point_count(2, 1024);
                const glm::vec3 direction = random_direction();
                const float thickness = std::pow(10.0f, uniform(-5.0f, -1.0f));
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back((uniform(-1.0f, 1.0f) * direction) + (thickness * glm::vec3{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)}));
                }
                unit.push_back(direction);
                unit.push_back(-direction);
                break;
            }
            case Case_kind::near_coplanar_tetrahedron: {
                const std::array<glm::vec3, 3> frame = random_frame();
                const float lift = std::pow(10.0f, uniform(-6.0f, -2.0f));
                for (int i = 0; i < 4; ++i) {
                    unit.push_back((uniform(-1.0f, 1.0f) * frame[0]) + (uniform(-1.0f, 1.0f) * frame[1]) + (lift * uniform(-1.0f, 1.0f) * frame[2]));
                }
                const std::size_t extra = static_cast<std::size_t>(uniform_int(0, 4));
                for (std::size_t i = 0; i < extra; ++i) {
                    unit.push_back((uniform(-1.0f, 1.0f) * frame[0]) + (uniform(-1.0f, 1.0f) * frame[1]) + (lift * uniform(-1.0f, 1.0f) * frame[2]));
                }
                break;
            }
            case Case_kind::identical: {
                const std::size_t count = point_count(1, 2048);
                const glm::vec3 p{uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f), uniform(-1.0f, 1.0f)};
                unit.assign(count, p);
                expected_unit = 0.0f;
                break;
            }
            case Case_kind::integer_lattice: {
                // Coordinates on a coarse grid: many exact ties between candidate supports.
                const int n = uniform_int(1, 4);
                const std::size_t count = point_count(2, 1024);
                for (std::size_t i = 0; i < count; ++i) {
                    unit.push_back(glm::vec3{
                        static_cast<float>(uniform_int(-n, n)) / static_cast<float>(n),
                        static_cast<float>(uniform_int(-n, n)) / static_cast<float>(n),
                        static_cast<float>(uniform_int(-n, n)) / static_cast<float>(n)
                    });
                }
                break;
            }
            default:
                break;
        }
    }

    std::mt19937_64 m_rng;
};

[[nodiscard]] auto env_value(const char* name, const double fallback) -> double
{
    const char* value = std::getenv(name);
    if ((value == nullptr) || (*value == '\0')) {
        return fallback;
    }
    return std::strtod(value, nullptr);
}

} // anonymous namespace

// Runs random cases for ERHE_SPHERE_SMOKE_SECONDS (default 1) from seed
// ERHE_SPHERE_SMOKE_SEED (default 20261017). Every failure reports the seed,
// iteration and kind, and the points as hexadecimal floats.
TEST(Sphere, smoke_pseudorandom_cases)
{
    const double        seconds = env_value("ERHE_SPHERE_SMOKE_SECONDS", 1.0);
    const std::uint64_t seed    = static_cast<std::uint64_t>(env_value("ERHE_SPHERE_SMOKE_SEED", 20261017.0));
    Smoke_generator generator{seed};
    std::array<std::size_t, static_cast<std::size_t>(Case_kind::count)> per_kind{};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::size_t iteration = 0;
    std::size_t failures  = 0;
    for (;; ++iteration) {
        if ((iteration % 64) == 0) {
            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed.count() >= seconds) {
                break;
            }
        }
        const Smoke_case smoke = generator.next();
        ++per_kind[static_cast<std::size_t>(smoke.kind)];
        const Check_result check = run(smoke.points, smoke.expected_radius);
        if (!check.ok) {
            ++failures;
            if (failures <= 10) {
                ADD_FAILURE() << "seed " << seed << " iteration " << iteration << " kind " << case_name(smoke.kind)
                              << " scale " << smoke.scale << " offset (" << smoke.offset.x << ", " << smoke.offset.y << ", " << smoke.offset.z << ")\n"
                              << check.message;
            }
        }
    }
    std::ostringstream summary;
    summary << "sphere smoke: seed " << seed << ", " << iteration << " cases in " << seconds << " s, " << failures << " failures;";
    for (std::size_t k = 0; k < per_kind.size(); ++k) {
        summary << " " << case_name(static_cast<Case_kind>(k)) << "=" << per_kind[k];
    }
    RecordProperty("summary", summary.str());
    std::cout << summary.str() << "\n";
    EXPECT_EQ(failures, 0u);
}
