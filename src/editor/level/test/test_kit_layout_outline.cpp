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
using editor::Kit_layout_count_mode;
using editor::Kit_layout_handle_set;
using editor::Kit_layout_item_sizes;
using editor::Kit_layout_outline;
using editor::Kit_layout_pattern;
using editor::Kit_layout_run;
using editor::Kit_region_handle;
using editor::Kit_region_handle_value;
using editor::Repeat_pattern_kind;
using editor::Repeat_ring_facing;
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

    // A count mode line of one piece has no step to set.
    pattern.kind  = Repeat_pattern_kind::line;
    pattern.count = 1;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(outline.region_handles.empty());
}

TEST(Kit_layout_outline, region_handle_sizes_from_a_point_in_the_node_frame)
{
    editor::Kit_layout_handle_drag drag;
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
    editor::apply_region_handle(plus_x, glm::vec3{6.5f, 2.0f, 1.0f}, drag, pattern);
    EXPECT_EQ(pattern.size_x, 13.0f);
    EXPECT_EQ(pattern.size_z, 6.0f);
    EXPECT_EQ(pattern.region_radius, 3.0f);
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(minus_z, glm::vec3{4.0f, 0.0f, -2.0f}, drag, pattern);
    EXPECT_EQ(pattern.size_z, 4.0f);
    EXPECT_EQ(pattern.size_x, 10.0f);

    // A corner: both sizes.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(corner, glm::vec3{-6.0f, 0.0f, 4.5f}, drag, pattern);
    EXPECT_EQ(pattern.size_x, 12.0f);
    EXPECT_EQ(pattern.size_z, 9.0f);

    // A disc: the distance from the origin in the xz plane, whichever way the handle went.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(disc, glm::vec3{3.0f, -1.0f, 4.0f}, drag, pattern);
    EXPECT_EQ(pattern.region_radius, 5.0f);
    EXPECT_EQ(pattern.size_x, 10.0f);

    // Past the origin, or beyond the range: clamped.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(plus_x, glm::vec3{-2.0f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.size_x, editor::c_min_scatter_region_size);
    editor::apply_region_handle(corner, glm::vec3{-1.0e6f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.size_x, editor::c_max_scatter_region_size);
    EXPECT_EQ(pattern.size_z, editor::c_min_scatter_region_size);
    editor::apply_region_handle(disc, glm::vec3{0.0f}, drag, pattern);
    EXPECT_EQ(pattern.region_radius, editor::c_min_scatter_region_size);

    // A point that is not finite changes nothing.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(plus_x, glm::vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern, start);
}

// The handle of the given value in the outline, its index checked against the expected order.
[[nodiscard]] auto handle_at(const Kit_layout_outline& outline, const std::size_t index, const Kit_region_handle_value value) -> Kit_region_handle
{
    EXPECT_LT(index, outline.region_handles.size());
    if (index >= outline.region_handles.size()) {
        return Kit_region_handle{};
    }
    EXPECT_EQ(outline.region_handles[index].value, value);
    return outline.region_handles[index];
}

TEST(Kit_layout_handles, line_end_sets_the_step_in_count_mode)
{
    editor::Kit_layout_handle_drag drag;
    Kit_layout_pattern pattern;
    pattern.kind      = Repeat_pattern_kind::line;
    pattern.count     = 5;
    pattern.step      = 2.0f;
    pattern.direction = glm::vec3{0.0f, 0.0f, 2.0f};
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.region_handles.size(), 1u);
    const Kit_region_handle end = handle_at(outline, 0, Kit_region_handle_value::step);
    EXPECT_TRUE(is_near(end.position, outline.polyline[1]));
    EXPECT_TRUE(is_near(end.position, glm::vec3{0.0f, 0.0f, 8.0f}));

    // The run's end is 4 steps out; only the reach along the line counts.
    const Kit_layout_pattern start = pattern;
    editor::apply_region_handle(end, glm::vec3{3.0f, 1.0f, 10.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.step, 2.5f);
    EXPECT_EQ(pattern.count, 5);
    EXPECT_EQ(pattern.direction, start.direction);
    // Past the origin it holds the least step; beyond the range the most.
    editor::apply_region_handle(end, glm::vec3{0.0f, 0.0f, -3.0f}, drag, pattern);
    EXPECT_EQ(pattern.step, editor::c_min_layout_handle_step);
    editor::apply_region_handle(end, glm::vec3{0.0f, 0.0f, 1.0e6f}, drag, pattern);
    EXPECT_EQ(pattern.step, editor::c_max_line_step);

    // A negative step runs the other way; its handle keeps the sign.
    pattern      = start;
    pattern.step = -1.0f;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    const Kit_region_handle back = handle_at(outline, 0, Kit_region_handle_value::step);
    EXPECT_TRUE(is_near(back.position, glm::vec3{0.0f, 0.0f, -4.0f}));
    editor::apply_region_handle(back, glm::vec3{0.0f, 0.0f, -6.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.step, -1.5f);
}

TEST(Kit_layout_handles, line_end_sets_the_length_in_gap_mode)
{
    editor::Kit_layout_handle_drag drag;
    Kit_layout_pattern pattern;
    pattern.kind       = Repeat_pattern_kind::line;
    pattern.count_mode = Kit_layout_count_mode::gap;
    pattern.length     = 6.0f;
    pattern.gap        = 0.5f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    const Kit_region_handle end = handle_at(outline, 0, Kit_region_handle_value::length);
    EXPECT_TRUE(is_near(end.position, glm::vec3{6.0f, 0.0f, 0.0f}));
    const Kit_layout_pattern start = pattern;
    editor::apply_region_handle(end, glm::vec3{9.0f, 0.0f, 2.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.length, 9.0f);
    EXPECT_EQ(pattern.gap, start.gap);
    EXPECT_EQ(pattern.step, start.step);
    editor::apply_region_handle(end, glm::vec3{-2.0f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.length, 0.0f);
    editor::apply_region_handle(end, glm::vec3{1.0e6f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.length, editor::c_max_line_length);

    // A length shorter than two pieces holds one piece; the handle stays at the length's end.
    pattern.length = 0.5f;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(is_near(handle_at(outline, 0, Kit_region_handle_value::length).position, glm::vec3{0.5f, 0.0f, 0.0f}));
}

TEST(Kit_layout_handles, grid_far_corner_and_edges_set_the_spacings)
{
    editor::Kit_layout_handle_drag drag;
    Kit_layout_pattern pattern;
    pattern.kind      = Repeat_pattern_kind::grid;
    pattern.count_x   = 3;
    pattern.count_z   = 2;
    pattern.spacing_x = 2.0f;
    pattern.spacing_z = -1.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.region_handles.size(), 3u);
    // The far edges: x at 2.5 spacings, z at 1.5 spacings (negative: toward -z); the edges' middles between the near ones.
    const Kit_region_handle corner = handle_at(outline, 0, Kit_region_handle_value::spacing_x_z);
    const Kit_region_handle edge_x = handle_at(outline, 1, Kit_region_handle_value::spacing_x);
    const Kit_region_handle edge_z = handle_at(outline, 2, Kit_region_handle_value::spacing_z);
    EXPECT_TRUE(is_near(corner.position, glm::vec3{5.0f, 0.0f, -1.5f}));
    EXPECT_TRUE(is_near(edge_x.position, glm::vec3{5.0f, 0.0f, -0.5f}));
    EXPECT_TRUE(is_near(edge_z.position, glm::vec3{2.0f, 0.0f, -1.5f}));
    EXPECT_TRUE(is_near(corner.position, outline.polyline[2]));

    const Kit_layout_pattern start = pattern;
    editor::apply_region_handle(edge_x, glm::vec3{7.5f, 4.0f, 3.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.spacing_x, 3.0f);
    EXPECT_EQ(pattern.spacing_z, start.spacing_z);
    EXPECT_EQ(pattern.count_x, start.count_x);
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(edge_z, glm::vec3{0.0f, 0.0f, -4.5f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.spacing_z, -3.0f);
    EXPECT_EQ(pattern.spacing_x, start.spacing_x);
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(corner, glm::vec3{10.0f, 0.0f, -0.75f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.spacing_x, 4.0f);
    EXPECT_FLOAT_EQ(pattern.spacing_z, -0.5f);

    // Past the near side each spacing holds the least size with its sign; beyond the range the most.
    editor::apply_region_handle(corner, glm::vec3{-1.0f, 0.0f, 1.0e6f}, drag, pattern);
    EXPECT_EQ(pattern.spacing_x, editor::c_min_layout_handle_step);
    EXPECT_EQ(pattern.spacing_z, -editor::c_min_layout_handle_step);
    editor::apply_region_handle(edge_x, glm::vec3{1.0e6f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.spacing_x, editor::c_max_grid_spacing);
}

TEST(Kit_layout_handles, ring_radius_at_the_arc_middle_and_arc_at_its_end)
{
    editor::Kit_layout_handle_drag drag;
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::ring;
    pattern.count       = 6;
    pattern.radius      = 2.0f;
    pattern.arc_degrees = 180.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    ASSERT_EQ(outline.region_handles.size(), 2u);
    const Kit_region_handle radius = handle_at(outline, 0, Kit_region_handle_value::radius);
    const Kit_region_handle arc    = handle_at(outline, 1, Kit_region_handle_value::arc_degrees);
    // A half ring: its middle at 90 degrees, its end at 180 (behind the center), the arc handle inside the ring.
    EXPECT_TRUE(is_near(radius.position, editor::get_arc_point(outline.arcs[0], 90.0f)));
    EXPECT_TRUE(is_near(radius.position, glm::vec3{2.0f, 0.0f, -2.0f}));
    EXPECT_TRUE(is_near(arc.position, glm::vec3{0.0f, 0.0f, -2.0f - (editor::c_ring_arc_handle_inset * 2.0f)}));

    // The radius: the handle's reach along its offset from element 0, which grows with the radius.
    const Kit_layout_pattern start = pattern;
    editor::apply_region_handle(radius, glm::vec3{3.0f, 5.0f, -3.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.radius, 3.0f);
    EXPECT_EQ(pattern.arc_degrees, start.arc_degrees);
    editor::apply_region_handle(radius, glm::vec3{-1.0f, 0.0f, 1.0f}, drag, pattern);
    EXPECT_EQ(pattern.radius, editor::c_min_ring_radius);
    editor::apply_region_handle(radius, glm::vec3{1.0e6f, 0.0f, -1.0e6f}, drag, pattern);
    EXPECT_EQ(pattern.radius, editor::c_max_ring_radius);

    // The arc: the angle about the center (0, 0, -2) from element 0, counter-clockwise seen from above.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(arc, glm::vec3{-1.0f, 0.0f, -2.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.arc_degrees, 270.0f);
    EXPECT_EQ(pattern.radius, start.radius);
    editor::apply_region_handle(arc, glm::vec3{-1.0f, 0.0f, -1.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.arc_degrees, 315.0f);
    // Turning on past element 0 takes the turn nearest the previous arc: a full ring holds 360.
    editor::apply_region_handle(arc, glm::vec3{0.5f, 0.0f, -1.0f}, drag, pattern);
    EXPECT_EQ(pattern.arc_degrees, editor::c_max_ring_arc);
    editor::apply_region_handle(arc, glm::vec3{-1.0f, 0.0f, -1.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.arc_degrees, 315.0f);
    // Back past element 0 the other way it holds the least arc.
    pattern = start;
    drag    = editor::begin_layout_handle_drag(start);
    editor::apply_region_handle(arc, glm::vec3{1.0f, 0.0f, -1.0f}, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.arc_degrees, 45.0f);
    editor::apply_region_handle(arc, glm::vec3{-0.2f, 0.0f, 0.0f}, drag, pattern);
    EXPECT_EQ(pattern.arc_degrees, editor::c_min_ring_arc);

    // A full ring: the radius handle opposite element 0, the arc handle inside the ring behind element 0.
    pattern             = start;
    drag                = editor::begin_layout_handle_drag(start);
    pattern.arc_degrees = 360.0f;
    pattern.facing      = Repeat_ring_facing::start_rotation;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    EXPECT_TRUE(is_near(handle_at(outline, 0, Kit_region_handle_value::radius).position, glm::vec3{0.0f, 0.0f, -4.0f}));
    EXPECT_TRUE(is_near(handle_at(outline, 1, Kit_region_handle_value::arc_degrees).position, glm::vec3{0.0f, 0.0f, -2.0f + (editor::c_ring_arc_handle_inset * 2.0f)}));
}

// A point on the circle of the ring (radius 2, center (0, 0, -2)) at the angle from element 0.
[[nodiscard]] auto ring_point(const float degrees) -> glm::vec3
{
    const float radians = glm::radians(degrees);
    return glm::vec3{0.0f, 0.0f, -2.0f} + (2.0f * glm::vec3{std::sin(radians), 0.0f, std::cos(radians)});
}

TEST(Kit_layout_handles, ring_arc_stays_held_until_the_pointer_comes_back)
{
    Kit_layout_pattern start;
    start.kind        = Repeat_pattern_kind::ring;
    start.count       = 6;
    start.radius      = 2.0f;
    start.arc_degrees = 360.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(start, 0.5f, outline), "");
    const Kit_region_handle arc = handle_at(outline, 1, Kit_region_handle_value::arc_degrees);

    // A full ring's end turned on past element 0: held at 360 however far the pointer goes, until it turns back.
    Kit_layout_pattern             pattern = start;
    editor::Kit_layout_handle_drag drag    = editor::begin_layout_handle_drag(start);
    for (const float degrees : {10.0f, 100.0f, 190.0f, 280.0f}) {
        editor::apply_region_handle(arc, ring_point(degrees), drag, pattern);
        EXPECT_EQ(pattern.arc_degrees, editor::c_max_ring_arc) << degrees;
    }
    for (const float degrees : {190.0f, 100.0f, 10.0f}) {
        editor::apply_region_handle(arc, ring_point(degrees), drag, pattern);
        EXPECT_EQ(pattern.arc_degrees, editor::c_max_ring_arc) << degrees;
    }
    editor::apply_region_handle(arc, ring_point(-30.0f), drag, pattern);
    EXPECT_NEAR(pattern.arc_degrees, 330.0f, 1.0e-3f);

    // A short arc's end turned back past element 0: held at the least arc, half a turn and more.
    pattern             = start;
    pattern.arc_degrees = 45.0f;
    drag                = editor::begin_layout_handle_drag(pattern);
    for (const float degrees : {-10.0f, -100.0f, -190.0f}) {
        editor::apply_region_handle(arc, ring_point(degrees), drag, pattern);
        EXPECT_EQ(pattern.arc_degrees, editor::c_min_ring_arc) << degrees;
    }
    for (const float degrees : {-100.0f, -10.0f, 20.0f}) {
        editor::apply_region_handle(arc, ring_point(degrees), drag, pattern);
    }
    EXPECT_NEAR(pattern.arc_degrees, 20.0f, 1.0e-3f);
}

TEST(Kit_layout_handles, ring_radius_handle_stays_clear_of_element_0_on_a_short_arc)
{
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::ring;
    pattern.count       = 2;
    pattern.radius      = 4.0f;
    pattern.arc_degrees = 5.0f;
    Kit_layout_outline outline;
    ASSERT_EQ(outline_of(pattern, 0.5f, outline), "");
    // At 60 degrees round, one radius from element 0, the mapping still exact.
    const Kit_region_handle radius = handle_at(outline, 0, Kit_region_handle_value::radius);
    EXPECT_NEAR(glm::length(radius.position), 4.0f, 1.0e-4f);
    EXPECT_TRUE(is_near(radius.position, editor::get_arc_point(Kit_layout_arc{.center = {0.0f, 0.0f, -4.0f}, .radius = 4.0f}, 60.0f)));
    editor::Kit_layout_handle_drag drag = editor::begin_layout_handle_drag(pattern);
    editor::apply_region_handle(radius, 1.5f * radius.position, drag, pattern);
    EXPECT_FLOAT_EQ(pattern.radius, 6.0f);
}

TEST(Kit_layout_handles, handle_set_names_what_the_indices_mean)
{
    Kit_layout_pattern pattern;
    pattern.kind  = Repeat_pattern_kind::line;
    pattern.count = 3;
    const Kit_layout_handle_set line = editor::get_layout_handle_set(pattern);
    EXPECT_EQ(line.count, 1u);
    pattern.count = 7;
    EXPECT_EQ(editor::get_layout_handle_set(pattern), line);
    pattern.count = 1;
    EXPECT_EQ(editor::get_layout_handle_set(pattern).count, 0u);
    pattern.count_mode = Kit_layout_count_mode::gap;
    EXPECT_NE(editor::get_layout_handle_set(pattern), line);
    EXPECT_EQ(editor::get_layout_handle_set(pattern).count, 1u);
    pattern.kind = Repeat_pattern_kind::grid;
    EXPECT_EQ(editor::get_layout_handle_set(pattern).count, 3u);
    pattern.kind = Repeat_pattern_kind::ring;
    EXPECT_EQ(editor::get_layout_handle_set(pattern).count, 2u);
    pattern.kind = Repeat_pattern_kind::scatter;
    const Kit_layout_handle_set rectangle = editor::get_layout_handle_set(pattern);
    EXPECT_EQ(rectangle.count, 8u);
    pattern.region = Repeat_scatter_region::disc;
    EXPECT_NE(editor::get_layout_handle_set(pattern), rectangle);
    pattern.kind = Repeat_pattern_kind::path;
    EXPECT_EQ(editor::get_layout_handle_set(pattern).count, 0u);
}

} // anonymous namespace
