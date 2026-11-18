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

// Tests of the shapes of kit_shapes_extra_25_32.cpp.
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
// Cargo.
//

const std::vector<std::string> cargo_floats{"fill", "width"};

class Kit_cargo_loads : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_cargo_loads, every_layer_count_passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& cargo = get_shape("harbour.cargo");
    ASSERT_EQ(cargo.material_slots, (std::vector<std::string>{"wood", "cloth"}));
    for (const int seed : sweep_seeds(cargo)) {
        for (int layers = 1; layers <= 3; ++layers) {
            Kit_parameter_values values = explicit_enum(cargo, "load", GetParam(), seed);
            ASSERT_TRUE(values.set_int("layers", layers));
            for_each_corner(values, cargo_floats, expect_valid_with_every_slot);
        }
    }
    expect_topology_from_integers(explicit_enum(cargo, "load", GetParam(), 1), cargo_floats);
}

// The pallet's eight boxes, the tarp, and four goods to a layer: a crate is a box under a lid, a sack and a barrel
// one part. The top connector is on the tarp.
TEST_P(Kit_cargo_loads, parts_and_top_follow_the_values)
{
    const Kit_shape_definition& cargo = get_shape("harbour.cargo");
    const std::string load = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(cargo, "load", load, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const int   layers = values.get_int("layers");
        const float width  = values.get_float("width");
        unsigned int goods = 0;
        for (int k = 0; k < layers; ++k) {
            const bool crates = (load == "crates") || ((load == "mixed") && (k == 0));
            goods += crates ? 8u : 4u;
        }
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 9u + goods);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * width);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * width);
        EXPECT_LE(report.bounds_max.z, 0.56f * width);
        expect_connector_names(values, {"base", "top"});
        EXPECT_NEAR(connectors_of(values)[1].position.y, report.bounds_max.y, 1.0e-5f);
        EXPECT_GT(report.bounds_max.y, 0.2f * width);
    }
}

// The four crates of a layer stand apart and on the pallet at every seed, fill and width: their turned footprints
// have a separating axis (the edges of one of the two).
TEST(Kit_cargo, goods_footprints_are_disjoint)
{
    const Kit_shape_definition& cargo = get_shape("harbour.cargo");
    for (int seed = 0; seed < 24; ++seed) {
        Kit_parameter_values values = explicit_enum(cargo, "load", "crates", seed);
        ASSERT_TRUE(values.set_int("layers", 1));
        for_each_corner(values, cargo_floats, [](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            const float half_w = 0.5f * corner.get_float("width");
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            // The pallet's eight boxes come first, then a crate's box and lid to a good.
            std::array<std::array<glm::vec2, 16>, 4> goods{};
            for (GEO::index_t k = 0; k < 4; ++k) {
                for (GEO::index_t v = 0; v < 16; ++v) {
                    const glm::vec3 p = point_of(*mesh, 64 + (16 * k) + v);
                    goods[k][v] = glm::vec2{p.x, p.z};
                    EXPECT_LE(std::abs(p.x), half_w + 1.0e-5f);
                    EXPECT_LE(std::abs(p.z), half_w + 1.0e-5f);
                }
            }
            const auto is_separated = [](const std::array<glm::vec2, 16>& a, const std::array<glm::vec2, 16>& b, const std::array<glm::vec2, 16>& edges) -> bool {
                // The lid's corners 0, 1 and 3 give its two edge directions.
                for (const glm::vec2 axis : {edges[9] - edges[8], edges[11] - edges[8]}) {
                    float a_min = 1.0e6f;
                    float a_max = -1.0e6f;
                    float b_min = 1.0e6f;
                    float b_max = -1.0e6f;
                    for (const glm::vec2& p : a) {
                        a_min = std::min(a_min, glm::dot(p, axis));
                        a_max = std::max(a_max, glm::dot(p, axis));
                    }
                    for (const glm::vec2& p : b) {
                        b_min = std::min(b_min, glm::dot(p, axis));
                        b_max = std::max(b_max, glm::dot(p, axis));
                    }
                    if ((a_max < b_min) || (b_max < a_min)) {
                        return true;
                    }
                }
                return false;
            };
            for (std::size_t i = 0; i < goods.size(); ++i) {
                for (std::size_t j = i + 1; j < goods.size(); ++j) {
                    EXPECT_TRUE(is_separated(goods[i], goods[j], goods[i]) || is_separated(goods[i], goods[j], goods[j])) << i << " " << j;
                }
            }
        });
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_cargo, Kit_cargo_loads, testing::Values("crates", "sacks", "barrels", "mixed"));

//
// Crane.
//

const std::vector<std::string> crane_floats{"height", "jib_length", "jib_heading", "jib_angle"};

class Kit_crane_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_crane_forms, passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& crane = get_shape("harbour.crane");
    ASSERT_EQ(crane.material_slots, (std::vector<std::string>{"metal", "wood"}));
    const Kit_parameter_values values = explicit_enum(crane, "form", GetParam(), 1);
    // A tower refuses a jib whose end stays over its slewing deck (a short jib at a steep angle on a tall tower).
    unsigned int refused = 0;
    for_each_corner(values, crane_floats, [&refused](const Kit_parameter_values& corner) {
        if (corner.validate() != "") {
            EXPECT_EQ(corner.get_enum("form"), "tower") << corner.to_canonical_text();
            ++refused;
            return;
        }
        expect_valid_with_every_slot(corner);
    });
    EXPECT_EQ(refused, (GetParam() == "tower") ? 4u : 0u);
    Kit_parameter_values long_jib = values;
    ASSERT_TRUE(long_jib.set_float("jib_length", 20.0f));
    expect_topology_from_integers(long_jib, {"height", "jib_heading", "jib_angle"});
    expect_topology_from_integers(values, {"jib_length", "jib_heading", "jib_angle"});
}

// A tower's hook line and hook hang clear of the slewing deck at every heading where the jib is long enough; a
// shorter jib is refused with a reason.
TEST(Kit_crane, tower_hook_hangs_clear_of_the_deck)
{
    const Kit_shape_definition& crane = get_shape("harbour.crane");
    Kit_parameter_values values = explicit_enum(crane, "form", "tower", 1);
    ASSERT_TRUE(values.set_float("height", 40.0f));
    ASSERT_TRUE(values.set_float("jib_angle", 60.0f));
    ASSERT_TRUE(values.set_float("jib_length", 1.0f));
    EXPECT_NE(values.validate(), "");
    ASSERT_TRUE(values.set_float("jib_length", 3.5f));
    ASSERT_EQ(values.validate(), "");
    const float deck = 1.2f * 1.2f; // half the deck's width at the tallest tower
    for (const float heading : {0.0f, 30.0f, 45.0f, 135.0f, 225.0f, 300.0f}) {
        ASSERT_TRUE(values.set_float("jib_heading", heading));
        SCOPED_TRACE(values.to_canonical_text());
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        // The hook line's box and the hook's are the last sixteen vertices.
        const GEO::index_t count = mesh->vertices.nb();
        for (GEO::index_t v = count - 16; v < count; ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            EXPECT_TRUE((std::abs(p.x) > deck) || (std::abs(p.z) > deck)) << p.x << " " << p.z;
        }
    }
}

// The parts of each form. The mast or the tower's head is the height, and the jib reaches out along its heading by
// its length at its angle.
TEST_P(Kit_crane_forms, parts_and_jib_follow_the_values)
{
    const Kit_shape_definition& crane = get_shape("harbour.crane");
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(crane, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float height  = values.get_float("height");
        const float jib     = values.get_float("jib_length");
        const float heading = glm::radians(values.get_float("jib_heading"));
        const float angle   = glm::radians(values.get_float("jib_angle"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, (GetParam() == "tower") ? 46u : 8u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_GE(report.bounds_max.y, height - 1.0e-4f);
        const glm::vec3 ahead{std::cos(heading), 0.0f, -std::sin(heading)};
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float reach = 0.0f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            reach = std::max(reach, glm::dot(point_of(*mesh, v), ahead));
        }
        EXPECT_GE(reach, jib * std::cos(angle));
        EXPECT_LE(reach, (jib * std::cos(angle)) + (0.1f * height) + 0.5f);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_crane, Kit_crane_forms, testing::Values("derrick", "tower"));

//
// Lighthouse.
//

const std::vector<std::string> lighthouse_floats{"height", "base_radius", "taper"};

class Kit_lighthouse_galleries : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_lighthouse_galleries, every_band_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& lighthouse = get_shape("harbour.lighthouse");
    ASSERT_EQ(lighthouse.material_slots, (std::vector<std::string>{"wall", "trim", "glow"}));
    for (const int bands : {1, 3, 6}) {
        for (const int segments : {8, 13, 24}) {
            Kit_parameter_values values = explicit_enum(lighthouse, "gallery", GetParam(), 1);
            ASSERT_TRUE(values.set_int("bands", bands));
            ASSERT_TRUE(values.set_int("segments", segments));
            for_each_corner(values, lighthouse_floats, expect_valid_with_every_slot);
        }
    }
    expect_topology_from_integers(explicit_enum(lighthouse, "gallery", GetParam(), 1), lighthouse_floats);
}

// The tower, the gallery, the lantern room, the roof and the door, and a rail on a post to every point. The roof's
// point is the height; the tower's stripes are wall and trim in turn, a band of trim to each of `bands`.
TEST_P(Kit_lighthouse_galleries, parts_bands_and_size_follow_the_values)
{
    const Kit_shape_definition& lighthouse = get_shape("harbour.lighthouse");
    const bool railed = (GetParam() == "rail");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(lighthouse, "gallery", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        height   = values.get_float("height");
        const float        foot     = values.get_float("base_radius");
        const unsigned int segments = static_cast<unsigned int>(values.get_int("segments"));
        const unsigned int bands    = static_cast<unsigned int>(values.get_int("bands"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 5u + (railed ? (segments + 1u) : 0u));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_FLOAT_EQ(report.bounds_max.x, std::max(foot + 0.06f, 1.35f * values.get_float("taper") * foot));
        // Trim: a band of the tower to each of `bands`, the gallery's two bands and two polygons, the roof's two bands
        // and its fan, the door's six faces, and the rail's posts and ring.
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const unsigned int rail = railed ? ((6u * segments) + (4u * segments)) : 0u;
        EXPECT_EQ(count_slot_facets(*mesh, 1u), static_cast<std::ptrdiff_t>((bands * segments) + ((2u * segments) + 2u) + ((3u * segments) + 1u) + 6u + rail));
        EXPECT_EQ(count_slot_facets(*mesh, 2u), static_cast<std::ptrdiff_t>(segments + 2u));
        expect_connector_names(values, {"base", "top"});
        EXPECT_EQ(connectors_of(values)[1].position.y, height);
    }
}

// One spot light of long reach beside the lantern room, on the gallery, shining level toward +X.
TEST_P(Kit_lighthouse_galleries, the_light_shines_from_beside_the_lantern_room)
{
    const Kit_shape_definition& lighthouse = get_shape("harbour.lighthouse");
    ASSERT_TRUE(static_cast<bool>(lighthouse.get_light_frames));
    for_each_corner(explicit_enum(lighthouse, "gallery", GetParam(), 1), lighthouse_floats, [](const Kit_parameter_values& corner) {
        SCOPED_TRACE(corner.to_canonical_text());
        std::vector<editor::Kit_light_frame> frames;
        editor::get_shape_light_frames(corner, frames);
        ASSERT_EQ(frames.size(), 1u);
        EXPECT_EQ(editor::check_light_frames(frames), "");
        EXPECT_EQ(std::string{frames[0].name}, "light");
        EXPECT_EQ(frames[0].start.type, editor::Kit_light_type::spot);
        EXPECT_EQ(frames[0].start.range, 100.0f);
        EXPECT_EQ(frames[0].direction, (glm::vec3{1.0f, 0.0f, 0.0f}));
        const float height = corner.get_float("height");
        const float head   = corner.get_float("taper") * corner.get_float("base_radius");
        EXPECT_GT(frames[0].position.x, 0.62f * head);
        EXPECT_LT(frames[0].position.x, 1.2f * head);
        EXPECT_EQ(frames[0].position.z, 0.0f);
        EXPECT_GT(frames[0].position.y, 0.78f * height);
        EXPECT_LT(frames[0].position.y, 0.9f * height);
        const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
        float nearest = height;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            nearest = std::min(nearest, glm::distance(point_of(*mesh, v), frames[0].position));
        }
        EXPECT_GT(nearest, 0.1f);
    });
}

INSTANTIATE_TEST_SUITE_P(Kit_lighthouse, Kit_lighthouse_galleries, testing::Values("none", "rail"));

//
// Tank.
//

const std::vector<std::string> tank_floats{"radius", "height"};

class Kit_tank_poses : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_tank_poses, every_end_and_band_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& tank = get_shape("industrial.tank");
    ASSERT_EQ(tank.material_slots, (std::vector<std::string>{"metal", "trim"}));
    for (const std::string& ends : enum_values_of(tank, "ends")) {
        for (const int bands : {0, 2, 4}) {
            for (const int segments : {8, 13, 24}) {
                Kit_parameter_values values = explicit_enum(tank, "pose", GetParam(), 1);
                ASSERT_TRUE(values.set_enum("ends", ends));
                ASSERT_TRUE(values.set_int("bands", bands));
                ASSERT_TRUE(values.set_int("segments", segments));
                for_each_corner(values, tank_floats, expect_valid_with_every_slot);
            }
            Kit_parameter_values values = explicit_enum(tank, "pose", GetParam(), 1);
            ASSERT_TRUE(values.set_enum("ends", ends));
            ASSERT_TRUE(values.set_int("bands", bands));
            expect_topology_from_integers(values, tank_floats);
        }
    }
}

// The drum and four legs, or the drum and two saddles. An upright tank is the height high, a lying one the height
// long; its bands and what it stands on are trim.
TEST_P(Kit_tank_poses, parts_bands_and_size_follow_the_values)
{
    const Kit_shape_definition& tank = get_shape("industrial.tank");
    const bool lying = (GetParam() == "lying");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(tank, "pose", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float        radius   = values.get_float("radius");
        const float        height   = values.get_float("height");
        const unsigned int segments = static_cast<unsigned int>(values.get_int("segments"));
        const unsigned int bands    = static_cast<unsigned int>(values.get_int("bands"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, lying ? 3u : 5u);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_EQ(count_slot_facets(*generate(values), 1u), static_cast<std::ptrdiff_t>((3u * bands * segments) + (lying ? 12u : 24u)));
        expect_connector_names(values, {"base", "top"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        if (lying) {
            EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * height);
            EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * height);
            EXPECT_LE(connectors[1].position.y, report.bounds_max.y + 1.0e-5f);
            EXPECT_GE(connectors[1].position.y, report.bounds_max.y - 0.05f);
            EXPECT_GT(report.bounds_max.y, 2.0f * radius);
        } else {
            EXPECT_FLOAT_EQ(report.bounds_max.y, height);
            EXPECT_LE(report.bounds_max.x, radius + 0.04f + 1.0e-5f);
            EXPECT_EQ(connectors[1].position.y, height);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_tank, Kit_tank_poses, testing::Values("upright", "lying"));

//
// Truss.
//

const std::vector<std::string> truss_floats{"length", "width", "height"};

class Kit_truss_patterns : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_truss_patterns, every_bay_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& truss = get_shape("industrial.truss");
    ASSERT_EQ(truss.material_slots, (std::vector<std::string>{"metal"}));
    for (const int bays : {2, 7, 16}) {
        Kit_parameter_values values = explicit_enum(truss, "pattern", GetParam(), 1);
        ASSERT_TRUE(values.set_int("bays", bays));
        for_each_corner(values, truss_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, truss_floats);
    }
}

// Four chords, two struts at every node, a diagonal (or two) to a bay of each face, and a Pratt truss's two posts
// at every node; each a box. The truss fills its length, width and height.
TEST_P(Kit_truss_patterns, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& truss = get_shape("industrial.truss");
    const std::string pattern = GetParam();
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(truss, "pattern", pattern, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int bays = static_cast<unsigned int>(values.get_int("bays"));
        const float half_l = 0.5f * values.get_float("length");
        const float half_w = 0.5f * values.get_float("width");
        const float height = values.get_float("height");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        const unsigned int nodes = bays + 1u;
        EXPECT_EQ(report.component_count, 4u + (2u * nodes) + ((pattern == "pratt") ? (2u * nodes) : 0u) + (2u * bays * ((pattern == "cross") ? 2u : 1u)));
        EXPECT_EQ(report.triangle_count, 12u * report.component_count);
        EXPECT_EQ(report.bounds_min, (glm::vec3{-half_l, 0.0f, -half_w}));
        EXPECT_EQ(report.bounds_max, (glm::vec3{half_l, height, half_w}));
        expect_connector_names(values, {"start", "end", "base", "top"});
        const std::vector<Kit_connector> connectors = connectors_of(values);
        EXPECT_EQ(connectors[0].position, (glm::vec3{-half_l, 0.0f, 0.0f}));
        EXPECT_EQ(connectors[1].position, (glm::vec3{half_l, 0.0f, 0.0f}));
        EXPECT_EQ(connectors[3].position.y, height);
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_truss, Kit_truss_patterns, testing::Values("warren", "pratt", "cross"));

//
// Container.
//

class Kit_container_lengths : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_container_lengths, every_door_and_rib_count_passes_with_every_slot)
{
    const Kit_shape_definition& container = get_shape("industrial.container");
    ASSERT_EQ(container.material_slots, (std::vector<std::string>{"metal", "trim"}));
    for (const std::string& door : enum_values_of(container, "door")) {
        for (const int ribs : {2, 6}) {
            Kit_parameter_values values = explicit_enum(container, "length", GetParam(), 1);
            ASSERT_TRUE(values.set_enum("door", door));
            ASSERT_TRUE(values.set_int("ribs", ribs));
            for_each_corner(values, {"door_angle"}, expect_valid_with_every_slot);
            expect_topology_from_integers(values, {"door_angle"});
        }
    }
}

// Twelve frame members, five panels, the ribs of two sides, and two door leaves under two bars each; each a box.
// A closed container fills 3, 6 or 12 m by 2.4 m by 2.6 m; open leaves stand out of its +X end.
TEST_P(Kit_container_lengths, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& container = get_shape("industrial.container");
    const unsigned int units  = (GetParam() == "short") ? 1u : ((GetParam() == "long") ? 4u : 2u);
    const float        half_l = 1.5f * static_cast<float>(units);
    for (const std::string& door : enum_values_of(container, "door")) {
        for (const int seed : {0, 1, 2}) {
            Kit_parameter_values values = derived_enum(container, "length", GetParam(), seed);
            values.set_value_source(container.find_parameter("door").value(), editor::Kit_value_source::explicit_value);
            ASSERT_TRUE(values.set_enum("door", door));
            SCOPED_TRACE(values.to_canonical_text());
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, 23u + (2u * static_cast<unsigned int>(values.get_int("ribs")) * units));
            EXPECT_EQ(report.triangle_count, 12u * report.component_count);
            EXPECT_EQ(report.bounds_min.x, -half_l);
            EXPECT_EQ(report.bounds_min.y, 0.0f);
            EXPECT_EQ(report.bounds_max.y, 2.6f);
            if (door == "closed") {
                EXPECT_NEAR(report.bounds_max.x, half_l, 1.0e-5f);
                EXPECT_EQ(report.bounds_min.z, -1.2f);
                EXPECT_EQ(report.bounds_max.z, 1.2f);
            } else {
                EXPECT_GT(report.bounds_max.x, half_l + 0.2f);
            }
            expect_connector_names(values, {"base", "top", "front", "back"});
            const std::vector<Kit_connector> connectors = connectors_of(values);
            EXPECT_EQ(connectors[1].position.y, 2.6f);
            EXPECT_EQ(connectors[2].position.z, 1.2f);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_container, Kit_container_lengths, testing::Values("short", "standard", "long"));

//
// Vent.
//

const std::vector<std::string> vent_floats{"width", "height"};

class Kit_vent_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_vent_forms, every_slat_count_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& vent = get_shape("industrial.vent");
    ASSERT_EQ(vent.material_slots, (std::vector<std::string>{"metal"}));
    for (int slats = 3; slats <= 8; ++slats) {
        Kit_parameter_values values = explicit_enum(vent, "form", GetParam(), 1);
        ASSERT_TRUE(values.set_int("slats", slats));
        for_each_corner(values, vent_floats, expect_valid_with_every_slot);
        expect_topology_from_integers(values, vent_floats);
    }
}

// Three parts and the slats (on two faces of a louvre box); the unit fills its width and its height.
TEST_P(Kit_vent_forms, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& vent = get_shape("industrial.vent");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = derived_enum(vent, "form", GetParam(), seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int slats = static_cast<unsigned int>(values.get_int("slats"));
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 3u + ((GetParam() == "louvre_box") ? (2u * slats) : slats));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("height"));
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * values.get_float("width"));
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * values.get_float("width"));
        EXPECT_LE(report.bounds_max.z, 0.5f * values.get_float("width") + 1.0e-5f);
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_vent, Kit_vent_forms, testing::Values("louvre_box", "fan", "stack", "duct_elbow"));

//
// Antenna.
//

const std::vector<std::string> antenna_floats{"height", "tilt"};

class Kit_antenna_forms : public testing::TestWithParam<std::string>
{
};

TEST_P(Kit_antenna_forms, every_element_count_passes_at_the_corners_over_seeds_with_every_slot)
{
    const Kit_shape_definition& antenna = get_shape("industrial.antenna");
    ASSERT_EQ(antenna.material_slots, (std::vector<std::string>{"metal"}));
    for (const int seed : sweep_seeds(antenna)) {
        for (int elements = 1; elements <= 5; ++elements) {
            Kit_parameter_values values = explicit_enum(antenna, "form", GetParam(), seed);
            ASSERT_TRUE(values.set_int("elements", elements));
            for_each_corner(values, antenna_floats, expect_valid_with_every_slot);
        }
    }
    expect_topology_from_integers(explicit_enum(antenna, "form", GetParam(), 1), antenna_floats);
}

// The plate, the mast and the elements: a dish with its feed, a whip, a bar. The mast, or the first whip, reaches the
// height.
TEST_P(Kit_antenna_forms, parts_and_height_follow_the_values)
{
    const Kit_shape_definition& antenna = get_shape("industrial.antenna");
    const std::string form = GetParam();
    for (int seed = 0; seed < 12; ++seed) {
        const Kit_parameter_values values = derived_enum(antenna, "form", form, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int elements = static_cast<unsigned int>(values.get_int("elements"));
        const float        height   = values.get_float("height");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, 2u + (elements * ((form == "dish") ? 2u : 1u)));
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_GE(report.bounds_max.y, height - (1.0e-5f * height));
        EXPECT_LE(report.bounds_max.y, 1.12f * height);
        if (form != "array") {
            EXPECT_NEAR(report.bounds_max.y, height, 1.0e-5f * height);
        }
        expect_connector_names(values, {"base"});
    }
}

INSTANTIATE_TEST_SUITE_P(Kit_antenna, Kit_antenna_forms, testing::Values("dish", "whip", "array"));

// Each dish stands above the next one down, at every height, tilt and heading: their height ranges are disjoint.
TEST(Kit_antenna, dishes_stand_apart)
{
    const Kit_shape_definition& antenna = get_shape("industrial.antenna");
    for (const int seed : {1, 2, 3, 4, 5, 6}) {
        for (int elements = 2; elements <= 5; ++elements) {
            Kit_parameter_values values = explicit_enum(antenna, "form", "dish", seed);
            ASSERT_TRUE(values.set_int("elements", elements));
            for_each_corner(values, antenna_floats, [elements](const Kit_parameter_values& corner) {
                SCOPED_TRACE(corner.to_canonical_text());
                const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
                // The plate and the mast come first, then a dish's five rings of eight and its feed's box to an element.
                std::vector<std::array<float, 2>> ranges;
                for (int k = 0; k < elements; ++k) {
                    std::array<float, 2> range{1.0e6f, -1.0e6f};
                    for (GEO::index_t v = 0; v < 48; ++v) {
                        const float y = point_of(*mesh, static_cast<GEO::index_t>(20 + (48 * k)) + v).y;
                        range[0] = std::min(range[0], y);
                        range[1] = std::max(range[1], y);
                    }
                    ranges.push_back(range);
                }
                ASSERT_EQ(mesh->vertices.nb(), static_cast<GEO::index_t>(20 + (48 * elements)));
                for (std::size_t k = 0; (k + 1) < ranges.size(); ++k) {
                    EXPECT_GT(ranges[k][0], ranges[k + 1][1]) << k;
                }
            });
        }
    }
}

// The elements' headings come from the seed.
TEST(Kit_antenna, the_seed_turns_the_elements)
{
    const Kit_shape_definition& antenna = get_shape("industrial.antenna");
    EXPECT_FALSE(editor::kit_meshes_equal(*generate(explicit_values(antenna, 1)), *generate(explicit_values(antenna, 2))));
}

} // anonymous namespace
