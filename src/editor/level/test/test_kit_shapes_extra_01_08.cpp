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
#include <string_view>
#include <vector>

// Tests of the shapes of kit_shapes_extra_01_08.cpp.
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

//
// Crate.
//

const std::vector<std::string> crate_floats{"width", "height", "depth"};

TEST(Kit_crate, every_lid_and_slat_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& crate = get_shape("prop.crate");
    ASSERT_EQ(crate.material_slots, (std::vector<std::string>{"wood"}));
    for (const std::string& lid : enum_values_of(crate, "lid")) {
        for (int slats = 2; slats <= 5; ++slats) {
            Kit_parameter_values values = explicit_enum(crate, "lid", lid, 1);
            ASSERT_TRUE(values.set_int("slats", slats));
            for_each_corner(values, crate_floats, expect_valid_with_every_slot);
        }
    }
}

TEST(Kit_crate, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& crate = get_shape("prop.crate");
    for (const std::string& lid : enum_values_of(crate, "lid")) {
        for (const int slats : {2, 5}) {
            Kit_parameter_values values = explicit_enum(crate, "lid", lid, 1);
            ASSERT_TRUE(values.set_int("slats", slats));
            expect_topology_from_integers(values, crate_floats);
        }
    }
}

// Every part is a box: two skids, the floor, four posts, the slats of four sides, and a lid of planks under two
// battens. The bounds are the width by the depth; a closed crate and one without a lid are the height high.
TEST(Kit_crate, parts_bounds_and_connectors_follow_the_values)
{
    const Kit_shape_definition& crate = get_shape("prop.crate");
    for (const std::string& lid : enum_values_of(crate, "lid")) {
        for (const int seed : derive_seeds()) {
            const Kit_parameter_values values = derived_enum(crate, "lid", lid, seed);
            SCOPED_TRACE(values.to_canonical_text());
            const float        width  = values.get_float("width");
            const float        height = values.get_float("height");
            const float        depth  = values.get_float("depth");
            const unsigned int slats  = static_cast<unsigned int>(values.get_int("slats"));
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 7u + (4u * slats) + ((lid == "none") ? 0u : (slats + 2u)));
            EXPECT_EQ(report.triangle_count, 12u * report.component_count);
            EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
            EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * depth);
            EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * depth);
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            expect_connector_names(values, {"base", "top"});
            const std::vector<Kit_connector> connectors = connectors_of(values);
            if (lid == "open") {
                // The lid stands over the rim, where the top connector is.
                EXPECT_GT(report.bounds_max.y, height + (0.5f * depth));
                EXPECT_LT(connectors[1].position.y, height);
                EXPECT_GT(connectors[1].position.y, 0.75f * height);
            } else {
                EXPECT_NEAR(report.bounds_max.y, height, 1.0e-5f);
                EXPECT_EQ(connectors[1].position.y, height);
            }
        }
    }
}

//
// Barrel.
//

const std::vector<std::string> barrel_floats{"height", "radius", "bulge"};

class Kit_barrel_poses : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_barrel_poses, every_hoop_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& barrel = get_shape("prop.barrel");
    ASSERT_EQ(barrel.material_slots, (std::vector<std::string>{"wood", "metal"}));
    for (int hoops = 2; hoops <= 4; ++hoops) {
        for (const int segments : {8, 13, 24}) {
            Kit_parameter_values values = explicit_enum(barrel, "pose", GetParam(), 1);
            ASSERT_TRUE(values.set_int("hoops", hoops));
            ASSERT_TRUE(values.set_int("segments", segments));
            for_each_corner(values, barrel_floats, expect_valid_with_every_slot);
        }
    }
}

// One part of bands of staves and hoops between two heads, and two chocks beside a lying barrel; it stands the height
// high within its radius and the hoops, or lies the height long on the ground under its top connector.
TEST_P(Kit_barrel_poses, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& barrel = get_shape("prop.barrel");
    const bool lying = (GetParam() == "lying");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(barrel, "pose", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        height   = values.get_float("height");
        const float        radius   = values.get_float("radius");
        const unsigned int hoops    = static_cast<unsigned int>(values.get_int("hoops"));
        const unsigned int segments = static_cast<unsigned int>(values.get_int("segments"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, lying ? 3u : 1u);
        // Bands: two to a head, eight of staves, and three more to a hoop.
        const unsigned int bands = 12u + (3u * hoops);
        EXPECT_EQ(report.triangle_count, (2u * segments * bands) + (2u * (segments - 2u)) + (lying ? 16u : 0u));
        expect_connector_names(values, {"base", "top"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        const float reach = radius + 0.012f;
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        if (lying) {
            EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * height);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * height);
            EXPECT_LE(report.bounds_max.z, reach);
            EXPECT_GE(report.bounds_max.y, 1.8f * radius);
            EXPECT_LE(report.bounds_max.y, 2.0f * reach);
            EXPECT_LE(connectors[1].position.y, report.bounds_max.y + 1.0e-5f);
            EXPECT_GE(connectors[1].position.y, 0.9f * report.bounds_max.y);
        } else {
            EXPECT_FLOAT_EQ(report.bounds_max.y, height);
            EXPECT_LE(report.bounds_max.x, reach);
            EXPECT_GE(report.bounds_max.x, 0.9f * radius);
            EXPECT_FLOAT_EQ(report.bounds_max.z, -report.bounds_min.z);
            EXPECT_EQ(connectors[1].position.y, height);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_barrel, Kit_barrel_poses, testing::Values("upright", "lying"));

TEST(Kit_barrel, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& barrel = get_shape("prop.barrel");
    for (const std::string& pose : enum_values_of(barrel, "pose")) {
        for (int hoops = 2; hoops <= 4; ++hoops) {
            Kit_parameter_values values = explicit_enum(barrel, "pose", pose, 1);
            ASSERT_TRUE(values.set_int("hoops", hoops));
            expect_topology_from_integers(values, barrel_floats);
        }
    }
}

// The hoops are the metal slot: three bands to a hoop.
TEST(Kit_barrel, hoops_are_the_metal_slot)
{
    const Kit_shape_definition& barrel = get_shape("prop.barrel");
    for (int hoops = 2; hoops <= 4; ++hoops) {
        Kit_parameter_values values = explicit_values(barrel, 1);
        ASSERT_TRUE(values.set_int("hoops", hoops));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        const std::ptrdiff_t segments = values.get_int("segments");
        EXPECT_EQ(std::count(facet_slots.begin(), facet_slots.end(), 1u), 3 * hoops * segments);
    }
}

// A lying barrel's top connector is on its mesh's top at odd and even segment counts.
TEST(Kit_barrel, lying_top_connector_is_on_the_top)
{
    const Kit_shape_definition& barrel = get_shape("prop.barrel");
    for (const int segments : {8, 9, 11, 12, 15}) {
        for (int hoops = 2; hoops <= 4; ++hoops) {
            Kit_parameter_values values = explicit_enum(barrel, "pose", "lying", 1);
            ASSERT_TRUE(values.set_int("segments", segments));
            ASSERT_TRUE(values.set_int("hoops", hoops));
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report            report     = editor::check_kit_shape(values);
            const std::vector<Kit_connector> connectors = connectors_of(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_NEAR(connectors[1].position.y, report.bounds_max.y, 1.0e-5f);
        }
    }
}

//
// Broken wall.
//

const std::vector<std::string> broken_wall_floats{"length", "height", "thickness", "intact", "course"};

class Kit_broken_wall_breaks : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_broken_wall_breaks, every_step_count_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& wall = get_shape("ruin.broken_wall");
    ASSERT_EQ(wall.material_slots, (std::vector<std::string>{"wall"}));
    for (const int seed : sweep_seeds(wall)) {
        for (const int steps : {3, 8, 24}) {
            Kit_parameter_values values = explicit_enum(wall, "break", GetParam(), seed);
            ASSERT_TRUE(values.set_int("steps", steps));
            for_each_corner(values, broken_wall_floats, expect_valid_with_every_slot);
        }
    }
}

TEST_P(Kit_broken_wall_breaks, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& wall = get_shape("ruin.broken_wall");
    for (const int steps : {3, 8, 24}) {
        Kit_parameter_values values = explicit_enum(wall, "break", GetParam(), 1);
        ASSERT_TRUE(values.set_int("steps", steps));
        expect_topology_from_integers(values, broken_wall_floats);
    }
}

// One part of twelve triangles to a column. The start is the wall's whole end face, so it joins an intact wall; the
// top falls toward the end, which is lower.
TEST_P(Kit_broken_wall_breaks, the_start_is_whole_and_the_top_falls_toward_the_end)
{
    const Kit_shape_definition& wall = get_shape("ruin.broken_wall");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(wall, "break", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float length    = values.get_float("length");
        const float height    = values.get_float("height");
        const float thickness = values.get_float("thickness");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 1u);
        EXPECT_EQ(report.triangle_count, 12u * static_cast<unsigned int>(values.get_int("steps")));
        EXPECT_EQ(report.bounds_min, (glm::vec3{-0.5f * length, 0.0f, -0.5f * thickness}));
        EXPECT_EQ(report.bounds_max, (glm::vec3{0.5f * length, height, 0.5f * thickness}));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<std::array<float, 3>> start = vertices_on_x(*mesh, -0.5f * length);
        const std::vector<std::array<float, 3>> end   = vertices_on_x(*mesh, 0.5f * length);
        ASSERT_EQ(start.size(), 4u);
        ASSERT_EQ(end.size(), 4u);
        float start_top = 0.0f;
        float end_top   = 0.0f;
        for (std::size_t i = 0; i < 4; ++i) {
            start_top = std::max(start_top, start[i][1]);
            end_top   = std::max(end_top, end[i][1]);
        }
        EXPECT_EQ(start_top, height);
        EXPECT_GT(end_top, 0.0f);
        EXPECT_LT(end_top, 0.75f * height);
        expect_connector_names(values, {"start", "end", "base", "front", "back"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position, (glm::vec3{-0.5f * length, 0.0f, 0.0f}));
        EXPECT_EQ(connectors[1].position, (glm::vec3{0.5f * length, 0.0f, 0.0f}));
    }
}

// The share of the face left standing follows `intact`.
TEST_P(Kit_broken_wall_breaks, the_standing_share_follows_intact)
{
    const Kit_shape_definition& wall = get_shape("ruin.broken_wall");
    for (const int seed : {1, 2, 3, 4}) {
        float previous = 0.0f;
        for (const float intact : {0.3f, 0.55f, 0.8f}) {
            Kit_parameter_values values = explicit_enum(wall, "break", GetParam(), seed);
            ASSERT_TRUE(values.set_float("intact", intact));
            ASSERT_TRUE(values.set_int("steps", 12));
            SCOPED_TRACE(values.to_canonical_text());
            const float half_t = 0.5f * values.get_float("thickness");
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            // The area of the facets in the front face.
            float area = 0.0f;
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                bool in_front = true;
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    in_front = in_front && (point_of(*mesh, mesh->facets.vertex(f, c)).z == half_t);
                }
                if (!in_front) {
                    continue;
                }
                const glm::vec3 p0 = point_of(*mesh, mesh->facets.vertex(f, 0));
                for (GEO::index_t c = 1; (c + 1) < mesh->facets.nb_vertices(f); ++c) {
                    const glm::vec3 p1 = point_of(*mesh, mesh->facets.vertex(f, c));
                    const glm::vec3 p2 = point_of(*mesh, mesh->facets.vertex(f, c + 1));
                    area += 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
                }
            }
            const float share = area / (values.get_float("length") * values.get_float("height"));
            EXPECT_NEAR(share, intact, 0.1f);
            EXPECT_GT(share, previous);
            previous = share;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_broken_wall, Kit_broken_wall_breaks, testing::Values("stepped", "jagged", "slope"));

// Replace carries what two shapes name alike: the broken wall has every parameter of the wall with its range and default.
TEST(Kit_broken_wall, takes_the_walls_parameters)
{
    const Kit_shape_definition& wall   = get_shape("architecture.wall");
    const Kit_shape_definition& broken = get_shape("ruin.broken_wall");
    for (const Kit_parameter& parameter : wall.parameters) {
        SCOPED_TRACE(parameter.name);
        ASSERT_TRUE(broken.find_parameter(parameter.name).has_value());
        const Kit_parameter& other = broken.parameters[broken.find_parameter(parameter.name).value()];
        EXPECT_EQ(other.type, parameter.type);
        EXPECT_EQ(other.unit, parameter.unit);
        EXPECT_EQ(other.default_scale, parameter.default_scale);
        EXPECT_EQ(other.float_default, parameter.float_default);
        EXPECT_EQ(other.float_min, parameter.float_min);
        EXPECT_EQ(other.float_max, parameter.float_max);
    }
}

//
// Rubble.
//

const std::vector<std::string> rubble_floats{"radius", "height", "block_size"};

TEST(Kit_rubble, every_block_count_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& rubble = get_shape("ruin.rubble");
    ASSERT_EQ(rubble.material_slots, (std::vector<std::string>{"stone"}));
    EXPECT_FALSE(static_cast<bool>(rubble.get_connectors));
    for (const int seed : sweep_seeds(rubble)) {
        for (const int blocks : {4, 5, 16, 48}) {
            Kit_parameter_values values = explicit_values(rubble, seed);
            ASSERT_TRUE(values.set_int("blocks", blocks));
            for_each_corner(values, rubble_floats, expect_valid_with_every_slot);
        }
    }
}

TEST(Kit_rubble, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& rubble = get_shape("ruin.rubble");
    for (const int blocks : {4, 16, 48}) {
        Kit_parameter_values values = explicit_values(rubble, 1);
        ASSERT_TRUE(values.set_int("blocks", blocks));
        expect_topology_from_integers(values, rubble_floats);
    }
}

// A mound and a box to a block, within the footprint's radius, on the ground, its top the height.
TEST(Kit_rubble, parts_stay_within_the_footprint_under_the_height)
{
    const Kit_shape_definition& rubble = get_shape("ruin.rubble");
    for (int seed = 0; seed < 24; ++seed) {
        const Kit_parameter_values values = all_derived(rubble, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        radius = values.get_float("radius");
        const float        height = values.get_float("height") * radius;
        const unsigned int blocks = static_cast<unsigned int>(values.get_int("blocks"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, blocks + 1u);
        EXPECT_EQ(report.triangle_count, (12u * blocks) + 46u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_max.y, height, 1.0e-5f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            EXPECT_LE(glm::length(glm::vec2{p.x, p.z}), 1.0001f * radius);
        }
        // The heap fills its footprint: it is at least 1.2 radii wide both ways.
        EXPECT_GE(report.bounds_max.x - report.bounds_min.x, 1.2f * radius);
        EXPECT_GE(report.bounds_max.z - report.bounds_min.z, 1.2f * radius);
    }
}

//
// Ladder.
//

TEST(Kit_ladder, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& ladder = get_shape("traversal.ladder");
    ASSERT_EQ(ladder.material_slots, (std::vector<std::string>{"wood"}));
    for_each_corner(defaults(ladder), {"height", "width", "rung_spacing", "lean"}, expect_valid_with_every_slot);
}

// The rung count is the one integer taken from lengths; the other floats only move vertices.
TEST(Kit_ladder, topology_follows_from_the_rung_count_alone)
{
    const Kit_shape_definition& ladder = get_shape("traversal.ladder");
    expect_topology_from_integers(defaults(ladder), {"width", "lean"});
}

// Two rails and the rungs in equal steps of at most the rung spacing; the top connector is on the rails' tops, which
// stand toward -Z of the feet by the lean.
TEST(Kit_ladder, rungs_and_connectors_follow_the_values)
{
    const Kit_shape_definition& ladder = get_shape("traversal.ladder");
    for (const float spacing : {0.15f, 0.3f, 0.5f}) {
        for (const int seed : derive_seeds()) {
            Kit_parameter_values values = all_derived(ladder, seed);
            ASSERT_TRUE(values.set_float("rung_spacing", spacing));
            SCOPED_TRACE(values.to_canonical_text());
            const float        height = values.get_float("height");
            const float        width  = values.get_float("width");
            const float        run    = height * std::tan(glm::radians(values.get_float("lean")));
            const unsigned int steps  = static_cast<unsigned int>(std::ceil((height / spacing) - 1.0e-4f));
            EXPECT_LE(height / static_cast<float>(steps), spacing + 1.0e-5f);
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 2u + (steps - 1u));
            EXPECT_EQ(report.triangle_count, 12u * report.component_count);
            EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_FLOAT_EQ(report.bounds_max.y, height);
            expect_connector_names(values, {"bottom", "top"});
            const std::vector<Kit_connector> connectors = connectors_of(values);
            EXPECT_EQ(connectors[0].position, glm::vec3{0.0f});
            EXPECT_EQ(connectors[0].outward, (glm::vec3{0.0f, -1.0f, 0.0f}));
            EXPECT_NEAR(glm::distance(connectors[1].position, glm::vec3{0.0f, height, -run}), 0.0f, 1.0e-5f);
            EXPECT_EQ(connectors[1].outward, (glm::vec3{0.0f, 0.0f, -1.0f}));
            // The rails' tops are about the top connector.
            EXPECT_LT(report.bounds_min.z, -run);
            EXPECT_GT(report.bounds_min.z, -run - 0.1f);
        }
    }
}

//
// Pier.
//

class Kit_pier_braces : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_pier_braces, passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& pier = get_shape("harbour.pier");
    ASSERT_EQ(pier.material_slots, (std::vector<std::string>{"wood"}));
    for (const int seed : sweep_seeds(pier)) {
        for (const int segments : {6, 12}) {
            Kit_parameter_values values = explicit_enum(pier, "braces", GetParam(), seed);
            ASSERT_TRUE(values.set_int("segments", segments));
            for_each_corner(values, {"length", "width", "pile_depth", "pile_spacing", "plank_jitter"}, expect_valid_with_every_slot);
        }
    }
}

// The plank count and the bent count are the integers taken from lengths; the other floats and the seed only move vertices.
TEST_P(Kit_pier_braces, topology_follows_from_the_counts_alone)
{
    const Kit_shape_definition& pier = get_shape("harbour.pier");
    const Kit_parameter_values values = explicit_enum(pier, "braces", GetParam(), 1);
    expect_topology_from_integers(values, {"width", "pile_depth", "plank_jitter"});
}

// A plank to every quarter meter, two stringers, and to every bent a cap beam, two piles and two braces. The bounds
// are the length by the width from the piles' feet up to the walking surface, where the four connectors are.
TEST_P(Kit_pier_braces, parts_bounds_and_walking_surface_follow_the_values)
{
    const Kit_shape_definition& pier = get_shape("harbour.pier");
    const Kit_agent agent{};
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(pier, "braces", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        length = values.get_float("length");
        const float        width  = values.get_float("width");
        const float        deck   = values.get_float("pile_depth");
        const unsigned int planks = static_cast<unsigned int>(std::lround(length / 0.25f));
        const unsigned int bents  = std::min(static_cast<unsigned int>(std::lround(length / values.get_float("pile_spacing"))), 8u);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, planks + 2u + (bents * ((GetParam() == "cross") ? 5u : 3u)));
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * length);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * width);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_LE(report.bounds_max.y, deck);
        EXPECT_GT(report.bounds_max.y, deck - 0.008f);
        expect_connector_names(values, {"start", "end", "left", "right"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position, (glm::vec3{-0.5f * length, deck, 0.0f}));
        EXPECT_EQ(connectors[1].position, (glm::vec3{0.5f * length, deck, 0.0f}));
        EXPECT_EQ(connectors[2].position, (glm::vec3{0.0f, deck, -0.5f * width}));
        EXPECT_EQ(connectors[3].position, (glm::vec3{0.0f, deck, 0.5f * width}));
        // The planks' tops: level, within 8 mm under the walking surface, a gap of at most 2 cm between two.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        unsigned int tops = 0;
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            const glm::vec3 p0 = point_of(*mesh, mesh->facets.vertex(f, 0));
            const glm::vec3 p1 = point_of(*mesh, mesh->facets.vertex(f, 1));
            const glm::vec3 p2 = point_of(*mesh, mesh->facets.vertex(f, 2));
            const glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            if ((normal.y > 0.999f) && (p0.y > (deck - 0.0081f))) {
                ++tops;
            }
        }
        EXPECT_EQ(tops, planks);
        EXPECT_EQ(pier.check_walkability(values, agent), "");
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_pier, Kit_pier_braces, testing::Values("none", "cross"));

// Without jitter every plank's top is the walking surface; an agent too wide for the narrowest pier is refused.
TEST(Kit_pier, a_pier_without_jitter_is_level_and_a_narrow_one_refuses_a_wide_agent)
{
    const Kit_shape_definition& pier = get_shape("harbour.pier");
    Kit_parameter_values values = explicit_values(pier, 1);
    ASSERT_TRUE(values.set_float("plank_jitter", 0.0f));
    const Kit_mesh_report report = editor::check_kit_shape(values);
    ASSERT_TRUE(report.is_valid()) << describe(report);
    EXPECT_EQ(report.bounds_max.y, values.get_float("pile_depth"));
    // Two piers joined end to start: the planks go on at their pitch.
    const float length = values.get_float("length");
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    float first = length;
    float last  = -length;
    for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
        const glm::vec3 p = point_of(*mesh, v);
        if (p.y == values.get_float("pile_depth")) {
            first = std::min(first, p.x);
            last  = std::max(last, p.x);
        }
    }
    EXPECT_NEAR(first + (0.5f * length), 0.01f, 1.0e-5f);
    EXPECT_NEAR((0.5f * length) - last, 0.01f, 1.0e-5f);

    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.6f;
    ASSERT_TRUE(values.set_float("width", parameter_of(pier, "width").float_min));
    EXPECT_EQ(pier.check_walkability(values, agent), "");
    EXPECT_NE(pier.check_walkability(values, wide), "");
}

//
// Mooring.
//

class Kit_mooring_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_mooring_forms, passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& mooring = get_shape("harbour.mooring");
    ASSERT_EQ(mooring.material_slots, (std::vector<std::string>{"metal", "wood"}));
    for (const int seed : sweep_seeds(mooring)) {
        for (const std::string& rope : enum_values_of(mooring, "rope")) {
            for (const int segments : {6, 10, 16}) {
                Kit_parameter_values values = explicit_enum(mooring, "form", GetParam(), seed);
                ASSERT_TRUE(values.set_enum("rope", rope));
                ASSERT_TRUE(values.set_int("segments", segments));
                for_each_corner(values, {"size", "lean"}, expect_valid_with_every_slot);
            }
        }
    }
}

TEST_P(Kit_mooring_forms, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& mooring = get_shape("harbour.mooring");
    for (const std::string& rope : enum_values_of(mooring, "rope")) {
        Kit_parameter_values values = explicit_enum(mooring, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_enum("rope", rope));
        expect_topology_from_integers(values, {"size", "lean"});
    }
}

// The parts of each form, three more for the rope's turns; it stands on the ground, its height in sizes.
TEST_P(Kit_mooring_forms, parts_and_height_follow_the_values)
{
    const Kit_shape_definition& mooring = get_shape("harbour.mooring");
    const std::string form = GetParam();
    const unsigned int parts  = (form == "bollard") ? 2u : ((form == "post") ? 1u : 4u);
    // A cleat's bar lies 0.35 sizes high, a flat side of its six up.
    const float        cleat  = 0.35f + (0.085f * std::cos(glm::radians(30.0f)));
    const float        height = (form == "bollard") ? 1.0f : ((form == "cleat") ? cleat : ((form == "post") ? 2.4f : 4.0f));
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(mooring, "form", form, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float size = values.get_float("size");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, parts + ((values.get_enum("rope") == "coil") ? 3u : 0u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_max.y, height * size, 1.0e-4f * size);
        // The lean moves the top by at most its tangent of the height.
        const float reach = (0.65f * size) + (height * size * std::tan(glm::radians(values.get_float("lean"))));
        EXPECT_LE(report.bounds_max.x, reach);
        EXPECT_GE(report.bounds_min.x, -reach);
        EXPECT_LE(report.bounds_max.z, reach);
        EXPECT_GE(report.bounds_min.z, -reach);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_mooring, Kit_mooring_forms, testing::Values("bollard", "cleat", "post", "pile_cluster"));

// The lean's direction comes from the seed; without a lean and a rope a bollard is the same at every seed.
TEST(Kit_mooring, the_seed_turns_the_lean)
{
    const Kit_shape_definition& mooring = get_shape("harbour.mooring");
    Kit_parameter_values leaning = explicit_values(mooring, 1);
    ASSERT_TRUE(leaning.set_float("lean", 8.0f));
    Kit_parameter_values other = leaning;
    ASSERT_TRUE(other.set_int("seed", 2));
    EXPECT_FALSE(editor::kit_meshes_equal(*generate(leaning), *generate(other)));
    ASSERT_TRUE(leaning.set_float("lean", 0.0f));
    ASSERT_TRUE(other.set_float("lean", 0.0f));
    EXPECT_TRUE(editor::kit_meshes_equal(*generate(leaning), *generate(other)));
}

//
// Lamp post.
//

const std::vector<std::string> lamp_floats{"height", "arm_length"};

// The lamp's unit, and its heads' unit: less on an arm too short for a head of the post's unit.
[[nodiscard]] auto lamp_unit(const Kit_parameter_values& values) -> float
{
    return std::clamp(0.028f * values.get_float("height"), 0.045f, 0.16f);
}
[[nodiscard]] auto lamp_head_unit(const Kit_parameter_values& values) -> float
{
    const float unit = lamp_unit(values);
    if (values.get_int("arms") == 0) {
        return unit;
    }
    const std::string_view head   = values.get_enum("head");
    const float            radius = (head == "lantern") ? 1.65f : ((head == "globe") ? 1.6f : 2.5f);
    return std::min(unit, (values.get_float("arm_length") - (1.2f * unit) - 0.01f) / radius);
}

class Kit_lamp_post_heads : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_lamp_post_heads, every_arm_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& lamp = get_shape("light.lamp_post");
    ASSERT_EQ(lamp.material_slots, (std::vector<std::string>{"metal", "glow"}));
    for (int arms = 0; arms <= 4; ++arms) {
        for (const int segments : {6, 8, 16}) {
            Kit_parameter_values values = explicit_enum(lamp, "head", GetParam(), 1);
            ASSERT_TRUE(values.set_int("arms", arms));
            ASSERT_TRUE(values.set_int("segments", segments));
            for_each_corner(values, lamp_floats, expect_valid_with_every_slot);
        }
    }
}

TEST_P(Kit_lamp_post_heads, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& lamp = get_shape("light.lamp_post");
    for (int arms = 0; arms <= 4; ++arms) {
        Kit_parameter_values values = explicit_enum(lamp, "head", GetParam(), 1);
        ASSERT_TRUE(values.set_int("arms", arms));
        expect_topology_from_integers(values, lamp_floats);
    }
}

// The post, under a finial with a beam and a strut to an arm, and the parts of each head. The post stands the height
// high on its plinth about the origin.
TEST_P(Kit_lamp_post_heads, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& lamp = get_shape("light.lamp_post");
    const std::string head = GetParam();
    for (int arms = 0; arms <= 4; ++arms) {
        for (const int seed : {0, 1, 2}) {
            Kit_parameter_values values = derived_enum(lamp, "head", head, seed);
            values.set_value_source(lamp.find_parameter("arms").value(), editor::Kit_value_source::explicit_value);
            ASSERT_TRUE(values.set_int("arms", arms));
            SCOPED_TRACE(values.to_canonical_text());
            const float        height = values.get_float("height");
            const unsigned int count  = static_cast<unsigned int>(arms);
            const unsigned int heads  = std::max(count, 1u);
            const unsigned int parts  = (head == "lantern") ? 9u : ((head == "globe") ? 2u : ((arms == 0) ? 7u : 3u));
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 1u + ((arms == 0) ? 0u : (1u + (2u * count))) + (heads * parts));
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_NEAR(report.bounds_max.y, height, 1.0e-5f * height);
            expect_connector_names(values, {"base"});
            if (arms == 0) {
                EXPECT_NEAR(report.bounds_max.x, -report.bounds_min.x, 1.0e-5f);
            } else {
                // The first arm reaches toward +X.
                EXPECT_GT(report.bounds_max.x, values.get_float("arm_length"));
            }
        }
    }
}

// One light to a head, named by the head's number: a point light, a spot shining down under a hood. It stands in the
// open by its head's glowing part: over it, or under a hanging hood's.
TEST_P(Kit_lamp_post_heads, each_head_carries_a_light_beside_its_glowing_part)
{
    const Kit_shape_definition& lamp = get_shape("light.lamp_post");
    ASSERT_TRUE(static_cast<bool>(lamp.get_light_frames));
    const std::string head = GetParam();
    for (int arms = 0; arms <= 4; ++arms) {
        Kit_parameter_values values = explicit_enum(lamp, "head", head, 1);
        ASSERT_TRUE(values.set_int("arms", arms));
        for_each_corner(values, lamp_floats, [arms, &head](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            std::vector<editor::Kit_light_frame> frames;
            editor::get_shape_light_frames(corner, frames);
            ASSERT_EQ(frames.size(), static_cast<std::size_t>(std::max(arms, 1)));
            EXPECT_EQ(editor::check_light_frames(frames), "");
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            const float unit    = lamp_head_unit(corner);
            const bool  hanging = (head == "hood") && (arms > 0);
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const editor::Kit_light_frame& frame = frames[i];
                EXPECT_EQ(std::string{frame.name}, fmt::format("light_{}", i + 1));
                EXPECT_EQ(frame.start.type, (head == "hood") ? editor::Kit_light_type::spot : editor::Kit_light_type::point);
                EXPECT_EQ(frame.direction, (glm::vec3{0.0f, -1.0f, 0.0f}));
                EXPECT_EQ(frame.start.range, 8.0f);
                const float out = glm::length(glm::vec2{frame.position.x, frame.position.z});
                EXPECT_NEAR(out, (arms == 0) ? 0.0f : corner.get_float("arm_length"), 1.0e-5f);
                // The glowing facets of this head, and how near any point of the mesh comes to the light.
                unsigned int glowing = 0;
                float        nearest = corner.get_float("height");
                for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                    for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                        const glm::vec3 p = point_of(*mesh, mesh->facets.vertex(f, c));
                        nearest = std::min(nearest, glm::distance(p, frame.position));
                        if ((facet_slots[f] != 1u) || (glm::length(glm::vec2{p.x - frame.position.x, p.z - frame.position.z}) > (3.0f * unit))) {
                            continue;
                        }
                        ++glowing;
                        if (hanging) {
                            EXPECT_GT(p.y, frame.position.y + (0.2f * unit));
                        } else {
                            EXPECT_LT(p.y, frame.position.y - (0.2f * unit));
                        }
                    }
                }
                EXPECT_GT(glowing, 0u);
                EXPECT_GT(nearest, 0.25f * unit);
            }
            if (arms > 0) {
                EXPECT_GT(frames[0].position.x, 0.0f);
                EXPECT_EQ(frames[0].position.z, 0.0f);
            }
        });
    }
}

// A head on an arm keeps clear of the post's collar: on short arms of tall posts the heads are smaller, and no
// glowing part of a head comes within the collar's radius of the post's axis.
TEST_P(Kit_lamp_post_heads, heads_on_arms_keep_clear_of_the_post)
{
    const Kit_shape_definition& lamp = get_shape("light.lamp_post");
    const std::string head = GetParam();
    for (int arms = 1; arms <= 4; ++arms) {
        Kit_parameter_values values = explicit_enum(lamp, "head", head, 1);
        ASSERT_TRUE(values.set_int("arms", arms));
        for_each_corner(values, lamp_floats, [](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            const float unit      = lamp_unit(corner);
            const float head_unit = lamp_head_unit(corner);
            EXPECT_GT(head_unit, 0.0f);
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                if (facet_slots[f] != 1u) {
                    continue;
                }
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    const glm::vec3 p = point_of(*mesh, mesh->facets.vertex(f, c));
                    EXPECT_GT(glm::length(glm::vec2{p.x, p.z}), 1.2f * unit);
                }
            }
        });
    }
    // At the shortest arm on the tallest post the heads are smaller than the post's unit.
    Kit_parameter_values values = explicit_enum(lamp, "head", head, 1);
    ASSERT_TRUE(values.set_int("arms", 1));
    ASSERT_TRUE(values.set_float("height", 12.0f));
    ASSERT_TRUE(values.set_float("arm_length", 0.3f));
    EXPECT_LT(lamp_head_unit(values), lamp_unit(values));
}

INSTANTIATE_TEST_SUITE_P(Kit_lamp_post, Kit_lamp_post_heads, testing::Values("lantern", "globe", "hood"));

// The new kits of doc/plans/level_kit_extra_shapes.md and the slots and palette materials of its P3.
TEST(Kit_extra_shapes, kits_slots_and_palette_materials_exist)
{
    const editor::Kit_shape_library& library = get_library();
    for (const char* kit : {"prop", "ruin", "traversal", "harbour", "light"}) {
        ASSERT_NE(library.find_kit(kit), nullptr) << kit;
        EXPECT_EQ(library.find_kit(kit)->triangle_ceiling, 2000u);
        EXPECT_FALSE(library.find_kit(kit)->display_name.empty());
    }
    for (const char* name : {"Kit Metal", "Kit Rust", "Kit Cloth", "Kit Clay", "Kit Glow"}) {
        EXPECT_NE(library.find_palette_material(name), nullptr) << name;
    }
    EXPECT_EQ(library.find_slot_default("metal")->name, "Kit Metal");
    EXPECT_EQ(library.find_slot_default("cloth")->name, "Kit Cloth");
    EXPECT_EQ(library.find_slot_default("glow")->name, "Kit Glow");
    EXPECT_GT(library.find_palette_material("Kit Metal")->metallic, 0.0f);
    EXPECT_EQ(library.find_palette_material("Kit Wood")->metallic, 0.0f);
    const std::vector<std::string>& stone = library.find_slot_palette("stone")->alternatives;
    EXPECT_NE(std::find(stone.begin(), stone.end(), "Kit Clay"), stone.end());
}

} // anonymous namespace
