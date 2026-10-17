// Point light cube face culling lanes (doc/plans/culling.md phase 3). No
// device: the lanes are arithmetic over the SoA bounds. The octant test
// (cube_face_lanes) must keep every box the face's frustum planes keep
// (a wrong reject leaves a caster out of a face), and the range sphere test
// (sphere_lanes) must agree with the box-sphere distance.

#include <gtest/gtest.h>

#include "erhe_scene_renderer/cull_lanes.hpp"
#include "erhe_scene_renderer/draw_list.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_math/coordinate_conventions.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene/projection.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using erhe::scene_renderer::Cull_sphere;
using erhe::scene_renderer::Draw_list_bounds;
namespace cull_lanes = erhe::scene_renderer::cull_lanes;

// The cube faces as Shadow_renderer poses them (c_cube_look / c_cube_up).
constexpr std::array<glm::vec3, 6> c_cube_look{
    glm::vec3{ 1.0f,  0.0f,  0.0f}, glm::vec3{-1.0f,  0.0f,  0.0f},
    glm::vec3{ 0.0f,  1.0f,  0.0f}, glm::vec3{ 0.0f, -1.0f,  0.0f},
    glm::vec3{ 0.0f,  0.0f,  1.0f}, glm::vec3{ 0.0f,  0.0f, -1.0f}
};
constexpr std::array<glm::vec3, 6> c_cube_up{
    glm::vec3{ 0.0f, -1.0f,  0.0f}, glm::vec3{ 0.0f, -1.0f,  0.0f},
    glm::vec3{ 0.0f,  0.0f,  1.0f}, glm::vec3{ 0.0f,  0.0f, -1.0f},
    glm::vec3{ 0.0f, -1.0f,  0.0f}, glm::vec3{ 0.0f, -1.0f,  0.0f}
};

// The face frustum planes the way Shadow_renderer built them for phase 2:
// the light's 90-degree perspective (Light_snapshot::
// point_light_projection_transforms(), far plane at the range) times the
// inverse of the face's look-at pose, planes from extract_frustum_planes().
[[nodiscard]] auto make_face_planes(const glm::vec3& light, const float range, const std::size_t face) -> std::array<glm::vec4, 6>
{
    const erhe::scene::Projection projection{
        .projection_type    = erhe::scene::Projection::Type::perspective,
        .perspective_z_near = 0.05f,
        .perspective_z_far  = range,
        .fov_x              = glm::half_pi<float>(),
        .fov_y              = glm::half_pi<float>()
    };
    const erhe::math::Viewport viewport{.x = 0, .y = 0, .width = 512, .height = 512};
    const glm::mat4 clip_from_face  = projection.clip_from_node_transform(viewport, true, erhe::math::Depth_range::zero_to_one).get_matrix();
    const glm::mat4 world_from_face = erhe::math::create_look_at(light, light + c_cube_look[face], c_cube_up[face]);
    const glm::mat4 clip_from_world = clip_from_face * glm::inverse(world_from_face);
    return erhe::math::extract_frustum_planes(clip_from_world, 0.0f, 1.0f);
}

class Boxes
{
public:
    void add(const glm::vec3& center, const glm::vec3& extent)
    {
        bounds.center_x.push_back(center.x);
        bounds.center_y.push_back(center.y);
        bounds.center_z.push_back(center.z);
        bounds.extent_x.push_back(extent.x);
        bounds.extent_y.push_back(extent.y);
        bounds.extent_z.push_back(extent.z);
    }
    [[nodiscard]] auto size() const -> std::size_t { return bounds.center_x.size(); }
    [[nodiscard]] auto center(const std::size_t i) const -> glm::vec3 { return {bounds.center_x[i], bounds.center_y[i], bounds.center_z[i]}; }
    [[nodiscard]] auto extent(const std::size_t i) const -> glm::vec3 { return {bounds.extent_x[i], bounds.extent_y[i], bounds.extent_z[i]}; }

    Draw_list_bounds bounds;
};

// The classification of every box for the six faces of a light: the sphere
// result and, per face, the face result (as classify_cull_views() runs the
// lanes).
class Face_classification
{
public:
    std::vector<uint32_t>                sphere_inside;
    std::vector<float>                   distance2;
    std::array<std::vector<uint32_t>, 6> face_inside;
};

[[nodiscard]] auto classify(const Boxes& boxes, const Cull_sphere& sphere) -> Face_classification
{
    const std::size_t count = boxes.size();
    Face_classification result;
    result.sphere_inside.resize(count);
    result.distance2.resize(count);
    std::vector<float> gap_x(count);
    std::vector<float> gap_y(count);
    std::vector<float> gap_z(count);
    const cull_lanes::Sphere_gaps gaps{.x = gap_x.data(), .y = gap_y.data(), .z = gap_z.data()};
    cull_lanes::sphere_lanes(sphere, boxes.bounds, count, gaps, result.distance2.data(), result.sphere_inside.data());
    const std::array<const float*, 3> gap_axes   {gap_x.data(), gap_y.data(), gap_z.data()};
    const std::array<const float*, 3> center_axes{boxes.bounds.center_x.data(), boxes.bounds.center_y.data(), boxes.bounds.center_z.data()};
    const std::array<const float*, 3> extent_axes{boxes.bounds.extent_x.data(), boxes.bounds.extent_y.data(), boxes.bounds.extent_z.data()};
    for (std::size_t face = 0; face < 6; ++face) {
        const std::size_t axis = face / 2;
        result.face_inside[face].resize(count);
        cull_lanes::cube_face_lanes(
            center_axes[axis],
            extent_axes[axis],
            sphere.center[static_cast<glm::length_t>(axis)],
            ((face % 2) == 0) ? 1.0f : -1.0f,
            gap_axes[(axis + 1) % 3],
            gap_axes[(axis + 2) % 3],
            result.sphere_inside.data(),
            count,
            result.face_inside[face].data()
        );
    }
    return result;
}

// Phase 2's face test: the range sphere, then the face planes without the
// near plane (which the octant test does not apply).
[[nodiscard]] auto planes_keep(const Boxes& boxes, const std::size_t i, const std::array<glm::vec4, 6>& planes, const uint32_t sphere_inside) -> bool
{
    if (sphere_inside == 0) {
        return false;
    }
    const glm::vec3 c = boxes.center(i);
    const glm::vec3 e = boxes.extent(i);
    for (std::size_t plane_index = 0; plane_index < 6; ++plane_index) {
        if (plane_index == erhe::math::plane_near) {
            continue;
        }
        const glm::vec4& n = planes[plane_index];
        const float d = (((n.x * c.x) + (n.y * c.y)) + (n.z * c.z)) + n.w + ((std::abs(n.x) * e.x) + (std::abs(n.y) * e.y) + (std::abs(n.z) * e.z));
        if (d < 0.0f) {
            return false;
        }
    }
    return true;
}

TEST(Cull_lanes, octant_keeps_every_box_the_face_planes_keep)
{
    const std::array<Cull_sphere, 3> lights{
        Cull_sphere{.center = glm::vec3{0.0f, 0.0f, 0.0f},     .radius = 25.0f},
        Cull_sphere{.center = glm::vec3{3.25f, -1.5f, 7.75f},  .radius = 10.0f},
        Cull_sphere{.center = glm::vec3{-120.0f, 40.0f, 9.0f}, .radius = 60.0f}
    };
    std::mt19937 random{12345u};
    for (const Cull_sphere& light : lights) {
        std::uniform_real_distribution<float> offset{-1.3f * light.radius, 1.3f * light.radius};
        std::uniform_real_distribution<float> size_log{-3.0f, 1.0f};
        std::uniform_int_distribution<int>    kind{0, 3};
        Boxes boxes;
        for (int n = 0; n < 4000; ++n) {
            glm::vec3 o{offset(random), offset(random), offset(random)};
            const float scale = 0.1f * light.radius;
            glm::vec3 e{
                scale * std::pow(10.0f, size_log(random)),
                scale * std::pow(10.0f, size_log(random)),
                scale * std::pow(10.0f, size_log(random))
            };
            switch (kind(random)) {
                case 0: {
                    // Straddling a 45-degree face seam: center on the
                    // diagonal plane of two axes.
                    o.y = std::copysign(std::abs(o.x), o.y);
                    break;
                }
                case 1: {
                    // Containing the light.
                    o = 0.5f * glm::vec3{std::min(e.x, std::abs(o.x)), std::min(e.y, std::abs(o.y)), std::min(e.z, std::abs(o.z))};
                    break;
                }
                default: {
                    break;
                }
            }
            boxes.add(light.center + o, e);
        }
        const Face_classification result = classify(boxes, light);
        for (std::size_t face = 0; face < 6; ++face) {
            const std::array<glm::vec4, 6> planes = make_face_planes(light.center, light.radius, face);
            std::size_t plane_kept  = 0;
            std::size_t octant_kept = 0;
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                const bool by_planes = planes_keep(boxes, i, planes, result.sphere_inside[i]);
                const bool by_octant = (result.face_inside[face][i] != 0);
                plane_kept  += by_planes ? 1 : 0;
                octant_kept += by_octant ? 1 : 0;
                EXPECT_TRUE(!by_planes || by_octant)
                    << "face " << face << " box " << i
                    << " center " << boxes.center(i).x << " " << boxes.center(i).y << " " << boxes.center(i).z
                    << " extent " << boxes.extent(i).x << " " << boxes.extent(i).y << " " << boxes.extent(i).z;
            }
            // Both tests are meaningful: each keeps some and rejects some.
            EXPECT_GT(plane_kept, 0u);
            EXPECT_LT(octant_kept, boxes.size());
        }
    }
}

TEST(Cull_lanes, sphere_agrees_with_box_sphere_distance)
{
    const Cull_sphere sphere{.center = glm::vec3{2.0f, -3.0f, 5.0f}, .radius = 8.0f};
    std::mt19937 random{777u};
    std::uniform_real_distribution<float> offset{-14.0f, 14.0f};
    std::uniform_real_distribution<float> size{0.0f, 4.0f};
    Boxes boxes;
    for (int n = 0; n < 4000; ++n) {
        boxes.add(sphere.center + glm::vec3{offset(random), offset(random), offset(random)}, glm::vec3{size(random), size(random), size(random)});
    }
    const Face_classification result = classify(boxes, sphere);
    const double r2 = static_cast<double>(sphere.radius) * static_cast<double>(sphere.radius);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        // Brute force: the box point closest to the sphere center.
        double distance2 = 0.0;
        for (glm::length_t axis = 0; axis < 3; ++axis) {
            const double lo = static_cast<double>(boxes.center(i)[axis]) - static_cast<double>(boxes.extent(i)[axis]);
            const double hi = static_cast<double>(boxes.center(i)[axis]) + static_cast<double>(boxes.extent(i)[axis]);
            const double s  = static_cast<double>(sphere.center[axis]);
            const double q  = std::clamp(s, lo, hi);
            distance2 += (q - s) * (q - s);
        }
        if (std::abs(distance2 - r2) < (1.0e-4 * r2)) {
            continue; // on the sphere within float rounding: either answer
        }
        EXPECT_EQ(result.sphere_inside[i] != 0, distance2 <= r2) << "box " << i;
    }
}

TEST(Cull_lanes, hand_written_faces)
{
    const Cull_sphere light{.center = glm::vec3{1.0f, 2.0f, 3.0f}, .radius = 20.0f};
    Boxes boxes;
    // 0: in front of +X only.
    boxes.add(light.center + glm::vec3{5.0f, 0.0f, 0.0f}, glm::vec3{0.5f});
    // 1: behind the light along X (in front of -X only).
    boxes.add(light.center + glm::vec3{-5.0f, 0.0f, 0.0f}, glm::vec3{0.5f});
    // 2: exactly on the +X / +Y seam, touching it along an edge: x in [1, 2],
    //    y in [2, 3] relative to the light - the corner (2, 2) lies on x = y.
    boxes.add(light.center + glm::vec3{1.5f, 2.5f, 0.0f}, glm::vec3{0.5f, 0.5f, 0.25f});
    // 3: containing the light: every face.
    boxes.add(light.center, glm::vec3{0.25f, 0.5f, 1.0f});
    // 4: on the +Z axis, centered.
    boxes.add(light.center + glm::vec3{0.0f, 0.0f, 3.0f}, glm::vec3{1.0f});
    // 5: on the -Y axis but beyond the range.
    boxes.add(light.center + glm::vec3{0.0f, -30.0f, 0.0f}, glm::vec3{1.0f});

    const Face_classification result = classify(boxes, light);
    const auto faces = [&result](const std::size_t i) -> std::array<uint32_t, 6> {
        std::array<uint32_t, 6> kept{};
        for (std::size_t face = 0; face < 6; ++face) {
            kept[face] = result.face_inside[face][i];
        }
        return kept;
    };
    //                                            +X  -X  +Y  -Y  +Z  -Z
    EXPECT_EQ(faces(0), (std::array<uint32_t, 6>{1u, 0u, 0u, 0u, 0u, 0u}));
    EXPECT_EQ(faces(1), (std::array<uint32_t, 6>{0u, 1u, 0u, 0u, 0u, 0u}));
    EXPECT_EQ(faces(2), (std::array<uint32_t, 6>{1u, 0u, 1u, 0u, 0u, 0u}));
    EXPECT_EQ(faces(3), (std::array<uint32_t, 6>{1u, 1u, 1u, 1u, 1u, 1u}));
    EXPECT_EQ(faces(4), (std::array<uint32_t, 6>{0u, 0u, 0u, 0u, 1u, 0u}));
    EXPECT_EQ(faces(5), (std::array<uint32_t, 6>{0u, 0u, 0u, 0u, 0u, 0u}));
}

TEST(Cull_lanes, write_view_bit_counts_survivors)
{
    const std::array<uint32_t, 5> inside   {1u, 0u, 0u, 1u, 0u};
    const std::array<uint32_t, 5> unbounded{0u, 0u, 1u, 1u, 0u};
    std::array<uint64_t, 5> masks{0u, 0u, 0u, 0u, uint64_t{1} << 7};
    const uint32_t survivors = cull_lanes::write_view_bit_lanes(inside.data(), unbounded.data(), inside.size(), masks.data(), 7u);
    EXPECT_EQ(survivors, 3u);
    EXPECT_EQ(masks, (std::array<uint64_t, 5>{uint64_t{1} << 7, 0u, uint64_t{1} << 7, uint64_t{1} << 7, uint64_t{1} << 7}));
}

} // anonymous namespace
