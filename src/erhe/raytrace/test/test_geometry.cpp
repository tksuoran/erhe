#include "test_helpers.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

TEST(Geometry, CommitWithoutBuffers)
{
    auto geometry = IGeometry::create_unique("empty", Geometry_type::GEOMETRY_TYPE_TRIANGLE);
    geometry->commit(); // Should not crash
}

TEST(Geometry, SingleTriangleHit)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    // Ray from z=1 pointing down -Z, through center of triangle
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_EQ(hit.triangle_id, 0u);
    EXPECT_NE(hit.geometry, nullptr);
    EXPECT_FLOAT_EQ(ray.t_far, 1.0f);
}

TEST(Geometry, SingleTriangleMissParallel)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    // Ray parallel to triangle (along X axis)
    Ray ray = make_ray({-1.0f, 0.25f, 0.0f}, {1.0f, 0.0f, 0.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_FALSE(is_hit);
}

TEST(Geometry, SingleTriangleMissAway)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    // Ray pointing away from triangle
    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, 1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_FALSE(is_hit);
}

TEST(Geometry, HitUpdatesRayTFar)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 3.0f}, {0.0f, 0.0f, -1.0f}, 1000.0f);
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 3.0f, 0.001f);
}

TEST(Geometry, HitNormalPointsAlongZ)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    // Normal should be along Z axis (triangle in XY plane)
    // Direction may be +Z or -Z depending on winding
    EXPECT_NEAR(std::abs(hit.normal.z), glm::length(hit.normal), 0.01f);
}

TEST(Geometry, HitUVInRange)
{
    Test_geometry tg = make_unit_triangle();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.25f, 0.25f, 1.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_GE(hit.uv.x, 0.0f);
    EXPECT_LE(hit.uv.x, 1.0f);
    EXPECT_GE(hit.uv.y, 0.0f);
    EXPECT_LE(hit.uv.y, 1.0f);
}

TEST(Geometry, CubeHitNearestFace)
{
    Test_geometry tg = make_cube();

    auto scene = IScene::create_unique("test_scene");
    scene->attach(tg.geometry.get());
    scene->commit();

    // Ray from z=2 aiming at cube center - should hit front face at z=0.5
    Ray ray = make_ray({0.0f, 0.0f, 2.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 1.5f, 0.01f); // distance from z=2 to z=0.5
}

TEST(Geometry, ClosestHitWins)
{
    // Two triangles at z=0 and z=-2, ray from z=5 going -Z
    Test_geometry near_tg = make_triangle_geometry(
        "near",
        { {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f} },
        { {0, 1, 2} }
    );
    Test_geometry far_tg = make_triangle_geometry(
        "far",
        { {-1.0f, -1.0f, -2.0f}, {1.0f, -1.0f, -2.0f}, {0.0f, 1.0f, -2.0f} },
        { {0, 1, 2} }
    );

    auto scene = IScene::create_unique("test_scene");
    scene->attach(near_tg.geometry.get());
    scene->attach(far_tg.geometry.get());
    scene->commit();

    Ray ray = make_ray({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f});
    Hit hit{};

    bool is_hit = scene->intersect(ray, hit);

    EXPECT_TRUE(is_hit);
    EXPECT_NEAR(ray.t_far, 5.0f, 0.01f); // hits z=0 triangle at distance 5
    EXPECT_EQ(hit.geometry, near_tg.geometry.get());
}

} // anonymous namespace
