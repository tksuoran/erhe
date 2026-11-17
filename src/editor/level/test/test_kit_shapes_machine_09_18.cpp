#include "kit_shape_test_support.hpp"

#include "level/kit_connector.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_machine_09_18.cpp.
namespace {

using editor::Kit_connector;
using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using namespace editor::kit_test;

constexpr float pi = std::numbers::pi_v<float>;

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

// The least distance of the mesh's vertices from a line along Y through (x, z).
[[nodiscard]] auto least_reach_from_y_line(const GEO::Mesh& mesh, const float x, const float z) -> float
{
    float least = 1.0e9f;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point_of(mesh, v);
        least = std::min(least, glm::length(glm::vec2{p.x - x, p.z - z}));
    }
    return least;
}

// Whether a facet crosses the Y axis: the axis lies inside the facet's projection on the XZ plane.
[[nodiscard]] auto y_axis_crosses_a_facet(const GEO::Mesh& mesh) -> bool
{
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        const glm::vec3 p0 = point_of(mesh, mesh.facets.vertex(f, 0));
        for (GEO::index_t c = 1; (c + 1) < mesh.facets.nb_vertices(f); ++c) {
            const glm::vec3 p1 = point_of(mesh, mesh.facets.vertex(f, c));
            const glm::vec3 p2 = point_of(mesh, mesh.facets.vertex(f, c + 1));
            const float d0 = (p0.x * p1.z) - (p0.z * p1.x);
            const float d1 = (p1.x * p2.z) - (p1.z * p2.x);
            const float d2 = (p2.x * p0.z) - (p2.z * p0.x);
            if (((d0 > 0.0f) && (d1 > 0.0f) && (d2 > 0.0f)) || ((d0 < 0.0f) && (d1 < 0.0f) && (d2 < 0.0f))) {
                return true;
            }
        }
    }
    return false;
}

// How many vertices lie the given distance from the Y axis.
[[nodiscard]] auto count_at_reach(const GEO::Mesh& mesh, const float reach, const float tolerance) -> int
{
    int count = 0;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point_of(mesh, v);
        count += (std::abs(glm::length(glm::vec2{p.x, p.z}) - reach) < tolerance) ? 1 : 0;
    }
    return count;
}

//
// Connecting rod.
//

const std::vector<std::string> rod_floats{"length", "small_bore", "big_bore", "thickness"};

TEST(Kit_connecting_rod, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& rod = get_shape("machine.connecting_rod");
    ASSERT_EQ(rod.material_slots, (std::vector<std::string>{"metal", "steel"}));
    const Kit_parameter_values values = explicit_values(rod, 1);
    for_each_corner(values, rod_floats, expect_valid_with_every_slot);
    expect_topology_from_integers(values, rod_floats);
}

// Two eyes, a web and two flanges, and two cap bolts; the eyes' axes are the length apart, each bored by its bore
// (kept to a share of the length so that the eyes stay apart), and the connectors are on the eyes' middles.
TEST(Kit_connecting_rod, eyes_and_size_follow_the_values)
{
    const Kit_shape_definition& rod = get_shape("machine.connecting_rod");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(rod, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float length     = values.get_float("length");
        const float thickness  = values.get_float("thickness");
        const float small_bore = std::min(0.5f * values.get_float("small_bore"), 0.1f * length);
        const float big_bore   = std::min(0.5f * values.get_float("big_bore"), 0.16f * length);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 7u);
        EXPECT_EQ(report.triangle_count, 284u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, thickness);
        EXPECT_NEAR(report.bounds_min.x, (-0.5f * length) - (1.8f * small_bore), 1.0e-5f);
        EXPECT_NEAR(report.bounds_max.x, (0.5f * length) + (1.6f * big_bore), 1.0e-5f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_NEAR(least_reach_from_y_line(*mesh, -0.5f * length, 0.0f), small_bore, 1.0e-5f);
        EXPECT_NEAR(least_reach_from_y_line(*mesh,  0.5f * length, 0.0f), big_bore, 1.0e-5f);
        expect_connector_names(values, {"base", "small_end", "big_end"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[1].position, (glm::vec3{-0.5f * length, 0.5f * thickness, 0.0f}));
        EXPECT_EQ(connectors[2].position, (glm::vec3{ 0.5f * length, 0.5f * thickness, 0.0f}));
        EXPECT_EQ(connectors[1].outward, (glm::vec3{-1.0f, 0.0f, 0.0f}));
        EXPECT_EQ(connectors[2].outward, (glm::vec3{ 1.0f, 0.0f, 0.0f}));
    }
}

// The default big end takes the default crankshaft's pin.
TEST(Kit_connecting_rod, the_default_big_end_fits_the_default_crank_pin)
{
    const Kit_parameter_values rod   = explicit_values(get_shape("machine.connecting_rod"), 1);
    const Kit_parameter_values crank = explicit_values(get_shape("machine.crankshaft"), 1);
    EXPECT_EQ(rod.get_float("big_bore"), crank.get_float("journal"));
}

//
// Crankshaft.
//

const std::vector<std::string> crank_floats{"stroke", "journal", "web"};

class Kit_crankshaft_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_crankshaft_forms, every_throw_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& crank = get_shape("machine.crankshaft");
    ASSERT_EQ(crank.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (int throws = 1; throws <= 6; ++throws) {
        Kit_parameter_values values = explicit_enum(crank, "phase", GetParam(), 1);
        ASSERT_TRUE(values.set_int("throws", throws));
        for_each_corner(values, crank_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, crank_floats);
    }
}

// Main journals one more than the throws, a pin and two webs for each throw, and two stands. The first throw's pin
// stands half the stroke over the axis; a cross shaft's second throw turns a quarter toward +Z.
TEST_P(Kit_crankshaft_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& crank = get_shape("machine.crankshaft");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(crank, "phase", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int throws  = static_cast<unsigned int>(values.get_int("throws"));
        const float        radius  = 0.5f * values.get_float("stroke");
        const float        journal = values.get_float("journal");
        const float        web     = values.get_float("web");
        const float        length  = (static_cast<float>(throws + 1u) * 1.5f * journal) + (static_cast<float>(throws) * ((2.0f * web) + journal));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (4u * throws) + 3u);
        EXPECT_EQ(report.triangle_count, (44u * ((2u * throws) + 1u)) + (24u * throws) + 24u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_min.x, -0.5f * length, 1.0e-5f);
        EXPECT_NEAR(report.bounds_max.x,  0.5f * length, 1.0e-5f);
        expect_connector_names(values, {"start", "end", "base", "pin"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        const float axis_y = connectors[0].position.y;
        EXPECT_NEAR(connectors[0].position.x, -0.5f * length, 1.0e-5f);
        EXPECT_NEAR(connectors[1].position.x,  0.5f * length, 1.0e-5f);
        EXPECT_EQ(connectors[1].position.y, axis_y);
        EXPECT_NEAR(connectors[3].position.y - axis_y, radius, 1.0e-5f);
        EXPECT_EQ(connectors[3].position.z, 0.0f);
        EXPECT_EQ(connectors[3].outward, (glm::vec3{0.0f, 1.0f, 0.0f}));
        // The first web reaches 0.8 journals past the pin, over the pin connector.
        EXPECT_NEAR(report.bounds_max.y, axis_y + radius + (0.8f * journal), 1.0e-5f);
        // The pin's two rings of twelve points around the pin connector's line along X.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        int pin_points = 0;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            const bool on_pin = std::abs(glm::length(glm::vec2{p.y - connectors[3].position.y, p.z}) - (0.5f * journal)) < 1.0e-5f;
            pin_points += (on_pin && (std::abs(p.x - connectors[3].position.x) < (0.5f * journal) + web)) ? 1 : 0;
        }
        EXPECT_EQ(pin_points, 24);
        if ((GetParam() == "cross") && (throws >= 2u)) {
            EXPECT_NEAR(report.bounds_max.z, radius + (0.8f * journal), 1.0e-5f);
        } else {
            EXPECT_NEAR(report.bounds_max.z, 0.8f * journal, 1.0e-5f);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_crankshaft, Kit_crankshaft_forms, testing::Values("flat", "cross"));

//
// Follower.
//

const std::vector<std::string> follower_floats{"stem", "diameter"};

class Kit_follower_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_follower_forms, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& follower = get_shape("machine.follower");
    ASSERT_EQ(follower.material_slots, (std::vector<std::string>{"steel", "metal"}));
    const Kit_parameter_values values = explicit_enum(follower, "form", GetParam(), 1);
    for_each_corner(values, follower_floats, expect_valid_with_every_slot);
    expect_topology_from_integers(values, follower_floats);
}

// The contact, the stem and its guide sleeve; a roller stands in a fork under a flange. The contact is on the
// ground around the origin (a knife's edge and a roller's lowest side along Z), the top on the stem's end.
TEST_P(Kit_follower_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& follower = get_shape("machine.follower");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(follower, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float d = values.get_float("diameter");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        const bool roller = (GetParam() == "roller");
        const bool flat   = (GetParam() == "flat");
        EXPECT_EQ(report.component_count, roller ? 6u : 3u);
        EXPECT_EQ(report.triangle_count, roller ? 220u : (flat ? 160u : 128u));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_NEAR(report.bounds_max.x, flat ? (1.5f * d * std::cos(pi / 12.0f)) : d, 1.0e-5f);
        expect_connector_names(values, {"base", "contact", "top"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[1].position, glm::vec3{0.0f});
        EXPECT_EQ(connectors[1].outward, (glm::vec3{0.0f, -1.0f, 0.0f}));
        EXPECT_NEAR(connectors[2].position.y, report.bounds_max.y, 1.0e-5f);
        if (!flat) {
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            // A narrow strip along Z around the origin: a roller's ring side, a knife's land.
            const float strip     = roller ? (d * std::sin(pi / 12.0f)) : (0.05f * d);
            int         on_ground = 0;
            float       least_x   = 0.0f;
            float       most_x    = 0.0f;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const glm::vec3 p = point_of(*mesh, v);
                if (std::abs(p.y) < 1.0e-6f) {
                    EXPECT_LE(std::abs(p.x), strip + 1.0e-6f);
                    least_x = std::min(least_x, p.x);
                    most_x  = std::max(most_x, p.x);
                    ++on_ground;
                }
            }
            EXPECT_EQ(on_ground, 4);
            EXPECT_LT(least_x, 0.0f);
            EXPECT_GT(most_x, 0.0f);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_follower, Kit_follower_forms, testing::Values("knife", "roller", "flat"));

//
// Camshaft.
//

const std::vector<std::string> camshaft_floats{"length", "lift", "radius", "step"};

TEST(Kit_camshaft, every_lobe_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& camshaft = get_shape("machine.camshaft");
    ASSERT_EQ(camshaft.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (int lobes = 1; lobes <= 8; ++lobes) {
        Kit_parameter_values values = explicit_values(camshaft, 1);
        ASSERT_TRUE(values.set_int("lobes", lobes));
        for_each_corner(values, camshaft_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, camshaft_floats);
    }
}

// A shaft, the lobes and two stands; the first lobe's nose stands up the radius and lift over the axis, and each
// next lobe's nose is turned by the step about X, from +Y toward +Z.
TEST(Kit_camshaft, parts_size_and_lobe_turns_follow_the_values)
{
    const Kit_shape_definition& camshaft = get_shape("machine.camshaft");
    for (const int seed : derive_seeds()) {
        for (const float step : {0.0f, 30.0f, 90.0f, 135.0f}) {
            Kit_parameter_values values = all_derived(camshaft, seed);
            values.set_value_source(camshaft.find_parameter("step").value(), editor::Kit_value_source::explicit_value);
            ASSERT_TRUE(values.set_float("step", step));
            SCOPED_TRACE(values.to_canonical_text());
            const unsigned int lobes  = static_cast<unsigned int>(values.get_int("lobes"));
            const float        length = values.get_float("length");
            const float        radius = values.get_float("radius");
            const float        lift   = values.get_float("lift");
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, lobes + 3u);
            EXPECT_EQ(report.triangle_count, 68u + (128u * lobes));
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_NEAR(report.bounds_min.x, -0.5f * length, 1.0e-5f);
            EXPECT_NEAR(report.bounds_max.x,  0.5f * length, 1.0e-5f);
            expect_connector_names(values, {"start", "end", "base"});
            const std::vector<Kit_connector> connectors = connectors_of(values);
            const float axis_y = connectors[0].position.y;
            EXPECT_EQ(connectors[1].position.y, axis_y);
            EXPECT_NEAR(connectors[0].position.x, -0.5f * length, 1.0e-6f);
            EXPECT_NEAR(connectors[1].position.x,  0.5f * length, 1.0e-6f);
            EXPECT_NEAR(report.bounds_max.y, axis_y + radius + lift, 1.0e-5f);
            // Each lobe's farthest point from the axis: its nose.
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            const float cell = length / static_cast<float>(lobes + 1u);
            for (unsigned int k = 0; k < lobes; ++k) {
                const float x0 = (-0.5f * length) + (cell * static_cast<float>(k + 1u)) - (0.2f * cell);
                float     most = 0.0f;
                glm::vec2 nose{0.0f};
                for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                    const glm::vec3 p = point_of(*mesh, v);
                    const glm::vec2 q{p.y - axis_y, p.z};
                    if ((p.x >= x0 - 1.0e-5f) && (p.x <= x0 + (0.4f * cell) + 1.0e-5f) && (glm::length(q) > most)) {
                        most = glm::length(q);
                        nose = q;
                    }
                }
                EXPECT_NEAR(most, radius + lift, 1.0e-5f) << k;
                const float turn = glm::radians(step) * static_cast<float>(k);
                EXPECT_NEAR(nose.x, (radius + lift) * std::cos(turn), 1.0e-5f) << k;
                EXPECT_NEAR(nose.y, (radius + lift) * std::sin(turn), 1.0e-5f) << k;
            }
        }
    }
}

//
// Bevel gear.
//

const std::vector<std::string> bevel_floats{"module", "face_width", "cone", "bore"};

TEST(Kit_bevel_gear, every_tooth_count_passes_at_the_corners)
{
    const Kit_shape_definition& bevel = get_shape("machine.bevel_gear");
    ASSERT_EQ(bevel.material_slots, (std::vector<std::string>{"metal"}));
    for (const int teeth : {8, 9, 16, 23, 32}) {
        Kit_parameter_values values = explicit_values(bevel, 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        for_each_corner(values, bevel_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, bevel_floats);
    }
}

// The body and a tooth for each of the teeth. The mesh connector is on the large end's pitch circle (the teeth
// times the module across), facing across the cone; the bore is at most half the small end.
TEST(Kit_bevel_gear, parts_size_and_the_mesh_point_follow_the_values)
{
    const Kit_shape_definition& bevel = get_shape("machine.bevel_gear");
    for (const int seed : derive_seeds()) {
        for (const float cone : {30.0f, 45.0f, 60.0f}) {
            Kit_parameter_values values = all_derived(bevel, seed);
            values.set_value_source(bevel.find_parameter("cone").value(), editor::Kit_value_source::explicit_value);
            ASSERT_TRUE(values.set_float("cone", cone));
            SCOPED_TRACE(values.to_canonical_text());
            const unsigned int teeth    = static_cast<unsigned int>(values.get_int("teeth"));
            const float        module   = values.get_float("module");
            const float        pitch    = 0.5f * module * static_cast<float>(teeth);
            const float        angle    = glm::radians(cone);
            const float        share    = std::clamp(1.0f - ((values.get_float("face_width") * std::sin(angle)) / pitch), 0.4f, 0.95f);
            const unsigned int segments = std::clamp(2u * teeth, 16u, 48u);
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, teeth + 1u);
            EXPECT_EQ(report.triangle_count, (10u * segments) + (12u * teeth));
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_GE(report.bounds_max.x, pitch);
            EXPECT_LE(report.bounds_max.x, std::hypot(pitch + (module * std::cos(angle)), 0.4f * module) + 1.0e-5f);
            expect_connector_names(values, {"base", "axis", "mesh"});
            const std::vector<Kit_connector> connectors = connectors_of(values);
            EXPECT_FLOAT_EQ(connectors[2].position.x, pitch);
            EXPECT_EQ(connectors[2].position.z, 0.0f);
            EXPECT_NEAR(connectors[2].outward.x, std::cos(angle), 1.0e-6f);
            EXPECT_NEAR(connectors[2].outward.y, std::sin(angle), 1.0e-6f);
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            // The body's pitch ring lies at the mesh connector's height.
            int pitch_ring = 0;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const glm::vec3 p = point_of(*mesh, v);
                const bool on_ring = std::abs(glm::length(glm::vec2{p.x, p.z}) - pitch) < 1.0e-5f;
                pitch_ring += (on_ring && (std::abs(p.y - connectors[2].position.y) < 1.0e-5f)) ? 1 : 0;
            }
            EXPECT_EQ(pitch_ring, static_cast<int>(segments));
            // The bore; the axis connector is on the bore's top rim.
            const float bore  = std::min(0.5f * values.get_float("bore"), 0.5f * share * pitch);
            float       least = 1.0e9f;
            float       rim   = 0.0f;
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const glm::vec3 p     = point_of(*mesh, v);
                const float     reach = glm::length(glm::vec2{p.x, p.z});
                least = std::min(least, reach);
                if (std::abs(reach - bore) < 1.0e-5f) {
                    rim = std::max(rim, p.y);
                }
            }
            EXPECT_NEAR(least, bore, 1.0e-5f);
            EXPECT_NEAR(connectors[1].position.y, rim, 1.0e-5f);
        }
    }
}

//
// Worm.
//

const std::vector<std::string> worm_floats{"module", "diameter"};

TEST(Kit_worm_forms, every_start_and_turn_count_of_a_worm_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& worm = get_shape("machine.worm");
    ASSERT_EQ(worm.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (int starts = 1; starts <= 3; ++starts) {
        for (const int turns : {2, 3, 6}) {
            Kit_parameter_values values = explicit_enum(worm, "form", "worm", 1);
            ASSERT_TRUE(values.set_int("starts", starts));
            ASSERT_TRUE(values.set_int("turns", turns));
            for_each_corner(values, worm_floats, expect_valid_with_every_slot);
            expect_topology_from_integers(values, worm_floats);
        }
    }
}

TEST(Kit_worm_forms, every_tooth_count_of_a_wheel_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& worm = get_shape("machine.worm");
    for (const int teeth : {16, 17, 24, 40}) {
        Kit_parameter_values values = explicit_enum(worm, "form", "wheel", 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        for_each_corner(values, worm_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, worm_floats);
    }
}

// A shaft and a thread for each start, of twelve stretches to a turn; the threaded part is the lead (pi times the
// module for each start) times the turns long, and the worm rests on its thread: as deep under the axis as its top
// is over it.
TEST(Kit_worm, a_worms_parts_and_size_follow_the_values)
{
    const Kit_shape_definition& worm = get_shape("machine.worm");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(worm, "form", "worm", seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int starts = static_cast<unsigned int>(values.get_int("starts"));
        const unsigned int turns  = static_cast<unsigned int>(values.get_int("turns"));
        const float        module = values.get_float("module");
        const float        radius = std::max(0.5f * values.get_float("diameter"), 1.5f * module);
        const float        lead   = pi * module * static_cast<float>(starts);
        const float        thread = lead * static_cast<float>(turns);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, starts + 1u);
        EXPECT_EQ(report.triangle_count, 44u + (starts * ((96u * turns) + 4u)));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        expect_connector_names(values, {"base", "axis", "mesh"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        const float axis_y = connectors[1].position.y;
        EXPECT_NEAR(report.bounds_max.y, 2.0f * axis_y, 1.0e-5f);
        EXPECT_NEAR(connectors[2].position.y, axis_y + radius, 1.0e-6f);
        EXPECT_EQ(connectors[2].position.x, 0.0f);
        EXPECT_FLOAT_EQ(connectors[1].position.x, report.bounds_max.x);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -report.bounds_max.x);
        // The thread's outer edge runs the threaded part's length.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float thread_min = 1.0e9f;
        float thread_max = -1.0e9f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (glm::length(glm::vec2{p.y - axis_y, p.z}) > (1.01f * radius)) {
                thread_min = std::min(thread_min, p.x);
                thread_max = std::max(thread_max, p.x);
            }
        }
        EXPECT_NEAR(thread_min, -0.5f * thread, 1.0e-5f);
        EXPECT_NEAR(thread_max,  0.5f * thread, 1.0e-5f);
    }
}

// A wheel, its bush and a tooth for each of the teeth. Its pitch diameter is the teeth times the module, where the
// mesh connector is; the axis connector is on the bush's top.
TEST(Kit_worm, a_wheels_parts_and_pitch_follow_the_values)
{
    const Kit_shape_definition& worm = get_shape("machine.worm");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(worm, "form", "wheel", seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int teeth    = static_cast<unsigned int>(values.get_int("teeth"));
        const float        module   = values.get_float("module");
        const float        pitch    = 0.5f * module * static_cast<float>(teeth);
        const unsigned int segments = std::clamp(2u * teeth, 16u, 48u);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, teeth + 2u);
        EXPECT_EQ(report.triangle_count, (8u * segments) + 96u + (20u * teeth));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_LE(report.bounds_max.x, std::hypot(pitch + module, 0.4f * module) + 1.0e-5f);
        EXPECT_GE(report.bounds_max.x, (pitch + module) * std::cos(pi / static_cast<float>(teeth)));
        expect_connector_names(values, {"base", "axis", "mesh"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[1].position.y, report.bounds_max.y);
        EXPECT_FLOAT_EQ(connectors[2].position.x, pitch);
        EXPECT_EQ(connectors[2].outward, (glm::vec3{1.0f, 0.0f, 0.0f}));
    }
}

// A worm and its wheel are one piece's two forms: they share the module, so they mesh.
TEST(Kit_worm, a_worm_and_its_wheel_have_one_module)
{
    const Kit_shape_definition& worm  = get_shape("machine.worm");
    const Kit_parameter_values  shaft = explicit_enum(worm, "form", "worm", 1);
    const Kit_parameter_values  wheel = explicit_enum(worm, "form", "wheel", 1);
    EXPECT_EQ(shaft.get_float("module"), wheel.get_float("module"));
    // The worm's lead for one start is the wheel's pitch along its pitch circle.
    const float circle = 2.0f * pi * 0.5f * wheel.get_float("module") * static_cast<float>(wheel.get_int("teeth"));
    EXPECT_NEAR(circle / static_cast<float>(wheel.get_int("teeth")), pi * shaft.get_float("module"), 1.0e-6f);
}

//
// Pulley.
//

const std::vector<std::string> pulley_floats{"diameter", "width", "bore"};

TEST(Kit_pulley_forms, every_groove_count_of_a_pulley_passes_at_the_corners)
{
    const Kit_shape_definition& pulley = get_shape("machine.pulley");
    ASSERT_EQ(pulley.material_slots, (std::vector<std::string>{"metal"}));
    for (int grooves = 1; grooves <= 4; ++grooves) {
        Kit_parameter_values values = explicit_enum(pulley, "form", "pulley", 1);
        ASSERT_TRUE(values.set_int("grooves", grooves));
        for_each_corner(values, pulley_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, pulley_floats);
    }
}

TEST(Kit_pulley_forms, every_tooth_count_of_a_sprocket_passes_at_the_corners)
{
    const Kit_shape_definition& pulley = get_shape("machine.pulley");
    for (const int teeth : {8, 9, 16, 32}) {
        Kit_parameter_values values = explicit_enum(pulley, "form", "sprocket", 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        for_each_corner(values, pulley_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, pulley_floats);
    }
}

// One lathe of 24 sides, the diameter across, with a groove bottom ring for each groove, bored through.
TEST(Kit_pulley, a_pulleys_grooves_and_bore_follow_the_values)
{
    const Kit_shape_definition& pulley = get_shape("machine.pulley");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(pulley, "form", "pulley", seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int grooves = static_cast<unsigned int>(values.get_int("grooves"));
        const float        radius  = 0.5f * values.get_float("diameter");
        const float        bore    = std::min(0.5f * values.get_float("bore"), 0.4f * radius);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 1u);
        EXPECT_EQ(report.triangle_count, 48u * (4u + (3u * grooves)));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("width"));
        EXPECT_NEAR(report.bounds_max.x, radius, 1.0e-6f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_NEAR(least_reach_from_y_line(*mesh, 0.0f, 0.0f), bore, 1.0e-6f);
        // The groove bottoms: the points neither on the rim nor on the bore.
        int bottoms = 0;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p     = point_of(*mesh, v);
            const float     reach = glm::length(glm::vec2{p.x, p.z});
            bottoms += ((reach > (bore + 1.0e-5f)) && (reach < (radius - 1.0e-5f))) ? 1 : 0;
        }
        EXPECT_EQ(bottoms, 24 * static_cast<int>(grooves));
        expect_connector_names(values, {"base", "axis"});
        EXPECT_FLOAT_EQ(connectors_of(values)[1].position.y, values.get_float("width"));
    }
}

// A toothed plate under a hub, its pitch circle the diameter: a module of the diameter over the teeth.
TEST(Kit_pulley, a_sprockets_teeth_and_bore_follow_the_values)
{
    const Kit_shape_definition& pulley = get_shape("machine.pulley");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(pulley, "form", "sprocket", seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int teeth    = static_cast<unsigned int>(values.get_int("teeth"));
        const float        radius   = 0.5f * values.get_float("diameter");
        const float        module   = (2.0f * radius) / static_cast<float>(teeth);
        const float        bore     = std::min(0.5f * values.get_float("bore"), 0.3f * (radius - (1.25f * module)));
        const unsigned int segments = std::clamp(2u * teeth, 16u, 48u);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, teeth + 1u);
        EXPECT_EQ(report.triangle_count, (12u * segments) + (20u * teeth));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("width"));
        EXPECT_LE(report.bounds_max.x, std::hypot(radius + module, 0.4f * module) + 1.0e-5f);
        EXPECT_GE(report.bounds_max.x, (radius + module) * std::cos(pi / static_cast<float>(teeth)));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_NEAR(least_reach_from_y_line(*mesh, 0.0f, 0.0f), bore, 1.0e-6f);
        expect_connector_names(values, {"base", "axis"});
        EXPECT_FLOAT_EQ(connectors_of(values)[1].position.y, values.get_float("width"));
    }
}

//
// Washer.
//

const std::vector<std::string> washer_floats{"diameter", "thickness"};

class Kit_washer_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_washer_forms, every_tooth_count_passes_at_the_corners)
{
    const Kit_shape_definition& washer = get_shape("machine.washer");
    ASSERT_EQ(washer.material_slots, (std::vector<std::string>{"steel"}));
    for (const int teeth : {6, 7, 16}) {
        Kit_parameter_values values = explicit_enum(washer, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        for_each_corner(values, washer_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, washer_floats);
    }
}

// Every form takes a bolt of its diameter, at the corners of the ranges too: no facet crosses the axis and no point
// lies inside the bolt.
TEST_P(Kit_washer_forms, a_bolt_of_the_diameter_goes_through_at_the_corners)
{
    const Kit_shape_definition& washer = get_shape("machine.washer");
    const Kit_parameter_values values = explicit_enum(washer, "form", GetParam(), 1);
    for_each_corner(values, washer_floats, [](const Kit_parameter_values& corner) {
        const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
        const float bolt = 0.5f * corner.get_float("diameter");
        EXPECT_FALSE(y_axis_crosses_a_facet(*mesh)) << corner.to_canonical_text();
        EXPECT_GE(least_reach_from_y_line(*mesh, 0.0f, 0.0f), bolt * (1.0f - 1.0e-5f)) << corner.to_canonical_text();
    });
}

// A flat ring of the thickness, the same ring with a tooth for each of the teeth around it, or a lock washer's one
// wire whose end stands over its start; the top connector is on the washer's highest point.
TEST_P(Kit_washer_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& washer = get_shape("machine.washer");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(washer, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int teeth = static_cast<unsigned int>(values.get_int("teeth"));
        const float        d     = values.get_float("diameter");
        const float        t     = values.get_float("thickness");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        if (GetParam() == "flat") {
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_EQ(report.triangle_count, 128u);
            EXPECT_FLOAT_EQ(report.bounds_max.y, t);
            EXPECT_NEAR(report.bounds_max.x, 1.1f * d, 1.0e-6f);
        } else if (GetParam() == "toothed") {
            EXPECT_EQ(report.component_count, teeth + 1u);
            EXPECT_EQ(report.triangle_count, 128u + (12u * teeth));
            EXPECT_FLOAT_EQ(report.bounds_max.y, t);
            // The teeth stand out of the ring.
            EXPECT_GT(report.bounds_max.x, 1.2f * d);
            EXPECT_LE(report.bounds_max.x, 1.45f * d);
        } else {
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_EQ(report.triangle_count, 100u);
            EXPECT_NEAR(report.bounds_max.y, 4.0f * t, 1.0e-6f);
        }
        expect_connector_names(values, {"base", "top"});
        EXPECT_NEAR(connectors_of(values)[1].position.y, report.bounds_max.y, 1.0e-6f);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_washer, Kit_washer_forms, testing::Values("flat", "lock", "toothed"));

//
// Cylinder.
//

const std::vector<std::string> cylinder_floats{"bore", "height", "wall"};

TEST(Kit_cylinder, every_fin_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& cylinder = get_shape("machine.cylinder");
    ASSERT_EQ(cylinder.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (const int fins : {0, 1, 5, 10}) {
        Kit_parameter_values values = explicit_values(cylinder, 1);
        ASSERT_TRUE(values.set_int("fins", fins));
        for_each_corner(values, cylinder_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, cylinder_floats);
    }
}

// A liner and a jacket with its foot flange and a ring of fins for each of the fins; the bore takes a piston of
// the bore's diameter.
TEST(Kit_cylinder, fins_size_and_bore_follow_the_values)
{
    const Kit_shape_definition& cylinder = get_shape("machine.cylinder");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(cylinder, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int fins   = static_cast<unsigned int>(values.get_int("fins"));
        const float        bore   = 0.5f * values.get_float("bore");
        const float        height = values.get_float("height");
        const float        wall   = values.get_float("wall");
        const float        outer  = (1.03f * bore) + wall;
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u);
        EXPECT_EQ(report.triangle_count, 320u + (128u * fins));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_NEAR(report.bounds_max.x, outer + (1.5f * wall), 1.0e-5f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_EQ(count_at_reach(*mesh, outer + (1.2f * wall), 1.0e-5f), 32 * static_cast<int>(fins));
        const float least = least_reach_from_y_line(*mesh, 0.0f, 0.0f);
        EXPECT_GT(least, bore);
        EXPECT_LT(least, 1.05f * bore);
        expect_connector_names(values, {"base", "top"});
        EXPECT_FLOAT_EQ(connectors_of(values)[1].position.y, height);
    }
}

//
// Pillow block.
//

const std::vector<std::string> pillow_floats{"bore", "height", "width", "depth"};

TEST(Kit_pillow_block, every_bolt_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& pillow = get_shape("machine.pillow_block");
    ASSERT_EQ(pillow.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (int bolts = 2; bolts <= 4; ++bolts) {
        Kit_parameter_values values = explicit_values(pillow, 1);
        ASSERT_TRUE(values.set_int("bolts", bolts));
        for_each_corner(values, pillow_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, pillow_floats);
    }
}

// A base, a pedestal, the housing and its bush, and a bolt head for each bolt (three bolts make two). The axis
// connector is on the shaft's line at the height, on the +Z face.
TEST(Kit_pillow_block, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& pillow = get_shape("machine.pillow_block");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(pillow, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int heads  = 2u * (static_cast<unsigned int>(values.get_int("bolts")) / 2u);
        const float        height = values.get_float("height");
        const float        width  = values.get_float("width");
        const float        depth  = values.get_float("depth");
        const float        bore   = std::min(0.5f * values.get_float("bore"), 0.5f * height);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 4u + heads);
        EXPECT_EQ(report.triangle_count, 248u + (20u * heads));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_max.y, height + bore + (0.3f * height), 1.0e-5f);
        EXPECT_FLOAT_EQ(report.bounds_max.x, std::max(0.5f * width, bore + (0.3f * height)));
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * depth);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * depth);
        expect_connector_names(values, {"base", "axis"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[1].position, (glm::vec3{0.0f, height, 0.5f * depth}));
        EXPECT_EQ(connectors[1].outward, (glm::vec3{0.0f, 0.0f, 1.0f}));
    }
}

// A shaft of the bore's diameter goes through the housing and its bush.
TEST(Kit_pillow_block, a_shaft_of_the_bore_goes_through)
{
    const Kit_shape_definition& pillow = get_shape("machine.pillow_block");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(pillow, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float height = values.get_float("height");
        const float shaft  = std::min(0.5f * values.get_float("bore"), 0.5f * height);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float least = 1.0e9f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            least = std::min(least, glm::length(glm::vec2{p.x, p.y - height}));
        }
        EXPECT_GE(least, shaft);
    }
}

} // anonymous namespace
