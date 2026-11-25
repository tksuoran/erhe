#include "kit_shape_test_support.hpp"

#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the palm shrub and the leaf plant (kit_shapes_nature_plants.cpp).
namespace {

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

// The mesh checks pass within the kit's ceiling, every slot has facets, and only the stem's base touches the ground:
// every facet of a leaf lies above it.
void expect_valid_plant(const Kit_parameter_values& values)
{
    SCOPED_TRACE(values.to_canonical_text());
    const Kit_mesh_report report = editor::check_kit_shape(values);
    EXPECT_TRUE(report.is_valid()) << describe(report);
    EXPECT_EQ(report.bounds_min.y, 0.0f); // the base is the lowest point
    EXPECT_LE(report.triangle_count, get_library().find_kit("nature")->triangle_ceiling);
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(*mesh, facet_slots);
    const std::set<uint32_t> used{facet_slots.begin(), facet_slots.end()};
    EXPECT_EQ(used.size(), values.get_shape()->material_slots.size());
    float lowest_leaf = 1.0e9f;
    for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
        if (facet_slots[f] == 0u) {
            continue;
        }
        for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
            lowest_leaf = std::min(lowest_leaf, point_of(*mesh, mesh->facets.vertex(f, c)).y);
        }
    }
    EXPECT_GT(lowest_leaf, 0.0f);
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

const std::vector<std::string> plant_slots{"bark", "leaf", "young_leaf"};

TEST(Kit_nature_plants, palm_shrub_species_pass_at_the_corners_over_seeds)
{
    const Kit_shape_definition& shrub = get_shape("nature.palm_shrub");
    ASSERT_EQ(shrub.material_slots, plant_slots);
    EXPECT_EQ(shrub.collision, editor::Kit_collision::convex_hull);
    for (const std::string& species : shrub.parameters[shrub.find_parameter("species").value()].enum_values) {
        for (const int seed : sweep_seeds(shrub)) {
            Kit_parameter_values values = explicit_values(shrub, seed);
            ASSERT_TRUE(values.set_enum("species", species));
            for_each_corner(values, {"size", "fronds"}, expect_valid_plant);
        }
    }
}

TEST(Kit_nature_plants, leaf_plant_forms_pass_at_the_corners_over_seeds)
{
    const Kit_shape_definition& plant = get_shape("nature.leaf_plant");
    ASSERT_EQ(plant.material_slots, plant_slots);
    EXPECT_EQ(plant.collision, editor::Kit_collision::none); // P5: a walker passes through it
    for (const std::string& form : plant.parameters[plant.find_parameter("form").value()].enum_values) {
        for (const int seed : sweep_seeds(plant)) {
            Kit_parameter_values values = explicit_values(plant, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            for_each_corner(values, {"height", "leaves", "droop", "leaf_width"}, expect_valid_plant);
        }
    }
}

// R3.7: a plant's facets, their order and their slots follow from the species or form, the leaf count and the seed
// alone; the lengths and ratios only move vertices.
TEST(Kit_nature_plants, plant_topology_follows_from_integers_alone)
{
    const Kit_shape_definition& shrub = get_shape("nature.palm_shrub");
    for (const std::string& species : shrub.parameters[shrub.find_parameter("species").value()].enum_values) {
        for (const int seed : {1, 3, 17, 4242}) {
            Kit_parameter_values values = explicit_values(shrub, seed);
            ASSERT_TRUE(values.set_enum("species", species));
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, {"size"}, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
    const Kit_shape_definition& plant = get_shape("nature.leaf_plant");
    for (const std::string& form : plant.parameters[plant.find_parameter("form").value()].enum_values) {
        for (const int seed : {1, 3, 17, 4242}) {
            Kit_parameter_values values = explicit_values(plant, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, {"height", "droop", "leaf_width"}, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
}

// Every leaf count gives its own mesh within the ceiling, and a leaf more never takes triangles away below the count
// where the fronds' leaflet pairs step down.
TEST(Kit_nature_plants, plants_keep_the_ceiling_at_every_leaf_count)
{
    const unsigned int ceiling = get_library().find_kit("nature")->triangle_ceiling;
    const Kit_shape_definition& shrub = get_shape("nature.palm_shrub");
    const Kit_parameter&        fronds = shrub.parameters[shrub.find_parameter("fronds").value()];
    for (const std::string& species : shrub.parameters[shrub.find_parameter("species").value()].enum_values) {
        for (int count = fronds.int_min; count <= fronds.int_max; ++count) {
            for (const int seed : sweep_seeds(shrub)) {
                Kit_parameter_values values = explicit_values(shrub, seed);
                ASSERT_TRUE(values.set_enum("species", species));
                ASSERT_TRUE(values.set_int("fronds", count));
                EXPECT_LE(editor::count_kit_triangles(*generate(values)), ceiling) << values.to_canonical_text();
            }
        }
    }
    const Kit_shape_definition& plant  = get_shape("nature.leaf_plant");
    const Kit_parameter&        leaves = plant.parameters[plant.find_parameter("leaves").value()];
    for (const std::string& form : plant.parameters[plant.find_parameter("form").value()].enum_values) {
        for (int count = leaves.int_min; count <= leaves.int_max; ++count) {
            for (const int seed : sweep_seeds(plant)) {
                Kit_parameter_values values = explicit_values(plant, seed);
                ASSERT_TRUE(values.set_enum("form", form));
                ASSERT_TRUE(values.set_int("leaves", count));
                EXPECT_LE(editor::count_kit_triangles(*generate(values)), ceiling) << values.to_canonical_text();
            }
        }
    }
}

// The stem stands on the origin: its base is a fan about a vertex there, in slot bark.
TEST(Kit_nature_plants, plants_stand_on_a_stem_at_the_origin)
{
    for (const char* const id : {"nature.palm_shrub", "nature.leaf_plant"}) {
        const Kit_shape_definition& shape = get_shape(id);
        for (const std::string& kind : shape.parameters[0].enum_values) {
            for (const int seed : derive_seeds()) {
                Kit_parameter_values values = all_derived(shape, seed);
                ASSERT_TRUE(values.set_enum(shape.parameters[0].name, kind));
                SCOPED_TRACE(values.to_canonical_text());
                const std::unique_ptr<GEO::Mesh> mesh = generate(values);
                std::vector<uint32_t> facet_slots;
                editor::read_kit_facet_slots(*mesh, facet_slots);
                int origin_facets = 0;
                for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                    for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                        if (point_of(*mesh, mesh->facets.vertex(f, c)) == glm::vec3{0.0f}) {
                            ++origin_facets;
                            EXPECT_EQ(facet_slots[f], 0u);
                        }
                    }
                }
                EXPECT_GE(origin_facets, 6);
            }
        }
    }
}

} // anonymous namespace
