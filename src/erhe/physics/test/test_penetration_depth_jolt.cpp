#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/transform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <memory>
#include <optional>

namespace {

using erhe::physics::ICollision_shape;
using erhe::physics::Transform;

[[nodiscard]] auto at(const glm::vec3 position) -> Transform
{
    return Transform{glm::mat3{1.0f}, position};
}

// A unit cube (half extents 0.5) as a triangle mesh with its corner at the origin.
[[nodiscard]] auto make_cube_mesh() -> std::shared_ptr<ICollision_shape>
{
    const std::array<float, 24> points{
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 1.0f,   1.0f, 1.0f, 1.0f,   0.0f, 1.0f, 1.0f
    };
    const std::array<uint32_t, 36> indices{
        0, 2, 1,  0, 3, 2,   4, 5, 6,  4, 6, 7,   0, 1, 5,  0, 5, 4,
        3, 6, 2,  3, 7, 6,   0, 4, 7,  0, 7, 3,   1, 2, 6,  1, 6, 5
    };
    return ICollision_shape::create_mesh_shape_shared(points.data(), 8, 3 * sizeof(float), indices.data(), 12);
}

} // anonymous namespace

TEST(Penetration_depth, boxes_report_their_overlap_depth)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    const std::optional<float> deep  = ICollision_shape::get_penetration_depth(*box, at(glm::vec3{0.0f}), *box, at(glm::vec3{0.75f, 0.0f, 0.0f}));
    const std::optional<float> touch = ICollision_shape::get_penetration_depth(*box, at(glm::vec3{0.0f}), *box, at(glm::vec3{1.0f, 0.0f, 0.0f}));
    const std::optional<float> apart = ICollision_shape::get_penetration_depth(*box, at(glm::vec3{0.0f}), *box, at(glm::vec3{3.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(deep.has_value());
    ASSERT_TRUE(touch.has_value());
    ASSERT_TRUE(apart.has_value());
    EXPECT_NEAR(deep.value(), 0.25f, 1.0e-3f);
    EXPECT_NEAR(touch.value(), 0.0f, 1.0e-3f);
    EXPECT_EQ(apart.value(), 0.0f);
}

TEST(Penetration_depth, rotation_and_center_of_mass_follow_the_transform)
{
    // The hull's center of mass is at (0.5, 0.5, 0.5): the node-space transform places its corner.
    const std::array<float, 24> corners{
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f,   1.0f, 0.0f, 1.0f,   1.0f, 1.0f, 1.0f,   0.0f, 1.0f, 1.0f
    };
    const std::shared_ptr<ICollision_shape> hull = ICollision_shape::create_convex_hull_shape_shared(corners.data(), 8, 3 * sizeof(float));
    const std::shared_ptr<ICollision_shape> box  = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    // The hull spans x 0..1; the box at x 1.4 spans 0.9..1.9.
    const std::optional<float> side = ICollision_shape::get_penetration_depth(*hull, at(glm::vec3{0.0f}), *box, at(glm::vec3{1.4f, 0.5f, 0.5f}));
    ASSERT_TRUE(side.has_value());
    EXPECT_NEAR(side.value(), 0.1f, 2.0e-3f);
    // Turned half a turn about y around its corner, the hull spans x -1..0 and misses the box.
    const glm::mat3 half_turn = glm::mat3_cast(glm::angleAxis(glm::pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f}));
    const std::optional<float> turned = ICollision_shape::get_penetration_depth(*hull, Transform{half_turn, glm::vec3{0.0f}}, *box, at(glm::vec3{1.4f, 0.5f, 0.5f}));
    ASSERT_TRUE(turned.has_value());
    EXPECT_EQ(turned.value(), 0.0f);
}

TEST(Penetration_depth, convex_against_mesh_is_measured_mesh_against_mesh_is_not)
{
    const std::shared_ptr<ICollision_shape> mesh = make_cube_mesh();
    const std::shared_ptr<ICollision_shape> box  = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    // The box spans y 0.8..1.8 and sinks 0.2 into the mesh cube's top face.
    const std::optional<float> sunk = ICollision_shape::get_penetration_depth(*box, at(glm::vec3{0.5f, 1.3f, 0.5f}), *mesh, at(glm::vec3{0.0f}));
    ASSERT_TRUE(sunk.has_value());
    EXPECT_NEAR(sunk.value(), 0.2f, 2.0e-3f);
    EXPECT_FALSE(ICollision_shape::get_penetration_depth(*mesh, at(glm::vec3{0.0f}), *mesh, at(glm::vec3{0.5f, 0.0f, 0.0f})).has_value());
}
