#include "test_helpers.hpp"

#include <gtest/gtest.h>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

TEST(Masking, GeometryMaskMatch)
{
    Test_geometry tg = make_unit_triangle();
    tg.geometry->set_mask(0x01u);

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    ray.mask = 0x01u; // matches geometry mask
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
}

TEST(Masking, GeometryMaskMismatch)
{
    Test_geometry tg = make_unit_triangle();
    tg.geometry->set_mask(0x01u);

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    ray.mask = 0x02u; // does NOT match geometry mask
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_FALSE(is_hit);
}

TEST(Masking, DisabledGeometry)
{
    Test_geometry tg = make_unit_triangle();
    tg.geometry->disable();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_FALSE(is_hit);
}

TEST(Masking, EnableDisableToggle)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    // First: enabled (default) - should hit
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }

    // Disable - should miss
    tg.geometry->disable();
    scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_FALSE(scene->intersect(ray, hit));
    }

    // Re-enable - should hit again
    tg.geometry->enable();
    scene->commit();
    {
        Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
        Hit hit{};
        EXPECT_TRUE(scene->intersect(ray, hit));
    }
}

} // anonymous namespace
