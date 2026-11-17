#include "kit_shape_test_support.hpp"

#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the recreated architecture shapes (kit_shapes_architecture_v2.cpp).
namespace {

using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_mesh_report;
using namespace editor::kit_test;

// The door wall, the pillar and the arch keep their originals' bounds, so connectors, stacking and runs of walls hold.
// An arch of an odd segment count is left out: the original's crown is a chord below rise + thickness there.
TEST(Kit_recreated, architecture_shapes_keep_their_originals_bounds)
{
    expect_recreated_keep_original_bounds(get_architecture_v2_rows().recreated_originals);
    const Kit_shape_definition& pillar   = get_shape("architecture.pillar.v2");
    const Kit_shape_definition& original = get_shape("architecture.pillar");
    for (const int segments : {3, 4, 7, 12, 16, 64}) {
        for (const float width : {0.05f, 0.5f, 16.0f}) {
            for (const float height : {0.1f, 3.0f, 64.0f}) {
                Kit_parameter_values values          = defaults(pillar);
                Kit_parameter_values original_values = defaults(original);
                for (Kit_parameter_values* const set : {&values, &original_values}) {
                    ASSERT_TRUE(set->set_enum("profile", "round"));
                    ASSERT_TRUE(set->set_int("segments", segments));
                    ASSERT_TRUE(set->set_float("width", width));
                    ASSERT_TRUE(set->set_float("height", height));
                }
                expect_original_bounds(values, original_values);
            }
        }
    }
}

// The recreated door wall leaves its stated opening clear and reaches it: no vertex inside, vertices on its edges.
TEST(Kit_recreated, door_wall_keeps_its_opening)
{
    const Kit_shape_definition& door_wall = get_shape("architecture.door_wall.v2");
    std::vector<Kit_parameter_values> cases{defaults(door_wall), range_corner(door_wall, Range_end::min), range_corner(door_wall, Range_end::max)};
    for (const int seed : {1, 3, 17}) {
        cases.push_back(all_derived(door_wall, seed));
    }
    for (const Kit_parameter_values& values : cases) {
        SCOPED_TRACE(values.to_canonical_text());
        const float half_width = 0.5f * values.get_float("opening_width");
        const float height     = values.get_float("opening_height");
        const float tolerance  = 1.0e-5f * std::max(1.0f, values.get_float("length"));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float nearest_x = std::numeric_limits<float>::max();
        float lowest_y  = std::numeric_limits<float>::max();
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
            EXPECT_FALSE((std::abs(p.x) < (half_width - tolerance)) && (p.y < (height - tolerance))) << p.x << " " << p.y;
            if (p.y < (height - tolerance)) {
                nearest_x = std::min(nearest_x, std::abs(p.x));
            }
            if ((std::abs(p.x) <= (half_width + tolerance)) && (p.y > tolerance)) {
                lowest_y = std::min(lowest_y, p.y);
            }
        }
        EXPECT_NEAR(nearest_x, half_width, tolerance);
        EXPECT_NEAR(lowest_y,  height,     tolerance);
    }
}

// The recreated arch's opening is the half ellipse of the span and the rise: no vertex inside it, the feet at the
// span and the crown at the rise; `segments` counts its stones, each stone and the whole a single closed solid.
TEST(Kit_recreated, arch_keeps_its_opening_at_every_stone_count)
{
    const Kit_shape_definition& arch = get_shape("architecture.arch.v2");
    for (int segments = 2; segments <= 48; ++segments) {
        for (const std::array<float, 4>& size : {std::array<float, 4>{2.0f, 2.4f, 0.5f, 0.3f}, std::array<float, 4>{0.3f, 32.0f, 16.0f, 0.05f},
                                                 std::array<float, 4>{32.0f, 0.2f, 0.05f, 8.0f}, std::array<float, 4>{5.0f, 2.2f, 0.6f, 0.4f},
                                                 std::array<float, 4>{32.0f, 32.0f, 0.05f, 0.05f}}) {
            Kit_parameter_values values = defaults(arch);
            ASSERT_TRUE(values.set_int("segments", segments));
            ASSERT_TRUE(values.set_float("span",      size[0]));
            ASSERT_TRUE(values.set_float("rise",      size[1]));
            ASSERT_TRUE(values.set_float("depth",     size[2]));
            ASSERT_TRUE(values.set_float("thickness", size[3]));
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_LE(report.triangle_count, get_library().find_kit("architecture")->triangle_ceiling);
            const float half_span = 0.5f * size[0];
            const float rise      = size[1];
            const float extent    = std::max(1.0f, std::max(size[0], rise));
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            float foot  = std::numeric_limits<float>::max();
            float crown = std::numeric_limits<float>::max();
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
                const float      x = p.x / half_span;
                const float      y = p.y / rise;
                EXPECT_GE((x * x) + (y * y), 1.0f - 1.0e-4f) << p.x << " " << p.y;
                if (p.y == 0.0f) {
                    foot = std::min(foot, std::abs(p.x));
                }
                if (p.x == 0.0f) {
                    crown = std::min(crown, p.y);
                }
            }
            EXPECT_NEAR(foot,  half_span, 1.0e-5f * extent);
            EXPECT_NEAR(crown, rise,      1.0e-5f * extent);
        }
    }
}

// The round pillar has a base and a capital inside the cylinder.
TEST(Kit_recreated, round_pillar_has_a_base_and_a_capital_inside_its_cylinder)
{
    const Kit_shape_definition& pillar = get_shape("architecture.pillar.v2");
    Kit_parameter_values values = defaults(pillar);
    ASSERT_TRUE(values.set_enum("profile", "round"));
    const float radius = 0.5f * values.get_float("width");
    const float height = values.get_float("height");
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    float shaft = radius;
    for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
        const float      r = std::sqrt((p.x * p.x) + (p.z * p.z));
        EXPECT_LE(r, radius + 1.0e-5f);
        if ((p.y > (0.25f * height)) && (p.y < (0.75f * height))) {
            shaft = std::min(shaft, r);
        }
        if ((p.y == 0.0f) || (p.y == height)) {
            EXPECT_NEAR(r, radius, 1.0e-5f);
        }
    }
    // No ring lies in the middle half: the shaft runs from the base to the capital in one band.
    EXPECT_EQ(shaft, radius);
    const Kit_mesh_report report = editor::check_kit_shape(values);
    EXPECT_TRUE(report.is_valid()) << describe(report);
    EXPECT_EQ(report.triangle_count, static_cast<unsigned int>((14 * 2 * values.get_int("segments")) + (2 * (values.get_int("segments") - 2))));
}

// The square pillar is a pier: its plinth and abacus fill the top and bottom of the original's box (the seam sides it
// states), and the shaft between them is inset on all four sides.
TEST(Kit_recreated, square_pillar_has_a_plinth_and_an_abacus_around_an_inset_shaft)
{
    const Kit_shape_definition& pillar = get_shape("architecture.pillar.v2");
    for (const float width : {0.05f, 0.5f, 16.0f}) {
        for (const float height : {0.1f, 3.0f, 64.0f}) {
            Kit_parameter_values values = defaults(pillar);
            ASSERT_TRUE(values.set_float("width", width));
            ASSERT_TRUE(values.set_float("height", height));
            SCOPED_TRACE(values.to_canonical_text());
            ASSERT_EQ(pillar.get_seam_sides(values), (std::vector<editor::Kit_side>{editor::Kit_side::y_neg, editor::Kit_side::y_pos}));
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.triangle_count, 100u);
            EXPECT_EQ(report.component_count, 1u);
            const float half = 0.5f * width;
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            float shaft = half;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
                EXPECT_EQ(std::abs(p.x), std::abs(p.z));
                EXPECT_LE(std::abs(p.x), half);
                if ((p.y == 0.0f) || (p.y == height)) {
                    EXPECT_EQ(std::abs(p.x), half);
                }
                if ((p.y > (0.25f * height)) && (p.y < (0.75f * height))) {
                    shaft = std::min(shaft, std::abs(p.x));
                }
            }
            // No ring lies in the middle half: the shaft runs from the base to the capital in one band.
            EXPECT_EQ(shaft, half);
        }
    }
}

// Both slots of the door wall and of the arch have facets at every value: the frame and the wall; the keystone with the
// springers, and the voussoirs - an arch of two to four segments has no voussoirs, and its springers are the ring's.
TEST(Kit_recreated, door_frame_and_arch_stones_have_their_slots)
{
    const auto slot_facets = [](const Kit_parameter_values& values) -> std::vector<unsigned int> {
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        std::vector<unsigned int> counts(values.get_shape()->material_slots.size(), 0u);
        for (const uint32_t slot : facet_slots) {
            ++counts[slot];
        }
        return counts;
    };
    const Kit_shape_definition& door_wall = get_shape("architecture.door_wall.v2");
    ASSERT_EQ(door_wall.material_slots, (std::vector<std::string>{"wall", "trim"}));
    for (const Kit_parameter_values& values : {defaults(door_wall), range_corner(door_wall, Range_end::min), range_corner(door_wall, Range_end::max)}) {
        const std::vector<unsigned int> counts = slot_facets(values);
        // The frame's facets are the same at every value: its cells on both faces and the walls of the opening and the base.
        EXPECT_EQ(counts[1], 131u) << values.to_canonical_text();
        EXPECT_EQ(counts[0] + counts[1], 314u);
    }
    const Kit_shape_definition& arch = get_shape("architecture.arch.v2");
    ASSERT_EQ(arch.material_slots, (std::vector<std::string>{"trim", "stone"}));
    unsigned int accent_at_twelve = 0;
    for (int segments = 2; segments <= 48; ++segments) {
        Kit_parameter_values values = defaults(arch);
        ASSERT_TRUE(values.set_int("segments", segments));
        const std::vector<unsigned int> counts = slot_facets(values);
        EXPECT_GT(counts[0], 0u) << segments;
        EXPECT_GT(counts[1], 0u) << segments;
        if (segments == 12) {
            accent_at_twelve = counts[1];
        }
        if (segments > 12) {
            EXPECT_GT(counts[0], counts[1]) << segments; // the voussoirs outnumber the three accent stones
        }
    }
    EXPECT_GT(accent_at_twelve, 0u);
}

} // anonymous namespace
