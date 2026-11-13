#include "erhe_physics/icollision_shape.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

using erhe::physics::Collision_shape_type;
using erhe::physics::ICollision_shape;

[[nodiscard]] auto make_box_points() -> std::vector<float>
{
    std::vector<float> points;
    points.reserve(8u * 3u);
    for (int corner = 0; corner < 8; ++corner) {
        points.push_back(((corner & 1) != 0) ? 1.0f : -1.0f);
        points.push_back(((corner & 2) != 0) ? 1.0f : -1.0f);
        points.push_back(((corner & 4) != 0) ? 1.0f : -1.0f);
    }
    return points;
}

} // anonymous namespace

TEST(convex_hull_jolt, box_makes_a_hull)
{
    const std::vector<float> points = make_box_points();
    const std::shared_ptr<ICollision_shape> hull =
        ICollision_shape::create_convex_hull_shape_shared(points.data(), 8, 3 * static_cast<int>(sizeof(float)));
    ASSERT_TRUE(hull);
    EXPECT_EQ(hull->get_shape_type(), Collision_shape_type::e_convex_hull);
}

// A tetrahedron (it has volume) whose initial triangle Jolt finds too small: refused, not fatal.
TEST(convex_hull_jolt, needle_tetrahedron_is_refused)
{
    const float points[4 * 3] = {
        0.0f,  0.0f,    0.0f,
        1.0f,  1.0e-7f, 0.0f,
        2.0f,  0.0f,    1.0e-7f,
        3.0f,  1.0e-7f, 1.0e-7f
    };
    const std::shared_ptr<ICollision_shape> hull =
        ICollision_shape::create_convex_hull_shape_shared(points, 4, 3 * static_cast<int>(sizeof(float)));
    EXPECT_FALSE(hull);
    const std::unique_ptr<ICollision_shape> raw_hull{ICollision_shape::create_convex_hull_shape(points, 4, 3 * static_cast<int>(sizeof(float)))};
    EXPECT_FALSE(raw_hull);
}
