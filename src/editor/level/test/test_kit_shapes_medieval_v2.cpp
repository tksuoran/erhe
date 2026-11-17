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
#include <memory>
#include <string>
#include <vector>

// Tests of the shapes of kit_shapes_medieval_v2.cpp.
namespace {

using editor::Kit_agent;
using editor::Kit_connector;
using editor::Kit_mesh_report;
using editor::Kit_parameter;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using namespace editor::kit_test;

// The triangles of each slot of the values' mesh.
[[nodiscard]] auto slot_triangles(const Kit_parameter_values& values) -> std::vector<unsigned int>
{
    const std::unique_ptr<GEO::Mesh> mesh = generate(values);
    std::vector<uint32_t> facet_slots;
    editor::read_kit_facet_slots(*mesh, facet_slots);
    std::vector<unsigned int> triangles(values.get_shape()->material_slots.size(), 0u);
    for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
        triangles[facet_slots[f]] += mesh->facets.nb_vertices(f) - 2;
    }
    return triangles;
}

// The mesh checks pass within the kit's ceiling and every slot has facets.
void expect_valid_with_every_slot(const Kit_parameter_values& values)
{
    SCOPED_TRACE(values.to_canonical_text());
    const Kit_mesh_report report = editor::check_kit_shape(values);
    EXPECT_TRUE(report.is_valid()) << describe(report);
    EXPECT_LE(report.triangle_count, get_library().find_kit("architecture")->triangle_ceiling);
    for (const unsigned int count : slot_triangles(values)) {
        EXPECT_GT(count, 0u);
    }
}

// Every combination of the named float parameters' range ends on the values, the others as given.
void for_each_corner(const Kit_parameter_values& values, const std::vector<std::string>& names, const auto& visit)
{
    const Kit_shape_definition& shape = *values.get_shape();
    for (std::size_t mask = 0; mask < (std::size_t{1} << names.size()); ++mask) {
        Kit_parameter_values corner = values;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const Kit_parameter& parameter = shape.parameters[shape.find_parameter(names[i]).value()];
            ASSERT_TRUE(corner.set_float(names[i], (((mask >> i) & 1u) != 0u) ? parameter.float_max : parameter.float_min));
        }
        visit(corner);
    }
}

TEST(Kit_recreated, battlement_passes_at_the_corners_with_both_slots)
{
    const Kit_shape_definition& battlement = get_shape("architecture.battlement.v2");
    ASSERT_EQ(battlement.material_slots, (std::vector<std::string>{"wall", "trim"}));
    int checked = 0;
    for (const std::string& merlon : battlement.parameters[battlement.find_parameter("merlon").value()].enum_values) {
        Kit_parameter_values values = explicit_values(battlement, 1);
        ASSERT_TRUE(values.set_enum("merlon", merlon));
        for_each_corner(values, {"length", "height", "thickness", "merlon_height", "merlon_width", "crenel_width"}, [&checked](const Kit_parameter_values& corner) {
            expect_valid_with_every_slot(corner);
            ++checked;
        });
    }
    EXPECT_EQ(checked, 3 * 64);
}

// A run tiles flush: both ends are the breastwork and the sill band alone, equal at every value, so pieces placed end
// to end share their end faces and join into whole crenels. The copings' top is the stated height at every merlon form.
TEST(Kit_recreated, battlement_runs_join_at_their_breastwork)
{
    const Kit_shape_definition& battlement = get_shape("architecture.battlement.v2");
    for (const std::string& merlon : {std::string{"square"}, std::string{"swallowtail"}, std::string{"pointed"}}) {
        std::vector<std::array<float, 3>> first_end;
        for (const float length : {1.0f, 2.0f, 5.5f, 24.0f}) {
            for (const float merlon_width : {0.4f, 1.2f, 4.0f}) {
                Kit_parameter_values values = explicit_values(battlement, 1);
                ASSERT_TRUE(values.set_enum("merlon", merlon));
                ASSERT_TRUE(values.set_float("length", length));
                ASSERT_TRUE(values.set_float("merlon_width", merlon_width));
                SCOPED_TRACE(values.to_canonical_text());
                const Kit_mesh_report report = editor::check_kit_shape(values);
                EXPECT_TRUE(report.is_valid()) << describe(report);
                EXPECT_NEAR(report.bounds_max.y, values.get_float("height") + values.get_float("merlon_height"), 1.0e-5f);
                EXPECT_EQ(report.bounds_min.x, -0.5f * length);
                EXPECT_EQ(report.bounds_max.x,  0.5f * length);
                const std::unique_ptr<GEO::Mesh> mesh = generate(values);
                std::vector<std::array<float, 3>> start = vertices_on_x(*mesh, -0.5f * length);
                std::vector<std::array<float, 3>> end   = vertices_on_x(*mesh,  0.5f * length);
                // The breastwork's four corners and the band's six.
                ASSERT_EQ(start.size(), 10u);
                ASSERT_EQ(end.size(), 10u);
                for (std::size_t i = 0; i < start.size(); ++i) {
                    EXPECT_EQ(start[i][1], end[i][1]);
                    EXPECT_EQ(start[i][2], end[i][2]);
                    EXPECT_LE(start[i][1], values.get_float("height")); // no merlon reaches an end
                    start[i][0] = 0.0f;
                }
                if (first_end.empty()) {
                    first_end = start;
                }
                EXPECT_EQ(start, first_end);
            }
        }
    }
}

// Every merlon is pierced by its arrow loop: a run has one closed part per merlon of genus one, so the wall slot's
// Euler characteristic is two for the breastwork and none for a merlon.
TEST(Kit_recreated, battlement_merlons_have_arrow_loops)
{
    const Kit_shape_definition& battlement = get_shape("architecture.battlement.v2");
    for (const float length : {2.0f, 6.0f}) {
        Kit_parameter_values values = explicit_values(battlement, 1);
        ASSERT_TRUE(values.set_float("length", length));
        const std::unique_ptr<GEO::Mesh> mesh = generate(values);
        std::vector<uint32_t> facet_slots;
        editor::read_kit_facet_slots(*mesh, facet_slots);
        GEO::Mesh masonry{3, true};
        editor::extract_kit_slot_mesh(*mesh, facet_slots, 0, masonry);
        const Kit_mesh_report report = editor::check_kit_mesh(masonry);
        EXPECT_TRUE(report.is_valid()) << describe(report);
        const int bays = static_cast<int>(std::lround(length / 2.0f));
        EXPECT_EQ(report.component_count, static_cast<unsigned int>(bays + 1));
        int edges = 0;
        for (GEO::index_t f = 0; f < masonry.facets.nb(); ++f) {
            edges += static_cast<int>(masonry.facets.nb_vertices(f));
        }
        EXPECT_EQ(static_cast<int>(masonry.vertices.nb()) - (edges / 2) + static_cast<int>(masonry.facets.nb()), 2);
    }
}

TEST(Kit_recreated, tower_passes_at_the_corners_with_every_slot)
{
    const Kit_shape_definition& tower = get_shape("architecture.tower.v2");
    ASSERT_EQ(tower.material_slots, (std::vector<std::string>{"wall", "trim", "floor"}));
    int checked = 0;
    for (const std::string& profile : {std::string{"round"}, std::string{"square"}}) {
        for (const std::string& top : {std::string{"battlement"}, std::string{"flat"}}) {
            for (const int slit_rows : {0, 2}) {
                Kit_parameter_values values = explicit_values(tower, 1);
                ASSERT_TRUE(values.set_enum("profile", profile));
                ASSERT_TRUE(values.set_enum("top", top));
                ASSERT_TRUE(values.set_int("slit_rows", slit_rows));
                for_each_corner(values, {"width", "height", "batter", "wall", "opening_width", "opening_height"}, [&checked](const Kit_parameter_values& corner) {
                    if (corner.validate().empty()) {
                        expect_valid_with_every_slot(corner);
                        ++checked;
                    }
                });
            }
        }
    }
    EXPECT_EQ(checked, 2 * 120); // as the original: a narrow tower with thick walls and a wide doorway, or a low one, is refused
}

// The doorway keeps its stated clear size at every in-range value: no vertex inside its box in front of the axis, the
// jamb stones' faces at its sides and the lintel's underside at its head. The floor slot is the walkable top at the
// height, and the connectors lie on the mesh.
TEST(Kit_recreated, tower_doorway_is_clear_and_the_top_floor_is_at_the_height)
{
    const Kit_shape_definition& tower = get_shape("architecture.tower.v2");
    for (const std::string& profile : {std::string{"round"}, std::string{"square"}}) {
        for (const std::string& top : {std::string{"battlement"}, std::string{"flat"}}) {
            Kit_parameter_values values = defaults(tower);
            ASSERT_TRUE(values.set_enum("profile", profile));
            ASSERT_TRUE(values.set_enum("top", top));
            for_each_corner(values, {"width", "batter", "wall", "opening_width", "opening_height"}, [&tower](const Kit_parameter_values& corner) {
                Kit_parameter_values at = corner;
                ASSERT_TRUE(at.set_float("height", std::max(at.get_float("height"), at.get_float("opening_height") + 1.5f)));
                if (!at.validate().empty()) {
                    return;
                }
                SCOPED_TRACE(at.to_canonical_text());
                EXPECT_EQ(tower.check_walkability(at, Kit_agent{}), "");
                const float half   = 0.5f * at.get_float("opening_width");
                const float clear  = at.get_float("opening_height");
                const float height = at.get_float("height");
                const std::unique_ptr<GEO::Mesh> mesh = generate(at);
                std::vector<uint32_t> facet_slots;
                editor::read_kit_facet_slots(*mesh, facet_slots);
                float nearest_x = 1.0e9f;
                float front_z   = 0.0f;
                for (GEO::index_t v = 0; v < mesh->vertices.nb(); ++v) {
                    const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, v);
                    const bool inside = (std::abs(p.x) < (half - 1.0e-4f)) && (p.y > 1.0e-4f) && (p.y < (clear - 1.0e-4f)) && (p.z > 0.0f);
                    EXPECT_FALSE(inside) << p.x << " " << p.y << " " << p.z;
                    if ((p.y < (clear - 1.0e-4f)) && (p.z > 0.0f)) {
                        nearest_x = std::min(nearest_x, std::abs(p.x));
                    }
                    if (p.y == 0.0f) {
                        front_z = std::max(front_z, p.z);
                    }
                }
                EXPECT_NEAR(nearest_x, half, 1.0e-5f);
                bool head = false; // a facet at the clear height across the doorway's middle: the lintel's underside
                for (GEO::index_t f = 0; f < mesh->facets.nb(); ++f) {
                    float min_x = 1.0e9f;
                    float max_x = -1.0e9f;
                    bool  level = true;
                    for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                        const GEO::vec3f p = erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(f, c));
                        min_x = std::min(min_x, p.x);
                        max_x = std::max(max_x, p.x);
                        level = level && (p.y == clear) && (p.z > 0.0f);
                    }
                    head = head || (level && (min_x <= -half) && (max_x >= half));
                    if (facet_slots[f] != 2) {
                        continue;
                    }
                    for (GEO::index_t c = 0; c < mesh->facets.nb_vertices(f); ++c) {
                        EXPECT_EQ(erhe::geometry::get_pointf(mesh->vertices, mesh->facets.vertex(f, c)).y, height);
                    }
                }
                EXPECT_TRUE(head);
                std::vector<Kit_connector> connectors;
                tower.get_connectors(at, connectors);
                ASSERT_EQ(connectors.size(), 6u);
                EXPECT_STREQ(connectors[1].name, "top");
                EXPECT_EQ(connectors[1].position.y, height);
                EXPECT_STREQ(connectors[2].name, "opening_front");
                EXPECT_NEAR(connectors[2].position.z, front_z, 1.0e-5f * std::max(1.0f, front_z));
            });
        }
    }
}

// Arrow slits go through the wall: each makes the wall's shell one handle more, so a row adds triangles and no part.
TEST(Kit_recreated, tower_slits_cut_through_the_wall)
{
    const Kit_shape_definition& tower = get_shape("architecture.tower.v2");
    for (const std::string& profile : {std::string{"round"}, std::string{"square"}}) {
        std::array<Kit_mesh_report, 3> reports;
        for (int rows = 0; rows <= 2; ++rows) {
            Kit_parameter_values values = defaults(tower);
            ASSERT_TRUE(values.set_enum("profile", profile));
            ASSERT_TRUE(values.set_float("height", 7.0f));
            ASSERT_TRUE(values.set_int("slit_rows", rows));
            reports[static_cast<std::size_t>(rows)] = editor::check_kit_shape(values);
            EXPECT_TRUE(reports[static_cast<std::size_t>(rows)].is_valid()) << describe(reports[static_cast<std::size_t>(rows)]);
        }
        EXPECT_GT(reports[1].triangle_count, reports[0].triangle_count) << profile;
        EXPECT_GT(reports[2].triangle_count, reports[1].triangle_count) << profile;
        EXPECT_EQ(reports[1].component_count, reports[0].component_count) << profile;
        EXPECT_EQ(reports[2].component_count, reports[0].component_count) << profile;
    }
    // The default tower has its one row.
    Kit_parameter_values with_row = defaults(tower);
    Kit_parameter_values without  = defaults(tower);
    ASSERT_TRUE(without.set_int("slit_rows", 0));
    EXPECT_GT(editor::check_kit_shape(with_row).triangle_count, editor::check_kit_shape(without).triangle_count);
}

} // anonymous namespace
