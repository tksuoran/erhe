// Bit-exact Geometry <-> Geometry_flat_data round-trip
// (doc/gltf-scene-roundtrip-plan.md phase 2). The flat form is what the
// glTF exporter maps onto accessors + the ERHE_geometry extension; the
// round-trip must preserve every geogram attribute byte-exact (attribute
// bytes are compared with memcmp, satisfying the hexfloat discipline).

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_serialization.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_geometry/shapes/torus.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

using erhe::geometry::Geometry;
using erhe::geometry::Geometry_attribute_record;
using erhe::geometry::Geometry_flat_data;

constexpr uint64_t k_process_flags =
    Geometry::process_flag_connect                       |
    Geometry::process_flag_build_edges                   |
    Geometry::process_flag_compute_facet_centroids       |
    Geometry::process_flag_compute_smooth_vertex_normals |
    Geometry::process_flag_generate_facet_texture_coordinates;

[[nodiscard]] auto make_processed_torus() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geometry = std::make_unique<Geometry>("torus");
    erhe::geometry::shapes::make_torus(geometry->get_mesh(), 1.0f, 0.25f, 12, 8);
    geometry->process({.flags = k_process_flags});
    return geometry;
}

[[nodiscard]] auto find_record(
    const Geometry_flat_data& data,
    const std::string&        element,
    const std::string&        name
) -> const Geometry_attribute_record*
{
    for (const Geometry_attribute_record& record : data.attributes) {
        if ((record.element == element) && (record.name == name)) {
            return &record;
        }
    }
    return nullptr;
}

void expect_flat_data_equal(const Geometry_flat_data& lhs, const Geometry_flat_data& rhs)
{
    EXPECT_EQ(lhs.vertex_count, rhs.vertex_count);
    EXPECT_EQ(lhs.facet_count,  rhs.facet_count);
    EXPECT_EQ(lhs.corner_count, rhs.corner_count);
    EXPECT_EQ(lhs.edge_count,   rhs.edge_count);

    ASSERT_EQ(lhs.positions.size(), rhs.positions.size());
    EXPECT_EQ(0, std::memcmp(lhs.positions.data(), rhs.positions.data(), lhs.positions.size() * sizeof(float)));

    EXPECT_EQ(lhs.triangle_indices,     rhs.triangle_indices);
    EXPECT_EQ(lhs.facet_vertex_counts,  rhs.facet_vertex_counts);
    EXPECT_EQ(lhs.facet_vertex_indices, rhs.facet_vertex_indices);
    EXPECT_EQ(lhs.edge_vertex_indices,  rhs.edge_vertex_indices);

    ASSERT_EQ(lhs.attributes.size(), rhs.attributes.size());
    for (const Geometry_attribute_record& lhs_record : lhs.attributes) {
        const Geometry_attribute_record* rhs_record = find_record(rhs, lhs_record.element, lhs_record.name);
        ASSERT_NE(rhs_record, nullptr) << "missing attribute " << lhs_record.element << "/" << lhs_record.name;
        EXPECT_EQ(lhs_record.element_type, rhs_record->element_type) << lhs_record.element << "/" << lhs_record.name;
        EXPECT_EQ(lhs_record.element_size, rhs_record->element_size) << lhs_record.element << "/" << lhs_record.name;
        EXPECT_EQ(lhs_record.dimension,    rhs_record->dimension)    << lhs_record.element << "/" << lhs_record.name;
        EXPECT_EQ(lhs_record.item_count,   rhs_record->item_count)   << lhs_record.element << "/" << lhs_record.name;
        ASSERT_EQ(lhs_record.bytes.size(), rhs_record->bytes.size()) << lhs_record.element << "/" << lhs_record.name;
        EXPECT_EQ(0, std::memcmp(lhs_record.bytes.data(), rhs_record->bytes.data(), lhs_record.bytes.size()))
            << "attribute bytes differ: " << lhs_record.element << "/" << lhs_record.name;
    }
}

} // anonymous namespace

TEST(GeometrySerialization, TorusRoundTripIsBitExact)
{
    std::unique_ptr<Geometry> torus = make_processed_torus();
    const Geometry_flat_data flat = erhe::geometry::geometry_to_flat_data(*torus);

    // The processed torus must exercise all element kinds.
    EXPECT_GT(flat.vertex_count, 0u);
    EXPECT_GT(flat.facet_count,  0u);
    EXPECT_GT(flat.corner_count, 0u);
    EXPECT_GT(flat.edge_count,   0u);
    EXPECT_EQ(flat.facet_vertex_counts.size(),  flat.facet_count);
    EXPECT_EQ(flat.facet_vertex_indices.size(), flat.corner_count);
    // Quads fan into two triangles each.
    EXPECT_EQ(flat.triangle_indices.size(), flat.facet_count * 2 * 3);
    // process() populated attributes on multiple elements (the store names
    // are the per-element-manager descriptor names, see geometry.hpp c_*).
    EXPECT_NE(find_record(flat, "vertex", "normal_smooth"), nullptr);
    EXPECT_NE(find_record(flat, "facet",  "centroid"),      nullptr);
    EXPECT_NE(find_record(flat, "corner", "texcoord_0"),    nullptr);

    Geometry restored{"torus_restored"};
    ASSERT_TRUE(erhe::geometry::geometry_from_flat_data(flat, restored));

    const GEO::Mesh& mesh = restored.get_mesh();
    EXPECT_EQ(mesh.vertices.nb(),      flat.vertex_count);
    EXPECT_EQ(mesh.facets.nb(),        flat.facet_count);
    EXPECT_EQ(mesh.facet_corners.nb(), flat.corner_count);
    EXPECT_EQ(mesh.edges.nb(),         flat.edge_count);

    const Geometry_flat_data flat_again = erhe::geometry::geometry_to_flat_data(restored);
    expect_flat_data_equal(flat, flat_again);
}

TEST(GeometrySerialization, CubeRoundTripIsBitExact)
{
    std::unique_ptr<Geometry> cube = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_cube(cube->get_mesh(), 1.0f);
    cube->process({.flags = k_process_flags});

    const Geometry_flat_data flat = erhe::geometry::geometry_to_flat_data(*cube);
    Geometry restored{"cube_restored"};
    ASSERT_TRUE(erhe::geometry::geometry_from_flat_data(flat, restored));
    const Geometry_flat_data flat_again = erhe::geometry::geometry_to_flat_data(restored);
    expect_flat_data_equal(flat, flat_again);
}

TEST(GeometrySerialization, InconsistentDataIsRejected)
{
    std::unique_ptr<Geometry> torus = make_processed_torus();
    Geometry_flat_data flat = erhe::geometry::geometry_to_flat_data(*torus);

    {
        Geometry_flat_data broken = flat;
        broken.facet_vertex_counts.pop_back(); // facet count mismatch
        Geometry restored{"broken"};
        EXPECT_FALSE(erhe::geometry::geometry_from_flat_data(broken, restored));
    }
    {
        Geometry_flat_data broken = flat;
        broken.facet_vertex_indices[0] = static_cast<uint32_t>(broken.vertex_count); // out of range
        Geometry restored{"broken"};
        EXPECT_FALSE(erhe::geometry::geometry_from_flat_data(broken, restored));
    }
    {
        Geometry_flat_data broken = flat;
        if (!broken.attributes.empty()) {
            broken.attributes[0].bytes.pop_back(); // byte size mismatch
            Geometry restored{"broken"};
            EXPECT_FALSE(erhe::geometry::geometry_from_flat_data(broken, restored));
        }
    }
}
