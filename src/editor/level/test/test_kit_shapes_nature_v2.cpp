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
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_nature_v2.cpp.
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
    EXPECT_LE(report.triangle_count, get_library().find_kit("nature")->triangle_ceiling);
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

// What a mesh's topology is: its counts and every facet's corner count and slot, in order.
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

TEST(Kit_recreated, rock_forms_pass_at_the_corners_over_seeds)
{
    const Kit_shape_definition& rock = get_shape("nature.rock.v2");
    ASSERT_EQ(rock.material_slots, (std::vector<std::string>{"rock"}));
    for (const std::string& form : rock.parameters[rock.find_parameter("form").value()].enum_values) {
        for (int seed = 0; seed < 50; ++seed) {
            Kit_parameter_values values = explicit_values(rock, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            ASSERT_TRUE(values.set_int("pieces", 1 + (seed % 4)));
            for_each_corner(values, {"size", "roughness"}, expect_valid_with_every_slot);
        }
    }
}

// R3.7: a rock's facets and their order follow from the form, the piece count and the seed alone; the lengths and the
// roughness only move vertices, so no float comparison decides the topology.
TEST(Kit_recreated, nature_topology_follows_from_integers_alone)
{
    const Kit_shape_definition& rock = get_shape("nature.rock.v2");
    for (const std::string& form : rock.parameters[rock.find_parameter("form").value()].enum_values) {
        for (const int seed : {1, 3, 17, 4242}) {
            Kit_parameter_values values = explicit_values(rock, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, {"size", "roughness"}, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
    const Kit_shape_definition& tree = get_shape("nature.tree.v2");
    for (const std::string& species : tree.parameters[tree.find_parameter("species").value()].enum_values) {
        for (const int seed : {1, 3, 17, 4242}) {
            Kit_parameter_values values = explicit_values(tree, seed);
            ASSERT_TRUE(values.set_enum("species", species));
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, {"trunk_height", "canopy_size"}, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
    for (const char* const id : {"nature.land_mass.v2", "nature.hill.v2", "nature.bush.v2"}) {
        const Kit_shape_definition& shape = get_shape(id);
        std::vector<std::string> lengths;
        for (const Kit_parameter& parameter : shape.parameters) {
            if (parameter.type == editor::Kit_parameter_type::float_value) {
                lengths.push_back(parameter.name);
            }
        }
        for (const int seed : {1, 3, 17, 4242}) {
            const Kit_parameter_values  values   = explicit_values(shape, seed);
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, lengths, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
}

// Seeds give a family: the side counts differ between seeds, so the vertex counts take several values.
TEST(Kit_recreated, rock_seeds_give_several_topologies)
{
    const Kit_shape_definition& rock = get_shape("nature.rock.v2");
    std::set<unsigned int> vertex_counts;
    for (int seed = 0; seed < 40; ++seed) {
        vertex_counts.insert(generate(explicit_values(rock, seed))->vertices.nb());
    }
    EXPECT_GE(vertex_counts.size(), 5u);
}

TEST(Kit_recreated, land_mass_passes_at_the_corners_with_a_flat_walkable_top)
{
    const Kit_shape_definition& land_mass = get_shape("nature.land_mass.v2");
    ASSERT_EQ(land_mass.material_slots, (std::vector<std::string>{"grass", "earth", "rock"}));
    for (int seed = 0; seed < 12; ++seed) {
        for_each_corner(explicit_values(land_mass, seed), {"footprint", "height", "erosion", "segments"}, expect_valid_with_every_slot);
    }
    // The turf's top is the plane y = height, at least the agent's width across in every direction, over derived values.
    const Kit_agent agent{};
    const float     need = (2.0f * agent.radius) + editor::kit_opening_margin;
    for (int seed = 0; seed < 100; ++seed) {
        const Kit_parameter_values values = all_derived(land_mass, seed);
        SCOPED_TRACE(values.to_canonical_text());
        EXPECT_EQ(land_mass.check_walkability(values, agent), "");
        const float                      height = values.get_float("height");
        const std::unique_ptr<GEO::Mesh> mesh   = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        float nearest_rim = 1.0e9f;
        int   top_facets  = 0;
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            const glm::vec3 a = point_of(*mesh, mesh->facets.vertex(f, 0));
            const glm::vec3 b = point_of(*mesh, mesh->facets.vertex(f, 1));
            const glm::vec3 c = point_of(*mesh, mesh->facets.vertex(f, 2));
            if ((a.y != height) || (b.y != height) || (c.y != height)) {
                continue;
            }
            ++top_facets;
            EXPECT_EQ(facet_slots[f], 0u);
            // A top facet is (rim, rim, center).
            EXPECT_EQ(c, (glm::vec3{0.0f, height, 0.0f}));
            nearest_rim = std::min(nearest_rim, std::min(glm::length(a - c), glm::length(b - c)));
        }
        EXPECT_EQ(top_facets, values.get_int("segments"));
        EXPECT_GE(2.0f * nearest_rim * std::cos(glm::radians(180.0f / static_cast<float>(top_facets))), need);
    }
}

TEST(Kit_recreated, land_mass_connectors_are_its_base_and_its_top)
{
    const Kit_shape_definition& land_mass = get_shape("nature.land_mass.v2");
    Kit_parameter_values values = explicit_values(land_mass, 1);
    ASSERT_TRUE(values.set_float("height", 1.75f));
    std::vector<Kit_connector> connectors;
    land_mass.get_connectors(values, connectors);
    ASSERT_EQ(connectors.size(), 2u);
    EXPECT_EQ(connectors[0].name, "base");
    EXPECT_EQ(connectors[1].name, "top");
    EXPECT_EQ(connectors[1].position, (glm::vec3{0.0f, 1.75f, 0.0f}));
}

// The grass facets of the values' hill mesh: the steepest in degrees from level, and the highest vertex.
class Hill_grass
{
public:
    float steepest_degrees{0.0f};
    float top             {0.0f};
};

[[nodiscard]] auto measure_hill_grass(const Kit_parameter_values& values) -> Hill_grass
{
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(*mesh, facet_slots);
    Hill_grass grass;
    for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
        if (facet_slots[f] != 0u) {
            continue;
        }
        const glm::vec3 a = point_of(*mesh, mesh->facets.vertex(f, 0));
        const glm::vec3 b = point_of(*mesh, mesh->facets.vertex(f, 1));
        const glm::vec3 c = point_of(*mesh, mesh->facets.vertex(f, 2));
        const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        grass.top = std::max(grass.top, std::max(a.y, std::max(b.y, c.y)));
        if (normal.y > 0.0f) {
            grass.steepest_degrees = std::max(grass.steepest_degrees, glm::degrees(std::acos(std::min(1.0f, normal.y))));
        }
    }
    return grass;
}

TEST(Kit_recreated, hill_passes_at_the_corners_and_its_grass_is_walkable_over_seeds)
{
    const Kit_shape_definition& hill = get_shape("nature.hill.v2");
    ASSERT_EQ(hill.material_slots, (std::vector<std::string>{"grass", "rock"}));
    for (int seed = 0; seed < 12; ++seed) {
        for_each_corner(explicit_values(hill, seed), {"radius", "height", "rocks", "segments"}, expect_valid_with_every_slot);
    }
    const Kit_agent agent{};
    for (int seed = 0; seed < 100; ++seed) {
        const Kit_parameter_values values = all_derived(hill, seed);
        SCOPED_TRACE(values.to_canonical_text());
        const Hill_grass grass = measure_hill_grass(values);
        EXPECT_LE(grass.steepest_degrees, agent.max_slope_degrees - 2.0f);
        EXPECT_EQ(grass.top, values.get_float("height")); // the crest is the top of the grass
        EXPECT_EQ(hill.check_walkability(values, agent), "");
    }
    // A hill as high as its radius is refused by the measured slope.
    Kit_parameter_values steep = explicit_values(hill, 1);
    ASSERT_TRUE(steep.set_float("height", steep.get_float("radius")));
    EXPECT_NE(hill.check_walkability(steep, agent), "");
}

TEST(Kit_recreated, tree_species_pass_at_the_corners_and_stand_on_the_origin)
{
    const Kit_shape_definition& tree = get_shape("nature.tree.v2");
    ASSERT_EQ(tree.material_slots, (std::vector<std::string>{"bark", "foliage"}));
    for (const std::string& species : tree.parameters[tree.find_parameter("species").value()].enum_values) {
        for (int seed = 0; seed < 25; ++seed) {
            Kit_parameter_values values = explicit_values(tree, seed);
            ASSERT_TRUE(values.set_enum("species", species));
            for_each_corner(values, {"trunk_height", "canopy_size"}, expect_valid_with_every_slot);
            // The trunk's base is a fan about a vertex on the origin, and the canopy's base is no lower than a tenth of it.
            const std::unique_ptr<GEO::Mesh> mesh = generate(values);
            std::vector<uint32_t> facet_slots;
            editor::read_kit_facet_slots(*mesh, facet_slots);
            bool  origin_vertex = false;
            float foliage_low   = 1.0e9f;
            for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                    const glm::vec3 p = point_of(*mesh, mesh->facets.vertex(f, c));
                    origin_vertex = origin_vertex || ((facet_slots[f] == 0u) && (p == glm::vec3{0.0f}));
                    if (facet_slots[f] == 1u) {
                        foliage_low = std::min(foliage_low, p.y);
                    }
                }
            }
            EXPECT_TRUE(origin_vertex) << values.to_canonical_text();
            EXPECT_GT(foliage_low, 0.1f * values.get_float("trunk_height")) << values.to_canonical_text();
        }
    }
}

TEST(Kit_recreated, bush_passes_at_the_corners_over_seeds)
{
    const Kit_shape_definition& bush = get_shape("nature.bush.v2");
    ASSERT_EQ(bush.material_slots, (std::vector<std::string>{"foliage", "bark"}));
    for (int seed = 0; seed < 50; ++seed) {
        for_each_corner(explicit_values(bush, seed), {"size", "twigs"}, expect_valid_with_every_slot);
    }
}

} // anonymous namespace
