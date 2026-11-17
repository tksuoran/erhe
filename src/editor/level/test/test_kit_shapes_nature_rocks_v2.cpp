#include "kit_shape_test_support.hpp"

#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_checks.hpp"
#include "level/kit_shapes.hpp"

#include "erhe_geometry/geometry.hpp"

#include <glm/glm.hpp>
#include <geogram/mesh/mesh.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_nature_rocks_v2.cpp.
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

const std::vector<std::string> spire_floats{"height", "radius", "taper", "lean", "roughness"};

TEST(Kit_recreated, rock_spire_forms_pass_at_the_corners_over_seeds)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire.v2");
    ASSERT_EQ(spire.material_slots, (std::vector<std::string>{"rock", "grass"}));
    for (const std::string& form : spire.parameters[spire.find_parameter("form").value()].enum_values) {
        Kit_parameter_values first = explicit_values(spire, 0);
        ASSERT_TRUE(first.set_enum("form", form));
        for_each_corner(first, {"height", "radius", "taper", "roughness", "strata", "segments"}, expect_valid_with_every_slot);
        Kit_parameter_values second = explicit_values(spire, 1);
        ASSERT_TRUE(second.set_enum("form", form));
        for_each_corner(second, {"height", "radius", "roughness", "segments"}, expect_valid_with_every_slot);
        // The lean against the height and the radius, at the other seeds.
        for (int seed = 2; seed < 6; ++seed) {
            Kit_parameter_values values = explicit_values(spire, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            ASSERT_TRUE(values.set_float("lean", 15.0f));
            for_each_corner(values, {"height", "radius", "roughness"}, expect_valid_with_every_slot);
        }
    }
}

// R3.7: the facets, their order and their slots follow from the form, the strata, the segments and the seed alone.
TEST(Kit_recreated, rock_spire_topology_follows_from_integers_alone)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire.v2");
    for (const std::string& form : spire.parameters[spire.find_parameter("form").value()].enum_values) {
        for (const int seed : {1, 3, 17, 4242}) {
            Kit_parameter_values values = explicit_values(spire, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            const std::vector<uint32_t> topology = topology_of(values);
            for_each_corner(values, spire_floats, [&topology](const Kit_parameter_values& corner) {
                EXPECT_EQ(topology_of(corner), topology) << corner.to_canonical_text();
            });
        }
    }
}

// Seeds give a family: the ledges' windows, the diagonals and the fallen blocks differ between seeds.
TEST(Kit_recreated, rock_spire_seeds_give_several_topologies)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire.v2");
    std::set<std::vector<uint32_t>> topologies;
    std::set<unsigned int>          vertex_counts;
    for (int seed = 0; seed < 30; ++seed) {
        const Kit_parameter_values values = explicit_values(spire, seed);
        topologies.insert(topology_of(values));
        vertex_counts.insert(generate(values)->vertices.nb());
    }
    EXPECT_EQ(topologies.size(), 30u);
    EXPECT_EQ(vertex_counts.size(), 3u);
}

// The shaft's rows never cross at any value: up every column each vertex lies above the one below, the base ring on
// the ground about a fan vertex on the origin.
TEST(Kit_recreated, rock_spire_rows_rise_up_every_column)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire.v2");
    for (const std::string& form : spire.parameters[spire.find_parameter("form").value()].enum_values) {
        for (int seed = 0; seed < 8; ++seed) {
            Kit_parameter_values values = explicit_values(spire, seed);
            ASSERT_TRUE(values.set_enum("form", form));
            for_each_corner(values, {"height", "radius", "roughness", "strata"}, [](const Kit_parameter_values& corner) {
                SCOPED_TRACE(corner.to_canonical_text());
                const std::unique_ptr<GEO::Mesh> mesh     = generate(corner);
                const GEO::index_t               segments = static_cast<GEO::index_t>(corner.get_int("segments"));
                // The grid's vertices come first, row by row; the origin vertex follows them.
                GEO::index_t origin = 0;
                while ((origin < mesh->vertices.nb()) && (point_of(*mesh, origin) != glm::vec3{0.0f})) {
                    ++origin;
                }
                ASSERT_LT(origin, mesh->vertices.nb());
                ASSERT_EQ(origin % segments, 0u);
                ASSERT_GE(origin / segments, static_cast<GEO::index_t>(corner.get_int("strata") + 2));
                for (GEO::index_t v = 0; v < origin; ++v) {
                    const float y = point_of(*mesh, v).y;
                    if (v < segments) {
                        EXPECT_EQ(y, 0.0f);
                    } else {
                        EXPECT_GT(y, point_of(*mesh, v - segments).y) << "vertex " << v;
                    }
                }
            });
        }
    }
}

// A mesa's top is flat at the height but its center, which rises a little.
TEST(Kit_recreated, rock_spire_mesa_top_is_flat_turf)
{
    const Kit_shape_definition& spire = get_shape("nature.rock_spire.v2");
    for (int seed = 0; seed < 20; ++seed) {
        Kit_parameter_values values = all_derived(spire, seed);
        ASSERT_TRUE(values.set_enum("form", "mesa"));
        SCOPED_TRACE(values.to_canonical_text());
        const float                      height = values.get_float("height");
        const std::unique_ptr<GEO::Mesh> mesh   = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        int top_facets = 0;
        for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
            const glm::vec3 a = point_of(*mesh, mesh->facets.vertex(f, 0));
            const glm::vec3 b = point_of(*mesh, mesh->facets.vertex(f, 1));
            const glm::vec3 c = point_of(*mesh, mesh->facets.vertex(f, 2));
            if ((a.y == height) && (b.y == height) && (c.y > height)) {
                ++top_facets;
                EXPECT_EQ(facet_slots[f], 1u);
                EXPECT_LT(c.y, height + (0.05f * values.get_float("radius")));
            }
        }
        EXPECT_EQ(top_facets, values.get_int("segments"));
    }
}

} // anonymous namespace
