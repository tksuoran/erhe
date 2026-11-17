#include "kit_shape_test_support.hpp"

#include "level/kit_connector.hpp"
#include "level/kit_light.hpp"
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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_extra_33_36.cpp.
namespace {

using editor::Kit_agent;
using editor::Kit_connector;
using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using namespace editor::kit_test;

[[nodiscard]] auto point_of(const GEO::Mesh& mesh, const GEO::index_t vertex) -> glm::vec3
{
    const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
    return glm::vec3{p.x, p.y, p.z};
}

// The mesh checks pass within the kit's ceiling and every slot has facets (a piece has a part for every slot).
void expect_valid_with_every_slot(const Kit_parameter_values& values)
{
    SCOPED_TRACE(values.to_canonical_text());
    const Kit_mesh_report report = editor::check_kit_shape(values);
    EXPECT_TRUE(report.is_valid()) << describe(report);
    EXPECT_LE(report.triangle_count, get_library().find_kit(values.get_shape()->kit)->triangle_ceiling);
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(*mesh, facet_slots);
    const std::set<uint32_t> used{facet_slots.begin(), facet_slots.end()};
    EXPECT_EQ(used.size(), values.get_shape()->material_slots.size());
}

// Every combination of the named parameters' range ends on the values, the others as given.
void for_each_corner(const Kit_parameter_values& values, const std::vector<std::string>& names, const auto& visit)
{
    const Kit_shape_definition& shape = *values.get_shape();
    for (std::size_t mask = 0; mask < (std::size_t{1} << names.size()); ++mask) {
        Kit_parameter_values corner = values;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const Kit_parameter& parameter = shape.parameters[shape.find_parameter(names[i]).value()];
            const bool           at_max    = ((mask >> i) & 1u) != 0u;
            if (parameter.type == editor::Kit_parameter_type::float_value) {
                ASSERT_TRUE(corner.set_float(names[i], at_max ? parameter.float_max : parameter.float_min));
            } else {
                ASSERT_TRUE(corner.set_int(names[i], at_max ? parameter.int_max : parameter.int_min));
            }
        }
        visit(corner);
    }
}

// What a mesh's topology is: its counts and every facet's slot and corners, in order.
[[nodiscard]] auto topology_of(const Kit_parameter_values& values) -> std::vector<uint32_t>
{
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(*mesh, facet_slots);
    std::vector<uint32_t> topology{mesh->vertices.nb(), mesh->facets.nb()};
    for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
        topology.push_back(mesh->facets.nb_vertices(f));
        topology.push_back(facet_slots[f]);
        for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
            topology.push_back(mesh->facets.vertex(f, c));
        }
    }
    return topology;
}

[[nodiscard]] auto enum_values_of(const Kit_shape_definition& shape, const char* const name) -> const std::vector<std::string>&
{
    return shape.parameters[shape.find_parameter(name).value()].enum_values;
}

[[nodiscard]] auto parameter_of(const Kit_shape_definition& shape, const char* const name) -> const Kit_parameter&
{
    return shape.parameters[shape.find_parameter(name).value()];
}

// R3.7: the topology at every corner of the float parameters, and at other seeds, equals the one at the given values.
void expect_topology_from_integers(const Kit_parameter_values& values, const std::vector<std::string>& floats)
{
    const std::vector<uint32_t> topology = topology_of(values);
    for_each_corner(values, floats, [&topology](const Kit_parameter_values& corner) {
        ASSERT_EQ(corner.validate(), "") << corner.to_canonical_text();
        EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
    });
    for (const int seed : {0, 2, 3, 17, 4711}) {
        Kit_parameter_values other = values;
        ASSERT_TRUE(other.set_int("seed", seed));
        EXPECT_EQ(topology_of(other), topology) << other.to_canonical_text();
    }
}

[[nodiscard]] auto connectors_of(const Kit_parameter_values& values) -> std::vector<Kit_connector>
{
    std::vector<Kit_connector> connectors;
    values.get_shape()->get_connectors(values, connectors);
    return connectors;
}

void expect_connector_names(const Kit_parameter_values& values, const std::vector<std::string>& names)
{
    const std::vector<Kit_connector> connectors = connectors_of(values);
    ASSERT_EQ(connectors.size(), names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        EXPECT_EQ(std::string{connectors[i].name}, names[i]);
        EXPECT_NEAR(glm::length(connectors[i].outward), 1.0f, 1.0e-5f);
        EXPECT_NEAR(glm::dot(connectors[i].outward, connectors[i].up), 0.0f, 1.0e-5f);
    }
}

// The values with an empty mask, so that every value is the one a test sets.
[[nodiscard]] auto explicit_enum(const Kit_shape_definition& shape, const char* const name, const std::string& value, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = explicit_values(shape, seed);
    EXPECT_TRUE(values.set_enum(name, value));
    return values;
}

// Every derivable parameter but the named enum derived from the seed.
[[nodiscard]] auto derived_enum(const Kit_shape_definition& shape, const char* const name, const std::string& value, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = all_derived(shape, seed);
    values.set_value_source(shape.find_parameter(name).value(), editor::Kit_value_source::explicit_value);
    EXPECT_TRUE(values.set_enum(name, value));
    return values;
}

// The level facets of the slot that face up: their heights.
[[nodiscard]] auto level_facet_heights(const GEO::Mesh& mesh, const uint32_t slot) -> std::vector<float>
{
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(mesh, facet_slots);
    std::vector<float> heights;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        if (facet_slots[f] != slot) {
            continue;
        }
        const glm::vec3 p0 = point_of(mesh, mesh.facets.vertex(f, 0));
        const glm::vec3 p1 = point_of(mesh, mesh.facets.vertex(f, 1));
        const glm::vec3 p2 = point_of(mesh, mesh.facets.vertex(f, 2));
        const glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        if (normal.y > 0.999f) {
            heights.push_back(p0.y);
        }
    }
    return heights;
}

// How many facets of the mesh have the slot.
[[nodiscard]] auto count_slot_facets(const GEO::Mesh& mesh, const uint32_t slot) -> std::ptrdiff_t
{
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(mesh, facet_slots);
    return std::count(facet_slots.begin(), facet_slots.end(), slot);
}

//
// Garland.
//

const std::vector<std::string> garland_floats{"span", "sag"};

class Kit_garland_items : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_garland_items, every_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& garland = get_shape("light.garland");
    ASSERT_EQ(garland.material_slots, (std::vector<std::string>{"metal", "glow", "cloth"}));
    for (const int count : {3, 4, 8, 24}) {
        Kit_parameter_values values = explicit_enum(garland, "item", GetParam(), 1);
        ASSERT_TRUE(values.set_int("count", count));
        for_each_corner(values, garland_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, garland_floats);
    }
}

// The wire, four ribbon ends, and the items: a bulb in a socket, a flag (and one lamp of three parts under them all),
// a paper lantern on a cord under a cap. The lowest item hangs on the ground plane, the wire's ends are the
// connectors, and the wire's middle hangs under them by the sag.
TEST_P(Kit_garland_items, parts_ends_and_sag_follow_the_values)
{
    const Kit_shape_definition& garland = get_shape("light.garland");
    const std::string item = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(garland, "item", item, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        span  = values.get_float("span");
        const float        sag   = values.get_float("sag") * span;
        const unsigned int count = static_cast<unsigned int>(values.get_int("count"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 5u + ((item == "bulb") ? (2u * count) : ((item == "flag") ? (count + 3u) : (3u * count))));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * span);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * span);
        expect_connector_names(values, {"start", "end"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position.x, -0.5f * span);
        EXPECT_EQ(connectors[1].position.x, 0.5f * span);
        EXPECT_EQ(connectors[0].position.y, connectors[1].position.y);
        // The wire's ends are the highest points but the ribbons' knots, and its middle lies the sag under them.
        EXPECT_NEAR(report.bounds_max.y, connectors[0].position.y, 0.02f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float middle = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (std::abs(p.x) < (0.51f * span / static_cast<float>(count + 1))) {
                middle = std::max(middle, p.y);
            }
        }
        EXPECT_LT(middle, connectors[0].position.y - (0.9f * sag) + 0.02f);
        EXPECT_GT(count_slot_facets(*mesh, 1u), 0);
        EXPECT_GT(count_slot_facets(*mesh, 2u), 0);
    }
}

// One point light at mid span, beside what hangs there, under the wire and in the open.
TEST_P(Kit_garland_items, the_light_stands_at_mid_span)
{
    const Kit_shape_definition& garland = get_shape("light.garland");
    ASSERT_TRUE(static_cast<bool>(garland.get_light_frames));
    for (const int count : {3, 8, 24}) {
        Kit_parameter_values values = explicit_enum(garland, "item", GetParam(), 1);
        ASSERT_TRUE(values.set_int("count", count));
        for_each_corner(values, garland_floats, [](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            std::vector<editor::Kit_light_frame> frames;
            editor::get_shape_light_frames(corner, frames);
            ASSERT_EQ(frames.size(), 1u);
            EXPECT_EQ(editor::check_light_frames(frames), "");
            EXPECT_EQ(std::string{frames[0].name}, "light");
            EXPECT_EQ(frames[0].start.type, editor::Kit_light_type::point);
            EXPECT_EQ(frames[0].position.x, 0.0f);
            EXPECT_GT(frames[0].position.y, 0.0f);
            EXPECT_LT(frames[0].position.y, connectors_of(corner)[0].position.y);
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            float nearest = 10.0f;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                nearest = std::min(nearest, glm::distance(point_of(*mesh, v), frames[0].position));
            }
            EXPECT_GT(nearest, 0.05f);
        });
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_garland, Kit_garland_items, testing::Values("bulb", "flag", "paper_lantern"));

//
// Rope bridge.
//

TEST(Kit_rope_bridge, passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& bridge = get_shape("traversal.rope_bridge");
    ASSERT_EQ(bridge.material_slots, (std::vector<std::string>{"wood", "cloth"}));
    for (const int seed : sweep_seeds(bridge)) {
        for_each_corner(explicit_values(bridge, seed), {"span", "width", "sag", "plank_gap", "missing", "rail_height"}, expect_valid_with_every_slot);
    }
}

// The plank count is the one integer taken from lengths; the other floats and the seed only move vertices.
TEST(Kit_rope_bridge, topology_follows_from_the_plank_count_alone)
{
    const Kit_shape_definition& bridge = get_shape("traversal.rope_bridge");
    expect_topology_from_integers(explicit_values(bridge, 1), {"width", "sag", "missing", "rail_height"});
}

// A plank to every plank and gap of the span, four ropes, two hangers at every fourth plank and four posts. The
// lowest point lies on the ground plane under mid span, the deck's ends are the connectors and lie the sag over its
// middle, and the default agent walks it at every seed.
TEST(Kit_rope_bridge, parts_ends_and_walkability_follow_the_values)
{
    const Kit_shape_definition& bridge = get_shape("traversal.rope_bridge");
    const Kit_agent agent{};
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(bridge, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        span   = values.get_float("span");
        const float        sag    = values.get_float("sag") * span;
        const unsigned int planks = std::clamp(static_cast<unsigned int>(std::lround(span / (0.2f + values.get_float("plank_gap")))), 4u, 64u);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, planks + 8u + (2u * ((planks + 1u) / 4u)));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * span);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * span);
        expect_connector_names(values, {"start", "end"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position.y, connectors[1].position.y);
        // The planks' tops: the highest lie at the ends by the connectors, the lowest the sag under them.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const float deck_end = connectors[0].position.y;
        EXPECT_GT(deck_end, sag);
        EXPECT_LT(deck_end, sag + 0.15f);
        EXPECT_NEAR(report.bounds_max.y, deck_end + values.get_float("rail_height") + 0.1f, 1.0e-5f);
        EXPECT_GT(count_slot_facets(*mesh, 1u), 0);
        EXPECT_EQ(bridge.check_walkability(values, agent), "");
    }
}

// An agent too wide for the hand ropes, one that cannot climb the ends, and one whose foot falls through a loose
// plank's gap are refused.
TEST(Kit_rope_bridge, walkability_bounds_the_width_the_slope_and_the_gap)
{
    const Kit_shape_definition& bridge = get_shape("traversal.rope_bridge");
    Kit_parameter_values values = explicit_values(bridge, 1);
    ASSERT_TRUE(values.set_float("sag", parameter_of(bridge, "sag").float_max));
    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.6f;
    Kit_agent weak{};
    weak.max_slope_degrees = 15.0f;
    Kit_agent small{};
    small.radius = 0.2f;
    EXPECT_EQ(bridge.check_walkability(values, agent), "");
    EXPECT_NE(bridge.check_walkability(values, wide), "");
    EXPECT_NE(bridge.check_walkability(values, weak), "");
    EXPECT_NE(bridge.check_walkability(values, small), "");
    ASSERT_TRUE(values.set_float("missing", 0.0f));
    EXPECT_EQ(bridge.check_walkability(values, small), "");
}

// With planks missing, some lie on the next plank: over the deck.
TEST(Kit_rope_bridge, loose_planks_lie_on_the_next)
{
    const Kit_shape_definition& bridge = get_shape("traversal.rope_bridge");
    Kit_parameter_values whole = explicit_values(bridge, 5);
    ASSERT_TRUE(whole.set_float("missing", 0.0f));
    Kit_parameter_values loose = whole;
    ASSERT_TRUE(loose.set_float("missing", 0.15f));
    EXPECT_EQ(topology_of(whole), topology_of(loose));
    EXPECT_FALSE(editor::kit_meshes_equal(*generate(whole), *generate(loose)));
}

//
// Cracked floor.
//

const std::vector<std::string> cracked_floats{"width", "depth", "thickness", "tilt", "missing"};

TEST(Kit_cracked_floor, every_fragment_count_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& floor = get_shape("ruin.cracked_floor");
    ASSERT_EQ(floor.material_slots, (std::vector<std::string>{"floor"}));
    for (const int seed : sweep_seeds(floor)) {
        for (const int fragments : {3, 4, 5, 9, 24}) {
            Kit_parameter_values values = explicit_values(floor, seed);
            ASSERT_TRUE(values.set_int("fragments", fragments));
            for_each_corner(values, cracked_floats, expect_valid_with_every_slot);
        }
    }
}

TEST(Kit_cracked_floor, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& floor = get_shape("ruin.cracked_floor");
    for (const int fragments : {3, 9, 24}) {
        Kit_parameter_values values = explicit_values(floor, 1);
        ASSERT_TRUE(values.set_int("fragments", fragments));
        expect_topology_from_integers(values, cracked_floats);
    }
}

// A prism to a fragment within the slab's bounds, its sides the slab's; no top lies over the slab's top, and the
// default agent walks it at every seed.
TEST(Kit_cracked_floor, fragments_fill_the_slab_under_its_top)
{
    const Kit_shape_definition& floor = get_shape("ruin.cracked_floor");
    const Kit_agent agent{};
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(floor, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        half_w    = 0.5f * values.get_float("width");
        const float        half_d    = 0.5f * values.get_float("depth");
        const float        thickness = values.get_float("thickness");
        const unsigned int fragments = static_cast<unsigned int>(values.get_int("fragments"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, fragments);
        EXPECT_EQ(report.triangle_count, 12u * fragments);
        EXPECT_EQ(report.bounds_min, (glm::vec3{-half_w, 0.0f, -half_d}));
        EXPECT_EQ(report.bounds_max.x, half_w);
        EXPECT_EQ(report.bounds_max.z, half_d);
        EXPECT_LE(report.bounds_max.y, thickness);
        EXPECT_GT(report.bounds_max.y, 0.5f * thickness);
        expect_connector_names(values, {"base", "top", "front", "back", "left", "right"});
        EXPECT_EQ(connectors_of(values)[1].position.y, thickness);
        EXPECT_EQ(floor.check_walkability(values, agent), "");
    }
}

// Without a tilt and without missing fragments every top is level at most 2 cm under the slab's top; a missing
// fragment lies deeper, and an agent that cannot climb out of it is refused.
TEST(Kit_cracked_floor, tilt_and_missing_lower_the_fragments)
{
    const Kit_shape_definition& floor = get_shape("ruin.cracked_floor");
    Kit_parameter_values values = explicit_values(floor, 3);
    ASSERT_TRUE(values.set_float("tilt", 0.0f));
    ASSERT_TRUE(values.set_float("missing", 0.0f));
    const float thickness = values.get_float("thickness");
    const std::vector<float> level = level_facet_heights(*generate(values), 0);
    ASSERT_EQ(level.size(), static_cast<std::size_t>(values.get_int("fragments")));
    for (const float top : level) {
        EXPECT_LE(top, thickness);
        EXPECT_GE(top, thickness - 0.02f - 1.0e-6f);
    }
    ASSERT_TRUE(values.set_float("missing", 0.2f));
    float lowest = thickness;
    for (int seed = 0; seed < 8; ++seed) {
        ASSERT_TRUE(values.set_int("seed", seed));
        for (const float top : level_facet_heights(*generate(values), 0)) {
            lowest = std::min(lowest, top);
        }
    }
    EXPECT_NEAR(lowest, 0.15f * thickness, 1.0e-6f);
    Kit_agent weak{};
    weak.max_climb = 0.1f;
    EXPECT_NE(floor.check_walkability(values, weak), "");
    EXPECT_EQ(floor.check_walkability(values, Kit_agent{}), "");
}

// Replace carries what two shapes name alike: the cracked floor has every parameter of the floor slab.
TEST(Kit_cracked_floor, takes_the_slabs_parameters)
{
    const Kit_shape_definition& slab  = get_shape("basic.floor_slab");
    const Kit_shape_definition& floor = get_shape("ruin.cracked_floor");
    for (const Kit_parameter& parameter : slab.parameters) {
        SCOPED_TRACE(parameter.name);
        ASSERT_TRUE(floor.find_parameter(parameter.name).has_value());
        const Kit_parameter& other = parameter_of(floor, parameter.name.c_str());
        EXPECT_EQ(other.type, parameter.type);
        EXPECT_EQ(other.default_scale, parameter.default_scale);
        EXPECT_EQ(other.float_default, parameter.float_default);
        EXPECT_EQ(other.float_min, parameter.float_min);
        EXPECT_EQ(other.float_max, parameter.float_max);
    }
}

//
// Boat.
//

const std::vector<std::string> boat_floats{"length", "beam", "sheer", "heel"};

class Kit_boat_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_boat_forms, every_thwart_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& boat = get_shape("harbour.boat");
    ASSERT_EQ(boat.material_slots, (std::vector<std::string>{"wood", "trim"}));
    for (int thwarts = 0; thwarts <= 4; ++thwarts) {
        Kit_parameter_values values = explicit_enum(boat, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("thwarts", thwarts));
        for_each_corner(values, boat_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, boat_floats);
    }
}

// The hull (one part of two skins under a rim), its two end boards, the thwarts and two sleepers. The hull is the length long and about
// the beam wide, and stands on its sleepers on the ground.
TEST_P(Kit_boat_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& boat = get_shape("harbour.boat");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(boat, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        length  = values.get_float("length");
        const float        beam    = values.get_float("beam");
        const unsigned int thwarts = static_cast<unsigned int>(values.get_int("thwarts"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 5u + thwarts);
        // Ten bands of eight strakes in two skins, the rim around them, the two end boards (of nine corners in a
        // round hull, of four in a flat one) and the boxes.
        const unsigned int board = (GetParam() == "rowboat") ? 32u : 12u;
        EXPECT_EQ(report.triangle_count, 320u + 72u + (2u * board) + (12u * (thwarts + 2u)));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * length);
        EXPECT_GE(report.bounds_max.z - report.bounds_min.z, 0.84f * beam);
        EXPECT_LE(report.bounds_max.z - report.bounds_min.z, 1.3f * beam);
        EXPECT_GT(report.bounds_max.y, 0.3f * beam);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_boat, Kit_boat_forms, testing::Values("rowboat", "skiff", "barge"));

// The heel lays the boat over toward +Z: its gunwale at +Z comes down and the one at -Z goes up.
TEST(Kit_boat, the_heel_lays_the_boat_over)
{
    const Kit_shape_definition& boat = get_shape("harbour.boat");
    for (const std::string& form : enum_values_of(boat, "form")) {
        Kit_parameter_values level = explicit_enum(boat, "form", form, 1);
        ASSERT_TRUE(level.set_float("heel", 0.0f));
        Kit_parameter_values heeled = level;
        ASSERT_TRUE(heeled.set_float("heel", 25.0f));
        const Kit_mesh_report upright = editor::check_kit_shape(level);
        const Kit_mesh_report over    = editor::check_kit_shape(heeled);
        EXPECT_NEAR(upright.bounds_max.z, -upright.bounds_min.z, 1.0e-5f) << form;
        EXPECT_GT(over.bounds_max.y, upright.bounds_max.y) << form;
        EXPECT_EQ(topology_of(level), topology_of(heeled)) << form;
    }
}

} // anonymous namespace
