// Tests for the bvh backend scene level acceleration structure. The bvh backend
// is one of several raytrace backends, so the whole file is compiled out when a
// different backend is selected.
#if defined(ERHE_RAYTRACE_LIBRARY_BVH)

#include "test_helpers.hpp"

#include "erhe_raytrace/bvh/bvh_scene.hpp"

#include <gtest/gtest.h>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

[[nodiscard]] auto as_bvh_scene(IScene* scene) -> Bvh_scene*
{
    return reinterpret_cast<Bvh_scene*>(scene);
}

TEST(Bvh_scene, ChildBecomesStatic)
{
    Test_geometry tg = make_unit_triangle();

    auto  scene     = IScene::create_unique("static_classification");
    auto* bvh_scene = as_bvh_scene(scene.get());
    scene->attach(tg.geometry.get());

    // Freshly attached children are not static yet.
    EXPECT_EQ(bvh_scene->get_static_child_count(4), 0);

    for (int i = 0; i < 4; ++i) {
        scene->commit();
    }
    EXPECT_EQ(bvh_scene->get_tick(), 4);
    EXPECT_EQ(bvh_scene->get_static_child_count(4), 1);
}

TEST(Bvh_scene, InstanceTransformResetsStaticness)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());
    child_scene->commit();

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);

    instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}));
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 0);

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);
}

TEST(Bvh_scene, GeometryCommitResetsStaticnessThroughInstance)
{
    // A geometry commit changes the bounds of the scene holding it, which
    // changes the bounds of the instance in the scene above.
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    for (int i = 0; i < 4; ++i) {
        root_scene->commit();
    }
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 1);

    tg.geometry->commit();
    EXPECT_EQ(bvh_root_scene->get_static_child_count(4), 0);
}

TEST(Bvh_scene, SceneBoundsFollowChildren)
{
    Test_geometry tg = make_unit_triangle();

    auto  scene     = IScene::create_unique("bounds");
    auto* bvh_scene = as_bvh_scene(scene.get());
    EXPECT_FALSE(bvh_scene->get_bbox().is_valid());

    scene->attach(tg.geometry.get());
    const erhe::math::Aabb bbox = bvh_scene->get_bbox();
    ASSERT_TRUE(bbox.is_valid());
    EXPECT_NEAR(bbox.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(bbox.max.x, 1.0f, 0.001f);
    EXPECT_NEAR(bbox.min.y, 0.0f, 0.001f);
    EXPECT_NEAR(bbox.max.y, 1.0f, 0.001f);
}

TEST(Bvh_scene, InstanceBoundsAreTransformed)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());
    instance->set_transform(glm::translate(glm::mat4{1.0f}, glm::vec3{10.0f, 0.0f, 0.0f}));

    auto  root_scene     = IScene::create_unique("root");
    auto* bvh_root_scene = as_bvh_scene(root_scene.get());
    root_scene->attach(instance.get());

    const erhe::math::Aabb bbox = bvh_root_scene->get_bbox();
    ASSERT_TRUE(bbox.is_valid());
    EXPECT_NEAR(bbox.min.x, 10.0f, 0.001f);
    EXPECT_NEAR(bbox.max.x, 11.0f, 0.001f);
}

TEST(Bvh_scene, DestroyedChildRemovesItselfFromScene)
{
    auto scene = IScene::create_unique("outliving");
    {
        Test_geometry tg = make_unit_triangle();
        scene->attach(tg.geometry.get());
        scene->commit();

        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }

    // The geometry is gone; the scene must not reference it any more.
    scene->commit();
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};
    EXPECT_FALSE(scene->intersect(ray, hit));
}

TEST(Bvh_scene, InstanceOutlivingItsSceneDoesNotHit)
{
    Test_geometry tg = make_unit_triangle();

    auto instance   = IInstance::create_unique("inst");
    auto root_scene = IScene::create_unique("root");
    root_scene->attach(instance.get());
    {
        auto child_scene = IScene::create_unique("child");
        child_scene->attach(tg.geometry.get());
        instance->set_scene(child_scene.get());
        root_scene->commit();

        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(root_scene->intersect(ray, hit));
    }

    root_scene->commit();
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};
    EXPECT_FALSE(root_scene->intersect(ray, hit));
}

} // anonymous namespace

#endif // defined(ERHE_RAYTRACE_LIBRARY_BVH)
