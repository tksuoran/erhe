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
#include "erhe_scene/pvs_database.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
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

// One box as its own bounds (reaches_view() of a single change).
[[nodiscard]] auto single_box(const Boxes& boxes, const std::size_t i) -> Boxes
{
    Boxes box;
    box.add(boxes.center(i), boxes.extent(i));
    return box;
}

// reaches_view() answers what the classification lanes answer: per box,
// and for a set of boxes whether any is kept (the shadow map cache's
// per-pass test of the changed bounds, doc/erhe/shadows.md "Shadow map
// cache").
TEST(Cull_lanes, reaches_view_matches_classification)
{
    using erhe::scene_renderer::Cull_view_kind;
    using erhe::scene_renderer::Cull_view_tests;
    const Cull_sphere light{.center = glm::vec3{3.25f, -1.5f, 7.75f}, .radius = 10.0f};
    std::mt19937 random{4242u};
    std::uniform_real_distribution<float> offset{-14.0f, 14.0f};
    std::uniform_real_distribution<float> size{0.0f, 3.0f};
    Boxes boxes;
    for (int n = 0; n < 500; ++n) {
        boxes.add(light.center + glm::vec3{offset(random), offset(random), offset(random)}, glm::vec3{size(random), size(random), size(random)});
    }
    const Face_classification result = classify(boxes, light);
    cull_lanes::View_reach_scratch scratch;
    for (std::size_t face = 0; face < 6; ++face) {
        const Cull_view_tests view{
            .purpose   = erhe::scene_renderer::Draw_purpose::shadow,
            .kind      = Cull_view_kind::cube_face,
            .sphere    = light,
            .cube_face = static_cast<uint32_t>(face)
        };
        bool any_kept = false;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const bool kept = (result.face_inside[face][i] != 0);
            any_kept = any_kept || kept;
            EXPECT_EQ(cull_lanes::reaches_view(view, single_box(boxes, i).bounds, scratch), kept) << "face " << face << " box " << i;
        }
        EXPECT_TRUE(any_kept);
        EXPECT_EQ(cull_lanes::reaches_view(view, boxes.bounds, scratch), any_kept);
    }

    // A planes view: the face frustum planes, as a spot pass adds them.
    const std::array<glm::vec4, 6> planes = make_face_planes(light.center, light.radius, 0);
    const Cull_view_tests plane_view{
        .purpose     = erhe::scene_renderer::Draw_purpose::shadow,
        .kind        = Cull_view_kind::planes,
        .plane_count = 6,
        .planes      = planes
    };
    std::size_t kept_count = 0;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        uint32_t inside = 1u;
        for (const glm::vec4& plane : planes) {
            cull_lanes::plane_lanes(plane, single_box(boxes, i).bounds, 1, &inside);
        }
        kept_count += inside;
        EXPECT_EQ(cull_lanes::reaches_view(plane_view, single_box(boxes, i).bounds, scratch), inside != 0u) << "box " << i;
    }
    EXPECT_GT(kept_count, 0u);
    EXPECT_LT(kept_count, boxes.size());
}

TEST(Cull_lanes, reaches_view_edge_cases)
{
    using erhe::scene_renderer::Cull_view_kind;
    using erhe::scene_renderer::Cull_view_tests;
    cull_lanes::View_reach_scratch scratch;
    const Cull_sphere light_a{.center = glm::vec3{0.0f, 1.0f, 0.0f},  .radius = 5.0f};
    const Cull_sphere light_b{.center = glm::vec3{20.0f, 1.0f, 0.0f}, .radius = 5.0f};
    const auto face_view = [](const Cull_sphere& light, const uint32_t face) -> Cull_view_tests {
        return Cull_view_tests{.purpose = erhe::scene_renderer::Draw_purpose::shadow, .kind = Cull_view_kind::cube_face, .sphere = light, .cube_face = face};
    };

    // No boxes: nothing changed, no view reached.
    const Boxes none;
    EXPECT_FALSE(cull_lanes::reaches_view(face_view(light_a, 0), none.bounds, scratch));
    EXPECT_FALSE(cull_lanes::reaches_view(Cull_view_tests{}, none.bounds, scratch));

    // A caster beside light A (+X of it): A's +X face only; no face of B.
    Boxes beside_a;
    beside_a.add(light_a.center + glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{0.25f});
    for (uint32_t face = 0; face < 6; ++face) {
        EXPECT_EQ(cull_lanes::reaches_view(face_view(light_a, face), beside_a.bounds, scratch), face == 0) << "face " << face;
        EXPECT_FALSE(cull_lanes::reaches_view(face_view(light_b, face), beside_a.bounds, scratch)) << "face " << face;
    }

    // Moved from A's reach to B's: the old box reaches A, the new one B.
    Boxes moved;
    moved.add(light_a.center + glm::vec3{2.0f, 0.0f, 0.0f}, glm::vec3{0.25f});
    moved.add(light_b.center + glm::vec3{-2.0f, 0.0f, 0.0f}, glm::vec3{0.25f});
    EXPECT_TRUE(cull_lanes::reaches_view(face_view(light_a, 0), moved.bounds, scratch));
    EXPECT_TRUE(cull_lanes::reaches_view(face_view(light_b, 1), moved.bounds, scratch));
    EXPECT_FALSE(cull_lanes::reaches_view(face_view(light_b, 0), moved.bounds, scratch));

    // A box without bounds reaches every view.
    Draw_list_bounds unbounded;
    unbounded.resize(1);
    EXPECT_TRUE(cull_lanes::reaches_view(face_view(light_b, 3), unbounded, scratch));

    // A planes view without planes (Cull_view::none) keeps every box.
    EXPECT_TRUE(cull_lanes::reaches_view(Cull_view_tests{}, beside_a.bounds, scratch));
}

TEST(Cull_lanes, view_tests_is_same)
{
    using erhe::scene_renderer::Cull_view_kind;
    using erhe::scene_renderer::Cull_view_tests;
    const Cull_view_tests face{
        .purpose   = erhe::scene_renderer::Draw_purpose::shadow,
        .kind      = Cull_view_kind::cube_face,
        .sphere    = Cull_sphere{.center = glm::vec3{1.0f, 2.0f, 3.0f}, .radius = 4.0f},
        .cube_face = 2
    };
    Cull_view_tests other_face = face;
    EXPECT_TRUE(face.is_same(other_face));
    other_face.cube_face = 3;
    EXPECT_FALSE(face.is_same(other_face));
    other_face = face;
    other_face.sphere.radius = 4.5f;
    EXPECT_FALSE(face.is_same(other_face));

    Cull_view_tests planes{.purpose = erhe::scene_renderer::Draw_purpose::shadow, .kind = Cull_view_kind::planes, .plane_count = 1};
    planes.planes[0] = glm::vec4{0.0f, 1.0f, 0.0f, -1.0f};
    Cull_view_tests other_planes = planes;
    other_planes.planes[3] = glm::vec4{9.0f}; // beyond plane_count: not a test
    EXPECT_TRUE(planes.is_same(other_planes));
    other_planes.planes[0].w = -2.0f;
    EXPECT_FALSE(planes.is_same(other_planes));
    EXPECT_FALSE(planes.is_same(face));
    EXPECT_TRUE(Cull_view_tests{}.is_same(Cull_view_tests{}));
}


// The potentially visible set rule (doc/plans/pvs.md "Runtime consumption";
// cull_classify.comp applies it with the same arithmetic). A 4 x 1 x 1 grid
// of 1 m cells at the origin, page size 2 (2 x 1 x 1 pages): page 0 (cells
// x = 0, 1) is stored, page 1 (cells x = 2, 3) is not. Three columns: cell 0
// sees column 0 only, cell 1 column 1 only; column 2 is seen from neither.
[[nodiscard]] auto make_pvs_test_database() -> std::shared_ptr<const erhe::scene::Pvs_database>
{
    erhe::scene::Pvs_header header{};
    header.scene_id    = "cull-lanes-test";
    header.grid_origin = glm::vec3{0.0f, 0.0f, 0.0f};
    header.cell_size   = 1.0f;
    header.cell_counts = glm::uvec3{4u, 1u, 1u};
    header.page_size   = 2;
    std::vector<erhe::scene::Pvs_column> columns;
    for (uint32_t i = 0; i < 3; ++i) {
        erhe::scene::Pvs_column column{};
        column.node_uid        = "node" + std::to_string(i);
        column.primitive_index = 0;
        column.world_transform = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, static_cast<float>(i), 0.5f, 0.5f};
        column.world_bounds    = {static_cast<float>(i), 0.0f, 0.0f, static_cast<float>(i) + 0.5f, 1.0f, 1.0f};
        column.transform_hash  = erhe::scene::compute_pvs_transform_hash(column.world_transform, column.world_bounds);
        columns.push_back(column);
    }
    std::vector<erhe::scene::Pvs_page_entry> pages(2);
    pages[0].word_offset    = 0;
    pages[0].cells_with_pvs = 0x3u; // local cells 0 and 1 (x = 0, 1)
    // Page 0, 8 local cells of one word: cell 0, cell 1, then the cells
    // outside the grid (no PVS: all ones).
    std::vector<uint32_t> bits{0x1u, 0x2u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
    EXPECT_EQ(erhe::scene::Pvs_database::validate(header, columns, pages, bits), std::string{});
    return std::make_shared<const erhe::scene::Pvs_database>(std::move(header), std::move(columns), std::move(pages), std::move(bits));
}

class Pvs_lane_result
{
public:
    std::vector<uint32_t> inside;
    std::vector<uint32_t> culled;
    uint32_t              tested      {0};
    uint32_t              culled_count{0};
};

// The resident pages with every stored page in a slot, in page order (or,
// with page 0 not resident, no slot for it).
class Pvs_test_resident
{
public:
    std::vector<uint32_t> page_table;
    std::vector<uint32_t> slot_words;
    uint32_t              words_per_page{0};

    [[nodiscard]] auto get() const -> erhe::scene_renderer::cull_lanes::Pvs_resident_pages
    {
        return erhe::scene_renderer::cull_lanes::Pvs_resident_pages{
            .page_table = page_table, .slot_words = slot_words, .words_per_page = words_per_page
        };
    }
};

enum class Pvs_test_page_0 : unsigned int
{
    resident     = 0,
    not_resident = 1
};

[[nodiscard]] auto make_pvs_test_resident(const erhe::scene::Pvs_database& database, const Pvs_test_page_0 page_0) -> Pvs_test_resident
{
    Pvs_test_resident resident{};
    resident.words_per_page = database.get_words_per_page();
    resident.page_table.assign(database.get_page_count(), 0xffffffffu);
    for (uint32_t page = 0, end = database.get_page_count(); page < end; ++page) {
        const std::span<const uint32_t> words = database.get_page_words(page);
        if (words.empty() || ((page == 0) && (page_0 == Pvs_test_page_0::not_resident))) {
            continue;
        }
        resident.page_table[page] = static_cast<uint32_t>(resident.slot_words.size() / resident.words_per_page);
        resident.slot_words.insert(resident.slot_words.end(), words.begin(), words.end());
    }
    return resident;
}

// Entries: columns 0, 1, 2, untracked, column 2 without bounds, column 2
// outside the view.
[[nodiscard]] auto run_pvs_lanes(const erhe::scene::Pvs_database& database, const glm::vec3& query_min, const glm::vec3& query_max, const Pvs_test_page_0 page_0 = Pvs_test_page_0::resident) -> Pvs_lane_result
{
    constexpr uint32_t untracked = erhe::scene::Pvs_database::untracked_column;
    const std::vector<uint32_t> columns  {0u, 1u, 2u, untracked, 2u, 2u};
    const std::vector<uint32_t> unbounded{0u, 0u, 0u, 0u,        1u, 0u};
    Pvs_lane_result result{};
    result.inside = {1u, 1u, 1u, 1u, 1u, 0u};
    result.culled.resize(columns.size(), 7u);
    erhe::scene_renderer::cull_lanes::Pvs_query_cells cells{};
    const Pvs_test_resident resident = make_pvs_test_resident(database, page_0);
    erhe::scene_renderer::cull_lanes::make_pvs_query_cells(database, resident.get(), query_min, query_max, cells);
    result.tested = erhe::scene_renderer::cull_lanes::pvs_lanes(
        cells, columns.data(), unbounded.data(), columns.size(), result.inside.data(), result.culled.data(), result.culled_count
    );
    return result;
}

TEST(Cull_lanes, pvs_corner_cell)
{
    glm::uvec3 cell{0u};
    const glm::uvec3 counts{4u, 1u, 1u};
    EXPECT_TRUE (erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{2.5f, 0.5f, 0.5f}, glm::vec3{0.0f}, 1.0f, counts, cell));
    EXPECT_EQ(cell, glm::uvec3(2u, 0u, 0u));
    EXPECT_TRUE (erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, 1.0f, counts, cell));
    EXPECT_EQ(cell, glm::uvec3(0u, 0u, 0u));
    EXPECT_FALSE(erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{-0.01f, 0.5f, 0.5f}, glm::vec3{0.0f}, 1.0f, counts, cell));
    EXPECT_FALSE(erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{4.0f, 0.5f, 0.5f}, glm::vec3{0.0f}, 1.0f, counts, cell));
    EXPECT_FALSE(erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{0.5f, 1.5f, 0.5f}, glm::vec3{0.0f}, 1.0f, counts, cell));
    EXPECT_FALSE(erhe::scene_renderer::cull_lanes::pvs_point_cell(glm::vec3{std::nanf(""), 0.5f, 0.5f}, glm::vec3{0.0f}, 1.0f, counts, cell));
}

TEST(Cull_lanes, pvs_box_in_one_cell)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_pvs_test_database();
    // Inside cell 0: column 0 is visible, columns 1 and 2 are not.
    const Pvs_lane_result result = run_pvs_lanes(*database, glm::vec3{0.4f, 0.4f, 0.4f}, glm::vec3{0.6f, 0.6f, 0.6f});
    EXPECT_EQ(result.inside, (std::vector<uint32_t>{1u, 0u, 0u, 1u, 1u, 0u}));
    EXPECT_EQ(result.culled, (std::vector<uint32_t>{0u, 1u, 1u, 0u, 0u, 0u}));
    // Tested: the tracked, bounded entries inside the view.
    EXPECT_EQ(result.tested, 3u);
    EXPECT_EQ(result.culled_count, 2u);
}

TEST(Cull_lanes, pvs_box_across_two_cells_keeps_either_cells_columns)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_pvs_test_database();
    // Corners in cells 0 and 1: a column visible in one of them is kept.
    const Pvs_lane_result result = run_pvs_lanes(*database, glm::vec3{0.9f, 0.4f, 0.4f}, glm::vec3{1.1f, 0.6f, 0.6f});
    EXPECT_EQ(result.inside, (std::vector<uint32_t>{1u, 1u, 0u, 1u, 1u, 0u}));
    EXPECT_EQ(result.culled_count, 1u);
}

TEST(Cull_lanes, pvs_unstored_page_keeps_everything)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_pvs_test_database();
    // One corner in cell 2 of page 1, which is not stored.
    const Pvs_lane_result result = run_pvs_lanes(*database, glm::vec3{1.9f, 0.4f, 0.4f}, glm::vec3{2.1f, 0.6f, 0.6f});
    EXPECT_EQ(result.inside, (std::vector<uint32_t>{1u, 1u, 1u, 1u, 1u, 0u}));
    EXPECT_EQ(result.culled, (std::vector<uint32_t>{0u, 0u, 0u, 0u, 0u, 0u}));
    EXPECT_EQ(result.culled_count, 0u);
}

TEST(Cull_lanes, pvs_page_without_slot_keeps_everything)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_pvs_test_database();
    // Cell 0's page is stored but holds no slot in this frame (not resident
    // yet, or evicted): nothing is culled (doc/plans/pvs.md "Residency and
    // streaming").
    const Pvs_lane_result result = run_pvs_lanes(*database, glm::vec3{0.4f, 0.4f, 0.4f}, glm::vec3{0.6f, 0.6f, 0.6f}, Pvs_test_page_0::not_resident);
    EXPECT_EQ(result.inside, (std::vector<uint32_t>{1u, 1u, 1u, 1u, 1u, 0u}));
    EXPECT_EQ(result.culled_count, 0u);
    EXPECT_EQ(result.tested, 0u);
}

TEST(Cull_lanes, pvs_corner_outside_grid_keeps_everything)
{
    const std::shared_ptr<const erhe::scene::Pvs_database> database = make_pvs_test_database();
    const Pvs_lane_result result = run_pvs_lanes(*database, glm::vec3{-0.1f, 0.4f, 0.4f}, glm::vec3{0.1f, 0.6f, 0.6f});
    EXPECT_EQ(result.inside, (std::vector<uint32_t>{1u, 1u, 1u, 1u, 1u, 0u}));
    EXPECT_EQ(result.culled_count, 0u);
}

TEST(Cull_lanes, pvs_query_box_is_part_of_view_identity)
{
    using erhe::scene_renderer::Cull_view_tests;
    Cull_view_tests a{};
    a.plane_count = 1;
    a.planes[0]   = glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
    Cull_view_tests b = a;
    EXPECT_FALSE(a.has_pvs_query());
    b.pvs_query_min = glm::vec3{0.0f};
    b.pvs_query_max = glm::vec3{0.1f};
    EXPECT_TRUE(b.has_pvs_query());
    EXPECT_FALSE(a.is_same(b));
    EXPECT_TRUE(b.is_same(b));
}

} // anonymous namespace
