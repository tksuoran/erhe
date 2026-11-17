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

// Tests of the shapes of kit_shapes_extra_17_24.cpp.
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

// The shape's every parameter of the intact shape has its type, range and default: Replace carries the values.
void expect_takes_the_parameters_of(const char* const ruin_id, const char* const intact_id)
{
    const Kit_shape_definition& intact = get_shape(intact_id);
    const Kit_shape_definition& ruin   = get_shape(ruin_id);
    for (const Kit_parameter& parameter : intact.parameters) {
        SCOPED_TRACE(parameter.name);
        ASSERT_TRUE(ruin.find_parameter(parameter.name).has_value());
        const Kit_parameter& other = parameter_of(ruin, parameter.name.c_str());
        EXPECT_EQ(other.type, parameter.type);
        EXPECT_EQ(other.unit, parameter.unit);
        EXPECT_EQ(other.default_scale, parameter.default_scale);
        EXPECT_EQ(other.float_default, parameter.float_default);
        EXPECT_EQ(other.float_min, parameter.float_min);
        EXPECT_EQ(other.float_max, parameter.float_max);
        EXPECT_EQ(other.int_default, parameter.int_default);
        EXPECT_EQ(other.int_min, parameter.int_min);
        EXPECT_EQ(other.int_max, parameter.int_max);
        EXPECT_EQ(other.enum_values, parameter.enum_values);
    }
}

// The shape's one light frame, named "light": a point light shining down, with no point of the mesh within `gap`.
[[nodiscard]] auto expect_one_light_in_the_open(const Kit_parameter_values& values, const float gap) -> glm::vec3
{
    std::vector<editor::Kit_light_frame> frames;
    editor::get_shape_light_frames(values, frames);
    EXPECT_EQ(frames.size(), 1u);
    if (frames.size() != 1u) {
        return glm::vec3{0.0f};
    }
    EXPECT_EQ(editor::check_light_frames(frames), "");
    EXPECT_EQ(std::string{frames[0].name}, "light");
    EXPECT_EQ(frames[0].start.type, editor::Kit_light_type::point);
    EXPECT_EQ(frames[0].direction, (glm::vec3{0.0f, -1.0f, 0.0f}));
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    float nearest = 1.0e6f;
    for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
        nearest = std::min(nearest, glm::distance(point_of(*mesh, v), frames[0].position));
    }
    EXPECT_GT(nearest, gap);
    return frames[0].position;
}

// The highest point of the slot's facets.
[[nodiscard]] auto slot_top(const GEO::Mesh& mesh, const uint32_t slot) -> float
{
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(mesh, facet_slots);
    float top = 0.0f;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        if (facet_slots[f] != slot) {
            continue;
        }
        for (GEO::index_t c = 0; c < mesh.facets.nb_vertices(f); ++c) {
            top = std::max(top, point_of(mesh, mesh.facets.vertex(f, c)).y);
        }
    }
    return top;
}

//
// Broken pillar.
//

const std::vector<std::string> broken_pillar_floats{"width", "height", "standing"};

class Kit_broken_pillar_profiles : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_broken_pillar_profiles, every_drum_count_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& pillar = get_shape("ruin.broken_pillar");
    ASSERT_EQ(pillar.material_slots, (std::vector<std::string>{"trim"}));
    for (const int seed : sweep_seeds(pillar)) {
        for (int fallen = 0; fallen <= 3; ++fallen) {
            for (const int segments : {3, 12, 64}) {
                Kit_parameter_values values = explicit_enum(pillar, "profile", GetParam(), seed);
                ASSERT_TRUE(values.set_int("fallen", fallen));
                ASSERT_TRUE(values.set_int("segments", segments));
                for_each_corner(values, broken_pillar_floats, expect_valid_with_every_slot);
            }
        }
    }
}

TEST_P(Kit_broken_pillar_profiles, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& pillar = get_shape("ruin.broken_pillar");
    for (int fallen = 0; fallen <= 3; ++fallen) {
        Kit_parameter_values values = explicit_enum(pillar, "profile", GetParam(), 1);
        ASSERT_TRUE(values.set_int("fallen", fallen));
        expect_topology_from_integers(values, broken_pillar_floats);
    }
}

// The stump and a drum to each fallen one, each a ring between two polygons. The stump stands about its share of
// the height high, and the drums lie on the ground about it.
TEST_P(Kit_broken_pillar_profiles, stump_and_drums_follow_the_values)
{
    const Kit_shape_definition& pillar = get_shape("ruin.broken_pillar");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = derived_enum(pillar, "profile", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        width  = values.get_float("width");
        const float        stump  = values.get_float("standing") * values.get_float("height");
        const unsigned int fallen = static_cast<unsigned int>(values.get_int("fallen"));
        const unsigned int points = (GetParam() == "round") ? static_cast<unsigned int>(values.get_int("segments")) : 4u;
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 1u + fallen);
        EXPECT_EQ(report.triangle_count, (1u + fallen) * ((4u * points) - 4u));
        EXPECT_NEAR(report.bounds_min.y, 0.0f, 1.0e-6f);
        EXPECT_GE(report.bounds_max.y, stump);
        EXPECT_LE(report.bounds_max.y, std::max(1.6f * stump, 1.5f * width) + 1.0e-5f);
        const glm::vec3 extent = report.bounds_max - report.bounds_min;
        EXPECT_GE(std::max(extent.x, extent.z), 0.95f * width);
        EXPECT_LE(std::max(extent.x, extent.z), 9.0f * width);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_broken_pillar, Kit_broken_pillar_profiles, testing::Values("square", "round"));

TEST(Kit_broken_pillar, takes_the_pillars_parameters)
{
    expect_takes_the_parameters_of("ruin.broken_pillar", "architecture.pillar");
}

//
// Broken arch.
//

const std::vector<std::string> broken_arch_floats{"span", "rise", "depth", "thickness", "gap", "gap_center"};

TEST(Kit_broken_arch, every_segment_count_passes_at_the_corners)
{
    const Kit_shape_definition& arch = get_shape("ruin.broken_arch");
    ASSERT_EQ(arch.material_slots, (std::vector<std::string>{"trim"}));
    for (const int segments : {2, 3, 12, 48}) {
        Kit_parameter_values values = explicit_values(arch, 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        for_each_corner(values, broken_arch_floats, expect_valid_with_every_slot);
    }
}

TEST(Kit_broken_arch, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& arch = get_shape("ruin.broken_arch");
    for (const int segments : {2, 7, 12}) {
        Kit_parameter_values values = explicit_values(arch, 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        expect_topology_from_integers(values, broken_arch_floats);
    }
}

// Two arms on the arch's two feet; the ring between the breaks is gone.
TEST(Kit_broken_arch, two_arms_stand_on_the_feet_about_the_gap)
{
    const Kit_shape_definition& arch = get_shape("ruin.broken_arch");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(arch, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float half      = 0.5f * values.get_float("span");
        const float thickness = values.get_float("thickness");
        const float gap       = values.get_float("gap");
        const float center    = values.get_float("gap_center");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u);
        // Each arm has a segment more than half the arch's, of eight triangles, and two ends.
        EXPECT_EQ(report.triangle_count, (16u * ((static_cast<unsigned int>(values.get_int("segments")) / 2u) + 1u)) + 8u);
        EXPECT_EQ(report.bounds_min, (glm::vec3{-(half + thickness), 0.0f, -0.5f * values.get_float("depth")}));
        EXPECT_FLOAT_EQ(report.bounds_max.x, half + thickness);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * values.get_float("depth"));
        EXPECT_LE(report.bounds_max.y, values.get_float("rise") + thickness + 1.0e-5f);
        // No point lies on the ring between the breaks, a tenth of the gap inside each (an arm is at least 0.08 of the ring).
        const float rise = values.get_float("rise");
        const float low  = std::numbers::pi_v<float> * std::max(center - (0.4f * gap), 0.09f);
        const float high = std::numbers::pi_v<float> * std::min(center + (0.4f * gap), 0.91f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            // The ring's angle of a point on the inner ellipse, else on the outer one.
            const bool  inner = std::abs(((p.x * p.x) / (half * half)) + ((p.y * p.y) / (rise * rise)) - 1.0f) < 1.0e-3f;
            const float angle = inner ? std::atan2(p.y / rise, p.x / half) : std::atan2(p.y / (rise + thickness), p.x / (half + thickness));
            EXPECT_FALSE((angle > low) && (angle < high)) << angle;
        }
        expect_connector_names(values, {"start", "end", "base"});
        EXPECT_EQ(connectors_of(values)[0].position, (glm::vec3{-(half + thickness), 0.0f, 0.0f}));
    }
}

TEST(Kit_broken_arch, takes_the_archs_parameters)
{
    expect_takes_the_parameters_of("ruin.broken_arch", "architecture.arch");
}

//
// Lantern.
//

const std::vector<std::string> lantern_floats{"size", "chain_length"};

class Kit_lantern_mounts : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_lantern_mounts, every_side_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& lantern = get_shape("light.lantern");
    ASSERT_EQ(lantern.material_slots, (std::vector<std::string>{"metal", "glow"}));
    for (int sides = 4; sides <= 8; ++sides) {
        Kit_parameter_values values = explicit_enum(lantern, "mount", GetParam(), 1);
        ASSERT_TRUE(values.set_int("sides", sides));
        for_each_corner(values, lantern_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, lantern_floats);
    }
}

// The plate, the core, the eaves, the roof, the loop and a bar to a side; five links of a chain, or a link, an arm, a
// plate and a strut of a bracket. The connectors are the same three at every mount.
TEST_P(Kit_lantern_mounts, parts_size_and_connectors_follow_the_values)
{
    const Kit_shape_definition& lantern = get_shape("light.lantern");
    const std::string mount = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(lantern, "mount", mount, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        size  = values.get_float("size");
        const float        chain = values.get_float("chain_length");
        const unsigned int sides = static_cast<unsigned int>(values.get_int("sides"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 5u + sides + ((mount == "hanging") ? 5u : ((mount == "bracket") ? 4u : 0u)));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        expect_connector_names(values, {"base", "top", "back"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        if (mount == "standing") {
            EXPECT_FLOAT_EQ(report.bounds_max.y, 1.7f * size);
            EXPECT_FLOAT_EQ(connectors[1].position.y, 1.7f * size);
        } else if (mount == "hanging") {
            EXPECT_FLOAT_EQ(report.bounds_max.y, (1.7f * size) + chain);
            EXPECT_FLOAT_EQ(connectors[1].position.y, report.bounds_max.y);
        } else {
            EXPECT_FLOAT_EQ(report.bounds_min.z, -1.3f * size);
            EXPECT_FLOAT_EQ(connectors[2].position.z, -1.3f * size);
            EXPECT_GT(connectors[2].position.y, 1.7f * size);
            EXPECT_LT(connectors[2].position.y, report.bounds_max.y);
        }
        EXPECT_EQ(connectors[2].outward, (glm::vec3{0.0f, 0.0f, -1.0f}));
    }
}

// One point light inside the cage: over the glowing core, under the eaves.
TEST_P(Kit_lantern_mounts, the_light_stands_in_the_cage_over_the_core)
{
    const Kit_shape_definition& lantern = get_shape("light.lantern");
    ASSERT_TRUE(static_cast<bool>(lantern.get_light_frames));
    for (int sides = 4; sides <= 8; ++sides) {
        Kit_parameter_values values = explicit_enum(lantern, "mount", GetParam(), 1);
        ASSERT_TRUE(values.set_int("sides", sides));
        for_each_corner(values, lantern_floats, [](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            const float     size  = corner.get_float("size");
            const glm::vec3 light = expect_one_light_in_the_open(corner, 0.05f * size);
            EXPECT_EQ(light.x, 0.0f);
            EXPECT_EQ(light.z, 0.0f);
            EXPECT_GT(light.y, slot_top(*generate(corner), 1u) + (0.05f * size));
            EXPECT_LT(light.y, 1.0f * size);
        });
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_lantern, Kit_lantern_mounts, testing::Values("standing", "hanging", "bracket"));

//
// Fire.
//

const std::vector<std::string> fire_floats{"size", "flame"};

class Kit_fire_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_fire_forms, passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& fire = get_shape("light.fire");
    ASSERT_EQ(fire.material_slots, (std::vector<std::string>{"metal", "glow", "stone"}));
    for (const int seed : sweep_seeds(fire)) {
        const Kit_parameter_values values = explicit_enum(fire, "form", GetParam(), seed);
        for_each_corner(values, fire_floats, expect_valid_with_every_slot);
    }
    expect_topology_from_integers(explicit_enum(fire, "form", GetParam(), 1), fire_floats);
}

// The parts of each form and the flame; one point light over the flame's tip, in the open.
TEST_P(Kit_fire_forms, parts_and_light_follow_the_values)
{
    const Kit_shape_definition& fire = get_shape("light.fire");
    ASSERT_TRUE(static_cast<bool>(fire.get_light_frames));
    const std::string form = GetParam();
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(fire, "form", form, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float size = values.get_float("size");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (form == "brazier") ? 7u : ((form == "torch") ? 5u : 12u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        const glm::vec3 light = expect_one_light_in_the_open(values, 0.04f);
        const float     glow  = slot_top(*generate(values), 1u);
        EXPECT_NEAR(light.y, glow + 0.06f, 1.0e-4f);
        // The flame's foot: in the brazier's bowl, on the torch's cup, on the campfire's embers.
        const float foot = ((form == "campfire") ? 0.1f : ((form == "torch") ? 0.97f : 0.95f)) * size;
        EXPECT_NEAR(glow, foot + values.get_float("flame"), 1.0e-4f);
        expect_connector_names(values, {"base", "back"});
        EXPECT_LE(connectors_of(values)[1].position.z, report.bounds_min.z + (0.1f * size));
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_fire, Kit_fire_forms, testing::Values("brazier", "torch", "campfire"));

// The flame's shape comes from the seed.
TEST(Kit_fire, the_seed_shapes_the_flame)
{
    const Kit_shape_definition& fire = get_shape("light.fire");
    EXPECT_FALSE(editor::kit_meshes_equal(*generate(explicit_values(fire, 1)), *generate(explicit_values(fire, 2))));
}

//
// Buoy.
//

const std::vector<std::string> buoy_floats{"size", "tilt"};

class Kit_buoy_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_buoy_forms, every_band_count_passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& buoy = get_shape("harbour.buoy");
    ASSERT_EQ(buoy.material_slots, (std::vector<std::string>{"metal", "trim", "glow"}));
    for (const int seed : sweep_seeds(buoy)) {
        for (int bands = 1; bands <= 3; ++bands) {
            for (const int segments : {8, 16}) {
                Kit_parameter_values values = explicit_enum(buoy, "form", GetParam(), seed);
                ASSERT_TRUE(values.set_int("bands", bands));
                ASSERT_TRUE(values.set_int("segments", segments));
                for_each_corner(values, buoy_floats, expect_valid_with_every_slot);
            }
        }
    }
    expect_topology_from_integers(explicit_enum(buoy, "form", GetParam(), 1), buoy_floats);
}

// The float, the mast, its cross piece and the lamp. The float has a stripe of trim to a band between stripes of
// metal, and the light stands over the lamp at every tilt.
TEST_P(Kit_buoy_forms, parts_stripes_and_light_follow_the_values)
{
    const Kit_shape_definition& buoy = get_shape("harbour.buoy");
    ASSERT_TRUE(static_cast<bool>(buoy.get_light_frames));
    const std::string form = GetParam();
    const float high = (form == "cone") ? 1.25f : ((form == "spar") ? 2.6f : 1.0f);
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(buoy, "form", form, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float size     = values.get_float("size");
        const int   bands    = values.get_int("bands");
        const int   segments = values.get_int("segments");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 4u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_NEAR(report.bounds_max.y, (high + 0.43f) * size, 1.0e-5f * size);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        // Of the float's twelve rings of facets, those in an odd stripe are trim.
        int trim = 0;
        for (int k = 0; k < 12; ++k) {
            trim += ((((k * ((2 * bands) + 1)) / 12) % 2) == 1) ? segments : 0;
        }
        EXPECT_EQ(std::count(facet_slots.begin(), facet_slots.end(), 1u), trim);
        const glm::vec3 light = expect_one_light_in_the_open(values, 0.05f * size);
        EXPECT_NEAR(light.y, (high + 0.5f) * size, 1.0e-5f * size);
        EXPECT_GT(light.y, slot_top(*mesh, 2u));
        // The light leans with the buoy: by the tilt's tangent of its height.
        const float lean = light.y * std::tan(glm::radians(values.get_float("tilt")));
        EXPECT_NEAR(glm::length(glm::vec2{light.x, light.z}), lean, 1.0e-4f);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_buoy, Kit_buoy_forms, testing::Values("can", "cone", "sphere", "spar"));

//
// Trestle.
//

const std::vector<std::string> trestle_floats{"height", "width", "splay"};

TEST(Kit_trestle, every_brace_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& trestle = get_shape("traversal.trestle");
    ASSERT_EQ(trestle.material_slots, (std::vector<std::string>{"wood"}));
    for (int braces = 0; braces <= 3; ++braces) {
        Kit_parameter_values values = explicit_values(trestle, 1);
        ASSERT_TRUE(values.set_int("braces", braces));
        for_each_corner(values, trestle_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, trestle_floats);
    }
}

// A sill, a cap beam, two legs and the braces, each a box. The cap beam's top is the height and the width long; the
// feet stand wider with the splay.
TEST(Kit_trestle, parts_and_size_follow_the_values)
{
    const Kit_shape_definition& trestle = get_shape("traversal.trestle");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(trestle, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float height = values.get_float("height");
        const float width  = values.get_float("width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 4u + static_cast<unsigned int>(values.get_int("braces")));
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_FLOAT_EQ(report.bounds_max.z, -report.bounds_min.z);
        EXPECT_GE(report.bounds_max.z, 0.5f * width);
        EXPECT_GE(report.bounds_max.z, (0.4f * width) + (0.8f * height * std::tan(glm::radians(values.get_float("splay")))));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> levels = level_facet_heights(*mesh, 0);
        EXPECT_FLOAT_EQ(*std::max_element(levels.begin(), levels.end()), height);
        float cap = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (p.y == report.bounds_max.y) {
                cap = std::max(cap, p.z);
            }
        }
        EXPECT_FLOAT_EQ(cap, 0.5f * width);
        expect_connector_names(values, {"base", "top"});
        EXPECT_EQ(connectors_of(values)[1].position.y, height);
    }
}

//
// Scaffold.
//

const std::vector<std::string> scaffold_floats{"width", "depth", "level_height"};

class Kit_scaffold_braces : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_scaffold_braces, every_level_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& scaffold = get_shape("traversal.scaffold");
    ASSERT_EQ(scaffold.material_slots, (std::vector<std::string>{"wood", "metal"}));
    for (const int levels : {1, 2, 6}) {
        Kit_parameter_values values = explicit_enum(scaffold, "braces", GetParam(), 1);
        ASSERT_TRUE(values.set_int("levels", levels));
        for_each_corner(values, scaffold_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, scaffold_floats);
    }
}

// Four poles on four plates, three guard rails, and to a level three planks, four ledgers and its braces. The bay
// fills its width and depth, and every deck is three level planks at its level.
TEST_P(Kit_scaffold_braces, parts_bounds_and_decks_follow_the_values)
{
    const Kit_shape_definition& scaffold = get_shape("traversal.scaffold");
    const Kit_agent agent{};
    const unsigned int braces = (GetParam() == "none") ? 0u : ((GetParam() == "single") ? 1u : 2u);
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(scaffold, "braces", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        width  = values.get_float("width");
        const float        depth  = values.get_float("depth");
        const float        level  = values.get_float("level_height");
        const unsigned int levels = static_cast<unsigned int>(values.get_int("levels"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 11u + (levels * (7u + braces)));
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * depth);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * depth);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, (level * static_cast<float>(levels)) + 1.0f);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> tops = level_facet_heights(*mesh, 0);
        for (unsigned int k = 1; k <= levels; ++k) {
            const float deck = level * static_cast<float>(k);
            EXPECT_EQ(std::count(tops.begin(), tops.end(), deck), 3) << "deck " << k;
        }
        EXPECT_EQ(scaffold.check_walkability(values, agent), "");
        expect_connector_names(values, {"base", "top", "left", "right", "front", "back"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[1].position.y, level * static_cast<float>(levels));
        EXPECT_EQ(connectors[2].position.x, -0.5f * width);
        EXPECT_EQ(connectors[5].position.z, -0.5f * depth);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_scaffold, Kit_scaffold_braces, testing::Values("none", "single", "cross"));

// The least bay is walked; an agent too wide for its decks or too tall for its levels is refused.
TEST(Kit_scaffold, walkability_bounds_the_depth_and_the_headroom)
{
    const Kit_shape_definition& scaffold = get_shape("traversal.scaffold");
    Kit_parameter_values values = explicit_values(scaffold, 1);
    ASSERT_TRUE(values.set_float("depth", parameter_of(scaffold, "depth").float_min));
    ASSERT_TRUE(values.set_float("level_height", parameter_of(scaffold, "level_height").float_min));
    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.5f;
    Kit_agent tall{};
    tall.height = 2.2f;
    EXPECT_EQ(scaffold.check_walkability(values, agent), "");
    EXPECT_NE(scaffold.check_walkability(values, wide), "");
    EXPECT_NE(scaffold.check_walkability(values, tall), "");
}

//
// Tunnel.
//

const std::vector<std::string> tunnel_floats{"length", "bore_width", "bore_height", "wall"};

class Kit_tunnel_profiles : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_tunnel_profiles, every_segment_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& tunnel = get_shape("architecture.tunnel");
    ASSERT_EQ(tunnel.material_slots, (std::vector<std::string>{"stone", "floor"}));
    for (const int segments : {2, 5, 12, 48}) {
        Kit_parameter_values values = explicit_enum(tunnel, "profile", GetParam(), 1);
        ASSERT_TRUE(values.set_int("segments", segments));
        for_each_corner(values, tunnel_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, tunnel_floats);
    }
}

// One part: a block the bore's width and two walls wide, and the floor, the bore's height and a wall high. The floor
// in the bore is one level facet the bore's width wide, at the height of the end connectors.
TEST_P(Kit_tunnel_profiles, block_bore_and_floor_follow_the_values)
{
    const Kit_shape_definition& tunnel = get_shape("architecture.tunnel");
    const Kit_agent agent{};
    const std::string profile = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(tunnel, "profile", profile, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float half_l = 0.5f * values.get_float("length");
        const float half   = 0.5f * values.get_float("bore_width");
        const float bore   = values.get_float("bore_height");
        const float wall   = values.get_float("wall");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 1u);
        // The bore's outline: its two feet, and the head's points or a lintel's two corners.
        const unsigned int segments = static_cast<unsigned int>(values.get_int("segments"));
        const unsigned int head     = (profile == "square") ? 2u : (((profile == "pointed") ? (segments - (segments % 2u)) : segments) + 1u);
        EXPECT_EQ(report.triangle_count, (4u * (head + 2u)) + 32u);
        EXPECT_EQ(report.bounds_min, (glm::vec3{-half_l, 0.0f, -(half + wall)}));
        EXPECT_FLOAT_EQ(report.bounds_max.x, half_l);
        EXPECT_FLOAT_EQ(report.bounds_max.z, half + wall);
        EXPECT_FLOAT_EQ(report.bounds_max.y, 0.1f + bore + wall);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const std::vector<float> floor = level_facet_heights(*mesh, 1);
        ASSERT_EQ(floor.size(), 1u);
        EXPECT_EQ(floor[0], 0.1f);
        // No point of the mesh lies inside the bore.
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            EXPECT_FALSE((std::abs(p.z) < (0.99f * half)) && (p.y > 0.11f) && (p.y < (0.1f + (0.39f * bore))));
        }
        EXPECT_EQ(tunnel.check_walkability(values, agent), "");
        expect_connector_names(values, {"start", "end", "base"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position, (glm::vec3{-half_l, 0.1f, 0.0f}));
        EXPECT_EQ(connectors[1].position, (glm::vec3{half_l, 0.1f, 0.0f}));
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_tunnel, Kit_tunnel_profiles, testing::Values("round", "pointed", "square"));

// The narrowest and lowest bore is walked in every profile; a wider or taller agent is refused.
TEST(Kit_tunnel, walkability_follows_the_bore)
{
    const Kit_shape_definition& tunnel = get_shape("architecture.tunnel");
    const Kit_agent agent{};
    Kit_agent wide{};
    wide.radius = 0.7f;
    Kit_agent tall{};
    tall.height = 2.2f;
    for (const std::string& profile : enum_values_of(tunnel, "profile")) {
        Kit_parameter_values values = explicit_enum(tunnel, "profile", profile, 1);
        ASSERT_TRUE(values.set_float("bore_width", parameter_of(tunnel, "bore_width").float_min));
        ASSERT_TRUE(values.set_float("bore_height", parameter_of(tunnel, "bore_height").float_min));
        EXPECT_EQ(tunnel.check_walkability(values, agent), "") << profile;
        EXPECT_NE(tunnel.check_walkability(values, wide), "") << profile;
        EXPECT_NE(tunnel.check_walkability(values, tall), "") << profile;
    }
}

} // anonymous namespace
