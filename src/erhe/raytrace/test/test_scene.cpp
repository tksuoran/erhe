#include "test_helpers.hpp"

#include <gtest/gtest.h>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

TEST(Scene, EmptySceneNoHit)
{
    auto scene = IScene::create_unique("empty");
    scene->commit();

    Ray ray = make_ray({0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_FALSE(is_hit);
}

TEST(Scene, AttachDetachGeometry)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");

    // Attach - should hit
    scene->attach(tg.geometry.get());
    scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }

    // Detach - should miss
    scene->detach(tg.geometry.get());
    scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }
}

TEST(Scene, AttachDetachInstance)
{
    Test_geometry tg = make_unit_triangle();

    auto child_scene = IScene::create_unique("child");
    child_scene->attach(tg.geometry.get());
    child_scene->commit();

    auto instance = IInstance::create_unique("inst");
    instance->set_scene(child_scene.get());
    instance->set_transform(glm::mat4{1.0f});
    instance->set_mask(0xffffffffu);
    instance->commit();

    auto root_scene = IScene::create_unique("root");

    // Attach - should hit
    root_scene->attach(instance.get());
    root_scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(root_scene->intersect(ray, hit));
    }

    // Detach - should miss
    root_scene->detach(instance.get());
    root_scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_FALSE(root_scene->intersect(ray, hit));
    }
}

TEST(Scene, MultipleGeometries)
{
    // Two triangles at different Z depths
    Test_geometry near_tg = make_triangle_geometry(
        "near",
        { {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { {0, 1, 2} }
    );
    Test_geometry far_tg = make_triangle_geometry(
        "far",
        { {-1.0f, -1.0f, -5.0f}, {1.0f, -1.0f, -5.0f}, {0.0f, 1.0f, -5.0f} },
        { {0, 1, 2} }
    );

    auto scene = IScene::create_unique("test_scene");
    scene->attach(near_tg.geometry.get());
    scene->attach(far_tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 10.0f, 0.01f); // hits z=0 at distance 10
    EXPECT_EQ(hit.geometry, near_tg.geometry.get());
}

} // anonymous namespace
