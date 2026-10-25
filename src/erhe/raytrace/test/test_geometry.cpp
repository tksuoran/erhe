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

namespace {

// IGeometry::release_buffers(): a backend that traverses and reports hits
// from its own copies lets the caller free the vertex / index buffers after
// commit(), and every hit stays identical (erhe::primitive::Primitive_raytrace
// frees its copies this way).
TEST(Geometry, ReleaseBuffersKeepsHitsIdentical)
{
    using namespace erhe::raytrace;
    using namespace erhe::raytrace::test;

    Test_geometry tg = make_cube();
    std::unique_ptr<IScene> scene = IScene::create_unique("release_buffers");
    scene->attach(tg.geometry.get());
    scene->commit();

    class Cast
    {
    public:
        bool hit_found{false};
        Hit  hit{};
        Ray  ray{};
    };
    const auto cast_all = [&scene]() -> std::vector<Cast> {
        std::vector<Cast> casts;
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                for (const glm::vec3 direction : {glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f}}) {
                    const glm::vec3 offset = 0.15f * glm::vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + y)};
                    Cast cast;
                    cast.ray = make_ray((-2.0f * direction) + (offset - (glm::dot(offset, direction) * direction)), direction);
                    cast.hit_found = scene->intersect(cast.ray, cast.hit);
                    casts.push_back(cast);
                }
            }
        }
        return casts;
    };
    const std::vector<Cast> before = cast_all();

    if (!tg.geometry->release_buffers()) {
        GTEST_SKIP() << "raytrace backend reads the buffers after commit";
    }
    tg.vertex_buffer.reset();
    tg.index_buffer.reset();

    const std::vector<Cast> after = cast_all();
    ASSERT_EQ(before.size(), after.size());
    std::size_t hit_count = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].hit_found, after[i].hit_found) << i;
        if (!before[i].hit_found) {
            continue;
        }
        ++hit_count;
        EXPECT_EQ(before[i].hit.triangle_id, after[i].hit.triangle_id) << i;
        EXPECT_EQ(before[i].hit.geometry,    after[i].hit.geometry) << i;
        EXPECT_EQ(before[i].hit.normal,      after[i].hit.normal) << i;
        EXPECT_EQ(before[i].hit.uv,          after[i].hit.uv) << i;
        EXPECT_EQ(before[i].ray.t_far,       after[i].ray.t_far) << i;
    }
    EXPECT_GT(hit_count, 0u);
}

} // anonymous namespace
