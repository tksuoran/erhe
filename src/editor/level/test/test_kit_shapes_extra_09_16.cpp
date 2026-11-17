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
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_extra_09_16.cpp.
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

//
// Quay wall.
//

const std::vector<std::string> quay_floats{"length", "height", "thickness", "batter"};

TEST(Kit_quay_wall, every_fender_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& quay = get_shape("harbour.quay_wall");
    ASSERT_EQ(quay.material_slots, (std::vector<std::string>{"stone", "wood"}));
    for (int fenders = 0; fenders <= 4; ++fenders) {
        Kit_parameter_values values = explicit_values(quay, 1);
        ASSERT_TRUE(values.set_int("fenders", fenders));
        for_each_corner(values, quay_floats, expect_valid_with_every_slot);
    }
}

TEST(Kit_quay_wall, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& quay = get_shape("harbour.quay_wall");
    for (const int fenders : {0, 2, 4}) {
        Kit_parameter_values values = explicit_values(quay, 1);
        ASSERT_TRUE(values.set_int("fenders", fenders));
        expect_topology_from_integers(values, quay_floats);
    }
}

// The wall, its plinth, its coping and the kerb, and a box to a fender. The land face is upright at half the
// thickness, the foot of the water face stands out by the batter, and the coping's top is the walking surface.
TEST(Kit_quay_wall, parts_bounds_and_walking_surface_follow_the_values)
{
    const Kit_shape_definition& quay = get_shape("harbour.quay_wall");
    const Kit_agent agent{};
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(quay, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        length    = values.get_float("length");
        const float        height    = values.get_float("height");
        const float        thickness = values.get_float("thickness");
        const float        foot      = (0.5f * thickness) + (height * std::tan(glm::radians(values.get_float("batter"))));
        const unsigned int fenders   = static_cast<unsigned int>(values.get_int("fenders"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 4u + fenders);
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * thickness);
        EXPECT_GE(report.bounds_max.z, foot + 0.06f - 1.0e-5f);
        EXPECT_LE(report.bounds_max.z, foot + 0.13f + 1.0e-5f);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_GT(report.bounds_max.y, height);
        EXPECT_LE(report.bounds_max.y, height + 0.12f + 1.0e-5f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> tops = level_facet_heights(*mesh, 0);
        ASSERT_FALSE(tops.empty());
        EXPECT_FLOAT_EQ(*std::max_element(tops.begin(), tops.end()), height);
        expect_connector_names(values, {"start", "end", "base", "top"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position, (glm::vec3{-0.5f * length, height, 0.0f}));
        EXPECT_EQ(connectors[1].position, (glm::vec3{0.5f * length, height, 0.0f}));
        EXPECT_EQ(connectors[3].position, (glm::vec3{0.0f, height, 0.0f}));
        EXPECT_EQ(quay.check_walkability(values, agent), "");
    }
}

TEST(Kit_quay_wall, the_narrowest_top_is_walked_and_refuses_a_wide_agent)
{
    const Kit_shape_definition& quay = get_shape("harbour.quay_wall");
    Kit_parameter_values values = explicit_values(quay, 1);
    ASSERT_TRUE(values.set_float("thickness", parameter_of(quay, "thickness").float_min));
    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.5f;
    EXPECT_EQ(quay.check_walkability(values, agent), "");
    EXPECT_NE(quay.check_walkability(values, wide), "");
}

//
// Pipe.
//

const std::vector<std::string> pipe_floats{"diameter", "length", "bend_angle"};

class Kit_pipe_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_pipe_forms, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& pipe = get_shape("industrial.pipe");
    ASSERT_EQ(pipe.material_slots, (std::vector<std::string>{"metal"}));
    for (const std::string& flanges : enum_values_of(pipe, "flanges")) {
        for (const int segments : {6, 9, 16}) {
            Kit_parameter_values values = explicit_enum(pipe, "form", GetParam(), 1);
            ASSERT_TRUE(values.set_enum("flanges", flanges));
            ASSERT_TRUE(values.set_int("segments", segments));
            for_each_corner(values, pipe_floats, expect_valid_with_every_slot);
        }
    }
}

TEST_P(Kit_pipe_forms, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& pipe = get_shape("industrial.pipe");
    for (const std::string& flanges : enum_values_of(pipe, "flanges")) {
        Kit_parameter_values values = explicit_enum(pipe, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_enum("flanges", flanges));
        expect_topology_from_integers(values, pipe_floats);
    }
}

// One tube, and a second for a tee's branch; it lies on the ground and reaches from its start connector to its end
// connector, which faces along the pipe's last leg.
TEST_P(Kit_pipe_forms, parts_and_ends_follow_the_values)
{
    const Kit_shape_definition& pipe = get_shape("industrial.pipe");
    const std::string form = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(pipe, "form", form, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        length   = values.get_float("length");
        const float        radius   = 0.5f * values.get_float("diameter");
        const bool         flanged  = (values.get_enum("flanges") == "ends");
        const unsigned int segments = static_cast<unsigned int>(values.get_int("segments"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (form == "tee") ? 2u : 1u);
        // Bands: two of the bore and two of a flange to an end, one along a straight pipe, seven more around a bend.
        const unsigned int end_bands = flanged ? 4u : 2u;
        const unsigned int run       = (2u * end_bands) + 1u + ((form == "elbow") ? 7u : 0u);
        // A tee's branch: the mouth's bands and one from the root, two more around a flanged tee's collar.
        const unsigned int branch    = (form == "tee") ? (flanged ? 7u : 3u) : 0u;
        const unsigned int tubes     = (form == "tee") ? 2u : 1u;
        EXPECT_EQ(report.triangle_count, (2u * segments * (run + branch)) + (tubes * 2u * (segments - 2u)));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_LE(report.bounds_max.y, 2.0f * (flanged ? (1.35f * radius) : radius) + 1.0e-5f);
        expect_connector_names(values, {"start", "end", "branch"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].outward, (glm::vec3{-1.0f, 0.0f, 0.0f}));
        EXPECT_FLOAT_EQ(connectors[0].position.x, -0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * length);
        EXPECT_NEAR(connectors[0].position.y, connectors[1].position.y, 1.0e-6f);
        // No point lies beyond the end connector, and the end's mouth lies on it.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float reach = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            reach = std::max(reach, glm::dot(point_of(*mesh, v) - connectors[1].position, connectors[1].outward));
        }
        EXPECT_NEAR(reach, 0.0f, 1.0e-5f);
        if (form == "elbow") {
            const float angle = glm::radians(values.get_float("bend_angle"));
            EXPECT_NEAR(glm::distance(connectors[1].outward, glm::vec3{std::cos(angle), 0.0f, -std::sin(angle)}), 0.0f, 1.0e-5f);
            EXPECT_NEAR(glm::length(glm::vec2{connectors[1].position.x, connectors[1].position.z}), 0.5f * length, 1.0e-5f);
        } else {
            EXPECT_EQ(connectors[1].outward, (glm::vec3{1.0f, 0.0f, 0.0f}));
            EXPECT_FLOAT_EQ(connectors[1].position.x, 0.5f * length);
        }
        if (form == "tee") {
            EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * length);
            EXPECT_FLOAT_EQ(connectors[2].position.z, -0.5f * length);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_pipe, Kit_pipe_forms, testing::Values("straight", "elbow", "tee"));

//
// Pot.
//

const std::vector<std::string> pot_floats{"height", "width", "neck"};

class Kit_pot_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_pot_forms, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& pot = get_shape("prop.pot");
    ASSERT_EQ(pot.material_slots, (std::vector<std::string>{"stone"}));
    for (const int segments : {8, 13, 20}) {
        Kit_parameter_values values = explicit_enum(pot, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        for_each_corner(values, pot_floats, expect_valid_with_every_slot);
    }
}

TEST_P(Kit_pot_forms, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& pot = get_shape("prop.pot");
    expect_topology_from_integers(explicit_enum(pot, "form", GetParam(), 1), pot_floats);
}

// One part (an amphora's four handle pieces more), as high as the height and as wide as the width, with the floor of
// its hollow under the rim.
TEST_P(Kit_pot_forms, size_and_hollow_follow_the_values)
{
    const Kit_shape_definition& pot = get_shape("prop.pot");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(pot, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float height = values.get_float("height");
        const float width  = values.get_float("width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (GetParam() == "amphora") ? 5u : 1u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.z, -report.bounds_min.z);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> levels = level_facet_heights(*mesh, 0);
        const float floor = *std::min_element(levels.begin(), levels.end());
        EXPECT_GT(floor, 0.1f * height);
        EXPECT_LT(floor, 0.9f * height);
        expect_connector_names(values, {"base", "top"});
        EXPECT_EQ(connectors_of(values)[1].position.y, height);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_pot, Kit_pot_forms, testing::Values("jar", "amphora", "urn", "bowl", "planter"));

//
// Table.
//

const std::vector<std::string> table_floats{"width", "depth", "height"};

class Kit_table_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_table_forms, both_legs_pass_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& table = get_shape("prop.table");
    ASSERT_EQ(table.material_slots, (std::vector<std::string>{"wood"}));
    for (const std::string& legs : enum_values_of(table, "legs")) {
        Kit_parameter_values values = explicit_enum(table, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_enum("legs", legs));
        for_each_corner(values, table_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, table_floats);
    }
}

// Every part is a box, and the piece fills its width, depth and height.
TEST_P(Kit_table_forms, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& table = get_shape("prop.table");
    const std::string form = GetParam();
    for (const std::string& legs : enum_values_of(table, "legs")) {
        const bool trestles = (legs == "trestles");
        // A top on four posts under four aprons, or on two trestles of two legs and a tie, with a stretcher.
        const unsigned int carried = trestles ? 7u : 8u;
        unsigned int parts = 0;
        if (form == "table") {
            parts = 1u + carried;
        } else if (form == "bench") {
            parts = 2u + carried + 4u;
        } else if (form == "stool") {
            parts = trestles ? 4u : 9u;
        } else {
            parts = 4u + (trestles ? 3u : 4u);
        }
        for (const int seed : {0, 1, 2}) {
            Kit_parameter_values values = derived_enum(table, "form", form, seed);
            values.set_value_source(table.find_parameter("legs").value(), editor::Kit_value_source::explicit_value);
            ASSERT_TRUE(values.set_enum("legs", legs));
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, parts);
            EXPECT_EQ(report.triangle_count, 12u * parts);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * values.get_float("width"));
            EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * values.get_float("width"));
            EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * values.get_float("depth"));
            EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * values.get_float("depth"));
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("height"));
            expect_connector_names(values, {"base", "top"});
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_table, Kit_table_forms, testing::Values("table", "bench", "stool", "shelf"));

// A bench's seat lies at 0.55 of its height under its back.
TEST(Kit_table, a_benchs_seat_lies_under_its_back)
{
    const Kit_shape_definition& table = get_shape("prop.table");
    const Kit_parameter_values values = explicit_enum(table, "form", "bench", 1);
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    const std::vector<float> levels = level_facet_heights(*mesh, 0);
    const float seat = 0.55f * values.get_float("height");
    EXPECT_EQ(std::count_if(levels.begin(), levels.end(), [seat](const float y) { return std::abs(y - seat) < 1.0e-5f; }), 2);
}

//
// Market stall.
//

const std::vector<std::string> stall_floats{"width", "depth", "height", "overhang"};

class Kit_market_stall_awnings : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_market_stall_awnings, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& stall = get_shape("prop.market_stall");
    ASSERT_EQ(stall.material_slots, (std::vector<std::string>{"wood", "cloth"}));
    const Kit_parameter_values values = explicit_enum(stall, "awning", GetParam(), 1);
    for_each_corner(values, stall_floats, expect_valid_with_every_slot);
    expect_topology_from_integers(values, stall_floats);
}

// Four posts, two rails, a counter of four boxes, a valance and one or two slabs of cloth. The cloth's highest line
// is the height, and it reaches the overhang beyond the posts at the front and the back.
TEST_P(Kit_market_stall_awnings, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& stall = get_shape("prop.market_stall");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(stall, "awning", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float width = values.get_float("width");
        const float reach = (0.5f * values.get_float("depth")) + values.get_float("overhang");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (GetParam() == "peaked") ? 13u : 12u);
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -reach);
        EXPECT_FLOAT_EQ(report.bounds_max.z, reach);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_max.y, values.get_float("height"), 1.0e-5f);
        expect_connector_names(values, {"base", "start", "end"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[1].position, (glm::vec3{-0.5f * width, 0.0f, 0.0f}));
        EXPECT_EQ(connectors[2].position, (glm::vec3{0.5f * width, 0.0f, 0.0f}));
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_market_stall, Kit_market_stall_awnings, testing::Values("flat", "sloped", "peaked"));

//
// Cart.
//

const std::vector<std::string> cart_floats{"length", "width", "wheel_radius", "tilt"};

class Kit_cart_sides : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_cart_sides, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& cart = get_shape("prop.cart");
    ASSERT_EQ(cart.material_slots, (std::vector<std::string>{"wood", "metal"}));
    for (const int segments : {8, 11, 16}) {
        Kit_parameter_values values = explicit_enum(cart, "sides", GetParam(), 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        for_each_corner(values, cart_floats, expect_valid_with_every_slot);
    }
    expect_topology_from_integers(explicit_enum(cart, "sides", GetParam(), 1), cart_floats);
}

// Two shafts, a deck, two legs, an axle and two wheels, and the parts of the sides. The wheels stand on the ground a
// width apart, and the handles reach forward over the ground.
TEST_P(Kit_cart_sides, parts_and_stance_follow_the_values)
{
    const Kit_shape_definition& cart = get_shape("prop.cart");
    const unsigned int sides = (GetParam() == "open") ? 0u : ((GetParam() == "rails") ? 6u : 4u);
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(cart, "sides", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float length = values.get_float("length");
        const float width  = values.get_float("width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 8u + sides);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_GE(report.bounds_max.y, 1.8f * values.get_float("wheel_radius"));
        // From the bed's back to the handles' ends: the length, less what the tilt takes.
        EXPECT_LE(report.bounds_max.z - report.bounds_min.z, length + 0.1f);
        EXPECT_GE(report.bounds_max.z - report.bounds_min.z, 0.9f * length);
        EXPECT_GT(report.bounds_max.z, 0.6f * length);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_cart, Kit_cart_sides, testing::Values("open", "rails", "boards"));

// The tilt lowers the handles' ends, which stay over the ground at every tilt.
TEST(Kit_cart, the_tilt_lowers_the_handles)
{
    const Kit_shape_definition& cart = get_shape("prop.cart");
    float previous = 10.0f;
    for (const float tilt : {0.0f, 5.0f, 10.0f, 15.0f}) {
        Kit_parameter_values values = explicit_values(cart, 1);
        ASSERT_TRUE(values.set_float("tilt", tilt));
        ASSERT_TRUE(values.set_float("wheel_radius", 0.5f));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        // The lowest point of the handles' ends, the points furthest toward +Z.
        float far_z = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            far_z = std::max(far_z, point_of(*mesh, v).z);
        }
        float low = 10.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (p.z > (far_z - 0.02f)) {
                low = std::min(low, p.y);
            }
        }
        EXPECT_GT(low, 0.0f);
        EXPECT_LT(low, previous);
        previous = low;
    }
}

//
// Well.
//

const std::vector<std::string> well_floats{"radius", "wall_height"};

class Kit_well_roofs : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_well_roofs, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& well = get_shape("prop.well");
    ASSERT_EQ(well.material_slots, (std::vector<std::string>{"stone", "wood"}));
    for (const int segments : {8, 13, 20}) {
        Kit_parameter_values values = explicit_enum(well, "roof", GetParam(), 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        for_each_corner(values, well_floats, expect_valid_with_every_slot);
    }
    expect_topology_from_integers(explicit_enum(well, "roof", GetParam(), 1), well_floats);
}

// The wall and the bucket; two posts, a beam, a windlass and two crank pieces; two roof slabs and two king posts.
// The wall is hollow down to the water's level, and the posts stand beside it.
TEST_P(Kit_well_roofs, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& well = get_shape("prop.well");
    const std::string roof = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(well, "roof", roof, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float radius = values.get_float("radius");
        const float wall   = values.get_float("wall_height");
        const float beam   = wall + std::clamp(1.4f * radius, 0.9f, 2.2f);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (roof == "none") ? 2u : ((roof == "crossbar") ? 8u : 12u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        if (roof == "none") {
            EXPECT_GT(report.bounds_max.y, wall + 0.1f);
            EXPECT_LT(report.bounds_max.y, wall + 0.35f);
            EXPECT_LE(report.bounds_max.x, radius + 0.04f + 1.0e-5f);
        } else {
            EXPECT_GT(report.bounds_max.x, radius + 0.04f);
            if (roof == "crossbar") {
                EXPECT_NEAR(report.bounds_max.y, beam, 1.0e-5f);
            } else {
                EXPECT_GT(report.bounds_max.y, beam + (0.5f * radius));
            }
        }
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> levels = level_facet_heights(*mesh, 0);
        ASSERT_FALSE(levels.empty());
        EXPECT_FLOAT_EQ(*std::min_element(levels.begin(), levels.end()), 0.15f * wall);
        EXPECT_FLOAT_EQ(*std::max_element(levels.begin(), levels.end()), wall);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_well, Kit_well_roofs, testing::Values("none", "crossbar", "gable"));

//
// Signpost.
//

const std::vector<std::string> sign_floats{"height", "arm_length", "lean"};

TEST(Kit_signpost, every_arm_count_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& sign = get_shape("prop.signpost");
    ASSERT_EQ(sign.material_slots, (std::vector<std::string>{"wood"}));
    for (const int seed : sweep_seeds(sign)) {
        for (int arms = 1; arms <= 4; ++arms) {
            Kit_parameter_values values = explicit_values(sign, seed);
            ASSERT_TRUE(values.set_int("arms", arms));
            for_each_corner(values, sign_floats, expect_valid_with_every_slot);
        }
    }
}

TEST(Kit_signpost, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& sign = get_shape("prop.signpost");
    for (int arms = 1; arms <= 4; ++arms) {
        Kit_parameter_values values = explicit_values(sign, 1);
        ASSERT_TRUE(values.set_int("arms", arms));
        expect_topology_from_integers(values, sign_floats);
    }
}

// A foot block, the post and a board to an arm; the post's point is the height at every lean, and the boards reach
// out of the post.
TEST(Kit_signpost, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& sign = get_shape("prop.signpost");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(sign, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int arms   = static_cast<unsigned int>(values.get_int("arms"));
        const float        length = values.get_float("arm_length");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u + arms);
        EXPECT_EQ(report.triangle_count, 26u + (16u * arms));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("height"));
        const glm::vec3 extent = report.bounds_max - report.bounds_min;
        EXPECT_GE(std::max(extent.x, extent.z), 0.7f * length);
        EXPECT_LE(std::max(extent.x, extent.z), (2.0f * length) + 1.0f);
        expect_connector_names(values, {"base"});
    }
}

// The headings come from the seed.
TEST(Kit_signpost, the_seed_turns_the_boards)
{
    const Kit_shape_definition& sign = get_shape("prop.signpost");
    EXPECT_FALSE(editor::kit_meshes_equal(*generate(explicit_values(sign, 1)), *generate(explicit_values(sign, 2))));
}

TEST(Kit_extra_shapes, the_industrial_kit_exists)
{
    const editor::Kit_definition* const kit = get_library().find_kit("industrial");
    ASSERT_NE(kit, nullptr);
    EXPECT_EQ(kit->triangle_ceiling, 2000u);
    EXPECT_EQ(kit->display_name, "Industrial");
}

} // anonymous namespace
