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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_build_21_24.cpp.
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

// The heights of the level facets of the slot that face up.
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

// The values of the form with an empty mask, so that every value is the one a test sets.
[[nodiscard]] auto explicit_form(const Kit_shape_definition& shape, const std::string& form, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = explicit_values(shape, seed);
    EXPECT_TRUE(values.set_enum("form", form));
    return values;
}

// Every derivable parameter but the form derived from the seed.
[[nodiscard]] auto derived_form(const Kit_shape_definition& shape, const std::string& form, const int seed) -> Kit_parameter_values
{
    Kit_parameter_values values = all_derived(shape, seed);
    values.set_value_source(shape.find_parameter("form").value(), editor::Kit_value_source::explicit_value);
    EXPECT_TRUE(values.set_enum("form", form));
    return values;
}

//
// Bridge deck.
//

const std::vector<std::string> bridge_floats{"length", "width", "thickness", "height", "parapet_height"};

class Kit_bridge_deck_supports : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_bridge_deck_supports, every_parapet_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& deck = get_shape("architecture.bridge_deck");
    ASSERT_EQ(deck.material_slots, (std::vector<std::string>{"stone", "floor", "trim"}));
    for (const std::string& parapet : enum_values_of(deck, "parapet")) {
        for (const int spans : {1, 4}) {
            for (const int courses : {1, 16}) {
                Kit_parameter_values values = defaults(deck);
                ASSERT_TRUE(values.set_enum("supports", GetParam()));
                ASSERT_TRUE(values.set_enum("parapet", parapet));
                ASSERT_TRUE(values.set_int("spans", spans));
                ASSERT_TRUE(values.set_int("courses", courses));
                for_each_corner(values, bridge_floats, expect_valid_with_every_slot);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_bridge_deck, Kit_bridge_deck_supports, testing::Values("none", "piers", "arched"));

TEST(Kit_bridge_deck, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& deck = get_shape("architecture.bridge_deck");
    for (const std::string& supports : enum_values_of(deck, "supports")) {
        for (const std::string& parapet : enum_values_of(deck, "parapet")) {
            Kit_parameter_values values = defaults(deck);
            ASSERT_TRUE(values.set_enum("supports", supports));
            ASSERT_TRUE(values.set_enum("parapet", parapet));
            ASSERT_TRUE(values.set_int("spans", 2));
            expect_topology_from_integers(values, bridge_floats);
        }
    }
}

// A slab, the paving stones (two and three to a course in turn from both ends), the two sides and the supports.
TEST(Kit_bridge_deck, parts_follow_the_counts)
{
    const Kit_shape_definition& deck = get_shape("architecture.bridge_deck");
    for (const int spans : {1, 2, 3, 4}) {
        for (const int courses : {1, 2, 7, 8, 16}) {
            Kit_parameter_values values = defaults(deck);
            ASSERT_TRUE(values.set_int("spans", spans));
            ASSERT_TRUE(values.set_int("courses", courses));
            unsigned int stones = 0;
            for (int k = 0; k < courses; ++k) {
                stones += ((std::min(k, courses - 1 - k) % 2) == 1) ? 3u : 2u;
            }
            const unsigned int supports  = static_cast<unsigned int>(spans) + 1u;
            const unsigned int bays      = static_cast<unsigned int>(spans);
            const unsigned int balusters = (spans <= 2) ? 3u : 2u;
            const unsigned int side_none  = 2u;
            const unsigned int side_solid = 2u * (1u + (2u * supports) + bays);
            const unsigned int side_open  = 2u * (1u + (2u * supports) + (bays * (2u + balusters)));
            const unsigned int on_beams   = 2u + supports;
            const unsigned int on_piers   = 2u + (3u * supports);
            const unsigned int on_arches  = (3u * supports) + (3u * bays);
            ASSERT_TRUE(values.set_enum("supports", "none"));
            ASSERT_TRUE(values.set_enum("parapet", "none"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 1u + stones + side_none + on_beams);
            ASSERT_TRUE(values.set_enum("supports", "piers"));
            ASSERT_TRUE(values.set_enum("parapet", "solid"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 1u + stones + side_solid + on_piers);
            ASSERT_TRUE(values.set_enum("supports", "arched"));
            ASSERT_TRUE(values.set_enum("parapet", "open"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 1u + stones + side_open + on_arches);
        }
    }
}

// The bounds are the length by the width from the ground, the paving stones' tops are one level walking surface at the
// height of the end connectors, and the agent walks between the parapets.
TEST(Kit_bridge_deck, bounds_ends_and_walking_surface_follow_the_values)
{
    const Kit_shape_definition& deck = get_shape("architecture.bridge_deck");
    const Kit_agent agent{};
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(deck, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float half_l    = 0.5f * values.get_float("length");
        const float half_w    = 0.5f * values.get_float("width");
        const float thickness = values.get_float("thickness");
        const bool  lies      = (values.get_enum("supports") == "none");
        const float top       = lies ? (std::clamp(0.8f * thickness, 0.1f, 0.5f) + thickness) : values.get_float("height");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -half_l);
        EXPECT_FLOAT_EQ(report.bounds_max.x, half_l);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -half_w);
        EXPECT_FLOAT_EQ(report.bounds_max.z, half_w);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 3u);
        EXPECT_STREQ(connectors[0].name, "start");
        EXPECT_STREQ(connectors[1].name, "end");
        EXPECT_STREQ(connectors[2].name, "base");
        EXPECT_FLOAT_EQ(connectors[0].position.x, -half_l);
        EXPECT_FLOAT_EQ(connectors[1].position.x, half_l);
        EXPECT_NEAR(connectors[0].position.y, top, 1.0e-5f);
        EXPECT_EQ(connectors[0].position.y, connectors[1].position.y);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> heights = level_facet_heights(*mesh, 1);
        EXPECT_GE(heights.size(), 2u * static_cast<std::size_t>(values.get_int("courses")));
        for (const float height : heights) {
            EXPECT_EQ(height, connectors[0].position.y);
        }
        EXPECT_EQ(deck.check_walkability(values, agent), "");
    }
}

// The narrowest deck is walked with every parapet; an agent too wide for its parapets is refused.
TEST(Kit_bridge_deck, walkability_follows_the_clear_width)
{
    const Kit_shape_definition& deck = get_shape("architecture.bridge_deck");
    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.5f;
    for (const std::string& parapet : enum_values_of(deck, "parapet")) {
        Kit_parameter_values values = defaults(deck);
        ASSERT_TRUE(values.set_enum("parapet", parapet));
        ASSERT_TRUE(values.set_float("width", deck.parameters[deck.find_parameter("width").value()].float_min));
        EXPECT_EQ(deck.check_walkability(values, agent), "") << parapet;
        EXPECT_EQ(deck.check_walkability(values, wide).empty(), parapet == "none") << parapet;
    }
}

//
// Buttress.
//

const std::vector<std::string> buttress_floats{"height", "depth", "width"};

TEST(Kit_buttress, every_cap_and_step_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& buttress = get_shape("architecture.buttress");
    ASSERT_EQ(buttress.material_slots, (std::vector<std::string>{"stone", "trim"}));
    for (const std::string& cap : enum_values_of(buttress, "cap")) {
        for (int steps = 1; steps <= 4; ++steps) {
            Kit_parameter_values values = defaults(buttress);
            ASSERT_TRUE(values.set_enum("cap", cap));
            ASSERT_TRUE(values.set_int("steps", steps));
            for_each_corner(values, buttress_floats, expect_valid_with_every_slot);
        }
    }
}

TEST(Kit_buttress, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& buttress = get_shape("architecture.buttress");
    for (const std::string& cap : enum_values_of(buttress, "cap")) {
        for (int steps = 1; steps <= 4; ++steps) {
            Kit_parameter_values values = defaults(buttress);
            ASSERT_TRUE(values.set_enum("cap", cap));
            ASSERT_TRUE(values.set_int("steps", steps));
            expect_topology_from_integers(values, buttress_floats);
        }
    }
}

// The bounds are the width by the height by the depth, the back is the wall face, and the parts are the plinth, the
// stages, a drip course under every offset and the cap's.
TEST(Kit_buttress, bounds_back_and_parts_follow_the_values)
{
    const Kit_shape_definition& buttress = get_shape("architecture.buttress");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(buttress, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        height = values.get_float("height");
        const float        depth  = values.get_float("depth");
        const float        width  = values.get_float("width");
        const unsigned int steps  = static_cast<unsigned int>(values.get_int("steps"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * depth);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * depth);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        const std::string_view cap = values.get_enum("cap");
        const unsigned int cap_parts = (cap == "weathered") ? 1u : ((cap == "gabled") ? 2u : 4u);
        EXPECT_EQ(report.component_count, 1u + steps + (steps - 1u) + cap_parts);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 2u);
        EXPECT_STREQ(connectors[0].name, "back");
        EXPECT_STREQ(connectors[1].name, "base");
        EXPECT_EQ(connectors[0].position.z, -0.5f * depth);
        EXPECT_EQ(connectors[0].outward.z, -1.0f);
    }
}

//
// Log.
//

const std::vector<std::string> log_floats{"length", "width", "bend"};

class Kit_log_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_log_forms, passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& log = get_shape("nature.log");
    ASSERT_EQ(log.material_slots, (std::vector<std::string>{"bark", "sapwood", "heartwood"}));
    for (const int seed : sweep_seeds(log)) {
        for (const int stubs : {0, 4}) {
            for (const int segments : {6, 9, 14}) {
                Kit_parameter_values values = explicit_form(log, GetParam(), seed);
                ASSERT_TRUE(values.set_int("stubs", stubs));
                ASSERT_TRUE(values.set_int("segments", segments));
                for_each_corner(values, log_floats, expect_valid_with_every_slot);
            }
        }
    }
}

// The trunk is one part and every stub one more; it lies or stands on the ground under its top connector.
TEST_P(Kit_log_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& log = get_shape("nature.log");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_form(log, GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float length = values.get_float("length");
        const float width  = values.get_float("width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 1u + static_cast<unsigned int>(values.get_int("stubs")));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 2u);
        EXPECT_STREQ(connectors[0].name, "base");
        EXPECT_STREQ(connectors[1].name, "top");
        if (GetParam() == "stump") {
            EXPECT_FLOAT_EQ(connectors[1].position.y, 0.4f * length);
            EXPECT_GE(report.bounds_max.y, 0.4f * length);
            EXPECT_GE(report.bounds_max.x - report.bounds_min.x, width);
        } else {
            EXPECT_GT(connectors[1].position.y, 0.9f * width);
            EXPECT_LE(connectors[1].position.y, width);
            EXPECT_GE(report.bounds_max.x - report.bounds_min.x, length);
            EXPECT_LE(report.bounds_max.x - report.bounds_min.x, (1.25f * length) + (3.0f * width));
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_log, Kit_log_forms, testing::Values("log", "stump", "driftwood"));

TEST(Kit_log, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& log = get_shape("nature.log");
    for (const std::string& form : enum_values_of(log, "form")) {
        for (const int stubs : {0, 3}) {
            Kit_parameter_values values = explicit_form(log, form, 1);
            ASSERT_TRUE(values.set_int("stubs", stubs));
            expect_topology_from_integers(values, log_floats);
        }
    }
}

//
// Shell.
//

class Kit_shell_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_shell_forms, every_rib_count_passes_at_the_size_ends_over_seeds_with_every_slot)
{
    const Kit_shape_definition& shell = get_shape("nature.shell");
    ASSERT_EQ(shell.material_slots, (std::vector<std::string>{"shell", "coral"}));
    EXPECT_EQ(shell.collision, editor::Kit_collision::none);
    const Kit_parameter& size = shell.parameters[shell.find_parameter("size").value()];
    for (const int seed : sweep_seeds(shell)) {
        for (int ribs = 5; ribs <= 12; ++ribs) {
            for (const float value : {size.float_min, 0.3f, size.float_max}) {
                Kit_parameter_values values = explicit_form(shell, GetParam(), seed);
                ASSERT_TRUE(values.set_int("ribs", ribs));
                ASSERT_TRUE(values.set_float("size", value));
                expect_valid_with_every_slot(values);
                // It lies on the ground about its origin, about its size wide and low.
                const Kit_mesh_report report = editor::check_kit_shape(values);
                const glm::vec3 extent = report.bounds_max - report.bounds_min;
                EXPECT_EQ(report.bounds_min.y, 0.0f);
                EXPECT_GE(std::max(extent.x, extent.z), 0.6f * value);
                EXPECT_LE(std::max(extent.x, extent.z), 1.15f * value);
                EXPECT_LE(extent.y, 0.6f * value);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_shell, Kit_shell_forms, testing::Values("conch", "scallop", "starfish"));

TEST(Kit_shell, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& shell = get_shape("nature.shell");
    for (const std::string& form : enum_values_of(shell, "form")) {
        for (const int ribs : {5, 8, 12}) {
            Kit_parameter_values values = explicit_form(shell, form, 1);
            ASSERT_TRUE(values.set_int("ribs", ribs));
            expect_topology_from_integers(values, {"size"});
        }
    }
}

// A conch and a scallop are one part; a starfish has three knobs to an arm and one on its middle.
TEST(Kit_shell, parts_follow_the_ribs)
{
    const Kit_shape_definition& shell = get_shape("nature.shell");
    for (int ribs = 5; ribs <= 12; ++ribs) {
        Kit_parameter_values values = explicit_values(shell, 3);
        ASSERT_TRUE(values.set_int("ribs", ribs));
        const unsigned int count = static_cast<unsigned int>(ribs);
        ASSERT_TRUE(values.set_enum("form", "conch"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, 1u);
        EXPECT_EQ(editor::check_kit_shape(values).triangle_count, 40u * count);
        ASSERT_TRUE(values.set_enum("form", "scallop"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, 1u);
        ASSERT_TRUE(values.set_enum("form", "starfish"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, 2u + (3u * count));
        EXPECT_EQ(editor::check_kit_shape(values).triangle_count, (32u * count) + (6u * (1u + (3u * count))));
    }
}

//
// Blocky tree.
//

const std::vector<std::string> blocky_floats{"trunk_height", "canopy_size"};

class Kit_blocky_tree_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_blocky_tree_forms, every_block_count_passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& tree = get_shape("nature.blocky_tree");
    ASSERT_EQ(tree.material_slots, (std::vector<std::string>{"bark", "foliage", "shoot"}));
    for (const int seed : sweep_seeds(tree)) {
        for (int blocks = 1; blocks <= 4; ++blocks) {
            Kit_parameter_values values = explicit_form(tree, GetParam(), seed);
            ASSERT_TRUE(values.set_int("blocks", blocks));
            for_each_corner(values, blocky_floats, expect_valid_with_every_slot);
        }
    }
}

// Every part is a box: the root block, the trunk and a root, then four to a canopy block, and a branch to every block
// of a cluster but the first. The canopy stands over the trunk's height, about its size wide.
TEST_P(Kit_blocky_tree_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& tree = get_shape("nature.blocky_tree");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_form(tree, GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int blocks       = static_cast<unsigned int>(values.get_int("blocks"));
        const float        trunk_height = values.get_float("trunk_height");
        const float        canopy       = values.get_float("canopy_size");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        const unsigned int branches = (GetParam() == "cluster") ? (blocks - 1u) : 0u;
        EXPECT_EQ(report.component_count, 3u + (4u * blocks) + branches);
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        const glm::vec3 extent = report.bounds_max - report.bounds_min;
        EXPECT_GE(report.bounds_max.y, trunk_height + (0.5f * canopy));
        EXPECT_GE(std::max(extent.x, extent.z), 0.55f * canopy);
        EXPECT_LE(std::max(extent.x, extent.z), 1.7f * canopy);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_blocky_tree, Kit_blocky_tree_forms, testing::Values("cube", "tiers", "cluster"));

TEST(Kit_blocky_tree, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& tree = get_shape("nature.blocky_tree");
    for (const std::string& form : enum_values_of(tree, "form")) {
        for (int blocks = 1; blocks <= 4; ++blocks) {
            Kit_parameter_values values = explicit_form(tree, form, 1);
            ASSERT_TRUE(values.set_int("blocks", blocks));
            expect_topology_from_integers(values, blocky_floats);
        }
    }
}

//
// Stone lantern.
//

const std::vector<std::string> lantern_floats{"height", "width", "cap_curve"};

TEST(Kit_stone_lantern, every_cap_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& lantern = get_shape("architecture.stone_lantern");
    ASSERT_EQ(lantern.material_slots, (std::vector<std::string>{"stone", "glow"}));
    for (const std::string& cap : enum_values_of(lantern, "cap")) {
        Kit_parameter_values values = defaults(lantern);
        ASSERT_TRUE(values.set_enum("cap", cap));
        for_each_corner(values, lantern_floats, expect_valid_with_every_slot);
    }
}

TEST(Kit_stone_lantern, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& lantern = get_shape("architecture.stone_lantern");
    for (const std::string& cap : enum_values_of(lantern, "cap")) {
        Kit_parameter_values values = defaults(lantern);
        ASSERT_TRUE(values.set_enum("cap", cap));
        expect_topology_from_integers(values, lantern_floats);
    }
}

// The bounds are the width by the height, the parts are the pedestal, the shaft, the platform, the light box's sill,
// head, core and a panel and a post to a side, the cap with a ridge to a corner, the collar and the jewel, and the glowing core is a prism.
TEST(Kit_stone_lantern, bounds_parts_and_core_follow_the_values)
{
    const Kit_shape_definition& lantern = get_shape("architecture.stone_lantern");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(lantern, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        height = values.get_float("height");
        const float        width  = values.get_float("width");
        const unsigned int sides  = (values.get_enum("cap") == "square") ? 4u : 6u;
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.z, -report.bounds_min.z);
        EXPECT_LE(report.bounds_max.z, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_EQ(report.component_count, 9u + (3u * sides));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        // The flame core: two fans and two bands of quads.
        EXPECT_EQ(std::count(facet_slots.begin(), facet_slots.end(), 1u), static_cast<std::ptrdiff_t>(4u * sides));
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 1u);
        EXPECT_STREQ(connectors[0].name, "base");
    }
}

// One point light at the middle of the light box, over the flame core at every value: no geometry encloses it.
TEST(Kit_stone_lantern, the_light_frame_sits_over_the_glowing_core_inside_the_light_box)
{
    const Kit_shape_definition& lantern = get_shape("architecture.stone_lantern");
    ASSERT_TRUE(static_cast<bool>(lantern.get_light_frames));
    for (const std::string& cap : enum_values_of(lantern, "cap")) {
        Kit_parameter_values values = defaults(lantern);
        ASSERT_TRUE(values.set_enum("cap", cap));
        for_each_corner(values, lantern_floats, [](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            std::vector<editor::Kit_light_frame> frames;
            editor::get_shape_light_frames(corner, frames);
            ASSERT_EQ(frames.size(), 1u);
            EXPECT_EQ(editor::check_light_frames(frames), "");
            EXPECT_STREQ(frames[0].name, "light");
            EXPECT_EQ(frames[0].start.type, editor::Kit_light_type::point);
            EXPECT_EQ(frames[0].position.x, 0.0f);
            EXPECT_EQ(frames[0].position.z, 0.0f);
            // The core's facets (slot 1) lie under the frame, so no geometry encloses the light (shadow casting on).
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            float low  = corner.get_float("height");
            float high = 0.0f;
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                if (facet_slots[f] != 1u) {
                    continue;
                }
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    const float y = point_of(*mesh, mesh->facets.vertex(f, c)).y;
                    low  = std::min(low, y);
                    high = std::max(high, y);
                }
            }
            const float height = corner.get_float("height");
            EXPECT_GT(high, low);
            EXPECT_GT(frames[0].position.y, high + (0.02f * height));
            // Inside the light box: over the sill's top and under the head's underside, the window band's middle.
            EXPECT_GT(frames[0].position.y, 0.606f * height);
            EXPECT_LT(frames[0].position.y, 0.739f * height);
            EXPECT_NEAR(frames[0].position.y, 0.6725f * height, 1.0e-4f * height);
        });
    }
}

// The cap's corners rise with cap_curve; its sides' middles stay.
TEST(Kit_stone_lantern, the_caps_corners_turn_up_with_the_curve)
{
    const Kit_shape_definition& lantern = get_shape("architecture.stone_lantern");
    for (const std::string& cap : enum_values_of(lantern, "cap")) {
        SCOPED_TRACE(cap);
        float previous = 0.0f;
        for (const float curve : {0.0f, 0.5f, 1.0f}) {
            Kit_parameter_values values = defaults(lantern);
            ASSERT_TRUE(values.set_enum("cap", cap));
            ASSERT_TRUE(values.set_float("cap_curve", curve));
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            // The highest vertex on the cap's corner at +X (a hexagonal cap) or +X +Z (a square one).
            const float half = 0.5f * values.get_float("width");
            float corner_top = 0.0f;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const glm::vec3 p = point_of(*mesh, v);
                const bool on_corner = (cap == "square") ? ((p.x == half) && (p.z == half)) : ((p.x == half) && (p.z == 0.0f));
                if (on_corner) {
                    corner_top = std::max(corner_top, p.y);
                }
            }
            EXPECT_GT(corner_top, previous);
            previous = corner_top;
        }
    }
}

} // anonymous namespace
