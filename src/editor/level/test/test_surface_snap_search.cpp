#include "level/surface_snap_search.hpp"
#include "level/kit_layout.hpp"

#include "erhe_property/enum_info.hpp"

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
    float                   span             {0.2f};
    float                   forward_distance {c_max_distance};
    float                   backward_distance{c_max_distance};
    Surface_support         support          {Surface_support::five_lines}; // line 0 is the base center (U52)

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
        return editor::find_support_height(
            search_lines, editor::Surface_search_window{.forward_distance = forward_distance, .backward_distance = backward_distance}, support
        );
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
    scene.forward_distance  = 2.5f;
    scene.backward_distance = 2.5f;
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
    far_search.forward_distance  = 1000.0f;
    far_search.backward_distance = 1000.0f;
    EXPECT_EQ(near_search.snap(0.0f).height, 0.0f);
    EXPECT_EQ(far_search.snap(0.0f).height, 0.0f);
}

// U51: the search reaches forward_distance along the direction (down) and backward_distance against it.
TEST(Surface_snap_search, backward_distance_zero_searches_along_the_direction_only)
{
    // Buried 0.3 m in a box 0.5 m tall standing on the ground: its top is 0.2 m up, the ground 0.3 m down.
    Scene scene{.lines = {{.boxes = {c_ground, Box{0.0f, 0.5f}}}}};
    scene.span = 0.1f;
    const Surface_offset either_way = scene.snap(0.3f);
    ASSERT_TRUE(either_way.found);
    EXPECT_NEAR(either_way.height, 0.2f, 1.0e-6f);
    scene.backward_distance = 0.0f;
    // Not rising, it rests on the ground under the box, inside it (U31: solids are not reconstructed).
    const Surface_offset forward_only = scene.snap(0.3f);
    ASSERT_TRUE(forward_only.found);
    EXPECT_NEAR(forward_only.height, -0.3f, 1.0e-6f);
    // Neither within 0.25 m down.
    scene.forward_distance = 0.25f;
    EXPECT_FALSE(scene.snap(0.3f).found);
}

TEST(Surface_snap_search, the_two_distances_bound_their_own_side)
{
    // The ground 0.6 m down and a box top 0.4 m up (the piece fits on it).
    Scene scene{.lines = {{.boxes = {c_ground, Box{0.7f, 1.0f}}}}};
    scene.span              = 0.1f;
    scene.forward_distance  = 1.0f;
    scene.backward_distance = 0.3f;
    // The box top is nearer but beyond the backward distance: the ground is taken.
    const Surface_offset forward = scene.snap(0.6f);
    ASSERT_TRUE(forward.found);
    EXPECT_NEAR(forward.height, -0.6f, 1.0e-6f);
    scene.backward_distance = 0.5f;
    const Surface_offset nearest = scene.snap(0.6f);
    ASSERT_TRUE(nearest.found);
    EXPECT_NEAR(nearest.height, 0.4f, 1.0e-6f);
    // Forward 0.5 m alone reaches neither.
    scene.forward_distance  = 0.5f;
    scene.backward_distance = 0.0f;
    EXPECT_FALSE(scene.snap(0.6f).found);
}

TEST(Surface_snap_search, a_rest_at_the_base_is_found_with_no_backward_distance)
{
    Scene scene{.lines = {{.boxes = {c_ground}}}};
    scene.backward_distance = 0.0f;
    const Surface_offset rested = scene.snap(0.0f);
    ASSERT_TRUE(rested.found);
    EXPECT_EQ(rested.height, 0.0f);
}

TEST(Surface_snap_search, no_surface_is_no_place)
{
    const Scene scene{.lines = {{}}};
    EXPECT_FALSE(scene.snap(0.5f).found);
}

// U52: five base lines, the center first: a table top over the center line, its legs elsewhere.
[[nodiscard]] auto make_table_scene(const Surface_support support) -> Scene
{
    // A 1 m piece under a table whose 0.05 m top is 0.75 m up over the center line only.
    Scene scene{
        .lines = {
            {.boxes = {c_ground, Box{0.75f, 0.8f}}},
            {.boxes = {c_ground}}, {.boxes = {c_ground}}, {.boxes = {c_ground}}, {.boxes = {c_ground}}
        },
        .span = 1.0f
    };
    scene.backward_distance = 0.0f;
    scene.support           = support;
    return scene;
}

TEST(Surface_support, a_table_top_over_the_center_refuses_unless_the_center_falls_back)
{
    for (const float base : {0.0f, 0.3f}) {
        EXPECT_FALSE(make_table_scene(Surface_support::five_lines).snap_twice(base).has_value()) << base;
        EXPECT_FALSE(make_table_scene(Surface_support::center_blocks).snap_twice(base).has_value()) << base;
        // The fallback rests on the ground under the center, inside the table as it is, and a second snap keeps it.
        const std::optional<float> fallback = make_table_scene(Surface_support::center_fallback).snap_twice(base);
        ASSERT_TRUE(fallback.has_value()) << base;
        EXPECT_NEAR(fallback.value(), -base, 1.0e-6f) << base;
    }
    // With a backward distance the table top is a place against the direction; the nearer ground is refused and the
    // fallback still goes along the direction.
    Scene reaching = make_table_scene(Surface_support::center_fallback);
    reaching.backward_distance = 1.0f;
    const std::optional<float> along = reaching.snap_twice(0.3f);
    ASSERT_TRUE(along.has_value());
    EXPECT_NEAR(along.value(), -0.3f, 1.0e-6f);
}

// A 0.3 m box of clutter under one footprint corner (line 3), the piece 0.5 m tall.
[[nodiscard]] auto make_clutter_scene(const Surface_support support) -> Scene
{
    Scene scene{
        .lines = {
            {.boxes = {c_ground}}, {.boxes = {c_ground}}, {.boxes = {c_ground}},
            {.boxes = {c_ground, Box{0.0f, 0.3f}}}, {.boxes = {c_ground}}
        },
        .span = 0.5f
    };
    scene.backward_distance = 0.0f;
    scene.support           = support;
    return scene;
}

TEST(Surface_support, clutter_under_a_corner_supports_in_every_mode)
{
    // From above, the clutter top is the nearest rest and nothing blocks it.
    for (const Surface_support support : {Surface_support::five_lines, Surface_support::center_blocks, Surface_support::center_fallback}) {
        const std::optional<float> height = make_clutter_scene(support).snap_twice(0.6f);
        ASSERT_TRUE(height.has_value()) << to_string(support);
        EXPECT_NEAR(height.value(), -0.3f, 1.0e-6f) << to_string(support);
    }
}

TEST(Surface_support, clutter_under_a_corner_blocks_five_lines_only)
{
    // Inside the clutter's height, it lies inside the piece on the corner line and the search goes along the direction only.
    for (const float base : {0.0f, 0.1f}) {
        EXPECT_FALSE(make_clutter_scene(Surface_support::five_lines).snap_twice(base).has_value()) << base;
        for (const Surface_support support : {Surface_support::center_blocks, Surface_support::center_fallback}) {
            const std::optional<float> height = make_clutter_scene(support).snap_twice(base);
            ASSERT_TRUE(height.has_value()) << to_string(support) << " " << base;
            EXPECT_NEAR(height.value(), -base, 1.0e-6f) << to_string(support) << " " << base;
        }
    }
}

TEST(Surface_support, a_refused_nearest_place_on_a_mezzanine_falls_back_onto_the_mezzanine)
{
    // A 0.05 m slab 1 m up with a void under corner 1 and a beam 1.4 m up over corner 2; the floor below; a 0.5 m piece from
    // 1.2 m up. The beam refuses the slab top, the piece fits under the slab on the floor.
    Scene scene{
        .lines = {
            {.boxes = {c_ground, Box{1.0f, 1.05f}}},
            {.boxes = {c_ground}},
            {.boxes = {c_ground, Box{1.0f, 1.05f}, Box{1.4f, 1.5f}}},
            {.boxes = {c_ground, Box{1.0f, 1.05f}}},
            {.boxes = {c_ground, Box{1.0f, 1.05f}}}
        },
        .span = 0.5f
    };
    scene.forward_distance  = 2.0f;
    scene.backward_distance = 0.0f;
    const std::optional<float> five_lines = scene.snap_twice(1.2f);
    ASSERT_TRUE(five_lines.has_value());
    EXPECT_NEAR(five_lines.value(), -1.2f, 1.0e-6f); // the floor under the slab
    scene.support = Surface_support::center_blocks;
    const std::optional<float> center_blocks = scene.snap_twice(1.2f);
    ASSERT_TRUE(center_blocks.has_value());
    EXPECT_NEAR(center_blocks.value(), -0.15f, 1.0e-6f);
    // No farther place is searched: the center rests on the slab, and a second snap from there stays.
    scene.support = Surface_support::center_fallback;
    const std::optional<float> center_fallback = scene.snap_twice(1.2f);
    ASSERT_TRUE(center_fallback.has_value());
    EXPECT_NEAR(center_fallback.value(), -0.15f, 1.0e-6f);
}

TEST(Surface_support, center_blocks_still_rests_on_a_corner_surface)
{
    // A ledge under one corner only, the center line empty within the window: the corner line supports.
    Scene scene{
        .lines = {{}, {.boxes = {Box{-0.5f, -0.4f}}}, {}, {}, {}},
        .span  = 0.5f
    };
    scene.support = Surface_support::center_blocks;
    const Surface_offset offset = scene.snap(0.0f);
    ASSERT_TRUE(offset.found);
    EXPECT_NEAR(offset.height, -0.4f, 1.0e-6f);
    // The fallback has no surface under the center when the ledge is refused.
    scene.support = Surface_support::center_fallback;
    scene.lines[2].sheets.push_back(Sheet{.height = -0.2f, .upward = false});
    EXPECT_FALSE(scene.snap(0.0f).found);
}

TEST(Surface_support, the_property_labels_are_the_enum_names)
{
    const erhe::property::Enum_info* const info = editor::Kit_layout::surface_support_property.get().get_enum_info();
    ASSERT_NE(info, nullptr);
    ASSERT_EQ(info->get_entries().size(), 3u);
    for (const Surface_support support : {Surface_support::five_lines, Surface_support::center_blocks, Surface_support::center_fallback}) {
        EXPECT_EQ(info->label_for(static_cast<int32_t>(support)), to_string(support));
        Surface_support read{Surface_support::five_lines};
        ASSERT_TRUE(from_string(to_string(support), read));
        EXPECT_EQ(read, support);
    }
}

// S2.2: the offset names the supporting hit, lines[line].hits[hit], whose mesh is the support.
[[nodiscard]] auto support_of(
    const std::vector<std::vector<Surface_hit_point>>& hits,
    const float                                        span,
    const Surface_support                              support
) -> Surface_offset
{
    std::vector<Surface_line> lines;
    for (const std::vector<Surface_hit_point>& line_hits : hits) {
        lines.push_back(Surface_line{.hits = line_hits, .span = span});
    }
    return editor::find_support_height(lines, editor::Surface_search_window{.forward_distance = 1.0f, .backward_distance = 1.0f}, support);
}

TEST(Surface_support_hit, names_the_hit_the_base_rests_on)
{
    const std::vector<std::vector<Surface_hit_point>> hits{
        {
            Surface_hit_point{.height =  0.8f, .surface = false},
            Surface_hit_point{.height = -0.8f, .surface = true},
            Surface_hit_point{.height = -0.3f, .surface = true},
            Surface_hit_point{.height = -0.5f, .surface = false}
        }
    };
    const Surface_offset offset = support_of(hits, 0.2f, Surface_support::five_lines);
    ASSERT_TRUE(offset.found);
    EXPECT_EQ(offset.height, -0.3f);
    EXPECT_EQ(offset.line, 0u);
    EXPECT_EQ(offset.hit, 2u);
}

TEST(Surface_support_hit, the_base_center_line_comes_first_at_the_place)
{
    // A corner line's hit is nearer by less than the tolerance: the height is the corner's, the support the center's.
    const float center = -0.3f - (0.5f * editor::c_surface_snap_tolerance);
    const std::vector<std::vector<Surface_hit_point>> hits{
        {Surface_hit_point{.height = -0.9f, .surface = true}, Surface_hit_point{.height = center, .surface = true}},
        {},
        {Surface_hit_point{.height = -0.3f, .surface = true}},
        {Surface_hit_point{.height = -0.3f, .surface = true}},
        {}
    };
    for (const Surface_support support : {Surface_support::five_lines, Surface_support::center_blocks, Surface_support::center_fallback}) {
        const Surface_offset offset = support_of(hits, 0.2f, support);
        ASSERT_TRUE(offset.found);
        EXPECT_EQ(offset.height, -0.3f);
        EXPECT_EQ(offset.line, 0u);
        EXPECT_EQ(offset.hit, 1u);
    }
}

TEST(Surface_support_hit, a_corner_line_supports_where_the_center_has_no_hit_at_the_place)
{
    // The center's surface is 0.2 m lower: the first corner line resting at the place is the support.
    const std::vector<std::vector<Surface_hit_point>> hits{
        {Surface_hit_point{.height = -0.5f, .surface = true}},
        {},
        {Surface_hit_point{.height = -0.3f, .surface = true}},
        {Surface_hit_point{.height = -0.3f, .surface = true}},
        {}
    };
    const Surface_offset offset = support_of(hits, 0.2f, Surface_support::center_blocks);
    ASSERT_TRUE(offset.found);
    EXPECT_EQ(offset.height, -0.3f);
    EXPECT_EQ(offset.line, 2u);
    EXPECT_EQ(offset.hit, 0u);
}

TEST(Surface_support_hit, the_center_fallback_rests_on_the_center_line)
{
    // Under one corner a ledge 0.2 m down with a table's underside 0.1 m up: no place fits the 0.5 m piece, and the
    // fallback rests the center on the floor 0.6 m down.
    const std::vector<std::vector<Surface_hit_point>> hits{
        {Surface_hit_point{.height = -0.6f, .surface = true}},
        {Surface_hit_point{.height = 0.1f, .surface = false}, Surface_hit_point{.height = -0.2f, .surface = true}, Surface_hit_point{.height = -0.6f, .surface = true}},
        {Surface_hit_point{.height = -0.6f, .surface = true}},
        {Surface_hit_point{.height = -0.6f, .surface = true}},
        {Surface_hit_point{.height = -0.6f, .surface = true}}
    };
    EXPECT_FALSE(support_of(hits, 0.5f, Surface_support::five_lines).found);
    const Surface_offset fallback = support_of(hits, 0.5f, Surface_support::center_fallback);
    ASSERT_TRUE(fallback.found);
    EXPECT_EQ(fallback.height, -0.6f);
    EXPECT_EQ(fallback.line, 0u);
    EXPECT_EQ(fallback.hit, 0u);
}

TEST(Surface_support_hit, a_point_rest_names_its_hit)
{
    const std::vector<Surface_hit_point> hits{
        Surface_hit_point{.height = -0.7f, .surface = true},
        Surface_hit_point{.height =  0.1f, .surface = false},
        Surface_hit_point{.height = -0.4f, .surface = true}
    };
    const editor::Point_rest rest = editor::find_point_rest(
        glm::vec3{0.0f}, glm::vec3{0.0f, -1.0f, 0.0f}, Surface_line{.hits = hits, .span = 0.0f},
        editor::Surface_search_window{.forward_distance = 1.0f, .backward_distance = 1.0f}
    );
    ASSERT_TRUE(rest.found);
    EXPECT_EQ(rest.hit, 2u);
}

} // anonymous namespace
