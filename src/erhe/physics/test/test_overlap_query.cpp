#include "erhe_physics/collision_filter.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/transform.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>

// Trial-placement overlap queries. Box3D's own overlap entry points are
// boolean, so these run the pairwise manifold functions and compare
// b3ManifoldPoint::separation against the tolerance -- the tolerance behavior
// is therefore the part most worth pinning down.

namespace {

using erhe::physics::ICollision_shape;
using erhe::physics::IRigid_body;
using erhe::physics::IRigid_body_create_info;
using erhe::physics::IWorld;
using erhe::physics::Motion_mode;
using erhe::physics::Transform;

constexpr float tolerance = 0.001f;

[[nodiscard]] auto at(const glm::vec3 position) -> Transform
{
    return Transform{glm::mat3{1.0f}, position};
}

class Overlap_query_fixture : public ::testing::Test
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
        const glm::vec3                          position    = glm::vec3{0.0f},
        const Motion_mode                        motion_mode = Motion_mode::e_static
    ) -> IRigid_body*
    {
        IRigid_body_create_info create_info;
        create_info.collision_shape = shape;
        create_info.motion_mode     = motion_mode;
        create_info.position        = position;
        create_info.debug_label     = "test body";
        const std::shared_ptr<IRigid_body> body = m_world->create_rigid_body_shared(create_info);
        m_world->add_rigid_body(body.get());
        m_bodies.push_back(body);
        return body.get();
    }

    std::unique_ptr<IWorld>                   m_world;
    std::vector<std::shared_ptr<IRigid_body>> m_bodies;
};

} // anonymous namespace

TEST_F(Overlap_query_fixture, boxes_intersect_only_when_they_actually_overlap)
{
    // Unit cubes: they touch face to face at a separation of exactly 2.
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    IRigid_body* body_a = add_body(box);
    IRigid_body* body_b = add_body(box);

    EXPECT_TRUE (m_world->would_bodies_intersect(*body_a, at(glm::vec3{0.0f}), *body_b, at(glm::vec3{1.0f, 0.0f, 0.0f}), tolerance));
    EXPECT_FALSE(m_world->would_bodies_intersect(*body_a, at(glm::vec3{0.0f}), *body_b, at(glm::vec3{3.0f, 0.0f, 0.0f}), tolerance));
}

TEST_F(Overlap_query_fixture, feature_aligned_touching_is_not_an_intersection)
{
    // This is the case the tolerance exists for: two boxes placed exactly face
    // to face must read as free space, or snapping a box against another would
    // always report a collision.
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    IRigid_body* body_a = add_body(box);
    IRigid_body* body_b = add_body(box);

    EXPECT_FALSE(m_world->would_bodies_intersect(*body_a, at(glm::vec3{0.0f}), *body_b, at(glm::vec3{2.0f, 0.0f, 0.0f}), tolerance));

    // Penetrating by less than the tolerance still counts as touching...
    EXPECT_FALSE(m_world->would_bodies_intersect(*body_a, at(glm::vec3{0.0f}), *body_b, at(glm::vec3{2.0f - (0.5f * tolerance), 0.0f, 0.0f}), tolerance));

    // ...but by more than it does not.
    EXPECT_TRUE (m_world->would_bodies_intersect(*body_a, at(glm::vec3{0.0f}), *body_b, at(glm::vec3{2.0f - (10.0f * tolerance), 0.0f, 0.0f}), tolerance));
}

TEST_F(Overlap_query_fixture, mixed_shape_types_dispatch_in_either_argument_order)
{
    // Box3D fixes the argument order of its manifold functions by shape type
    // (the sphere is always B, the hull always A), so the dispatch swaps the
    // pair and inverts the transform. Both orders must agree.
    const std::shared_ptr<ICollision_shape> box     = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    const std::shared_ptr<ICollision_shape> sphere  = ICollision_shape::create_sphere_shape_shared(1.0f);
    const std::shared_ptr<ICollision_shape> capsule = ICollision_shape::create_capsule_shape_shared(erhe::physics::Axis::Y, 0.5f, 1.0f);

    IRigid_body* box_body     = add_body(box);
    IRigid_body* sphere_body  = add_body(sphere);
    IRigid_body* capsule_body = add_body(capsule);

    const glm::vec3 overlapping{0.5f, 0.0f, 0.0f};
    const glm::vec3 far_away   {8.0f, 0.0f, 0.0f};

    for (IRigid_body* other : {sphere_body, capsule_body}) {
        EXPECT_TRUE (m_world->would_bodies_intersect(*box_body, at(glm::vec3{0.0f}), *other, at(overlapping), tolerance));
        EXPECT_TRUE (m_world->would_bodies_intersect(*other, at(overlapping), *box_body, at(glm::vec3{0.0f}), tolerance));
        EXPECT_FALSE(m_world->would_bodies_intersect(*box_body, at(glm::vec3{0.0f}), *other, at(far_away), tolerance));
        EXPECT_FALSE(m_world->would_bodies_intersect(*other, at(far_away), *box_body, at(glm::vec3{0.0f}), tolerance));
    }

    EXPECT_TRUE (m_world->would_bodies_intersect(*sphere_body, at(glm::vec3{0.0f}), *capsule_body, at(overlapping), tolerance));
    EXPECT_FALSE(m_world->would_bodies_intersect(*sphere_body, at(glm::vec3{0.0f}), *capsule_body, at(far_away), tolerance));
}

TEST_F(Overlap_query_fixture, rotation_of_the_trial_transform_is_respected)
{
    // A long thin box that only reaches the target when turned: at identity the
    // two are clear of each other, rotated 90 degrees about Y it spans the gap.
    const std::shared_ptr<ICollision_shape> bar   = ICollision_shape::create_box_shape_shared(glm::vec3{0.25f, 0.25f, 3.0f});
    const std::shared_ptr<ICollision_shape> small = ICollision_shape::create_box_shape_shared(glm::vec3{0.25f, 0.25f, 0.25f});

    IRigid_body* bar_body   = add_body(bar);
    IRigid_body* small_body = add_body(small, glm::vec3{2.0f, 0.0f, 0.0f});

    const Transform unrotated{glm::mat3{1.0f}, glm::vec3{0.0f}};
    const Transform rotated{
        glm::mat3{glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f})},
        glm::vec3{0.0f}
    };

    EXPECT_FALSE(m_world->would_bodies_intersect(*bar_body, unrotated, *small_body, at(glm::vec3{2.0f, 0.0f, 0.0f}), tolerance));
    EXPECT_TRUE (m_world->would_bodies_intersect(*bar_body, rotated,   *small_body, at(glm::vec3{2.0f, 0.0f, 0.0f}), tolerance));
}

TEST_F(Overlap_query_fixture, compound_children_are_all_tested)
{
    // Two cubes at x = +/-3 with a gap between them. A probe in the gap must
    // miss, and one placed on a child must hit -- so every child shape of the
    // body is reached, not just the first.
    erhe::physics::Compound_shape_create_info create_info;
    for (int i = 0; i < 2; ++i) {
        erhe::physics::Compound_child child;
        child.shape     = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
        child.transform = Transform{glm::mat3{1.0f}, glm::vec3{(i == 0) ? -3.0f : 3.0f, 0.0f, 0.0f}};
        create_info.children.push_back(child);
    }
    IRigid_body* compound_body = add_body(ICollision_shape::create_compound_shape_shared(create_info));
    IRigid_body* probe_body    = add_body(ICollision_shape::create_sphere_shape_shared(0.5f));

    EXPECT_FALSE(m_world->would_bodies_intersect(*compound_body, at(glm::vec3{0.0f}), *probe_body, at(glm::vec3{0.0f}), tolerance));
    EXPECT_TRUE (m_world->would_bodies_intersect(*compound_body, at(glm::vec3{0.0f}), *probe_body, at(glm::vec3{ 3.0f, 0.0f, 0.0f}), tolerance));
    EXPECT_TRUE (m_world->would_bodies_intersect(*compound_body, at(glm::vec3{0.0f}), *probe_body, at(glm::vec3{-3.0f, 0.0f, 0.0f}), tolerance));
}

TEST_F(Overlap_query_fixture, world_query_finds_other_bodies_and_ignores_the_tested_one)
{
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});
    IRigid_body* occupant = add_body(box, glm::vec3{5.0f, 0.0f, 0.0f});
    IRigid_body* mover    = add_body(box, glm::vec3{0.0f, 0.0f, 0.0f});
    static_cast<void>(occupant);

    // Free space, and the mover's own current position must not count as a hit
    // against itself.
    EXPECT_FALSE(m_world->would_body_intersect_world(*mover, at(glm::vec3{0.0f, 0.0f, 0.0f}), tolerance));
    EXPECT_FALSE(m_world->would_body_intersect_world(*mover, at(glm::vec3{-20.0f, 0.0f, 0.0f}), tolerance));

    // On top of the occupant.
    EXPECT_TRUE (m_world->would_body_intersect_world(*mover, at(glm::vec3{5.0f, 0.0f, 0.0f}), tolerance));

    // Exactly face to face with it: touching, not intersecting.
    EXPECT_FALSE(m_world->would_body_intersect_world(*mover, at(glm::vec3{3.0f, 0.0f, 0.0f}), tolerance));
}

TEST_F(Overlap_query_fixture, world_query_ignores_collision_filters)
{
    // The trial-placement queries are documented as pure geometric tests, so a
    // body that would never collide in simulation still occupies space.
    const std::shared_ptr<ICollision_shape> box = ICollision_shape::create_box_shape_shared(glm::vec3{1.0f, 1.0f, 1.0f});

    const std::shared_ptr<erhe::physics::Collision_filter> filter_a = std::make_shared<erhe::physics::Collision_filter>();
    filter_a->collision_systems        = {"a"};
    filter_a->not_collide_with_systems = {"b"};

    const std::shared_ptr<erhe::physics::Collision_filter> filter_b = std::make_shared<erhe::physics::Collision_filter>();
    filter_b->collision_systems        = {"b"};
    filter_b->not_collide_with_systems = {"a"};

    IRigid_body_create_info occupant_create_info;
    occupant_create_info.collision_shape  = box;
    occupant_create_info.motion_mode      = Motion_mode::e_static;
    occupant_create_info.collision_filter = filter_a;
    occupant_create_info.debug_label      = "occupant";
    const std::shared_ptr<IRigid_body> occupant = m_world->create_rigid_body_shared(occupant_create_info);
    m_world->add_rigid_body(occupant.get());
    m_bodies.push_back(occupant);

    IRigid_body_create_info mover_create_info;
    mover_create_info.collision_shape  = box;
    mover_create_info.motion_mode      = Motion_mode::e_static;
    mover_create_info.position         = glm::vec3{20.0f, 0.0f, 0.0f};
    mover_create_info.collision_filter = filter_b;
    mover_create_info.debug_label      = "mover";
    const std::shared_ptr<IRigid_body> mover = m_world->create_rigid_body_shared(mover_create_info);
    m_world->add_rigid_body(mover.get());
    m_bodies.push_back(mover);

    EXPECT_TRUE(m_world->would_bodies_intersect(*mover, at(glm::vec3{0.0f}), *occupant, at(glm::vec3{0.0f}), tolerance));
    EXPECT_TRUE(m_world->would_body_intersect_world(*mover, at(glm::vec3{0.0f}), tolerance));
}
