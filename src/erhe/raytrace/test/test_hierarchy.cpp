#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

// Two-level hierarchy:
//   root_scene contains outer_instance (transform A)
//     outer_instance references middle_scene
//       middle_scene contains inner_instance (transform B)
//         inner_instance references leaf_scene
//           leaf_scene contains geometry
//
// Geometry should appear at world position = A * B * local_position

TEST(Hierarchy, NestedTranslation)
{
    // Geometry: unit triangle at origin
    // Inner instance: translate by (3, 0, 0)
    // Outer instance: translate by (0, 5, 0)
    // Expected world position: triangle at (3, 5, 0)

    Test_geometry tg = make_unit_triangle();

    auto leaf_scene = IScene::create_unique("leaf");
    leaf_scene->attach(tg.geometry.get());
    leaf_scene->commit();

    glm::mat4 inner_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f});
    auto inner_instance = IInstance::create_unique("inner");
    inner_instance->set_scene(leaf_scene.get());
    inner_instance->set_transform(inner_transform);
    inner_instance->set_mask(0xffffffffu);
    inner_instance->commit();

    auto middle_scene = IScene::create_unique("middle");
    middle_scene->attach(inner_instance.get());
    middle_scene->commit();

    glm::mat4 outer_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 5.0f, 0.0f});
    auto outer_instance = IInstance::create_unique("outer");
    outer_instance->set_scene(middle_scene.get());
    outer_instance->set_transform(outer_transform);
    outer_instance->set_mask(0xffffffffu);
    outer_instance->commit();

    auto root_scene = IScene::create_unique("root");
    root_scene->attach(outer_instance.get());
    root_scene->commit();

    // Ray aimed at combined position (3.25, 5.25, 0) from z=10
    {
        Ray ray = make_ray({3.25f, 5.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_TRUE(is_hit);
        EXPECT_NEAR(ray.t_far, 10.0f, 0.01f);
    }

    // Ray at origin should miss (geometry has been translated away)
    {
        Ray ray = make_ray({0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_FALSE(is_hit);
    }

    // Ray at only inner offset (3, 0, 0) should miss (outer adds y=5)
    {
        Ray ray = make_ray({3.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_FALSE(is_hit);
    }
}

TEST(Hierarchy, RotationThenTranslation)
{
    // Geometry: unit triangle at origin in XY plane
    // Inner instance: translate by (2, 0, 0) — moves triangle to x=2
    // Outer instance: rotate 90 degrees around Z — x=2 becomes y=2
    // Expected: triangle centered around (0, 2, 0)

    Test_geometry tg = make_unit_triangle();

    auto leaf_scene = IScene::create_unique("leaf");
    leaf_scene->attach(tg.geometry.get());
    leaf_scene->commit();

    glm::mat4 inner_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    auto inner_instance = IInstance::create_unique("inner");
    inner_instance->set_scene(leaf_scene.get());
    inner_instance->set_transform(inner_transform);
    inner_instance->set_mask(0xffffffffu);
    inner_instance->commit();

    auto middle_scene = IScene::create_unique("middle");
    middle_scene->attach(inner_instance.get());
    middle_scene->commit();

    // Rotate 90 degrees around Z axis: (x,y) -> (-y, x)
    glm::mat4 outer_transform = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{0.0f, 0.0f, 1.0f});
    auto outer_instance = IInstance::create_unique("outer");
    outer_instance->set_scene(middle_scene.get());
    outer_instance->set_transform(outer_transform);
    outer_instance->set_mask(0xffffffffu);
    outer_instance->commit();

    auto root_scene = IScene::create_unique("root");
    root_scene->attach(outer_instance.get());
    root_scene->commit();

    // After rotate 90 CCW around Z: local (2.25, 0.25, 0) -> world (-0.25, 2.25, 0)
    {
        Ray ray = make_ray({-0.25f, 2.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_TRUE(is_hit);
        EXPECT_NEAR(ray.t_far, 10.0f, 0.01f);
    }

    // Original un-rotated position should miss
    {
        Ray ray = make_ray({2.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_FALSE(is_hit);
    }
}

TEST(Hierarchy, ScalePropagation)
{
    // Geometry: unit triangle at origin
    // Inner instance: translate by (1, 0, 0)
    // Outer instance: uniform scale 3x
    // Expected: triangle at world position (3, 0, 0) with 3x size

    Test_geometry tg = make_unit_triangle();

    auto leaf_scene = IScene::create_unique("leaf");
    leaf_scene->attach(tg.geometry.get());
    leaf_scene->commit();

    glm::mat4 inner_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
    auto inner_instance = IInstance::create_unique("inner");
    inner_instance->set_scene(leaf_scene.get());
    inner_instance->set_transform(inner_transform);
    inner_instance->set_mask(0xffffffffu);
    inner_instance->commit();

    auto middle_scene = IScene::create_unique("middle");
    middle_scene->attach(inner_instance.get());
    middle_scene->commit();

    glm::mat4 outer_transform = glm::scale(glm::mat4{1.0f}, glm::vec3{3.0f, 3.0f, 3.0f});
    auto outer_instance = IInstance::create_unique("outer");
    outer_instance->set_scene(middle_scene.get());
    outer_instance->set_transform(outer_transform);
    outer_instance->set_mask(0xffffffffu);
    outer_instance->commit();

    auto root_scene = IScene::create_unique("root");
    root_scene->attach(outer_instance.get());
    root_scene->commit();

    // Local (1.5, 0.1, 0) scaled 3x -> world (4.5, 0.3, 0)
    // This is inside the scaled triangle (local vertex at (1,0,0)+(1,0,0) = (2,0,0) -> world (6,0,0))
    {
        Ray ray = make_ray({4.5f, 0.3f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_TRUE(is_hit);
        EXPECT_NEAR(ray.t_far, 10.0f, 0.01f);
    }

    // Point outside scaled bounds should miss
    // Local (1, 0, 0) + triangle extends to (2, 1, 0), scaled 3x -> (6, 3, 0)
    // (7, 0, 0) is beyond the rightmost extent
    {
        Ray ray = make_ray({7.0f, 0.1f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_FALSE(is_hit);
    }
}

TEST(Hierarchy, ThreeLevelNesting)
{
    // Three levels of translation: +X, +Y, +Z at each level
    // Geometry should end up at (2, 3, 4)

    Test_geometry tg = make_unit_triangle();

    // Level 1 (innermost): translate Z+4
    auto scene_1 = IScene::create_unique("scene_1");
    scene_1->attach(tg.geometry.get());
    scene_1->commit();

    glm::mat4 t1 = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, 4.0f});
    auto inst_1 = IInstance::create_unique("inst_1");
    inst_1->set_scene(scene_1.get());
    inst_1->set_transform(t1);
    inst_1->set_mask(0xffffffffu);
    inst_1->commit();

    // Level 2: translate Y+3
    auto scene_2 = IScene::create_unique("scene_2");
    scene_2->attach(inst_1.get());
    scene_2->commit();

    glm::mat4 t2 = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f});
    auto inst_2 = IInstance::create_unique("inst_2");
    inst_2->set_scene(scene_2.get());
    inst_2->set_transform(t2);
    inst_2->set_mask(0xffffffffu);
    inst_2->commit();

    // Level 3 (outermost): translate X+2
    auto scene_3 = IScene::create_unique("scene_3");
    scene_3->attach(inst_2.get());
    scene_3->commit();

    glm::mat4 t3 = glm::translate(glm::mat4{1.0f}, glm::vec3{2.0f, 0.0f, 0.0f});
    auto inst_3 = IInstance::create_unique("inst_3");
    inst_3->set_scene(scene_3.get());
    inst_3->set_transform(t3);
    inst_3->set_mask(0xffffffffu);
    inst_3->commit();

    auto root_scene = IScene::create_unique("root");
    root_scene->attach(inst_3.get());
    root_scene->commit();

    // Triangle is in XY plane, translated to (2, 3, 4)
    // Ray from (2.25, 3.25, 10) pointing -Z should hit at z=4, distance=6
    {
        Ray ray = make_ray({2.25f, 3.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        bool is_hit = root_scene->intersect(ray, hit);
        EXPECT_TRUE(is_hit);
        EXPECT_NEAR(ray.t_far, 6.0f, 0.01f);
    }

    // Ray at origin should miss
    {
        Ray ray = make_ray({0.25f, 0.25f, 10.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_FALSE(root_scene->intersect(ray, hit));
    }
}

} // anonymous namespace
