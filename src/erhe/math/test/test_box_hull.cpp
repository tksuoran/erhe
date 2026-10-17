#include "erhe_math/aabb.hpp"
#include "erhe_math/box_hull.hpp"
#include "erhe_math/math_util.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <span>
#include <vector>

namespace {

[[nodiscard]] auto make_light_from_world(const glm::vec3& direction) -> glm::mat3
{
    const glm::vec3 rz = glm::normalize(direction);
    const glm::vec3 up = (std::abs(rz.y) > 0.999f) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 rx = glm::normalize(glm::cross(up, rz));
    const glm::vec3 ry = glm::cross(rz, rx);
    return glm::transpose(glm::mat3{rx, ry, rz});
}

// The hull of all projected corners and points, the way the fit computed it
// before the boxes got their own routine.
[[nodiscard]] auto reference_hull(const std::vector<erhe::math::Aabb>& boxes, const std::vector<glm::vec3>& points, const glm::mat3& light_from_world) -> std::vector<glm::vec2>
{
    std::vector<glm::vec2> projected;
    for (const erhe::math::Aabb& b : boxes) {
        for (std::size_t i = 0; i < 8; ++i) {
            const glm::vec3 p{
                ((i & 4u) != 0u) ? b.max.x : b.min.x,
                ((i & 2u) != 0u) ? b.max.y : b.min.y,
                ((i & 1u) != 0u) ? b.max.z : b.min.z
            };
            projected.push_back(glm::vec2{light_from_world * p});
        }
    }
    for (const glm::vec3& p : points) {
        projected.push_back(glm::vec2{light_from_world * p});
    }
    std::vector<glm::vec2> hull;
    erhe::math::calculate_bounding_convex_hull(std::span<const glm::vec2>{projected}, hull);
    return hull;
}

[[nodiscard]] auto same_hull(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b) -> bool
{
    return (a.size() == b.size()) && std::equal(a.begin(), a.end(), b.begin(), [](const glm::vec2& l, const glm::vec2& r) { return (l.x == r.x) && (l.y == r.y); });
}

class Box_set
{
public:
    std::vector<erhe::math::Aabb> boxes;
    std::vector<glm::vec3>        points;
};

[[nodiscard]] auto make_set(const std::size_t count, const float world_offset, const float flatness, const std::size_t point_count, const uint32_t seed) -> Box_set
{
    std::mt19937 rng{seed};
    std::uniform_real_distribution<float> u01{0.0f, 1.0f};
    Box_set set;
    for (std::size_t i = 0; i < count; ++i) {
        const float a = 6.2831853f * u01(rng);
        const float r = 80.0f * std::sqrt(u01(rng));
        const glm::vec3 center{world_offset + (r * std::cos(a)), world_offset + (10.0f * u01(rng)), world_offset + (r * std::sin(a))};
        const glm::vec3 extent{0.1f + u01(rng), (0.1f + u01(rng)) * flatness, 0.1f + u01(rng)};
        set.boxes.push_back(erhe::math::Aabb{center - extent, center + extent});
    }
    for (std::size_t i = 0; i < point_count; ++i) {
        set.points.push_back(glm::vec3{world_offset + (100.0f * (u01(rng) - 0.5f)), world_offset + (20.0f * u01(rng)), world_offset + (100.0f * (u01(rng) - 0.5f))});
    }
    return set;
}

} // anonymous namespace

TEST(Box_hull, matches_plain_hull_exactly)
{
    const glm::vec3 directions[] = {
        {0.3f, 0.8f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.2f, -0.4f}, {-0.6f, 0.1f, 0.9f}, {0.01f, 0.02f, 1.0f}
    };
    std::size_t case_count = 0;
    erhe::math::Box_hull_scratch scratch;
    for (const std::size_t count : {0u, 1u, 3u, 63u, 64u, 65u, 200u, 1500u, 6000u}) {
        for (const float offset : {0.0f, 500.0f, -3000.0f}) {
            for (const float flatness : {1.0f, 0.0f}) { // 0: zero thickness boxes (flat)
                for (const std::size_t point_count : {0u, 5u, 400u}) {
                    for (const glm::vec3& direction : directions) {
                        const uint32_t seed = static_cast<uint32_t>(count * 31u + point_count * 7u + case_count);
                        const Box_set set = make_set(count, offset, flatness, point_count, seed);
                        const glm::mat3 light_from_world = make_light_from_world(direction);
                        std::vector<glm::vec2> hull;
                        erhe::math::calculate_bounding_convex_hull_of_boxes(set.boxes, set.points, light_from_world, scratch, hull);
                        const std::vector<glm::vec2> expected = reference_hull(set.boxes, set.points, light_from_world);
                        EXPECT_TRUE(same_hull(hull, expected))
                            << "count " << count << " offset " << offset << " flatness " << flatness << " points " << point_count
                            << " direction " << direction.x << " " << direction.y << " " << direction.z;
                        ++case_count;
                    }
                }
            }
        }
    }
    EXPECT_GT(case_count, 500u);
}

TEST(Box_hull, identical_and_nested_boxes)
{
    erhe::math::Box_hull_scratch scratch;
    const glm::mat3 light_from_world = make_light_from_world(glm::vec3{0.3f, 0.8f, 0.5f});
    Box_set same;
    for (int i = 0; i < 300; ++i) {
        same.boxes.push_back(erhe::math::Aabb{glm::vec3{-1.0f}, glm::vec3{1.0f}});
    }
    Box_set nested;
    for (int i = 1; i <= 300; ++i) {
        const float h = static_cast<float>(i) * 0.1f;
        nested.boxes.push_back(erhe::math::Aabb{glm::vec3{-h}, glm::vec3{h}});
    }
    for (const Box_set* set : {&same, &nested}) {
        std::vector<glm::vec2> hull;
        erhe::math::calculate_bounding_convex_hull_of_boxes(set->boxes, set->points, light_from_world, scratch, hull);
        EXPECT_TRUE(same_hull(hull, reference_hull(set->boxes, set->points, light_from_world)));
    }
}
