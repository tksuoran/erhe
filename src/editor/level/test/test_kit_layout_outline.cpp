#include "level/kit_layout_outline.hpp"
#include "level/kit_layout_values.hpp"

#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using editor::Kit_layout_arc;
using editor::Kit_layout_item_sizes;
using editor::Kit_layout_outline;
using editor::Kit_layout_pattern;
using editor::Kit_layout_run;
using editor::Kit_region_handle;
using editor::Kit_region_handle_value;
using editor::Repeat_pattern_kind;
using editor::Repeat_pose;
using editor::Repeat_scatter_item;
using editor::Repeat_scatter_region;

constexpr float c_tolerance = 1.0e-5f;

[[nodiscard]] auto is_near(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return glm::length(a - b) <= c_tolerance;
}

// Resolves the pattern with one item of the footprint radius and makes its outline.
[[nodiscard]] auto outline_of(const Kit_layout_pattern& pattern, const float radius, Kit_layout_outline& out_outline) -> std::string
{
    Kit_layout_item_sizes sizes;
    sizes.extent = 1.0f;
    sizes.items.push_back(Repeat_scatter_item{.weight = 1, .radius = radius});
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    const std::string error = editor::resolve_layout_pattern(pattern, 7, sizes, nullptr, run, &poses);
    if (error.empty()) {
        editor::make_layout_outline(pattern, run, poses, sizes.items, nullptr, out_outline);
    }
    return error;
}

TEST(Kit_layout_outline, line_segment_and_ticks_across)
{
    Kit_layout_pattern pattern;
    pattern.kind  = Repeat_pattern_kind::line;
    pattern.count = 3;
    pattern.step  = 2.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.polyline.size(), 2u);
    EXPECT_FALSE(outline.closed);
    EXPECT_TRUE(is_near(outline.polyline[0], glm::vec3{0.0f}));
    EXPECT_TRUE(is_near(outline.polyline[1], glm::vec3{4.0f, 0.0f, 0.0f}));
    ASSERT_EQ(outline.ticks.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(is_near(outline.ticks[i].position, glm::vec3{2.0f * static_cast<float>(i), 0.0f, 0.0f}));
        EXPECT_TRUE(is_near(outline.ticks[i].direction, glm::vec3{0.0f, 0.0f, -1.0f}));
    }
    EXPECT_TRUE(outline.arcs.empty());
    EXPECT_TRUE(outline.circles.empty());

    // A vertical line ticks along z.
    pattern.direction = glm::vec3{0.0f, 1.0f, 0.0f};
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(is_near(outline.polyline[1], glm::vec3{0.0f, 4.0f, 0.0f}));
    EXPECT_TRUE(is_near(outline.ticks[0].direction, glm::vec3{0.0f, 0.0f, 1.0f}));
}

TEST(Kit_layout_outline, grid_cell_extent_and_row_column_ticks)
{
    Kit_layout_pattern pattern;
    pattern.kind      = Repeat_pattern_kind::grid;
    pattern.count_x   = 3;
    pattern.count_z   = 2;
    pattern.spacing_x = 2.0f;
    pattern.spacing_z = 1.5f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.polyline.size(), 4u);
    EXPECT_TRUE(outline.closed);
    EXPECT_TRUE(is_near(outline.polyline[0], glm::vec3{-1.0f, 0.0f, -0.75f}));
    EXPECT_TRUE(is_near(outline.polyline[1], glm::vec3{ 5.0f, 0.0f, -0.75f}));
    EXPECT_TRUE(is_near(outline.polyline[2], glm::vec3{ 5.0f, 0.0f,  2.25f}));
    EXPECT_TRUE(is_near(outline.polyline[3], glm::vec3{-1.0f, 0.0f,  2.25f}));
    ASSERT_EQ(outline.ticks.size(), 5u);
    EXPECT_TRUE(is_near(outline.ticks[2].position, glm::vec3{4.0f, 0.0f, -0.75f}));
    EXPECT_TRUE(is_near(outline.ticks[2].direction, glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_TRUE(is_near(outline.ticks[4].position, glm::vec3{-1.0f, 0.0f, 1.5f}));
    EXPECT_TRUE(is_near(outline.ticks[4].direction, glm::vec3{1.0f, 0.0f, 0.0f}));
}

TEST(Kit_layout_outline, ring_arc_through_its_elements_with_radial_ticks)
{
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::ring;
    pattern.count       = 3;
    pattern.radius      = 2.0f;
    pattern.arc_degrees = 180.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.arcs.size(), 1u);
    const Kit_layout_arc& arc = outline.arcs.front();
    EXPECT_TRUE(is_near(arc.center, glm::vec3{0.0f, 0.0f, -2.0f}));
    EXPECT_FLOAT_EQ(arc.radius, 2.0f);
    EXPECT_FLOAT_EQ(arc.degrees, 180.0f);
    EXPECT_TRUE(is_near(editor::get_arc_point(arc, 0.0f), glm::vec3{0.0f}));
    EXPECT_TRUE(is_near(editor::get_arc_point(arc, 90.0f), glm::vec3{2.0f, 0.0f, -2.0f}));
    ASSERT_EQ(outline.ticks.size(), 3u);
    // The elements sit on the arc: the first at its start, the last at its end.
    EXPECT_TRUE(is_near(outline.ticks[0].position, editor::get_arc_point(arc, 0.0f)));
    EXPECT_TRUE(is_near(outline.ticks[1].position, editor::get_arc_point(arc, 90.0f)));
    EXPECT_TRUE(is_near(outline.ticks[2].position, editor::get_arc_point(arc, 180.0f)));
    EXPECT_TRUE(is_near(outline.ticks[1].direction, glm::vec3{1.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(is_near(outline.ticks[2].direction, glm::vec3{0.0f, 0.0f, -1.0f}));
    EXPECT_TRUE(outline.polyline.empty());
}

TEST(Kit_layout_outline, scatter_region_and_spacing_circles)
{
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::scatter;
    pattern.count       = 12;
    pattern.size_x      = 10.0f;
    pattern.size_z      = 6.0f;
    pattern.min_spacing = 0.8f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.polyline.size(), 4u);
    EXPECT_TRUE(outline.closed);
    EXPECT_TRUE(is_near(outline.polyline[0], glm::vec3{-5.0f, 0.0f, -3.0f}));
    EXPECT_TRUE(is_near(outline.polyline[2], glm::vec3{ 5.0f, 0.0f,  3.0f}));
    EXPECT_TRUE(outline.ticks.empty());
    ASSERT_EQ(outline.circles.size(), 12u);
    for (std::size_t i = 0; i < outline.circles.size(); ++i) {
        EXPECT_FLOAT_EQ(outline.circles[i].footprint_radius, 0.5f);
        EXPECT_FLOAT_EQ(outline.circles[i].spacing_radius, 0.9f);
        // Accepted points keep their footprints the minimum spacing apart: the outer circles do not overlap.
        for (std::size_t j = 0; j < i; ++j) {
            const float distance = glm::length(outline.circles[i].center - outline.circles[j].center);
            EXPECT_GE(distance + c_tolerance, outline.circles[i].spacing_radius + outline.circles[j].spacing_radius);
        }
    }

    pattern.region = Repeat_scatter_region::disc;
    pattern.region_radius = 3.0f;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(outline.polyline.empty());
    ASSERT_EQ(outline.arcs.size(), 1u);
    EXPECT_TRUE(is_near(outline.arcs[0].center, glm::vec3{0.0f}));
    EXPECT_FLOAT_EQ(outline.arcs[0].radius, 3.0f);
    EXPECT_FLOAT_EQ(outline.arcs[0].degrees, 360.0f);
    for (const editor::Kit_layout_spacing_circle& circle : outline.circles) {
        EXPECT_LE(glm::length(circle.center), 3.0f + c_tolerance);
    }
}

TEST(Kit_layout_outline, path_draws_nothing_of_its_own)
{
    Kit_layout_pattern pattern;
    pattern.kind = Repeat_pattern_kind::path;
    Kit_layout_outline outline;
    outline.ticks.push_back({});
    editor::make_layout_outline(pattern, Kit_layout_run{}, {}, {}, nullptr, outline);
    EXPECT_EQ(outline.kind, Repeat_pattern_kind::path);
    EXPECT_TRUE(outline.polyline.empty() && outline.arcs.empty() && outline.ticks.empty() && outline.circles.empty());
}

TEST(Kit_layout_outline, segments_draw_the_chords_with_a_tick_at_each_point)
{
    erhe::math::Curve_path path;
    for (const glm::vec3& position : {glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{4.0f, 0.0f, 0.0f}, glm::vec3{4.0f, 0.0f, 2.0f}}) {
        path.points.push_back(erhe::math::Curve_point{.position = position});
    }
    erhe::math::Curve_path_table table;
    table.build(path);
    Kit_layout_pattern pattern;
    pattern.kind = Repeat_pattern_kind::segments;
    Kit_layout_outline outline;
    editor::make_layout_outline(pattern, Kit_layout_run{}, {}, {}, &table, outline);
    ASSERT_EQ(outline.polyline.size(), 3u);
    EXPECT_FALSE(outline.closed);
    EXPECT_TRUE(is_near(outline.polyline[2], glm::vec3{4.0f, 0.0f, 2.0f}));
    ASSERT_EQ(outline.ticks.size(), 3u);
    // The end tick is across its one segment; the corner tick across the two segments' summed heading.
    EXPECT_TRUE(is_near(outline.ticks[0].direction, glm::vec3{0.0f, 0.0f, -1.0f}) || is_near(outline.ticks[0].direction, glm::vec3{0.0f, 0.0f, 1.0f}));
    EXPECT_NEAR(std::abs(glm::dot(outline.ticks[1].direction, glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}))), 0.0f, c_tolerance);

    path.closure = erhe::math::Curve_closure::closed;
    table.build(path);
    editor::make_layout_outline(pattern, Kit_layout_run{}, {}, {}, &table, outline);
    EXPECT_EQ(outline.polyline.size(), 3u);
    EXPECT_TRUE(outline.closed);
    EXPECT_EQ(outline.ticks.size(), 3u);
}

TEST(Kit_layout_outline, scatter_region_handles_on_the_outline)
{
    Kit_layout_pattern pattern;
    pattern.kind   = Repeat_pattern_kind::scatter;
    pattern.count  = 3;
    pattern.size_x = 10.0f;
    pattern.size_z = 6.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.region_handles.size(), 8u);
    // The corners at the polyline's points, then the edge midpoints between each corner and the next.
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(is_near(outline.region_handles[i].position, outline.polyline[i]));
        EXPECT_EQ(outline.region_handles[i].value, Kit_region_handle_value::size_x_z);
        const glm::vec3 midpoint = 0.5f * (outline.polyline[i] + outline.polyline[(i + 1) % 4]);
        EXPECT_TRUE(is_near(outline.region_handles[4 + i].position, midpoint));
    }
    EXPECT_EQ(outline.region_handles[4].value, Kit_region_handle_value::size_z);
    EXPECT_EQ(outline.region_handles[5].value, Kit_region_handle_value::size_x);
    EXPECT_EQ(outline.region_handles[6].value, Kit_region_handle_value::size_z);
    EXPECT_EQ(outline.region_handles[7].value, Kit_region_handle_value::size_x);

    pattern.region        = Repeat_scatter_region::disc;
    pattern.region_radius = 3.0f;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.region_handles.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(outline.region_handles[i].value, Kit_region_handle_value::region_radius);
        EXPECT_TRUE(is_near(outline.region_handles[i].position, editor::get_arc_point(outline.arcs[0], 90.0f * static_cast<float>(i))));
    }

    pattern.kind = Repeat_pattern_kind::line;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(outline.region_handles.empty());
}

TEST(Kit_layout_outline, region_handle_sizes_from_a_point_in_the_node_frame)
{
    Kit_layout_pattern start;
    start.kind          = Repeat_pattern_kind::scatter;
    start.size_x        = 10.0f;
    start.size_z        = 6.0f;
    start.region_radius = 3.0f;
    const Kit_region_handle plus_x {.position = {5.0f, 0.0f, 0.0f},  .side = {1.0f, 0.0f, 0.0f},  .value = Kit_region_handle_value::size_x};
    const Kit_region_handle minus_z{.position = {0.0f, 0.0f, -3.0f}, .side = {0.0f, 0.0f, -1.0f}, .value = Kit_region_handle_value::size_z};
    const Kit_region_handle corner {.position = {-5.0f, 0.0f, 3.0f}, .side = {-1.0f, 0.0f, 1.0f}, .value = Kit_region_handle_value::size_x_z};
    const Kit_region_handle disc   {.position = {0.0f, 0.0f, 3.0f},  .side = {0.0f, 0.0f, 1.0f},  .value = Kit_region_handle_value::region_radius};

    // An edge: twice its reach along its side, symmetric about the origin; the other values kept, y ignored.
    Kit_layout_pattern pattern = start;
    editor::apply_region_handle(plus_x, glm::vec3{6.5f, 2.0f, 1.0f}, pattern);
    EXPECT_EQ(pattern.size_x, 13.0f);
    EXPECT_EQ(pattern.size_z, 6.0f);
    EXPECT_EQ(pattern.region_radius, 3.0f);
    pattern = start;
    editor::apply_region_handle(minus_z, glm::vec3{4.0f, 0.0f, -2.0f}, pattern);
    EXPECT_EQ(pattern.size_z, 4.0f);
    EXPECT_EQ(pattern.size_x, 10.0f);

    // A corner: both sizes.
    pattern = start;
    editor::apply_region_handle(corner, glm::vec3{-6.0f, 0.0f, 4.5f}, pattern);
    EXPECT_EQ(pattern.size_x, 12.0f);
    EXPECT_EQ(pattern.size_z, 9.0f);

    // A disc: the distance from the origin in the xz plane, whichever way the handle went.
    pattern = start;
    editor::apply_region_handle(disc, glm::vec3{3.0f, -1.0f, 4.0f}, pattern);
    EXPECT_EQ(pattern.region_radius, 5.0f);
    EXPECT_EQ(pattern.size_x, 10.0f);

    // Past the origin, or beyond the range: clamped.
    pattern = start;
    editor::apply_region_handle(plus_x, glm::vec3{-2.0f, 0.0f, 0.0f}, pattern);
    EXPECT_EQ(pattern.size_x, editor::c_min_scatter_region_size);
    editor::apply_region_handle(corner, glm::vec3{-1.0e6f, 0.0f, 0.0f}, pattern);
    EXPECT_EQ(pattern.size_x, editor::c_max_scatter_region_size);
    EXPECT_EQ(pattern.size_z, editor::c_min_scatter_region_size);
    editor::apply_region_handle(disc, glm::vec3{0.0f}, pattern);
    EXPECT_EQ(pattern.region_radius, editor::c_min_scatter_region_size);

    // A point that is not finite changes nothing.
    pattern = start;
    editor::apply_region_handle(plus_x, glm::vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, pattern);
    EXPECT_EQ(pattern, start);
}

} // anonymous namespace
