#include "level/surface_snap_search.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

namespace {

using editor::Surface_hit_point;
using editor::Surface_line;
using editor::Surface_offset;

// A closed box along the up axis (the snap direction is down): a surface hit at its top, another hit at its bottom.
class Box
{
public:
    float bottom;
    float top;
};

// One face: an upward one is a surface, a downward one only blocks.
class Sheet
{
public:
    float height;
    bool  upward;
};

class Line_faces
{
public:
    std::vector<Box>   boxes;
    std::vector<Sheet> sheets{};
};

constexpr float c_max_distance = 1.0f;
const Box       c_ground{.bottom = -0.2f, .top = 0.0f};

class Scene
{
public:
    std::vector<Line_faces> lines;
    float                   span        {0.2f};
    float                   max_distance{c_max_distance};

    [[nodiscard]] auto snap(const float base) const -> Surface_offset
    {
        std::vector<std::vector<Surface_hit_point>> hits(lines.size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            for (const Box& box : lines[i].boxes) {
                hits[i].push_back(Surface_hit_point{.height = box.top - base,    .surface = true});
                hits[i].push_back(Surface_hit_point{.height = box.bottom - base, .surface = false});
            }
            for (const Sheet& sheet : lines[i].sheets) {
                hits[i].push_back(Surface_hit_point{.height = sheet.height - base, .surface = sheet.upward});
            }
        }
        std::vector<Surface_line> search_lines;
        for (const std::vector<Surface_hit_point>& line_hits : hits) {
            search_lines.push_back(Surface_line{.hits = line_hits, .span = span});
        }
        return editor::find_snap_height(search_lines, max_distance);
    }

    // Snaps from base, then again from the result: the second snap moves by no more than the tolerance, which
    // find_surface_placement() answers with the start pose bit for bit.
    [[nodiscard]] auto snap_twice(const float base) const -> std::optional<float>
    {
        const Surface_offset first = snap(base);
        if (!first.found) {
            return std::nullopt;
        }
        const Surface_offset second = snap(base + first.height);
        EXPECT_TRUE(second.found);
        EXPECT_LE(std::abs(second.height), editor::c_surface_snap_tolerance);
        return first.height;
    }
};

TEST(Surface_snap_search, a_piece_over_the_ground_drops_onto_it)
{
    const Scene scene{.lines = {{.boxes = {c_ground}}}};
    const std::optional<float> height = scene.snap_twice(0.6f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), -0.6f, 1.0e-5f);
}

TEST(Surface_snap_search, a_box_hit_inside_the_span_lifts_the_piece_onto_it)
{
    // A 0.5 m piece standing in a 0.3 m box on the ground: the box top is inside its span, the box top is the place.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.0f, 0.3f}}}}, .span = 0.5f};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.3f, 1.0e-5f);
}

TEST(Surface_snap_search, buried_in_a_floating_box_drops_to_the_ground_under_it)
{
    // A box from 0.5 m to 1.5 m over the ground; the piece at 0.6 m: the ground is 0.6 m below, the box top 0.9 m above.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.5f, 1.5f}}}}};
    const std::optional<float> height = scene.snap_twice(0.6f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), -0.6f, 1.0e-5f);
}

TEST(Surface_snap_search, a_piece_that_does_not_fit_under_a_box_rises)
{
    // The gap under the box (0.4 m) is lower than the piece (0.5 m): the nearer ground is no place for it.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.4f, 1.0f}}}}, .span = 0.5f};
    const std::optional<float> height = scene.snap_twice(0.45f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.55f, 1.0e-5f);
}

TEST(Surface_snap_search, equal_distances_go_along_the_direction)
{
    // The box top 0.5 m up and the ground 0.5 m down, the piece fitting under the box.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.75f, 1.0f}}}}};
    EXPECT_EQ(scene.snap(0.5f).height, -0.5f);
}

TEST(Surface_snap_search, nothing_within_the_distance_is_no_place)
{
    // A piece floating 2 m over the ground.
    Scene scene{.lines = {{.boxes = {c_ground}}}};
    EXPECT_FALSE(scene.snap(2.0f).found);
    scene.max_distance = 2.5f;
    const std::optional<float> height = scene.snap_twice(2.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), -2.0f, 1.0e-5f);
}

TEST(Surface_snap_search, inside_a_solid_taller_than_the_piece_it_rests_on_the_surface_inside)
{
    // The documented limit (U31): a 0.5 m piece at the foot of a 3 m wall standing on the ground sees no hit in its
    // span, so it stays on the ground top inside the wall.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.0f, 3.0f}}}}, .span = 0.5f};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_EQ(height.value(), 0.0f);
}

TEST(Surface_snap_search, an_open_floor_sheet_above_leaves_the_free_floor)
{
    // A single upward quad 1.5 m up (an upper floor) and a floor 0.5 m under the piece: the floor is the place.
    const Scene scene{.lines = {{.boxes = {Box{-0.7f, -0.5f}}, .sheets = {Sheet{1.5f, true}}}}};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), -0.5f, 1.0e-5f);
}

TEST(Surface_snap_search, buried_box_and_a_quad_above_snap_twice_to_the_same_pose)
{
    // In a box whose top is 0.8 m up, an upward quad 1.5 m up: the box top, and no move on the second snap, where
    // the quad is within the window.
    const Scene scene{.lines = {{.boxes = {Box{-1.5f, 0.8f}}, .sheets = {Sheet{1.5f, true}}}}, .span = 0.3f};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.8f, 1.0e-5f);
}

TEST(Surface_snap_search, an_open_sheet_is_a_surface)
{
    const Scene scene{.lines = {{.boxes = {}, .sheets = {Sheet{-0.4f, true}}}}};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), -0.4f, 1.0e-5f);
}

TEST(Surface_snap_search, a_ceiling_sheet_blocks_only_spans_through_it)
{
    // On the ground under a downward quad 0.5 m up, a box top at 0.9 m above that.
    Scene scene{.lines = {{.boxes = {c_ground, Box{0.8f, 0.9f}}, .sheets = {Sheet{0.5f, false}}}}};
    const std::optional<float> low = scene.snap_twice(0.0f);
    ASSERT_TRUE(low.has_value());
    EXPECT_EQ(low.value(), 0.0f);
    scene.span = 0.6f;
    const std::optional<float> tall = scene.snap_twice(0.0f);
    ASSERT_TRUE(tall.has_value());
    EXPECT_NEAR(tall.value(), 0.9f, 1.0e-5f);
}

TEST(Surface_snap_search, a_sheet_of_either_facing_blocks_a_span_that_contains_it)
{
    // A sheet 0.1 m over the ground blocks the ground for a 0.2 m piece; an upward one is then the place itself.
    const Scene upward  {.lines = {{.boxes = {c_ground}, .sheets = {Sheet{0.1f, true}}}}};
    const Scene downward{.lines = {{.boxes = {c_ground}, .sheets = {Sheet{0.1f, false}}}}};
    const std::optional<float> height = upward.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.1f, 1.0e-5f);
    EXPECT_FALSE(downward.snap(0.0f).found);
}

TEST(Surface_snap_search, flush_neighbours_leave_the_span_free)
{
    // A neighbour box's faces exactly at the piece's base and top heights are not inside the span.
    const Scene scene{.lines = {{.boxes = {c_ground}}, {.boxes = {c_ground, Box{0.0f, 0.2f}}}}};
    EXPECT_EQ(scene.snap(0.0f).height, 0.0f);
}

TEST(Surface_snap_search, one_buried_base_point_lifts_the_piece_out)
{
    // One base point in a 0.15 m box, the other over open ground: the box top is inside the span at the ground.
    const Scene scene{.lines = {{.boxes = {c_ground, Box{0.0f, 0.15f}}}, {.boxes = {c_ground}}}};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.15f, 1.0e-5f);
}

TEST(Surface_snap_search, an_edge_over_a_stack_goes_to_the_top_where_it_fits)
{
    // Slabs 0.25 m thick, tops 0.3 m apart, under one base point of a 0.5 m piece; the 5 cm gaps are too low for it.
    Line_faces stack{.boxes = {c_ground}};
    for (int k = 0; k < 3; ++k) {
        stack.boxes.push_back(Box{0.05f + (0.3f * static_cast<float>(k)), 0.3f + (0.3f * static_cast<float>(k))});
    }
    Scene scene{.lines = {stack, {.boxes = {c_ground}}}, .span = 0.5f};
    const std::optional<float> height = scene.snap_twice(0.0f);
    ASSERT_TRUE(height.has_value());
    EXPECT_NEAR(height.value(), 0.9f, 1.0e-5f);
    for (int k = 3; k < 20; ++k) {
        scene.lines[0].boxes.push_back(Box{0.05f + (0.3f * static_cast<float>(k)), 0.3f + (0.3f * static_cast<float>(k))});
    }
    EXPECT_FALSE(scene.snap(0.0f).found);
}

TEST(Surface_snap_search, a_rested_piece_stays_at_any_search_distance)
{
    const Scene near_search{.lines = {{.boxes = {c_ground, Box{3.0f, 3.2f}}}}};
    Scene far_search = near_search;
    far_search.max_distance = 1000.0f;
    EXPECT_EQ(near_search.snap(0.0f).height, 0.0f);
    EXPECT_EQ(far_search.snap(0.0f).height, 0.0f);
}

TEST(Surface_snap_search, no_surface_is_no_place)
{
    const Scene scene{.lines = {{}}};
    EXPECT_FALSE(scene.snap(0.5f).found);
}

} // anonymous namespace
