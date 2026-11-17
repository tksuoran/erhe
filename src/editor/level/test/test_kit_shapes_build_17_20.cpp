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
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_build_17_20.cpp.
namespace {

using editor::Kit_agent;
using editor::Kit_connector;
using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_stairs_layout;
using namespace editor::kit_test;

[[nodiscard]] auto point_of(const GEO::Mesh& mesh, const GEO::index_t vertex) -> glm::vec3
{
    const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
    return glm::vec3{p.x, p.y, p.z};
}

// The mesh checks pass within the kit's ceiling and every slot has facets.
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

// R3.7: the topology at every corner of the float parameters equals the one at the given values.
void expect_topology_from_integers(const Kit_parameter_values& values, const std::vector<std::string>& floats)
{
    const std::vector<uint32_t> topology = topology_of(values);
    for_each_corner(values, floats, [&topology](const Kit_parameter_values& corner) {
        ASSERT_EQ(corner.validate(), "") << corner.to_canonical_text();
        EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
    });
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

// Every step's height is the height of a level facet of the slot.
void expect_a_tread_at_every_step(const GEO::Mesh& mesh, const uint32_t slot, const int steps, const float step_rise)
{
    const std::vector<float> heights = level_facet_heights(mesh, slot);
    for (int i = 1; i <= steps; ++i) {
        const float y     = step_rise * static_cast<float>(i);
        const bool  found = std::any_of(heights.begin(), heights.end(), [y](const float height) { return std::abs(height - y) < 1.0e-4f; });
        EXPECT_TRUE(found) << "no tread at " << y;
    }
}

//
// Timber bay.
//

const std::vector<std::string> timber_floats{"length", "height", "post_width"};

TEST(Kit_timber_bay, every_infill_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& bay = get_shape("architecture.timber_bay");
    ASSERT_EQ(bay.material_slots, (std::vector<std::string>{"wood", "wall"}));
    for (const std::string& infill : enum_values_of(bay, "infill")) {
        for (const int panels : {1, 4}) {
            for (const int rails : {0, 3}) {
                Kit_parameter_values values = defaults(bay);
                ASSERT_TRUE(values.set_enum("infill", infill));
                ASSERT_TRUE(values.set_int("panels", panels));
                ASSERT_TRUE(values.set_int("rails", rails));
                for_each_corner(values, timber_floats, expect_valid_with_every_slot);
            }
        }
    }
}

TEST(Kit_timber_bay, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& bay = get_shape("architecture.timber_bay");
    for (const std::string& infill : enum_values_of(bay, "infill")) {
        Kit_parameter_values values = defaults(bay);
        ASSERT_TRUE(values.set_enum("infill", infill));
        expect_topology_from_integers(values, timber_floats);
    }
}

// A screen is 7 boxes and its lattice; the bay's own members are 7 boxes. A plaster field has a box for itself, for
// each rail and for each stud, and a half wall one more for its cap.
TEST(Kit_timber_bay, parts_follow_the_counts)
{
    const Kit_shape_definition& bay = get_shape("architecture.timber_bay");
    for (const int panels : {1, 2, 3, 4}) {
        for (const int rails : {0, 1, 3}) {
            Kit_parameter_values values = defaults(bay);
            ASSERT_TRUE(values.set_int("panels", panels));
            ASSERT_TRUE(values.set_int("rails", rails));
            const unsigned int uprights = (panels == 1) ? 5u : ((panels == 2) ? 3u : 2u);
            const unsigned int screens  = static_cast<unsigned int>(panels) * (7u + uprights + 2u + (2u * static_cast<unsigned int>(rails)));
            const unsigned int field    = static_cast<unsigned int>(rails + panels);
            ASSERT_TRUE(values.set_enum("infill", "open"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 7u);
            ASSERT_TRUE(values.set_enum("infill", "shoji"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 7u + screens);
            EXPECT_EQ(editor::check_kit_shape(values).triangle_count, 12u * (7u + screens));
            ASSERT_TRUE(values.set_enum("infill", "plaster"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 7u + field);
            ASSERT_TRUE(values.set_enum("infill", "half"));
            EXPECT_EQ(editor::check_kit_shape(values).component_count, 8u + field);
        }
    }
}

// The bounds are the length by the height, the ends the half posts, the beam and the tie rail, and nothing of the
// infill stands out of the posts' depth.
TEST(Kit_timber_bay, bounds_and_ends_follow_the_values)
{
    const Kit_shape_definition& bay = get_shape("architecture.timber_bay");
    for (int seed = 0; seed < 24; ++seed) {
        const Kit_parameter_values values = all_derived(bay, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float half_l = 0.5f * values.get_float("length");
        const float height = values.get_float("height");
        const float post   = values.get_float("post_width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -half_l);
        EXPECT_FLOAT_EQ(report.bounds_max.x, half_l);
        EXPECT_FLOAT_EQ(report.bounds_max.y, height);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.62f * post);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.62f * post);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        EXPECT_EQ(vertices_on_x(*mesh, half_l).size(), 12u);
        EXPECT_EQ(vertices_on_x(*mesh, -half_l).size(), 12u);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 6u);
        EXPECT_STREQ(connectors[0].name, "start");
        EXPECT_STREQ(connectors[3].name, "top");
        EXPECT_EQ(connectors[3].position.y, height);
        EXPECT_EQ(connectors[4].position.z, 0.5f * post);
    }
}

//
// Railing.
//

const std::vector<std::string> railing_floats{"length", "height", "post_width"};

TEST(Kit_railing, every_style_passes_at_the_corners)
{
    const Kit_shape_definition& railing = get_shape("architecture.railing");
    for (const std::string& style : enum_values_of(railing, "style")) {
        for (const int bays : {1, 2, 6}) {
            for (const int infill : {1, 3, 4, 8}) {
                Kit_parameter_values values = defaults(railing);
                ASSERT_TRUE(values.set_enum("style", style));
                ASSERT_TRUE(values.set_int("bays", bays));
                ASSERT_TRUE(values.set_int("infill", infill));
                for_each_corner(values, railing_floats, expect_valid_with_every_slot);
            }
        }
    }
}

TEST(Kit_railing, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& railing = get_shape("architecture.railing");
    for (const std::string& style : enum_values_of(railing, "style")) {
        Kit_parameter_values values = defaults(railing);
        ASSERT_TRUE(values.set_enum("style", style));
        expect_topology_from_integers(values, railing_floats);
    }
}

// A post for every bay and one more, the styles' rails, and a picket, a baluster or two slats for each of the infill
// in each bay; the run is the length long and the posts' height high.
TEST(Kit_railing, parts_and_bounds_follow_the_values)
{
    const Kit_shape_definition& railing = get_shape("architecture.railing");
    for (int seed = 0; seed < 24; ++seed) {
        const Kit_parameter_values values = all_derived(railing, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const unsigned int bays   = static_cast<unsigned int>(values.get_int("bays"));
        const unsigned int infill = static_cast<unsigned int>(values.get_int("infill"));
        const std::string  style{values.get_enum("style")};
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        unsigned int expected = bays + 1;
        if (style == "rail") {
            expected += (infill + 1) / 2;
        } else if (style == "picket") {
            expected += 2 + (bays * infill);
        } else if (style == "balustrade") {
            expected += (bays + 1) + 2 + (bays * infill);
        } else {
            expected += (bays + 1) + 2 + (2 * bays * infill);
        }
        EXPECT_EQ(report.component_count, expected);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -0.5f * values.get_float("length"));
        EXPECT_FLOAT_EQ(report.bounds_max.x, 0.5f * values.get_float("length"));
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("height"));
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 3u);
        EXPECT_STREQ(connectors[0].name, "start");
        EXPECT_EQ(connectors[1].position.x, 0.5f * values.get_float("length"));
        EXPECT_STREQ(connectors[2].name, "base");
    }
}

// The more balusters a run has, the fewer triangles each takes.
TEST(Kit_railing, balusters_take_fewer_triangles_in_a_longer_run)
{
    const Kit_shape_definition& railing = get_shape("architecture.railing");
    unsigned int most = 0;
    for (int bays = 1; bays <= 6; ++bays) {
        for (int infill = 1; infill <= 8; ++infill) {
            Kit_parameter_values values = defaults(railing);
            ASSERT_TRUE(values.set_int("bays", bays));
            ASSERT_TRUE(values.set_int("infill", infill));
            const int          total = bays * infill;
            const unsigned int each  = (total <= 6) ? 124u : ((total <= 12) ? 68u : ((total <= 24) ? 44u : 28u));
            const unsigned int count = editor::count_kit_triangles(*generate(values));
            EXPECT_EQ(count, (static_cast<unsigned int>(bays + 1) * 26u) + 24u + (static_cast<unsigned int>(total) * each)) << bays << " bays of " << infill;
            most = std::max(most, count);
        }
    }
    EXPECT_EQ(most, 1550u);
}

//
// Bamboo.
//

const std::vector<std::string> bamboo_floats{"height", "radius", "lean", "cane_width"};

class Kit_bamboo_culms : public testing::TestWithParam<int>
{
};

TEST_P(Kit_bamboo_culms, corners_pass_over_seeds_with_every_slot)
{
    const Kit_shape_definition& bamboo = get_shape("nature.bamboo");
    ASSERT_EQ(bamboo.material_slots, (std::vector<std::string>{"cane", "bark", "foliage"}));
    for (const int seed : {0, 5}) {
        Kit_parameter_values values = explicit_values(bamboo, seed);
        ASSERT_TRUE(values.set_int("culms", GetParam()));
        for_each_corner(values, bamboo_floats, expect_valid_with_every_slot);
    }
}

// A cane, and a twig and its leaves for each spray; the counts follow from the canes alone, whatever the seed.
TEST_P(Kit_bamboo_culms, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& bamboo = get_shape("nature.bamboo");
    const int culms = GetParam();
    Kit_parameter_values values = explicit_values(bamboo, 1);
    ASSERT_TRUE(values.set_int("culms", culms));
    expect_topology_from_integers(values, bamboo_floats);
    Kit_parameter_values other = values;
    ASSERT_TRUE(other.set_int("seed", 2));
    EXPECT_EQ(topology_of(other), topology_of(values));
    const unsigned int sprays = (culms <= 4) ? 7u : ((culms <= 8) ? 5u : 3u);
    const unsigned int leaves = (culms <= 4) ? 4u : 3u;
    EXPECT_EQ(editor::check_kit_shape(values).component_count, static_cast<unsigned int>(culms) * (1u + (sprays * (1u + leaves))));
}

INSTANTIATE_TEST_SUITE_P(Kit_bamboo, Kit_bamboo_culms, testing::Values(3, 4, 5, 8, 9, 12, 13, 16));

// The tallest cane stands at the origin and reaches the height; the feet stay within the radius and a cane's width.
TEST(Kit_bamboo, canes_stand_within_the_radius_and_reach_the_height)
{
    const Kit_shape_definition& bamboo = get_shape("nature.bamboo");
    for (int seed = 0; seed < 16; ++seed) {
        const Kit_parameter_values values = all_derived(bamboo, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float height = values.get_float("height");
        const float reach  = values.get_float("radius") + values.get_float("cane_width");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_GE(report.bounds_max.y, height);
        EXPECT_LE(report.bounds_max.y, 1.2f * height);
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        bool at_origin = false;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (p.y == 0.0f) {
                EXPECT_LE(glm::length(glm::vec2{p.x, p.z}), reach);
                at_origin = at_origin || (glm::length(glm::vec2{p.x, p.z}) <= (0.5f * values.get_float("cane_width")));
            }
        }
        EXPECT_TRUE(at_origin);
    }
}

//
// Stairs.
//

TEST(Kit_stairs, every_form_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& stairs = get_shape("architecture.stairs");
    ASSERT_EQ(stairs.material_slots, (std::vector<std::string>{"stone", "trim"}));
    for (const std::string& form : enum_values_of(stairs, "form")) {
        int checked = 0;
        Kit_parameter_values values = defaults(stairs);
        ASSERT_TRUE(values.set_enum("form", form));
        for_each_corner(values, {"rise", "width", "step_rise_limit", "wall_height"}, [&checked](const Kit_parameter_values& corner) {
            if (corner.validate().empty()) {
                expect_valid_with_every_slot(corner);
                ++checked;
            }
        });
        // The highest rise over the lowest step makes too many steps; the lowest rise makes one step, no landing's.
        EXPECT_EQ(checked, (form == "walled") ? 12 : 8) << form;
    }
}

// Few steps: a landing after one riser has no first flight, a second flight of one riser no sloping wall.
TEST(Kit_stairs, few_steps_pass_in_every_form)
{
    const Kit_shape_definition& stairs = get_shape("architecture.stairs");
    for (const std::string& form : enum_values_of(stairs, "form")) {
        for (int steps = 1; steps <= 6; ++steps) {
            Kit_parameter_values values = defaults(stairs);
            ASSERT_TRUE(values.set_enum("form", form));
            ASSERT_TRUE(values.set_float("rise", 0.2f * static_cast<float>(steps)));
            ASSERT_EQ(editor::get_stairs_layout(values).step_count, steps);
            if ((steps == 1) && (form != "walled")) {
                EXPECT_NE(values.validate(), "");
                continue;
            }
            ASSERT_EQ(values.validate(), "");
            expect_valid_with_every_slot(values);
            expect_a_tread_at_every_step(*generate(values), 1, steps, 0.2f);
        }
    }
}

// The step count is the one integer the floats give (as basic.stairs); at a step count the other floats move vertices only.
TEST(Kit_stairs, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& stairs = get_shape("architecture.stairs");
    for (const std::string& form : enum_values_of(stairs, "form")) {
        Kit_parameter_values values = defaults(stairs);
        ASSERT_TRUE(values.set_enum("form", form));
        expect_topology_from_integers(values, {"width", "wall_height"});
        // Another rise and limit of the same step count.
        Kit_parameter_values other = values;
        ASSERT_TRUE(other.set_float("rise", 4.5f));
        ASSERT_TRUE(other.set_float("step_rise_limit", 0.3f));
        ASSERT_EQ(editor::get_stairs_layout(other).step_count, editor::get_stairs_layout(values).step_count);
        EXPECT_EQ(topology_of(other), topology_of(values));
    }
}

// Every step has its tread, the foot lies on the ground before the first riser and the top at the last tread's end,
// facing the way the last flight climbs.
TEST(Kit_stairs, treads_and_connectors_follow_the_form)
{
    const Kit_shape_definition& stairs = get_shape("architecture.stairs");
    for (int seed = 0; seed < 30; ++seed) {
        const Kit_parameter_values values = all_derived(stairs, seed);
        SCOPED_TRACE(values.to_canonical_text());
        ASSERT_EQ(values.validate(), "");
        const Kit_stairs_layout layout = editor::get_stairs_layout(values);
        const std::string       form{values.get_enum("form")};
        const float             rise = values.get_float("rise");
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        expect_a_tread_at_every_step(*mesh, 1, layout.step_count, layout.step_rise);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 3u);
        EXPECT_STREQ(connectors[0].name, "bottom");
        EXPECT_STREQ(connectors[1].name, "top");
        EXPECT_STREQ(connectors[2].name, "base");
        EXPECT_EQ(connectors[0].position.y, 0.0f);
        EXPECT_EQ(connectors[0].outward, (glm::vec3{0.0f, 0.0f, 1.0f}));
        EXPECT_FLOAT_EQ(connectors[1].position.y, rise);
        const glm::vec3 way = (form == "walled") ? glm::vec3{0.0f, 0.0f, -1.0f} : ((form == "l_landing") ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f});
        EXPECT_EQ(connectors[1].outward, way);
        // The top lies on the last tread's far edge: a tread vertex at the rise stands within a nosing of it.
        float nearest = 1.0e6f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            if (std::abs(p.y - rise) < 1.0e-4f) {
                nearest = std::min(nearest, std::abs(glm::dot(p - connectors[1].position, way)));
            }
        }
        EXPECT_LT(nearest, 0.01f);
        if (form == "walled") {
            EXPECT_FLOAT_EQ(connectors[0].position.z, 0.5f * layout.run);
            EXPECT_FLOAT_EQ(connectors[1].position.z, -0.5f * layout.run);
        }
    }
}

TEST(Kit_stairs, too_many_steps_and_a_landing_without_steps_are_refused)
{
    const Kit_shape_definition& stairs = get_shape("architecture.stairs");
    Kit_parameter_values values = defaults(stairs);
    ASSERT_TRUE(values.set_float("rise", 12.0f));
    ASSERT_TRUE(values.set_float("step_rise_limit", 0.19f));
    EXPECT_EQ(editor::get_stairs_layout(values).step_count, 64);
    EXPECT_EQ(values.validate(), "");
    ASSERT_TRUE(values.set_float("step_rise_limit", 0.18f));
    EXPECT_NE(values.validate(), "");
    // A step rise limit above the climb makes stairs the agent cannot walk, a narrow flight one it does not fit.
    Kit_parameter_values steep = defaults(stairs);
    ASSERT_TRUE(steep.set_float("step_rise_limit", 0.6f));
    EXPECT_NE(stairs.check_walkability(steep, Kit_agent{}), "");
    Kit_parameter_values narrow = defaults(stairs);
    ASSERT_TRUE(narrow.set_float("width", 0.5f));
    EXPECT_NE(stairs.check_walkability(narrow, Kit_agent{}), "");
}

//
// Spiral stairs.
//

TEST(Kit_spiral_stairs, both_turns_pass_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& spiral = get_shape("architecture.spiral_stairs");
    ASSERT_EQ(spiral.material_slots, (std::vector<std::string>{"stone", "trim"}));
    for (const std::string& turn : enum_values_of(spiral, "turn")) {
        int checked = 0;
        Kit_parameter_values values = defaults(spiral);
        ASSERT_TRUE(values.set_enum("turn", turn));
        for_each_corner(values, {"rise", "radius", "step_rise_limit", "newel_width"}, [&checked](const Kit_parameter_values& corner) {
            if (corner.validate().empty()) {
                expect_valid_with_every_slot(corner);
                ++checked;
            }
        });
        // The highest rise over the lowest step makes too many steps; the widest newel leaves the least radius no steps.
        EXPECT_EQ(checked, 9) << turn;
    }
}

TEST(Kit_spiral_stairs, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& spiral = get_shape("architecture.spiral_stairs");
    for (const std::string& turn : enum_values_of(spiral, "turn")) {
        Kit_parameter_values values = defaults(spiral);
        ASSERT_TRUE(values.set_enum("turn", turn));
        ASSERT_TRUE(values.set_float("radius", 2.0f)); // every newel fits
        expect_topology_from_integers(values, {"newel_width"});
        Kit_parameter_values other = values;
        ASSERT_TRUE(other.set_float("radius", 4.0f));
        ASSERT_TRUE(other.set_float("rise", 4.5f));
        ASSERT_TRUE(other.set_float("step_rise_limit", 0.3f));
        ASSERT_EQ(editor::get_stairs_layout(other).step_count, editor::get_stairs_layout(values).step_count);
        EXPECT_EQ(topology_of(other), topology_of(values));
    }
}

// A wedge with its tread for every step and the newel's three parts; the foot lies before the first riser on the X
// axis, the top on the last step's far edge at the rise, and a right turn is the left turn's mirror image.
TEST(Kit_spiral_stairs, steps_and_connectors_follow_the_values)
{
    const Kit_shape_definition& spiral = get_shape("architecture.spiral_stairs");
    for (int seed = 0; seed < 30; ++seed) {
        const Kit_parameter_values values = all_derived(spiral, seed);
        SCOPED_TRACE(values.to_canonical_text());
        ASSERT_EQ(values.validate(), "");
        const Kit_stairs_layout layout = editor::get_stairs_layout(values);
        const float radius = values.get_float("radius");
        const float newel  = 0.5f * values.get_float("newel_width");
        const float side   = (values.get_enum("turn") == "right") ? -1.0f : 1.0f;
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.component_count, static_cast<unsigned int>(layout.step_count) + 3u);
        EXPECT_EQ(report.triangle_count, (16u * static_cast<unsigned int>(layout.step_count)) + 108u);
        EXPECT_FLOAT_EQ(report.bounds_max.y, values.get_float("rise"));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        expect_a_tread_at_every_step(*mesh, 1, layout.step_count, layout.step_rise);
        EXPECT_EQ(level_facet_heights(*mesh, 1).size(), static_cast<std::size_t>(layout.step_count));
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            EXPECT_LE(glm::length(glm::vec2{p.x, p.z}), radius * 1.00001f);
        }
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 3u);
        EXPECT_STREQ(connectors[0].name, "bottom");
        EXPECT_FLOAT_EQ(connectors[0].position.x, side * 0.5f * (newel + radius));
        EXPECT_EQ(connectors[0].position.y, 0.0f);
        EXPECT_EQ(connectors[0].position.z, 0.0f);
        EXPECT_STREQ(connectors[1].name, "top");
        EXPECT_FLOAT_EQ(connectors[1].position.y, values.get_float("rise"));
        EXPECT_NEAR(glm::length(glm::vec2{connectors[1].position.x, connectors[1].position.z}), 0.5f * (newel + radius), 1.0e-5f);
        // The way out of the top is level, across the radius, and the way the steps turn.
        const glm::vec3 radial = glm::normalize(glm::vec3{connectors[1].position.x, 0.0f, connectors[1].position.z});
        EXPECT_NEAR(glm::dot(connectors[1].outward, radial), 0.0f, 1.0e-5f);
        EXPECT_NEAR(glm::length(connectors[1].outward), 1.0f, 1.0e-5f);
        EXPECT_GT(side * glm::cross(radial, connectors[1].outward).y, 0.99f);

        Kit_parameter_values mirrored = values;
        ASSERT_TRUE(mirrored.set_enum("turn", (side > 0.0f) ? "right" : "left"));
        const Kit_mesh_report other = editor::check_kit_shape(mirrored);
        ASSERT_TRUE(other.is_valid()) << describe(other);
        EXPECT_EQ(other.triangle_count, report.triangle_count);
        EXPECT_NEAR(other.bounds_min.x, -report.bounds_max.x, 1.0e-5f);
        EXPECT_NEAR(other.bounds_max.x, -report.bounds_min.x, 1.0e-5f);
        EXPECT_NEAR(other.bounds_min.z, report.bounds_min.z, 1.0e-5f);
        const std::vector<Kit_connector> mirrored_connectors = connectors_of(mirrored);
        EXPECT_FLOAT_EQ(mirrored_connectors[1].position.x, -connectors[1].position.x);
        EXPECT_FLOAT_EQ(mirrored_connectors[1].outward.x, -connectors[1].outward.x);
        EXPECT_FLOAT_EQ(mirrored_connectors[1].outward.z, connectors[1].outward.z);
    }
}

TEST(Kit_spiral_stairs, walkability_bounds_the_newel_the_radius_and_the_headroom)
{
    const Kit_shape_definition& spiral = get_shape("architecture.spiral_stairs");
    const Kit_agent agent{};
    Kit_parameter_values values = defaults(spiral);
    EXPECT_EQ(spiral.check_walkability(values, agent), "");
    // A thin newel leaves the inner walking line too steep.
    Kit_parameter_values thin = values;
    ASSERT_TRUE(thin.set_float("newel_width", 0.1f));
    EXPECT_NE(spiral.check_walkability(thin, agent), "");
    // A small radius leaves the steps too narrow.
    Kit_parameter_values tight = values;
    ASSERT_TRUE(tight.set_float("radius", 0.9f));
    EXPECT_NE(spiral.check_walkability(tight, agent), "");
    // High steps are thick: the turn above leaves too little headroom.
    Kit_parameter_values high = values;
    ASSERT_TRUE(high.set_float("rise", 3.6f));
    ASSERT_TRUE(high.set_float("step_rise_limit", 0.4f));
    EXPECT_NE(spiral.check_walkability(high, agent), "");
    // A newel wider than the radius is refused, and so are more than 96 steps.
    Kit_parameter_values wide = values;
    ASSERT_TRUE(wide.set_float("radius", 0.8f));
    ASSERT_TRUE(wide.set_float("newel_width", 0.9f));
    EXPECT_NE(wide.validate(), "");
    Kit_parameter_values many = values;
    ASSERT_TRUE(many.set_float("rise", 12.0f));
    ASSERT_TRUE(many.set_float("step_rise_limit", 0.125f));
    EXPECT_EQ(editor::get_stairs_layout(many).step_count, 96);
    EXPECT_EQ(many.validate(), "");
    ASSERT_TRUE(many.set_float("step_rise_limit", 0.12f));
    EXPECT_NE(many.validate(), "");
}

//
// Rock arch.
//

const std::vector<std::string> rock_arch_floats{"span", "rise", "thickness", "depth", "roughness"};

class Kit_rock_arch_segments : public testing::TestWithParam<int>
{
};

TEST_P(Kit_rock_arch_segments, corners_pass_over_seeds)
{
    const Kit_shape_definition& arch = get_shape("nature.rock_arch");
    for (const int seed : {0, 1, 2}) {
        Kit_parameter_values values = explicit_values(arch, seed);
        ASSERT_TRUE(values.set_int("segments", GetParam()));
        for_each_corner(values, rock_arch_floats, expect_valid_with_every_slot);
    }
}

TEST_P(Kit_rock_arch_segments, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& arch = get_shape("nature.rock_arch");
    Kit_parameter_values values = explicit_values(arch, 1);
    ASSERT_TRUE(values.set_int("segments", GetParam()));
    expect_topology_from_integers(values, rock_arch_floats);
    Kit_parameter_values other = values;
    ASSERT_TRUE(other.set_int("seed", 2));
    EXPECT_EQ(topology_of(other), topology_of(values));
    // The band of sections of ten points with its two feet, and four loose rocks.
    const Kit_mesh_report report = editor::check_kit_shape(values);
    EXPECT_EQ(report.component_count, 5u);
    EXPECT_EQ(report.triangle_count, static_cast<unsigned int>((20 * GetParam()) + 20 + (4 * 24)));
}

INSTANTIATE_TEST_SUITE_P(Kit_rock_arch, Kit_rock_arch_segments, testing::Values(8, 14, 24));

// No vertex stands inside the half ellipse of the span and the rise, the feet lie on the ground on both sides of the
// origin, and the ends lie beyond the feet.
TEST(Kit_rock_arch, opening_is_at_least_the_stated_ellipse)
{
    const Kit_shape_definition& arch = get_shape("nature.rock_arch");
    for (int seed = 0; seed < 40; ++seed) {
        Kit_parameter_values values = all_derived(arch, seed);
        ASSERT_TRUE(values.set_int("segments", 8 + (seed % 17)));
        SCOPED_TRACE(values.to_canonical_text());
        const float half_span = 0.5f * values.get_float("span");
        const float rise      = values.get_float("rise");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_EQ(report.bounds_min.y, 0.0f);
        EXPECT_GT(report.bounds_max.y, rise + (0.5f * values.get_float("thickness")));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        float nearest_right = 1.0e6f;
        float nearest_left  = 1.0e6f;
        for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
            const glm::vec3 p = point_of(*mesh, v);
            const float     x = p.x / half_span;
            const float     y = p.y / rise;
            EXPECT_GE((x * x) + (y * y), 1.0f - 1.0e-5f) << p.x << " " << p.y << " " << p.z;
            if (p.y == 0.0f) {
                nearest_right = (p.x > 0.0f) ? std::min(nearest_right, p.x) : nearest_right;
                nearest_left  = (p.x < 0.0f) ? std::min(nearest_left, -p.x) : nearest_left;
            }
        }
        // The feet stand at the opening's edge, at most the outline's allowance beyond the half span.
        EXPECT_LT(nearest_right, 1.15f * half_span);
        EXPECT_LT(nearest_left,  1.15f * half_span);
        const std::vector<Kit_connector> connectors = connectors_of(values);
        ASSERT_EQ(connectors.size(), 3u);
        EXPECT_STREQ(connectors[0].name, "start");
        EXPECT_STREQ(connectors[1].name, "end");
        EXPECT_STREQ(connectors[2].name, "base");
        EXPECT_GT(connectors[1].position.x, half_span + values.get_float("thickness"));
        EXPECT_EQ(connectors[0].position.x, -connectors[1].position.x);
    }
}

// The opening suits the agent at its height, not only at the ground.
TEST(Kit_rock_arch, low_and_narrow_openings_are_not_walkable)
{
    const Kit_shape_definition& arch = get_shape("nature.rock_arch");
    const Kit_agent agent{};
    Kit_parameter_values values = defaults(arch);
    EXPECT_EQ(arch.check_walkability(values, agent), "");
    Kit_parameter_values low = values;
    ASSERT_TRUE(low.set_float("rise", 1.9f));
    EXPECT_NE(arch.check_walkability(low, agent), "");
    // 2.1 m high over 1 m: the agent's head has 0.3 m of width.
    Kit_parameter_values narrow = values;
    ASSERT_TRUE(narrow.set_float("rise", 2.1f));
    ASSERT_TRUE(narrow.set_float("span", 1.0f));
    EXPECT_NE(arch.check_walkability(narrow, agent), "");
}

} // anonymous namespace
