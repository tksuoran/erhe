#include "kit_shape_test_support.hpp"

#include "level/kit_connector.hpp"
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

// Tests of the shapes of kit_shapes_machine_01_08.cpp.
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

constexpr float pi = std::numbers::pi_v<float>;

// The mean z of the top points beyond the given distance from the Y axis whose direction lies nearest +X.
[[nodiscard]] auto nearest_tip_z(const GEO::Mesh& mesh, const float top, const float beyond, const float z_reach) -> float
{
    float sum   = 0.0f;
    int   count = 0;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point_of(mesh, v);
        if ((std::abs(p.y - top) < 1.0e-5f) && (p.x > 0.0f) && (glm::length(glm::vec2{p.x, p.z}) > beyond) && (std::abs(p.z) < z_reach)) {
            sum += p.z;
            ++count;
        }
    }
    return (count > 0) ? (sum / static_cast<float>(count)) : 1.0e9f;
}

//
// Gear.
//

const std::vector<std::string> gear_floats{"module", "face_width", "bore"};

class Kit_gear_bodies : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_gear_bodies, tooth_and_spoke_counts_pass_at_the_corners)
{
    const Kit_shape_definition& gear = get_shape("machine.gear");
    ASSERT_EQ(gear.material_slots, (std::vector<std::string>{"metal"}));
    for (const int teeth : {8, 9, 16, 23, 40}) {
        for (const int spokes : {3, 8}) {
            Kit_parameter_values values = explicit_enum(gear, "body", GetParam(), 1);
            ASSERT_TRUE(values.set_int("teeth", teeth));
            ASSERT_TRUE(values.set_int("spokes", spokes));
            for_each_corner(values, gear_floats, expect_valid_with_every_slot);
            expect_topology_from_integers(values, gear_floats);
        }
    }
}

// A body, a tooth for each of the teeth, and in a spoked gear a hub and the spokes besides. The pitch circle's
// diameter is the teeth times the module; the teeth stand a module beyond it.
TEST_P(Kit_gear_bodies, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& gear = get_shape("machine.gear");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(gear, "body", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int teeth  = static_cast<unsigned int>(values.get_int("teeth"));
        const unsigned int spokes = static_cast<unsigned int>(values.get_int("spokes"));
        const float        module = values.get_float("module");
        const float        pitch  = 0.5f * module * static_cast<float>(teeth);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (GetParam() == "spokes") ? (teeth + spokes + 2u) : (teeth + 1u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, 1.25f * values.get_float("face_width"));
        const float tip = std::hypot(pitch + module, 0.4f * module);
        EXPECT_LE(report.bounds_max.x, tip + 1.0e-5f);
        EXPECT_GE(report.bounds_max.x, (pitch + module) * std::cos(pi / static_cast<float>(teeth)));
        expect_connector_names(values, {"base", "axis", "mesh", "mesh_2", "mesh_3", "mesh_4"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[1].position.y, report.bounds_max.y);
        EXPECT_FLOAT_EQ(connectors[2].position.x, pitch);
        EXPECT_EQ(connectors[2].outward, (glm::vec3{1.0f, 0.0f, 0.0f}));
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_gear, Kit_gear_bodies, testing::Values("solid", "web", "spokes"));

// The mean z of the top tooth tips nearest +X once the mesh is turned about Y so that the direction at the angle
// (from +X toward -Z) lies on +X.
[[nodiscard]] auto turned_tip_z(const GEO::Mesh& mesh, const float angle, const float top, const float beyond, const float z_reach) -> float
{
    float sum   = 0.0f;
    int   count = 0;
    for (GEO::index_t v = 0; v < mesh.vertices.nb(); ++v) {
        const glm::vec3 p = point_of(mesh, v);
        const glm::vec3 q{(p.x * std::cos(angle)) - (p.z * std::sin(angle)), p.y, (p.x * std::sin(angle)) + (p.z * std::cos(angle))};
        if ((std::abs(q.y - top) < 1.0e-5f) && (q.x > 0.0f) && (glm::length(glm::vec2{q.x, q.z}) > beyond) && (std::abs(q.z) < z_reach)) {
            sum += q.z;
            ++count;
        }
    }
    return (count > 0) ? (sum / static_cast<float>(count)) : 1.0e9f;
}

// U53: mesh_2 - mesh_4 stand on the pitch circle half the face up, facing out, at the tooth at or before each quarter
// turn, with a tooth a quarter pitch before each as before mesh; so two gears joined by any two of their four
// boundary connectors engage, their axes the sum of the pitch radii apart.
TEST(Kit_gear, the_boundary_connectors_engage_as_the_mesh_connector_does)
{
    const Kit_shape_definition& gear = get_shape("machine.gear");
    for (int teeth = 8; teeth <= 40; ++teeth) {
        Kit_parameter_values values = explicit_values(gear, 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        SCOPED_TRACE(values.to_canonical_text());
        const float module = values.get_float("module");
        const float width  = values.get_float("face_width");
        const float pitch  = 0.5f * module * static_cast<float>(teeth);
        const float tooth  = (2.0f * pi) / static_cast<float>(teeth);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 6u);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        for (std::size_t k = 2; k < 6; ++k) {
            const Kit_connector& connector = connectors[k];
            const int            quarter   = static_cast<int>(k) - 2;
            const float          angle     = tooth * static_cast<float>((quarter * teeth) / 4);
            EXPECT_NEAR(connector.outward.x, std::cos(angle), 1.0e-5f) << connector.name;
            EXPECT_NEAR(connector.outward.z, -std::sin(angle), 1.0e-5f) << connector.name;
            EXPECT_EQ(connector.outward.y, 0.0f);
            EXPECT_EQ(connector.up, (glm::vec3{0.0f, 1.0f, 0.0f}));
            EXPECT_NEAR(connector.position.x, pitch * connector.outward.x, 1.0e-5f);
            EXPECT_NEAR(connector.position.z, pitch * connector.outward.z, 1.0e-5f);
            EXPECT_FLOAT_EQ(connector.position.y, 0.5f * width);
            const float z = turned_tip_z(*mesh, angle, width, pitch + (0.9f * module), 1.7f * module);
            EXPECT_NEAR(z, (pitch + module) * std::sin(0.25f * tooth), 1.0e-5f) << connector.name;
        }
    }
    // A gear of 16 teeth and one of 23, joined by each pair of their boundary connectors.
    Kit_parameter_values first  = explicit_values(gear, 1);
    Kit_parameter_values second = explicit_values(gear, 1);
    ASSERT_TRUE(first.set_int("teeth", 16));
    ASSERT_TRUE(second.set_int("teeth", 23));
    const float distance = 0.5f * first.get_float("module") * static_cast<float>(16 + 23);
    const std::vector<Kit_connector> a = connectors_of(first);
    const std::vector<Kit_connector> b = connectors_of(second);
    for (std::size_t i = 2; i < 6; ++i) {
        for (std::size_t j = 2; j < 6; ++j) {
            const editor::Kit_join_pose pose = editor::join_connectors(b[j], a[i], 0, 0.0f);
            EXPECT_NEAR(glm::length(glm::vec2{pose.position.x, pose.position.z}), distance, 1.0e-5f) << a[i].name << " " << b[j].name;
            EXPECT_NEAR(pose.position.y, 0.0f, 1.0e-6f);
            EXPECT_NEAR(glm::dot(glm::mat3_cast(pose.rotation) * glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}), 1.0f, 1.0e-6f);
        }
    }
}

// The tooth nearest the mesh connector stands a quarter pitch beside it, toward +Z, at every tooth count: two gears
// joined mesh to mesh (the second turned half around) have their teeth half a pitch apart.
TEST(Kit_gear, a_tooth_stands_a_quarter_pitch_beside_the_mesh_connector)
{
    const Kit_shape_definition& gear = get_shape("machine.gear");
    for (int teeth = 8; teeth <= 40; ++teeth) {
        Kit_parameter_values values = explicit_values(gear, 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        const float module = values.get_float("module");
        const float pitch  = 0.5f * module * static_cast<float>(teeth);
        const float angle  = (0.5f * pi) / static_cast<float>(teeth); // a quarter of a tooth's turn
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const float z = nearest_tip_z(*mesh, values.get_float("face_width"), pitch + (0.9f * module), 1.7f * module);
        EXPECT_NEAR(z, (pitch + module) * std::sin(angle), 1.0e-5f) << teeth;
        // On the pitch circle the tooth's middle is a quarter of the pitch (pi times the module) along the arc.
        EXPECT_NEAR(pitch * angle, 0.25f * pi * module, 1.0e-6f);
    }
}

// Two default gears and a default rack have one module: they mesh.
TEST(Kit_gear, the_default_gear_and_rack_have_one_module)
{
    const Kit_parameter_values gear = explicit_values(get_shape("machine.gear"), 1);
    const Kit_parameter_values rack = explicit_values(get_shape("machine.rack"), 1);
    EXPECT_EQ(gear.get_float("module"), rack.get_float("module"));
    EXPECT_EQ(gear.get_float("face_width"), rack.get_float("face_width"));
}

//
// Rack.
//

const std::vector<std::string> rack_floats{"module", "face_width", "height"};

TEST(Kit_rack, every_tooth_count_passes_at_the_corners)
{
    const Kit_shape_definition& rack = get_shape("machine.rack");
    ASSERT_EQ(rack.material_slots, (std::vector<std::string>{"metal"}));
    for (const int teeth : {4, 5, 12, 27, 40}) {
        Kit_parameter_values values = explicit_values(rack, 1);
        ASSERT_TRUE(values.set_int("teeth", teeth));
        for_each_corner(values, rack_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, rack_floats);
    }
}

// The bar is the teeth times the pitch long; a tooth stands a quarter pitch before the mesh connector, toward -X,
// and the end teeth stand half a pitch inside the ends, so chained racks keep the pitch.
TEST(Kit_rack, parts_size_and_the_mesh_point_follow_the_values)
{
    const Kit_shape_definition& rack = get_shape("machine.rack");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(rack, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int teeth  = static_cast<unsigned int>(values.get_int("teeth"));
        const float        module = values.get_float("module");
        const float        pitch  = pi * module;
        const float        half   = 0.5f * pitch * static_cast<float>(teeth);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, teeth + 1u);
        EXPECT_EQ(report.triangle_count, 12u + (20u * teeth));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("face_width"));
        EXPECT_FLOAT_EQ(report.bounds_min.x, -half);
        EXPECT_FLOAT_EQ(report.bounds_max.x, half);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -values.get_float("height"));
        EXPECT_NEAR(report.bounds_max.z, 2.25f * module, 1.0e-6f);
        expect_connector_names(values, {"start", "end", "base", "mesh"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[0].position.x, -half);
        EXPECT_FLOAT_EQ(connectors[1].position.x, half);
        EXPECT_NEAR(connectors[3].position.z, 1.25f * module, 1.0e-6f);
        // The tips of the tooth before the mesh point.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float sum   = 0.0f;
        int   count = 0;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if ((p.z > (2.2f * module)) && (p.y == 0.0f) && (std::abs(p.x - (connectors[3].position.x - (0.25f * pitch))) < (0.45f * pitch))) {
                sum += p.x;
                ++count;
            }
        }
        ASSERT_EQ(count, 2);
        EXPECT_NEAR(0.5f * sum, connectors[3].position.x - (0.25f * pitch), 1.0e-5f);
    }
}

//
// Bolt.
//

const std::vector<std::string> bolt_floats{"diameter", "length", "thread"};

class Kit_bolt_heads : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_bolt_heads, every_turn_count_passes_at_the_corners)
{
    const Kit_shape_definition& bolt = get_shape("machine.bolt");
    ASSERT_EQ(bolt.material_slots, (std::vector<std::string>{"steel"}));
    for (const char* const style : {"helix", "rings"}) {
        for (const int turns : {4, 10, 28, 29, 35, 38, 40}) {
            Kit_parameter_values values = explicit_enum(bolt, "head", GetParam(), 1);
            ASSERT_TRUE(values.set_enum("thread_style", style));
            ASSERT_TRUE(values.set_int("turns", turns));
            for_each_corner(values, bolt_floats, expect_valid_with_every_slot);
            expect_topology_from_integers(values, bolt_floats);
        }
    }
}

// A helical shank of four triangles for each point of a turn and each turn and one turn more, or a ringed shank of
// ten sides with two rings to a turn, under a head; the tip stands on the origin and the seat is the length over it.
TEST_P(Kit_bolt_heads, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& bolt = get_shape("machine.bolt");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(bolt, "head", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        d      = values.get_float("diameter");
        const float        length = values.get_float("length");
        const unsigned int turns  = static_cast<unsigned int>(values.get_int("turns"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        const bool headless = (GetParam() == "none");
        EXPECT_EQ(report.component_count, headless ? 1u : 2u);
        if (headless) {
            const unsigned int points = std::clamp(1880u / (4u * (turns + 1u)), 10u, 16u);
            EXPECT_EQ(report.triangle_count, (values.get_enum("thread_style") == "helix") ? (4u * points * (turns + 1u)) : (76u + (40u * turns)));
            EXPECT_FLOAT_EQ(report.bounds_max.y, length);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * d);
        } else {
            EXPECT_GT(report.bounds_max.y, length);
            EXPECT_LE(report.bounds_max.y, length + (1.01f * d));
            // The head is wider than the shank.
            EXPECT_GT(report.bounds_max.x, 0.7f * d);
        }
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        expect_connector_names(values, {"base", "seat", "nut"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[1].position.y, length);
        EXPECT_EQ(connectors[1].outward, (glm::vec3{0.0f, -1.0f, 0.0f}));
        // The nut's place lies on the thread, a diameter over the tip.
        EXPECT_FLOAT_EQ(connectors[2].position.y, std::min(d, length));
        EXPECT_LT(connectors[2].position.y, values.get_float("thread") * length);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_bolt, Kit_bolt_heads, testing::Values("hex", "socket", "round", "countersunk", "none"));

// U53: the ridge of a helical thread at full radius rises a turn for each turn about Y from +X toward -Z (a right-hand
// thread): every such point lies on one helix through the first ridge's start, 0.65 of a turn over the tip.
TEST(Kit_bolt, a_helical_ridge_rises_a_turn_for_each_turn)
{
    const Kit_shape_definition& bolt = get_shape("machine.bolt");
    for (int seed = 0; seed < 12; ++seed) {
        Kit_parameter_values values = derived_enum(bolt, "thread_style", "helix", seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float r      = 0.5f * values.get_float("diameter");
        const float thread = 0.88f * values.get_float("thread") * values.get_float("length");
        const float turn   = thread / static_cast<float>(values.get_int("turns"));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        int on_ridge = 0;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if ((std::abs(glm::length(glm::vec2{p.x, p.z}) - r) > (1.0e-5f * r)) || (p.y > thread)) {
                continue;
            }
            float angle = std::atan2(-p.z, p.x);
            angle += (angle < -1.0e-4f) ? (2.0f * pi) : 0.0f;
            const float turns_up = ((p.y - (0.65f * turn)) / turn) - (angle / (2.0f * pi));
            EXPECT_NEAR(turns_up, std::round(turns_up), 1.0e-3f) << p.x << " " << p.y << " " << p.z;
            ++on_ridge;
        }
        // The ridge points of every turn but the last, which runs out into the root.
        EXPECT_GE(on_ridge, ((values.get_int("turns") - 2) * 10) + 1);
    }
}

//
// Nut.
//

const std::vector<std::string> nut_floats{"diameter", "height"};

class Kit_nut_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_nut_forms, passes_at_the_corners)
{
    const Kit_shape_definition& nut = get_shape("machine.nut");
    ASSERT_EQ(nut.material_slots, (std::vector<std::string>{"steel"}));
    const Kit_parameter_values values = explicit_enum(nut, "form", GetParam(), 1);
    for_each_corner(values, nut_floats, expect_valid_with_every_slot);
    expect_topology_from_integers(values, nut_floats);
}

// The top connector is on the nut's highest point and the bolt connector on its bored body's top (a cap nut's on its
// base, facing down); the nut is wider
// than its bolt, and bored for it: the bore is the bolt's thread root wide (0.84 of the diameter).
TEST_P(Kit_nut_forms, size_and_bore_follow_the_values)
{
    const Kit_shape_definition& nut = get_shape("machine.nut");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(nut, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float d = values.get_float("diameter");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (GetParam() == "wing") ? 3u : ((GetParam() == "cap") ? 2u : 1u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_GT(report.bounds_max.x, 0.7f * d);
        expect_connector_names(values, {"base", "top", "bolt"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_NEAR(connectors[1].position.y, report.bounds_max.y, 1.0e-6f);
        if (GetParam() == "cap") {
            EXPECT_EQ(connectors[2].position, glm::vec3{0.0f});
            EXPECT_EQ(connectors[2].outward, (glm::vec3{0.0f, -1.0f, 0.0f}));
        } else {
            EXPECT_EQ(connectors[2].position, (glm::vec3{0.0f, values.get_float("height"), 0.0f}));
            EXPECT_EQ(connectors[2].outward, (glm::vec3{0.0f, 1.0f, 0.0f}));
        }
        // The points nearest the axis are the bore's.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float least = 1.0e9f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (p.y == 0.0f) {
                least = std::min(least, glm::length(glm::vec2{p.x, p.z}));
            }
        }
        EXPECT_NEAR(least, 0.42f * d, 1.0e-6f);
    }
}

// A default nut of each form joined by its bolt connector to a default bolt's nut connector (twist 0): its bored body
// stands around the shank, at or over the bolt's tip and under the nut connector, upright; a cap nut upside down with
// its dome past the tip (a_joined_cap_nut_covers_the_bolts_tip), the other nuts wholly at or over the tip.
TEST_P(Kit_nut_forms, a_joined_nut_sits_on_the_bolt_over_its_tip)
{
    const bool cap = (GetParam() == "cap");
    const Kit_parameter_values bolt = explicit_values(get_shape("machine.bolt"), 1);
    const Kit_parameter_values nut  = explicit_enum(get_shape("machine.nut"), "form", GetParam(), 1);
    const std::vector<Kit_connector> bolt_connectors = connectors_of(bolt);
    const std::vector<Kit_connector> nut_connectors  = connectors_of(nut);
    const float nut_y = bolt_connectors[2].position.y;
    const float h     = nut.get_float("height");
    EXPECT_GE(nut_y, h);
    const editor::Kit_join_pose pose = editor::join_connectors(nut_connectors[2], bolt_connectors[2], 0, 0.0f);
    const glm::mat3 rotation = glm::mat3_cast(pose.rotation);
    EXPECT_NEAR(glm::dot(rotation * glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}), cap ? -1.0f : 1.0f, 1.0e-6f);
    EXPECT_NEAR(glm::length(glm::vec2{pose.position.x, pose.position.z}), 0.0f, 1.0e-6f);
    const std::unique_ptr<GEO::Mesh> mesh = generate(nut);
    float lowest = 1.0e9f;
    for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
        lowest = std::min(lowest, (pose.position + (rotation * point_of(*mesh, v))).y);
    }
    if (cap) {
        EXPECT_LT(lowest, 0.0f);
    } else {
        EXPECT_GE(lowest, -1.0e-6f);
    }
    // The connector is on the body's face away from the tip, so the body lies between the tip and the connector.
    EXPECT_NEAR((pose.position + (rotation * nut_connectors[2].position)).y, nut_y, 1.0e-6f);
    EXPECT_NEAR((pose.position + (rotation * glm::vec3{0.0f, cap ? h : 0.0f, 0.0f})).y, nut_y - h, 1.0e-6f);
    EXPECT_LT(nut_y, bolt.get_float("length"));
}

INSTANTIATE_TEST_SUITE_P(Kit_nut, Kit_nut_forms, testing::Values("hex", "square", "wing", "cap"));

// U53: a cap nut joined by its bolt connector to the nut connector of a bolt of its diameter (twist 0) hangs upside
// down, its hexagon between the connector and the tip and its dome below: the blind hole's ceiling lies past the
// tip, and the shank's widest ring (the thread's ridge) stays inside the sleeve and the dome around it.
TEST(Kit_nut, a_joined_cap_nut_covers_the_bolts_tip)
{
    for (int seed = 0; seed < 12; ++seed) {
        Kit_parameter_values bolt = all_derived(get_shape("machine.bolt"), seed);
        Kit_parameter_values nut  = derived_enum(get_shape("machine.nut"), "form", "cap", seed + 100);
        bolt.set_value_source(bolt.get_shape()->find_parameter("diameter").value(), editor::Kit_value_source::explicit_value);
        ASSERT_TRUE(bolt.set_float("diameter", nut.get_float("diameter")));
        SCOPED_TRACE(bolt.to_canonical_text() + " " + nut.to_canonical_text());
        const float d = nut.get_float("diameter");
        const float h = nut.get_float("height");
        const std::vector<Kit_connector> bolt_connectors = connectors_of(bolt);
        const std::vector<Kit_connector> nut_connectors  = connectors_of(nut);
        const float nut_y = bolt_connectors[2].position.y;
        const editor::Kit_join_pose pose = editor::join_connectors(nut_connectors[2], bolt_connectors[2], 0, 0.0f);
        const glm::mat3 rotation = glm::mat3_cast(pose.rotation);
        EXPECT_NEAR(glm::dot(rotation * glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, -1.0f, 0.0f}), 1.0f, 1.0e-6f);
        EXPECT_NEAR(glm::length(glm::vec3{pose.position.x, pose.position.y - nut_y, pose.position.z}), 0.0f, 1.0e-6f);
        // The nut's points in the bolt's frame: the hexagon's base at the connector, the rest toward and past the tip.
        const std::unique_ptr<GEO::Mesh> mesh = generate(nut);
        float highest = -1.0e9f;
        float ceiling = -1.0e9f; // the blind hole's ceiling: the higher of the two points on the axis
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = pose.position + (rotation * point_of(*mesh, v));
            highest = std::max(highest, p.y);
            if (glm::length(glm::vec2{p.x, p.z}) < 1.0e-6f) {
                ceiling = std::max(ceiling, p.y);
            }
        }
        EXPECT_NEAR(highest, nut_y, 1.0e-6f);
        EXPECT_LT(ceiling, -0.1f * d);
        // The outer profile below the hexagon, (radius, depth under the connector), as doc/editor/level_kit_shapes.md
        // "Nut" states it; a point of the shank under the hexagon lies inside the profile's flats.
        const float hole_ceiling = std::max(1.15f * d, h + (0.15f * d));
        const float dome_base    = std::max(hole_ceiling - (0.45f * d), h);
        const std::array<glm::vec2, 5> profile{{
            {0.76f * d, h}, {0.76f * d, dome_base}, {0.66f * d, dome_base + (0.38f * d)}, {0.38f * d, dome_base + (0.66f * d)}, {0.0f, dome_base + (0.76f * d)}
        }};
        EXPECT_NEAR(dome_base + (0.76f * d), nut_connectors[1].position.y, 1.0e-6f);
        const auto outer_radius = [&profile](const float depth) -> float {
            for (std::size_t i = 0; (i + 1) < profile.size(); ++i) {
                if (depth <= profile[i + 1].y) {
                    const float t = (depth - profile[i].y) / std::max(profile[i + 1].y - profile[i].y, 1.0e-9f);
                    return profile[i].x + (t * (profile[i + 1].x - profile[i].x));
                }
            }
            return 0.0f;
        };
        const std::unique_ptr<GEO::Mesh> shank = generate(bolt);
        int under = 0;
        for (GEO::index_t v = 0; v < shank->vertices.nb(); ++v) {
            const glm::vec3 p     = point_of(*shank, v);
            const float     depth = nut_y - p.y;
            if (depth > h) {
                EXPECT_LT(glm::length(glm::vec2{p.x, p.z}), outer_radius(depth) * std::cos(pi / 12.0f)) << p.y;
                ++under;
            }
        }
        // With no shank point under the hexagon the tip lies in its bore.
        EXPECT_TRUE((under > 0) || (h >= nut_y)) << h << " " << nut_y;
    }
}

//
// Spring.
//

const std::vector<std::string> spring_floats{"diameter", "wire", "length"};

class Kit_spring_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_spring_forms, every_coil_count_passes_at_the_corners)
{
    const Kit_shape_definition& spring = get_shape("machine.spring");
    ASSERT_EQ(spring.material_slots, (std::vector<std::string>{"steel"}));
    for (const int coils : {3, 4, 8, 16}) {
        Kit_parameter_values values = explicit_enum(spring, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("coils", coils));
        for_each_corner(values, spring_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, spring_floats);
    }
}

// The wire has twelve stretches to a coil, of eight triangles each; hooks and legs are parts of their own. The
// coil is the diameter across at the wire's middle, and the two connectors are on the spring's two ends.
TEST_P(Kit_spring_forms, parts_size_and_ends_follow_the_values)
{
    const Kit_shape_definition& spring = get_shape("machine.spring");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(spring, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int coils  = static_cast<unsigned int>(values.get_int("coils"));
        const float        radius = 0.5f * values.get_float("diameter");
        const float        wire   = values.get_float("wire");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        expect_connector_names(values, {"start", "end"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        const unsigned int helix = (96u * coils) + 4u;
        if (GetParam() == "compression") {
            const float length = std::max(values.get_float("length"), 1.08f * wire * static_cast<float>(coils + 1u));
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_EQ(report.triangle_count, helix);
            EXPECT_NEAR(report.bounds_max.y, length, 1.0e-5f);
            EXPECT_FLOAT_EQ(report.bounds_max.x, radius + (0.5f * wire));
            EXPECT_FLOAT_EQ(report.bounds_min.x, -radius - (0.5f * wire));
            EXPECT_EQ(connectors[0].position, glm::vec3{0.0f});
            EXPECT_NEAR(connectors[1].position.y, length, 1.0e-5f);
        } else if (GetParam() == "extension") {
            // One wire: the coil and eight stretches of each hook.
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_EQ(report.triangle_count, helix + 128u);
            // The hooks reach beyond the coils, which are wound wire on wire.
            const float body = 1.08f * wire * static_cast<float>(coils);
            EXPECT_GT(report.bounds_max.x, (0.5f * body) + (0.9f * radius));
            EXPECT_NEAR(report.bounds_max.x, -report.bounds_min.x, 1.0e-5f);
            EXPECT_NEAR(report.bounds_max.y, 2.0f * connectors[0].position.y, 1.0e-5f);
            EXPECT_LT(connectors[0].position.x, -0.5f * body);
            EXPECT_GT(connectors[1].position.x, 0.5f * body);
            EXPECT_LT(connectors[1].position.x, report.bounds_max.x);
        } else {
            // One wire: the coil and a stretch of each leg.
            EXPECT_EQ(report.component_count, 1u);
            EXPECT_EQ(report.triangle_count, helix + 16u);
            EXPECT_NEAR(report.bounds_max.y, 1.08f * wire * static_cast<float>(coils) + wire, 1.0e-5f);
            // The legs' ends: three coil radii out, on the ground toward +Z and at the top toward -Z.
            EXPECT_FLOAT_EQ(report.bounds_max.z, 3.0f * radius);
            EXPECT_FLOAT_EQ(report.bounds_min.z, -3.0f * radius);
            EXPECT_FLOAT_EQ(connectors[0].position.z, report.bounds_max.z);
            EXPECT_FLOAT_EQ(connectors[1].position.z, report.bounds_min.z);
            EXPECT_GT(connectors[1].position.y, connectors[0].position.y);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_spring, Kit_spring_forms, testing::Values("compression", "extension", "torsion"));

// An extension spring is one wire at every wire thickness (thin wires included): the hook at -X from its far point to
// the coil, the coil, then the hook at +X, four corners to a point; the coil's end sections are the hooks' joints.
TEST(Kit_spring, an_extension_springs_hooks_continue_the_coil)
{
    const Kit_shape_definition& spring = get_shape("machine.spring");
    for (const float wire : {0.004f, 0.006f, 0.02f, 0.1f}) {
        for (const int coils : {3, 8, 16}) {
            Kit_parameter_values values = explicit_enum(spring, "form", "extension", 1);
            ASSERT_TRUE(values.set_float("diameter", 0.2f));
            ASSERT_TRUE(values.set_float("wire", wire));
            ASSERT_TRUE(values.set_int("coils", coils));
            SCOPED_TRACE(values.to_canonical_text());
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            const auto section = [&mesh](const GEO::index_t first) -> glm::vec3 {
                return 0.25f * (point_of(*mesh, first) + point_of(*mesh, first + 1) + point_of(*mesh, first + 2) + point_of(*mesh, first + 3));
            };
            const GEO::index_t coil_points = static_cast<GEO::index_t>((12 * coils) + 1);
            const GEO::index_t hook_points = 8; // besides the joint
            ASSERT_EQ(mesh->vertices.nb(), 4u * (coil_points + (2u * hook_points)));
            // Each hook's far point stands half a ring (the coil's diameter, 0.2 m) over the coil's end, at its x.
            const glm::vec3 first = section(0);
            const glm::vec3 last  = section(4u * (coil_points + (2u * hook_points) - 1u));
            const glm::vec3 coil_start = section(4u * hook_points);
            const glm::vec3 coil_end   = section(4u * (hook_points + coil_points - 1u));
            EXPECT_NEAR(first.x, coil_start.x, 1.0e-5f);
            EXPECT_NEAR(last.x, coil_end.x, 1.0e-5f);
            EXPECT_GT(first.y, coil_start.y + 0.15f);
            EXPECT_GT(last.y, coil_end.y + 0.15f);
            const Kit_mesh_report report = editor::check_kit_shape(values);
            EXPECT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 1u);
            // The hooks reach no lower than the coils: the spring stands on the ground.
            EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        }
    }
}

// A compression spring's two end coils lie flat: twelve of the wire's points on the ground and as many at the top.
TEST(Kit_spring, a_compression_springs_end_coils_are_flat)
{
    const Kit_shape_definition& spring = get_shape("machine.spring");
    for (const int coils : {3, 5, 8, 16}) {
        Kit_parameter_values values = explicit_enum(spring, "form", "compression", 1);
        ASSERT_TRUE(values.set_int("coils", coils));
        const float length = values.get_float("length");
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        int low  = 0;
        int high = 0;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            low  += (p.y == 0.0f) ? 1 : 0;
            high += (std::abs(p.y - length) < 1.0e-5f) ? 1 : 0;
        }
        // One corner of each of the twelve points before the wire steps onto, or off, the next coil.
        EXPECT_EQ(low, 12) << coils;
        EXPECT_EQ(high, 12) << coils;
    }
}

//
// Bearing.
//

const std::vector<std::string> bearing_floats{"bore", "diameter", "width"};

class Kit_bearing_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_bearing_forms, every_element_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& bearing = get_shape("machine.bearing");
    ASSERT_EQ(bearing.material_slots, (std::vector<std::string>{"steel", "metal"}));
    for (const int elements : {6, 7, 10, 20}) {
        Kit_parameter_values values = explicit_enum(bearing, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("elements", elements));
        for_each_corner(values, bearing_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, bearing_floats);
    }
}

// Two races, a cage and the rolling elements; the bearing is its diameter across and its width high, and the
// cage alone has the slot metal.
TEST_P(Kit_bearing_forms, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& bearing = get_shape("machine.bearing");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(bearing, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int elements = static_cast<unsigned int>(values.get_int("elements"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, elements + 3u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("width"));
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * values.get_float("diameter"));
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * values.get_float("diameter"));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_EQ(count_slot_facets(*mesh, 1), 64);
        expect_connector_names(values, {"base", "axis"});
        EXPECT_FLOAT_EQ(connectors_of(values)[1].position.y, report.bounds_max.y);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_bearing, Kit_bearing_forms, testing::Values("ball", "roller", "needle"));

//
// Piston.
//

const std::vector<std::string> piston_floats{"diameter", "height"};

TEST(Kit_piston, every_ring_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& piston = get_shape("machine.piston");
    ASSERT_EQ(piston.material_slots, (std::vector<std::string>{"metal", "steel"}));
    for (int rings = 1; rings <= 4; ++rings) {
        Kit_parameter_values values = explicit_values(piston, 1);
        ASSERT_TRUE(values.set_int("rings", rings));
        for_each_corner(values, piston_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, piston_floats);
    }
}

// The body and the wrist pin; three bands of steel to a ring, and the pin's sides and ends. The pin connector is
// inside the piston, under its middle.
TEST(Kit_piston, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& piston = get_shape("machine.piston");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(piston, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const int   rings  = values.get_int("rings");
        const float radius = 0.5f * values.get_float("diameter");
        const float height = values.get_float("height");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        // Sixteen sides with a flat toward +X; the rings stand 0.008 radii out of the body.
        EXPECT_NEAR(report.bounds_max.x, 1.008f * radius * std::cos(pi / 16.0f), 1.0e-5f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_EQ(count_slot_facets(*mesh, 1), (48 * rings) + 10);
        expect_connector_names(values, {"base", "crown", "pin"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[1].position.y, height);
        EXPECT_GT(connectors[2].position.y, 0.25f * height);
        EXPECT_LT(connectors[2].position.y, 0.5f * height);
        EXPECT_EQ(connectors[2].outward, (glm::vec3{0.0f, -1.0f, 0.0f}));
    }
}

//
// Cam.
//

const std::vector<std::string> cam_floats{"radius", "lift", "thickness", "bore"};

class Kit_cam_profiles : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_cam_profiles, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& cam = get_shape("machine.cam");
    ASSERT_EQ(cam.material_slots, (std::vector<std::string>{"metal", "steel"}));
    const Kit_parameter_values values = explicit_enum(cam, "profile", GetParam(), 1);
    for_each_corner(values, cam_floats, expect_valid_with_every_slot);
    expect_topology_from_integers(values, cam_floats);
}

// The plate and its collar. The profile reaches the base circle's radius plus the lift toward +X, where the
// follower connector is, and no point of it lies inside the base circle.
TEST_P(Kit_cam_profiles, the_profile_reaches_from_the_base_circle_to_the_lift)
{
    const Kit_shape_definition& cam = get_shape("machine.cam");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(cam, "profile", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float radius = values.get_float("radius");
        const float lift   = values.get_float("lift");
        const float bore   = std::min(0.5f * values.get_float("bore"), 0.5f * radius);
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u);
        EXPECT_EQ(report.triangle_count, 384u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, 1.6f * values.get_float("thickness"));
        EXPECT_FLOAT_EQ(report.bounds_max.x, radius + lift);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float least = 1.0e9f;
        float most  = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p     = point_of(*mesh, v);
            const float     reach = glm::length(glm::vec2{p.x, p.z});
            // The plate's rim: beyond the collar.
            if ((p.y == 0.0f) && (reach > (1.01f * bore))) {
                least = std::min(least, reach);
                most  = std::max(most, reach);
            }
        }
        EXPECT_NEAR(least, radius, 1.0e-5f);
        EXPECT_NEAR(most, radius + lift, 1.0e-5f);
        expect_connector_names(values, {"base", "axis", "follower"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_FLOAT_EQ(connectors[1].position.y, report.bounds_max.y);
        EXPECT_FLOAT_EQ(connectors[2].position.x, radius + lift);
        EXPECT_EQ(connectors[2].outward, (glm::vec3{1.0f, 0.0f, 0.0f}));
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_cam, Kit_cam_profiles, testing::Values("eccentric", "pear", "heart", "snail"));

} // anonymous namespace
