#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/transform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

// IRigid_body::set_collision_shape(): a body's shape replaced in place, as the
// editor does when a node's world scale changes (doc/erhe/physics.md).

namespace {

using erhe::physics::ICollision_shape;
using erhe::physics::IRigid_body;
using erhe::physics::IRigid_body_create_info;
using erhe::physics::IWorld;
using erhe::physics::Motion_mode;
using erhe::physics::Transform;

constexpr float tolerance = 0.001f;

class Shape_swap_fixture : public ::testing::Test
{
public:
    void SetUp() override
    {
        m_world = IWorld::create_unique();
    }

    void TearDown() override
    {
        for (const std::shared_ptr<IRigid_body>& body : m_bodies) {
            m_world->remove_rigid_body(body.get());
        }
        m_bodies.clear();
        m_world.reset();
    }

    [[nodiscard]] auto add_body(
        const std::shared_ptr<ICollision_shape>& shape,
        const Motion_mode                        motion_mode,
        const std::optional<float>               mass
    ) -> IRigid_body*
    {
        IRigid_body_create_info create_info;
        create_info.collision_shape = shape;
        create_info.motion_mode     = motion_mode;
        create_info.mass            = mass;
        create_info.debug_label     = "test body";
        const std::shared_ptr<IRigid_body> body = m_world->create_rigid_body_shared(create_info);
        m_world->add_rigid_body(body.get());
        m_bodies.push_back(body);
        return body.get();
    }

    std::unique_ptr<IWorld>                   m_world;
    std::vector<std::shared_ptr<IRigid_body>> m_bodies;
};

[[nodiscard]] auto at(const glm::vec3 position) -> Transform
{
    return Transform{glm::mat3{1.0f}, position};
}

} // anonymous namespace

TEST_F(Shape_swap_fixture, a_static_body_collides_with_its_new_shape)
{
    const std::shared_ptr<ICollision_shape> unit_box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    IRigid_body* wall  = add_body(unit_box, Motion_mode::e_static, std::nullopt);
    IRigid_body* probe = add_body(ICollision_shape::create_box_shape_shared(glm::vec3{0.1f}), Motion_mode::e_kinematic_non_physical, std::nullopt);

    // The probe sits beyond the unit box and inside the box scaled by 3 along X.
    const Transform far_probe = at(glm::vec3{1.2f, 0.0f, 0.0f});
    EXPECT_FALSE(m_world->would_bodies_intersect(*probe, far_probe, *wall, at(glm::vec3{0.0f}), tolerance));

    wall->set_collision_shape(ICollision_shape::create_scaled_shape_shared(unit_box, glm::vec3{3.0f, 1.0f, 1.0f}));
    EXPECT_TRUE(m_world->would_bodies_intersect(*probe, far_probe, *wall, at(glm::vec3{0.0f}), tolerance));
    EXPECT_TRUE(m_world->would_body_intersect_world(*probe, far_probe, tolerance));

    wall->set_collision_shape(unit_box);
    EXPECT_FALSE(m_world->would_bodies_intersect(*probe, far_probe, *wall, at(glm::vec3{0.0f}), tolerance));
    EXPECT_FALSE(m_world->would_body_intersect_world(*probe, far_probe, tolerance));
}

TEST_F(Shape_swap_fixture, an_implicit_mass_is_the_volume_of_the_new_shape)
{
    // Without an explicit mass the mass is the shape volume times the default material density (1).
    const std::shared_ptr<ICollision_shape> unit_box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    IRigid_body* body = add_body(unit_box, Motion_mode::e_dynamic, std::nullopt);
    EXPECT_NEAR(body->get_mass(), 1.0f, 1.0e-4f);
    const float inertia_before = body->get_local_inertia()[0][0];

    body->set_collision_shape(ICollision_shape::create_uniform_scaling_shape_shared(unit_box, 2.0f));
    EXPECT_NEAR(body->get_mass(), 8.0f, 1.0e-3f);
    // Inertia of a box goes with mass times size squared: 8 * 4.
    EXPECT_NEAR(body->get_local_inertia()[0][0], 32.0f * inertia_before, 1.0e-3f);

    body->set_collision_shape(ICollision_shape::create_scaled_shape_shared(unit_box, glm::vec3{3.0f, 1.0f, 1.0f}));
    EXPECT_NEAR(body->get_mass(), 3.0f, 1.0e-3f);
}

TEST_F(Shape_swap_fixture, an_explicit_mass_stays_and_the_inertia_follows_the_shape)
{
    const std::shared_ptr<ICollision_shape> unit_box = ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    IRigid_body* body = add_body(unit_box, Motion_mode::e_dynamic, 5.0f);
    EXPECT_FLOAT_EQ(body->get_mass(), 5.0f);
    const float inertia_before = body->get_local_inertia()[0][0];

    body->set_collision_shape(ICollision_shape::create_uniform_scaling_shape_shared(unit_box, 2.0f));
    EXPECT_FLOAT_EQ(body->get_mass(), 5.0f);
    EXPECT_NEAR(body->get_local_inertia()[0][0], 4.0f * inertia_before, 1.0e-3f);
}
