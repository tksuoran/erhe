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

// Tests of the shapes of kit_shapes_build_13_16.cpp.
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
        EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
    });
}

//
// Q3: the arch head points.
//

TEST(Kit_arch_head, heads_are_mirrored_convex_and_end_on_their_feet)
{
    for (const editor::Kit_arch_head head : {editor::Kit_arch_head::round, editor::Kit_arch_head::pointed}) {
        for (const float rise : {0.2f, 1.0f, 1.2f, 3.0f}) {
            for (const int segments : {2, 5, 12, 32}) {
                SCOPED_TRACE(fmt::format("head {} rise {} segments {}", static_cast<int>(head), rise, segments));
                const std::vector<glm::vec2> points = editor::make_kit_arch_head_points(head, 1.0f, rise, segments);
                const bool        pointed = (head == editor::Kit_arch_head::pointed);
                const std::size_t count   = static_cast<std::size_t>(pointed ? (2 * (segments / 2)) : segments);
                ASSERT_EQ(points.size(), count + 1);
                EXPECT_EQ(points.front(), (glm::vec2{1.0f, 0.0f}));
                EXPECT_EQ(points.back(),  (glm::vec2{-1.0f, 0.0f}));
                for (std::size_t i = 0; i < points.size(); ++i) {
                    const glm::vec2 mirrored = points[points.size() - 1 - i];
                    EXPECT_EQ(points[i].x, -mirrored.x);
                    EXPECT_EQ(points[i].y, mirrored.y);
                    EXPECT_LE(points[i].y, rise);
                    EXPECT_LE(std::abs(points[i].x), 1.0f);
                }
                if ((count % 2) == 0) {
                    EXPECT_EQ(points[count / 2], (glm::vec2{0.0f, rise}));
                }
                // Convex: every turn from the right foot to the left is to the left.
                for (std::size_t i = 1; (i + 1) < points.size(); ++i) {
                    const glm::vec2 a = points[i] - points[i - 1];
                    const glm::vec2 b = points[i + 1] - points[i];
                    EXPECT_GT((a.x * b.y) - (a.y * b.x), 0.0f) << "point " << i;
                }
            }
        }
    }
}

TEST(Kit_arch_head, jitter_moves_every_point_but_the_feet_along_its_radius)
{
    editor::Kit_random random{5};
    const std::vector<glm::vec2> plain    = editor::make_kit_arch_head_points(editor::Kit_arch_head::round, 2.0f, 1.5f, 12);
    const std::vector<glm::vec2> jittered = editor::make_kit_arch_head_points(editor::Kit_arch_head::round, 2.0f, 1.5f, 12, 0.1f, &random);
    ASSERT_EQ(plain.size(), jittered.size());
    EXPECT_EQ(plain.front(), jittered.front());
    EXPECT_EQ(plain.back(),  jittered.back());
    for (std::size_t i = 1; (i + 1) < plain.size(); ++i) {
        const float factor = glm::length(jittered[i]) / glm::length(plain[i]);
        EXPECT_NE(plain[i], jittered[i]);
        EXPECT_GE(factor, 0.9f - 1.0e-5f);
        EXPECT_LE(factor, 1.1f + 1.0e-5f);
        EXPECT_NEAR((plain[i].x * jittered[i].y) - (plain[i].y * jittered[i].x), 0.0f, 1.0e-5f);
    }
}

//
// Window wall.
//

TEST(Kit_window_wall, every_head_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& wall = get_shape("architecture.window_wall");
    ASSERT_EQ(wall.material_slots, (std::vector<std::string>{"wall", "trim", "wood"}));
    for (const std::string& head : enum_values_of(wall, "head")) {
        int checked = 0;
        for (const int bars : {0, 3}) {
            Kit_parameter_values values = defaults(wall);
            ASSERT_TRUE(values.set_enum("head", head));
            ASSERT_TRUE(values.set_int("mullions", bars));
            ASSERT_TRUE(values.set_int("transoms", bars));
            for_each_corner(values, {"length", "height", "thickness", "opening_width", "opening_height", "sill_height", "segments"}, [&checked](const Kit_parameter_values& corner) {
                if (corner.validate().empty()) {
                    expect_valid_with_every_slot(corner);
                    ++checked;
                }
            });
        }
        // An opening as wide as the shortest wall, or as high as the lowest with its sill, is refused.
        EXPECT_EQ(checked, 2 * 60) << head;
    }
}

TEST(Kit_window_wall, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& wall = get_shape("architecture.window_wall");
    for (const std::string& head : enum_values_of(wall, "head")) {
        Kit_parameter_values values = defaults(wall);
        ASSERT_TRUE(values.set_enum("head", head));
        // Values every corner of the opening's ranges fits in.
        ASSERT_TRUE(values.set_float("length", 8.0f));
        ASSERT_TRUE(values.set_float("height", 12.0f));
        const std::vector<uint32_t> topology = topology_of(values);
        for_each_corner(values, {"thickness", "opening_width", "opening_height", "sill_height"}, [&topology](const Kit_parameter_values& corner) {
            ASSERT_EQ(corner.validate(), "");
            EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
        });
    }
}

// The opening is clear: no vertex of the wall or its surround lies inside it, and the casing and its bars stay within
// the wall's thickness. The ends are plain rectangles, so window walls and walls join (R2.4).
TEST(Kit_window_wall, opening_is_clear_and_the_ends_are_plain)
{
    const Kit_shape_definition& wall = get_shape("architecture.window_wall");
    for (const std::string& head : enum_values_of(wall, "head")) {
        for (const int seed : derive_seeds()) {
            Kit_parameter_values values = all_derived(wall, seed);
            ASSERT_TRUE(values.set_enum("head", head));
            ASSERT_EQ(values.validate(), "");
            SCOPED_TRACE(values.to_canonical_text());
            const float half_length = 0.5f * values.get_float("length");
            const float half_width  = 0.5f * values.get_float("opening_width");
            const float half_t      = 0.5f * values.get_float("thickness");
            const float sill        = values.get_float("sill_height");
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            std::vector<bool> is_wood(mesh->vertices.nb(), false);
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    is_wood[mesh->facets.vertex(f, c)] = is_wood[mesh->facets.vertex(f, c)] || (facet_slots[f] == 2);
                }
            }
            for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                const glm::vec3 p = point_of(*mesh, v);
                EXPECT_LE(std::abs(p.z), half_t);
                if (is_wood[v]) {
                    EXPECT_LT(std::abs(p.z), 0.5f * half_t);
                    continue;
                }
                // Below the springing the opening is as wide as stated.
                const bool inside = (std::abs(p.x) < (half_width - 1.0e-5f)) && (p.y > (sill + 1.0e-5f)) && (p.y < (sill + (0.4f * values.get_float("opening_height"))));
                EXPECT_FALSE(inside) << p.x << " " << p.y << " " << p.z;
            }
            EXPECT_EQ(vertices_on_x(*mesh, half_length).size(), 4u);
            EXPECT_EQ(vertices_on_x(*mesh, -half_length).size(), 4u);
            std::vector<Kit_connector> connectors;
            wall.get_connectors(values, connectors);
            ASSERT_EQ(connectors.size(), 6u);
            EXPECT_STREQ(connectors[0].name, "start");
            EXPECT_STREQ(connectors[3].name, "top");
            EXPECT_EQ(connectors[3].position.y, values.get_float("height"));
        }
    }
}

// A sill the agent climbs makes the window a doorway that has to fit the agent.
TEST(Kit_window_wall, low_sill_needs_a_doorway)
{
    const Kit_shape_definition& wall = get_shape("architecture.window_wall");
    Kit_parameter_values values = defaults(wall);
    EXPECT_EQ(wall.check_walkability(values, Kit_agent{}), "");
    ASSERT_TRUE(values.set_float("sill_height", 0.3f));
    EXPECT_NE(wall.check_walkability(values, Kit_agent{}), "");
    ASSERT_TRUE(values.set_float("opening_height", 2.2f));
    EXPECT_EQ(wall.check_walkability(values, Kit_agent{}), "");
    ASSERT_TRUE(values.set_float("opening_width", 3.9f));
    EXPECT_NE(values.validate(), ""); // a 4 m wall keeps 0.15 m beside the opening
}

//
// Pediment.
//

const std::vector<std::string> pediment_floats{"width", "pitch", "depth", "recess"};

TEST(Kit_pediment, corners_pass_with_every_slot_and_dentils_are_parts)
{
    const Kit_shape_definition& pediment = get_shape("architecture.pediment");
    ASSERT_EQ(pediment.material_slots, (std::vector<std::string>{"trim", "wall"}));
    for (const int dentils : {0, 1, 9, 16}) {
        Kit_parameter_values values = defaults(pediment);
        ASSERT_TRUE(values.set_int("dentils", dentils));
        for_each_corner(values, pediment_floats, expect_valid_with_every_slot);
        // Four dentils per count: under both rakes, on both faces.
        const Kit_mesh_report report = editor::check_kit_shape(values);
        EXPECT_EQ(report.component_count, static_cast<unsigned int>(1 + (4 * dentils)));
        EXPECT_EQ(report.triangle_count, static_cast<unsigned int>(88 + (48 * dentils)));
    }
}

TEST(Kit_pediment, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& pediment = get_shape("architecture.pediment");
    for (const int dentils : {0, 9}) {
        Kit_parameter_values values = defaults(pediment);
        ASSERT_TRUE(values.set_int("dentils", dentils));
        expect_topology_from_integers(values, pediment_floats);
    }
}

// The apex stands over the origin at the eave's height plus the half width's rise, the bounds are the width by the
// depth, and the field lies behind the cornice on both faces.
TEST(Kit_pediment, bounds_follow_the_width_the_pitch_and_the_depth)
{
    const Kit_shape_definition& pediment = get_shape("architecture.pediment");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(pediment, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float half  = 0.5f * values.get_float("width");
        const float depth = values.get_float("depth");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_FLOAT_EQ(report.bounds_min.x, -half);
        EXPECT_FLOAT_EQ(report.bounds_max.x, half);
        EXPECT_FLOAT_EQ(report.bounds_max.z, 0.5f * depth);
        EXPECT_FLOAT_EQ(report.bounds_min.z, -0.5f * depth);
        const float rise = half * std::tan(glm::radians(values.get_float("pitch")));
        EXPECT_GT(report.bounds_max.y, rise);
        EXPECT_LT(report.bounds_max.y, rise + (0.1f * half));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        int fields = 0;
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            if (facet_slots[f] != 1) {
                continue;
            }
            ++fields;
            for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                EXPECT_LT(std::abs(point_of(*mesh, mesh->facets.vertex(f, c)).z), (0.5f * depth) - (0.9f * std::min(values.get_float("recess"), 0.1f * depth)));
            }
        }
        EXPECT_EQ(fields, 2);
    }
}

//
// Stepped platform.
//

const std::vector<std::string> platform_floats{"width", "depth", "tier_rise", "tier_inset"};

TEST(Kit_stepped_platform, corners_pass_with_every_slot_at_every_tier_count)
{
    const Kit_shape_definition& platform = get_shape("architecture.stepped_platform");
    ASSERT_EQ(platform.material_slots, (std::vector<std::string>{"stone", "floor"}));
    for (int tiers = 1; tiers <= 9; ++tiers) {
        Kit_parameter_values values = defaults(platform);
        ASSERT_TRUE(values.set_int("tiers", tiers));
        for_each_corner(values, platform_floats, expect_valid_with_every_slot);
        EXPECT_EQ(editor::check_kit_shape(values).triangle_count, static_cast<unsigned int>((24 * tiers) + 4));
    }
}

TEST(Kit_stepped_platform, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& platform = get_shape("architecture.stepped_platform");
    for (const int tiers : {1, 3, 9}) {
        Kit_parameter_values values = defaults(platform);
        ASSERT_TRUE(values.set_int("tiers", tiers));
        expect_topology_from_integers(values, platform_floats);
    }
}

// The deck is the floor slot at the tiers' height, where the top connector lies, and keeps at least 0.3 of the smaller
// side at any inset; each riser is the tier rise high.
TEST(Kit_stepped_platform, deck_lies_at_the_top_and_keeps_its_share)
{
    const Kit_shape_definition& platform = get_shape("architecture.stepped_platform");
    for (int tiers = 1; tiers <= 9; ++tiers) {
        Kit_parameter_values values = defaults(platform);
        ASSERT_TRUE(values.set_int("tiers", tiers));
        for_each_corner(values, platform_floats, [tiers, &platform](const Kit_parameter_values& corner) {
            SCOPED_TRACE(corner.to_canonical_text());
            const float top   = corner.get_float("tier_rise") * static_cast<float>(tiers);
            const float least = std::min(corner.get_float("width"), corner.get_float("depth"));
            const std::unique_ptr<GEO::Mesh> mesh = generate(corner);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            int   decks  = 0;
            float across = 0.0f;
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                if (facet_slots[f] != 1) {
                    continue;
                }
                ++decks;
                glm::vec3 low {1.0e9f};
                glm::vec3 high{-1.0e9f};
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    const glm::vec3 p = point_of(*mesh, mesh->facets.vertex(f, c));
                    EXPECT_EQ(p.y, top);
                    low  = glm::min(low, p);
                    high = glm::max(high, p);
                }
                across = std::min(high.x - low.x, high.z - low.z);
            }
            EXPECT_EQ(decks, 1);
            EXPECT_GT(across, 0.15f * least);
            EXPECT_FLOAT_EQ(editor::check_kit_shape(corner).bounds_max.y, top);
            std::vector<Kit_connector> connectors;
            platform.get_connectors(corner, connectors);
            ASSERT_EQ(connectors.size(), 6u);
            EXPECT_STREQ(connectors[1].name, "top");
            EXPECT_EQ(connectors[1].position.y, top);
            EXPECT_STREQ(connectors[2].name, "front");
            EXPECT_EQ(connectors[2].position.z, 0.5f * corner.get_float("depth"));
        });
    }
}

//
// Succulent and grass tuft.
//

// Every form at the corners of its sizes and counts over seeds: the checks, the ceiling, every slot.
void expect_forms_pass_at_the_corners(const char* const id, const std::vector<std::string>& names)
{
    const Kit_shape_definition& shape = get_shape(id);
    for (const std::string& form : enum_values_of(shape, "form")) {
        for (const int seed : sweep_seeds(shape)) {
            Kit_parameter_values values = explicit_values(shape, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            for_each_corner(values, names, expect_valid_with_every_slot);
        }
    }
}

// R3.7: the facets, their order and their slots follow from the form and the integer values alone.
void expect_forms_topology_from_integers(const char* const id, const std::vector<std::string>& floats, const std::vector<int>& seeds)
{
    const Kit_shape_definition& shape = get_shape(id);
    for (const std::string& form : enum_values_of(shape, "form")) {
        for (const int seed : seeds) {
            Kit_parameter_values values = explicit_values(shape, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            expect_topology_from_integers(values, floats);
        }
    }
}

TEST(Kit_succulent, every_form_passes_at_the_corners_over_seeds)
{
    ASSERT_EQ(get_shape("nature.succulent").material_slots, (std::vector<std::string>{"foliage", "bark", "shoot"}));
    expect_forms_pass_at_the_corners("nature.succulent", {"size", "leaves", "spread"});
}

TEST(Kit_succulent, topology_follows_from_integers_alone)
{
    // No seed changes it either: the leaves' count is a value.
    expect_forms_topology_from_integers("nature.succulent", {"size", "spread"}, {1, 3, 17});
    const Kit_shape_definition& succulent = get_shape("nature.succulent");
    EXPECT_EQ(topology_of(explicit_values(succulent, 1)), topology_of(explicit_values(succulent, 4242)));
}

// A leaf is one closed part, a yucca's two swords; a rosette has its stem and its bud beside them.
TEST(Kit_succulent, each_leaf_is_a_part)
{
    const Kit_shape_definition& succulent = get_shape("nature.succulent");
    for (const std::string& form : enum_values_of(succulent, "form")) {
        for (const int leaves : {6, 16, 32}) {
            Kit_parameter_values values = explicit_values(succulent, 2);
            ASSERT_TRUE(values.set_enum("form", form));
            ASSERT_TRUE(values.set_int("leaves", leaves));
            const Kit_mesh_report report = editor::check_kit_shape(values);
            ASSERT_TRUE(report.is_valid()) << describe(report);
            EXPECT_EQ(report.component_count, static_cast<unsigned int>((((form == "yucca") ? 2 : 1) * leaves) + 2)) << form;
        }
    }
}

TEST(Kit_grass_tuft, every_form_passes_at_the_corners_over_seeds)
{
    ASSERT_EQ(get_shape("nature.grass_tuft").material_slots, (std::vector<std::string>{"blade", "plume"}));
    expect_forms_pass_at_the_corners("nature.grass_tuft", {"height", "blades", "spread"});
}

TEST(Kit_grass_tuft, topology_follows_from_integers_alone)
{
    expect_forms_topology_from_integers("nature.grass_tuft", {"height", "spread"}, {1, 3, 17});
    const Kit_shape_definition& tuft = get_shape("nature.grass_tuft");
    EXPECT_EQ(topology_of(explicit_values(tuft, 1)), topology_of(explicit_values(tuft, 4242)));
}

// The parts of each form: the tussock, the blades, and the stalks, or the canes with their panicles.
TEST(Kit_grass_tuft, parts_follow_the_blade_count)
{
    const Kit_shape_definition& tuft = get_shape("nature.grass_tuft");
    for (const int blades : {4, 16, 40}) {
        Kit_parameter_values values = explicit_values(tuft, 2);
        ASSERT_TRUE(values.set_int("blades", blades));
        ASSERT_TRUE(values.set_enum("form", "grass"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, static_cast<unsigned int>(1 + blades + 1 + (blades / 10)));
        ASSERT_TRUE(values.set_enum("form", "pampas"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, static_cast<unsigned int>(1 + blades + 2 + (blades / 6)));
        ASSERT_TRUE(values.set_enum("form", "reed"));
        EXPECT_EQ(editor::check_kit_shape(values).component_count, static_cast<unsigned int>(1 + blades + (2 * std::max(2, blades / 3))));
    }
}

//
// Boulder cluster and stepping stone.
//

TEST(Kit_boulder_cluster, every_form_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& cluster = get_shape("nature.boulder_cluster");
    for (const std::string& form : enum_values_of(cluster, "form")) {
        for (const int seed : sweep_seeds(cluster)) {
            Kit_parameter_values values = explicit_values(cluster, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            for_each_corner(values, {"radius", "count", "size_spread", "roughness"}, [](const Kit_parameter_values& corner) {
                expect_valid_with_every_slot(corner);
                // Every boulder is a closed part.
                EXPECT_EQ(editor::check_kit_shape(corner).component_count, static_cast<unsigned int>(corner.get_int("count")));
            });
        }
    }
}

TEST(Kit_boulder_cluster, topology_follows_from_integers_alone)
{
    expect_forms_topology_from_integers("nature.boulder_cluster", {"radius", "size_spread", "roughness"}, {1, 3, 17, 4242});
}

// Seeds give a family: the boulders' side counts and, in a mixed heap, their forms differ between seeds.
TEST(Kit_boulder_cluster, seeds_give_several_topologies)
{
    const Kit_shape_definition& cluster = get_shape("nature.boulder_cluster");
    std::set<std::vector<uint32_t>> topologies;
    for (int seed = 0; seed < 30; ++seed) {
        topologies.insert(topology_of(explicit_values(cluster, seed)));
    }
    EXPECT_GE(topologies.size(), 20u);
}

// The heap stays about its footprint and over the origin, at any size spread.
TEST(Kit_boulder_cluster, heap_keeps_to_its_footprint)
{
    const Kit_shape_definition& cluster = get_shape("nature.boulder_cluster");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(cluster, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float radius = values.get_float("radius");
        const Kit_mesh_report report = editor::check_kit_shape(values);
        ASSERT_TRUE(report.is_valid()) << describe(report);
        EXPECT_LT(std::max(std::max(-report.bounds_min.x, report.bounds_max.x), std::max(-report.bounds_min.z, report.bounds_max.z)), 1.3f * radius);
        EXPECT_GT(std::max(report.bounds_max.x - report.bounds_min.x, report.bounds_max.z - report.bounds_min.z), 1.0f * radius);
        EXPECT_GT(report.bounds_max.y, 0.25f * radius);
        EXPECT_LT(report.bounds_max.y, 1.5f * radius);
    }
}

TEST(Kit_stepping_stone, corners_pass_over_seeds_and_sides)
{
    const Kit_shape_definition& stone = get_shape("nature.stepping_stone");
    for (const int seed : sweep_seeds(stone)) {
        for_each_corner(explicit_values(stone, seed), {"diameter", "thickness", "sides"}, expect_valid_with_every_slot);
    }
}

TEST(Kit_stepping_stone, topology_follows_from_integers_alone)
{
    const Kit_shape_definition& stone = get_shape("nature.stepping_stone");
    for (int sides = 5; sides <= 9; ++sides) {
        Kit_parameter_values values = explicit_values(stone, 1);
        ASSERT_TRUE(values.set_int("sides", sides));
        expect_topology_from_integers(values, {"diameter", "thickness"});
        // No seed changes it: four quads a side and its corner, the base and the top.
        Kit_parameter_values other = explicit_values(stone, 4242);
        ASSERT_TRUE(other.set_int("sides", sides));
        EXPECT_EQ(topology_of(other), topology_of(values));
        EXPECT_EQ(generate(values)->facets.nb(), static_cast<GEO::index_t>((6 * sides) + 2));
    }
}

// The top is one level polygon at the thickness, where the top connector lies, and it is most of the stone's width.
TEST(Kit_stepping_stone, top_is_level_at_the_thickness)
{
    const Kit_shape_definition& stone = get_shape("nature.stepping_stone");
    for (const int seed : derive_seeds()) {
        const Kit_parameter_values values = all_derived(stone, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const float thickness = values.get_float("thickness");
        const float diameter  = values.get_float("diameter");
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        const GEO::index_t top   = mesh->facets.nb() - 1;
        const GEO::index_t sides = static_cast<GEO::index_t>(values.get_int("sides"));
        ASSERT_EQ(mesh->facets.nb_vertices(top), 2 * sides);
        glm::vec3 low {1.0e9f};
        glm::vec3 high{-1.0e9f};
        for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(top); ++c) {
            const glm::vec3 p = point_of(*mesh, mesh->facets.vertex(top, c));
            EXPECT_EQ(p.y, thickness);
            low  = glm::min(low, p);
            high = glm::max(high, p);
        }
        EXPECT_GT(std::max(high.x - low.x, high.z - low.z), 0.6f * diameter);
        EXPECT_FLOAT_EQ(editor::check_kit_shape(values).bounds_max.y, thickness);
        std::vector<Kit_connector> connectors;
        stone.get_connectors(values, connectors);
        ASSERT_EQ(connectors.size(), 2u);
        EXPECT_STREQ(connectors[1].name, "top");
        EXPECT_EQ(connectors[1].position.y, thickness);
    }
}

} // anonymous namespace
