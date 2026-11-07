#include "test_helpers.hpp"

#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>

// Rays with zero direction components, from origins more than one unit off
// the world origin along those axes, through BVHs that have inner nodes.

namespace {

using namespace erhe::raytrace;
using namespace erhe::raytrace::test;

constexpr float pi = 3.14159265358979f;

auto make_torus(
    const std::string_view name,
    const glm::vec3        center,
    const float            major_radius,
    const float            minor_radius,
    const int              major_segments,
    const int              minor_segments
) -> Test_geometry
{
    std::vector<glm::vec3>  vertices;
    std::vector<glm::uvec3> triangles;
    for (int i = 0; i <= major_segments; ++i) {
        const float u = (static_cast<float>(i) / static_cast<float>(major_segments)) * 2.0f * pi;
        for (int j = 0; j <= minor_segments; ++j) {
            const float v = (static_cast<float>(j) / static_cast<float>(minor_segments)) * 2.0f * pi;
            const float r = major_radius + (minor_radius * std::cos(v));
            vertices.push_back(center + glm::vec3{r * std::cos(u), minor_radius * std::sin(v), r * std::sin(u)});
        }
    }
    for (int i = 0; i < major_segments; ++i) {
        for (int j = 0; j < minor_segments; ++j) {
            const uint32_t a = static_cast<uint32_t>((i * (minor_segments + 1)) + j);
            const uint32_t b = static_cast<uint32_t>(((i + 1) * (minor_segments + 1)) + j);
            triangles.push_back(glm::uvec3{a, a + 1, b});
            triangles.push_back(glm::uvec3{b, a + 1, b + 1});
        }
    }
    return make_triangle_geometry(name, vertices, triangles);
}

auto make_floor(const std::string_view name, const glm::vec3 center, const float half_size) -> Test_geometry
{
    return make_triangle_geometry(
        name,
        {
            center + glm::vec3{-half_size, 0.0f, -half_size},
            center + glm::vec3{ half_size, 0.0f, -half_size},
            center + glm::vec3{ half_size, 0.0f,  half_size},
            center + glm::vec3{-half_size, 0.0f,  half_size}
        },
        { {0, 2, 1}, {0, 3, 2} }
    );
}

class Expected_hit
{
public:
    glm::vec3  origin;
    glm::vec3  direction;
    IGeometry* geometry;
    float      t;
};

void expect_hit(IScene& scene, const Expected_hit& expected)
{
    Ray ray = make_ray(expected.origin, expected.direction);
    Hit hit{};
    const bool is_hit = scene.intersect(ray, hit);
    EXPECT_TRUE(is_hit);
    EXPECT_EQ(hit.geometry, expected.geometry);
    EXPECT_NEAR(ray.t_far, expected.t, 0.01f);
}

// The torus BVH has inner nodes; the geometries are attached to the scene directly.
// madmann91/bvh's safe_inverse() gives FLT_MAX, so the fast traversal's -org * inv_dir overflows; the bvh backend uses robust traversal.
TEST(Axis_aligned_ray, GeometryBvhInnerNodes)
{
    const glm::vec3 center{5.0f, 0.0f, 5.0f};
    Test_geometry torus = make_torus("torus", center, 3.0f, 1.0f, 48, 24);
    Test_geometry floor = make_floor("floor", center + glm::vec3{0.0f, -2.0f, 0.0f}, 8.0f);

    std::unique_ptr<IScene> scene = IScene::create_unique("scene");
    scene->attach(torus.geometry.get());
    scene->attach(floor.geometry.get());
    scene->commit();

    // Top of the tube near u = 0, off the mesh vertices
    expect_hit(*scene, Expected_hit{.origin = {8.05f, 5.0f, 5.05f}, .direction = {0.0f, -1.0f, 0.0f}, .geometry = torus.geometry.get(), .t = 4.0070f});
    // Through the hole
    expect_hit(*scene, Expected_hit{.origin = {5.0f, 5.0f, 5.0f}, .direction = {0.0f, -1.0f, 0.0f}, .geometry = floor.geometry.get(), .t = 7.0f});
    // Along -x into the outside of the tube near u = 0, off the mesh vertices
    expect_hit(*scene, Expected_hit{.origin = {20.0f, 0.05f, 5.05f}, .direction = {-1.0f, 0.0f, 0.0f}, .geometry = torus.geometry.get(), .t = 11.0099f});
}

// The torus and floor of scripts/prepared_asset_verify.py's small.gltf as
// instances: the TLAS and the torus BLAS both have inner nodes.
TEST(Axis_aligned_ray, InstancedTorusOverFloor)
{
    Test_geometry torus = make_torus("torus", glm::vec3{0.0f}, 0.6f, 0.22f, 48, 24);
    Test_geometry floor = make_floor("floor", glm::vec3{0.0f}, 4.0f);

    std::unique_ptr<IScene> torus_scene = IScene::create_unique("torus");
    std::unique_ptr<IScene> floor_scene = IScene::create_unique("floor");
    torus_scene->attach(torus.geometry.get());
    floor_scene->attach(floor.geometry.get());
    torus_scene->commit();
    floor_scene->commit();

    const float     half_angle = glm::radians(60.0f) * 0.5f;
    const glm::quat rotation{std::cos(half_angle), std::sin(half_angle), 0.0f, 0.0f};
    const glm::mat4 torus_transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1.8f, 0.6f, 0.4f}) * glm::mat4_cast(rotation);

    std::unique_ptr<IInstance> torus_instance = IInstance::create_unique("torus");
    std::unique_ptr<IInstance> floor_instance = IInstance::create_unique("floor");
    torus_instance->set_scene(torus_scene.get());
    torus_instance->set_transform(torus_transform);
    torus_instance->set_mask(0xffffffffu);
    torus_instance->commit();
    floor_instance->set_scene(floor_scene.get());
    floor_instance->set_transform(glm::mat4{1.0f});
    floor_instance->set_mask(0xffffffffu);
    floor_instance->commit();

    std::unique_ptr<IScene> root_scene = IScene::create_unique("root");
    root_scene->attach(torus_instance.get());
    root_scene->attach(floor_instance.get());
    root_scene->commit();

    // Straight down onto the tube
    expect_hit(*root_scene, Expected_hit{.origin = {1.8f, 5.0f, 0.0f}, .direction = {0.0f, -1.0f, 0.0f}, .geometry = torus.geometry.get(), .t = 3.6857f});
    // Along the torus axis through the hole (prepared_asset_verify.py TORUS_RAY)
    expect_hit(*root_scene, Expected_hit{.origin = {1.8f, 2.1f, 2.9980762f}, .direction = {0.0f, -0.5f, -0.8660254f}, .geometry = floor.geometry.get(), .t = 4.2f});
    // Straight down onto the floor, away from the torus
    expect_hit(*root_scene, Expected_hit{.origin = {-3.0f, 5.0f, -3.0f}, .direction = {0.0f, -1.0f, 0.0f}, .geometry = floor.geometry.get(), .t = 5.0f});
}

} // namespace
