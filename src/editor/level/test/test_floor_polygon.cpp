#include "level/floor_polygon.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"

#include <geogram/mesh/mesh.h>
#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

auto l_shape() -> std::vector<glm::vec2>
{
    // Counter-clockwise seen from above, as make_floor_rectangle().
    return {{0.0f, 0.0f}, {0.0f, 4.0f}, {2.0f, 4.0f}, {2.0f, 2.0f}, {4.0f, 2.0f}, {4.0f, 0.0f}};
}

} // anonymous namespace

TEST(FloorPolygon, RectangleConnectorsSitInsetOnEachEdge)
{
    std::vector<glm::vec2> outline;
    editor::make_floor_rectangle(4.0f, 6.0f, outline);
    ASSERT_TRUE(editor::check_floor_outline(outline).empty());
    const editor::Floor_values values{};
    std::vector<editor::Kit_connector> connectors;
    editor::get_floor_connectors(outline, values, connectors);
    ASSERT_EQ(connectors.size(), 8u); // four wall-floor edge connectors, then four floor connectors
    // Edge 0 runs along x = -2: its connector is 0.125 m inside, at the top, its up axis toward +x.
    EXPECT_NEAR(connectors[0].position.x, -2.0f + 0.125f, 1.0e-6f);
    EXPECT_NEAR(connectors[0].position.y, values.thickness, 1.0e-6f);
    EXPECT_NEAR(connectors[0].position.z, 0.0f, 1.0e-6f);
    EXPECT_NEAR(connectors[0].up.x, 1.0f, 1.0e-6f);
    EXPECT_NEAR(connectors[0].outward.y, 1.0f, 1.0e-6f);
    EXPECT_TRUE(connectors[0].types.contains(editor::Kit_connector_type::wall_floor));
    EXPECT_STREQ(connectors[3].name, "edge_3");
    // The floor connectors: on the edge's midpoint, at the top, facing out.
    EXPECT_STREQ(connectors[4].name, "floor_0");
    EXPECT_NEAR(connectors[4].position.x, -2.0f, 1.0e-6f);
    EXPECT_NEAR(connectors[4].outward.x, -1.0f, 1.0e-6f);
    EXPECT_TRUE(connectors[4].types.contains(editor::Kit_connector_type::floor));
    connectors.resize(4);
    for (const editor::Kit_connector& connector : connectors) {
        // Every wall-floor connector is half a wall thickness inside the rectangle's edge.
        const float inside = std::min(2.0f - std::abs(connector.position.x), 3.0f - std::abs(connector.position.z));
        EXPECT_NEAR(inside, 0.125f, 1.0e-6f) << connector.name;
    }
    EXPECT_NEAR(editor::get_floor_edge_length(outline, 0), 6.0f, 1.0e-6f);
    EXPECT_NEAR(editor::get_floor_edge_length(outline, 1), 4.0f, 1.0e-6f);
}

TEST(FloorPolygon, ReversedWindingKeepsConnectorsInside)
{
    std::vector<glm::vec2> outline = l_shape();
    std::vector<glm::vec2> reversed(outline.rbegin(), outline.rend());
    std::vector<editor::Kit_connector> connectors;
    editor::get_floor_connectors(reversed, editor::Floor_values{}, connectors);
    for (std::size_t i = 0; i < reversed.size(); ++i) {
        const glm::vec2 inward = editor::get_floor_edge_inward(reversed, i);
        const glm::vec2 mid    = 0.5f * (reversed[i] + reversed[(i + 1) % reversed.size()]);
        const glm::vec2 probe  = mid + (0.01f * inward);
        // The probe point just inside an edge of the L lies inside the L.
        const bool in_l = (probe.x > 0.0f) && (probe.y > 0.0f) && (probe.x < 4.0f) && (probe.y < 4.0f) && !((probe.x > 2.0f) && (probe.y > 2.0f));
        EXPECT_TRUE(in_l) << i;
    }
}

TEST(FloorPolygon, OutlineChecksRefuse)
{
    EXPECT_FALSE(editor::check_floor_outline(std::vector<glm::vec2>{{0.0f, 0.0f}, {1.0f, 0.0f}}).empty());
    const std::vector<glm::vec2> bow_tie{{0.0f, 0.0f}, {2.0f, 2.0f}, {2.0f, 0.0f}, {0.0f, 2.0f}};
    EXPECT_NE(editor::check_floor_outline(bow_tie).find("cross"), std::string::npos);
    const std::vector<glm::vec2> short_edge{{0.0f, 0.0f}, {0.0f, 2.0f}, {0.001f, 2.0f}, {2.0f, 0.0f}};
    EXPECT_NE(editor::check_floor_outline(short_edge).find("shorter"), std::string::npos);
    const std::vector<glm::vec2> l = l_shape();
    std::vector<glm::vec2> reversed(l.rbegin(), l.rend());
    EXPECT_TRUE(editor::check_floor_outline(l).empty());
    EXPECT_NE(editor::check_floor_outline_change(l, reversed).find("inside out"), std::string::npos);
}

TEST(FloorPolygon, ConcaveMeshIsClosedAndCovers)
{
    const std::vector<glm::vec2> l = l_shape();
    GEO::Mesh mesh{3, true};
    ASSERT_TRUE(editor::make_floor_mesh(l, editor::Floor_values{}, mesh).empty());
    // Two caps of four triangles each (six corners) and six side quads.
    EXPECT_EQ(mesh.facets.nb(), 4u + 4u + 6u);
    EXPECT_EQ(mesh.vertices.nb(), 12u);
    // Upward facet areas sum to the L's area (12 square meters).
    double up_area = 0.0;
    for (GEO::index_t f = 0; f < mesh.facets.nb(); ++f) {
        if (mesh.facets.nb_vertices(f) != 3) {
            continue;
        }
        const auto corner = [&](const GEO::index_t k) -> glm::vec3 {
            const float* const p = mesh.vertices.single_precision_point_ptr(mesh.facets.vertex(f, k));
            return glm::vec3{p[0], p[1], p[2]};
        };
        const glm::vec3 n = glm::cross(corner(1) - corner(0), corner(2) - corner(0));
        if (n.y > 0.0f) {
            up_area += 0.5 * static_cast<double>(n.y);
        }
    }
    EXPECT_NEAR(up_area, 12.0, 1.0e-4);
}

// C7.1: every mapping names a parameter (length) or a slot (material) of its shape.
TEST(FloorPolygon, EveryPropertyMappingNamesItsTarget)
{
    static const editor::Kit_shape_library library;
    std::size_t mapped_shapes = 0;
    for (const editor::Kit_shape_definition& shape : library.get_shapes()) {
        if (!shape.property_mappings.empty()) {
            ++mapped_shapes;
        }
        for (const editor::Kit_property_mapping& mapping : shape.property_mappings) {
            if (editor::get_kind(mapping.property) == editor::Kit_property_kind::material) {
                EXPECT_NE(std::find(shape.material_slots.begin(), shape.material_slots.end(), mapping.target), shape.material_slots.end())
                    << shape.id << " " << mapping.target;
            } else {
                const std::optional<std::size_t> index = shape.find_parameter(mapping.target);
                ASSERT_TRUE(index.has_value()) << shape.id << " " << mapping.target;
                EXPECT_EQ(shape.parameters[index.value()].type, editor::Kit_parameter_type::float_value) << shape.id << " " << mapping.target;
            }
        }
    }
    EXPECT_EQ(mapped_shapes, 4u); // the wall run shapes (K3.3)
}

TEST(FloorPolygon, WallSuppliesOverItsEndsOnly)
{
    static const editor::Kit_shape_library library;
    const editor::Kit_shape_definition* const wall = library.find("architecture.wall");
    ASSERT_NE(wall, nullptr);
    const editor::Kit_parameter_values values = editor::make_default_values(*wall, 1.0f, 3.0f);
    const std::array<std::string_view, 1> materials{"Kit Plaster"};
    const editor::Kit_piece_view view{.values = &values, .slot_materials = materials};
    std::vector<editor::Kit_connector> connectors;
    editor::get_shape_connectors(values, connectors);
    editor::Kit_supply_list supply;
    for (std::size_t i = 0; i < connectors.size(); ++i) {
        wall->get_supply(view, connectors[i].name, supply);
        const std::string_view name{connectors[i].name};
        if ((name == "start") || (name == "end")) {
            ASSERT_EQ(supply.get().size(), 3u) << name;
            EXPECT_NEAR(supply.find(editor::Kit_standard_property::wall_thickness)->length, 0.25f, 1.0e-6f);
            EXPECT_NEAR(supply.find(editor::Kit_standard_property::room_height)->length, 3.0f, 1.0e-6f);
            EXPECT_EQ(supply.find(editor::Kit_standard_property::wall_material)->material, "Kit Plaster");
            EXPECT_EQ(supply.find(editor::Kit_standard_property::wall_material)->priority, editor::c_wall_supply_priority);
            EXPECT_EQ(supply.find(editor::Kit_standard_property::child_length), nullptr);
        } else {
            EXPECT_TRUE(supply.get().empty()) << name;
        }
    }
}

// C7.1, L9: walls standing on each edge connector at the lengths the floor supplies meet at their end connectors.
TEST(FloorPolygon, WallsAtSuppliedLengthsMeetAtTheCorners)
{
    {
        const std::vector<glm::vec2> outline{{-2.0f, -3.0f}, {-2.0f, 3.0f}, {2.0f, 3.0f}, {2.0f, -3.0f}};
        const editor::Floor_values values{};
        std::vector<editor::Kit_connector> connectors;
        editor::get_floor_connectors(outline, values, connectors);
        editor::Kit_supply_list supply;
        const std::size_t n = outline.size();
        std::vector<std::pair<glm::vec2, glm::vec2>> ends; // per edge: the wall's end toward corner i and toward corner i + 1
        for (std::size_t i = 0; i < n; ++i) {
            editor::get_floor_supply(outline, values, "Kit Plaster", i, supply);
            const float length = supply.find(editor::Kit_standard_property::child_length)->length;
            EXPECT_EQ(supply.find(editor::Kit_standard_property::child_length)->priority, editor::c_floor_supply_priority);
            const glm::vec2 center{connectors[i].position.x, connectors[i].position.z};
            const glm::vec2 along = glm::normalize(outline[(i + 1) % n] - outline[i]);
            ends.emplace_back(center - (0.5f * length * along), center + (0.5f * length * along));
        }
        for (std::size_t i = 0; i < n; ++i) {
            EXPECT_NEAR(glm::length(ends[i].second - ends[(i + 1) % n].first), 0.0f, 1.0e-5f) << i;
        }
    }
}
