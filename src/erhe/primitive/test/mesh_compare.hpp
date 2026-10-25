#pragma once

// Bit-exact GEO::Mesh comparison shared by the soup-to-mesh tests
// (test_stored_colocation.cpp, test_prepared_primitive.cpp).

#include "erhe_geometry/geometry.hpp"

#include <gtest/gtest.h>

#include <geogram/basic/attributes.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace erhe_primitive_test {

[[nodiscard]] inline auto hex(const float value) -> std::string
{
    return std::format("{:a}", value);
}

inline void compare_attribute_stores(const GEO::AttributesManager& a, const GEO::AttributesManager& b, const char* const domain)
{
    GEO::vector<std::string> names_a;
    GEO::vector<std::string> names_b;
    a.list_attribute_names(names_a);
    b.list_attribute_names(names_b);
    std::sort(names_a.begin(), names_a.end());
    std::sort(names_b.begin(), names_b.end());
    // Mesh_attributes binds its stores in every domain, so an empty list
    // would mean nothing was compared.
    ASSERT_FALSE(names_a.empty()) << domain;
    ASSERT_EQ(std::vector<std::string>(names_a.begin(), names_a.end()), std::vector<std::string>(names_b.begin(), names_b.end())) << domain;
    for (const std::string& name : names_a) {
        const GEO::AttributeStore* const store_a = a.find_attribute_store(name);
        const GEO::AttributeStore* const store_b = b.find_attribute_store(name);
        ASSERT_NE(store_a, nullptr) << domain << "." << name;
        ASSERT_NE(store_b, nullptr) << domain << "." << name;
        ASSERT_EQ(store_a->size(),         store_b->size())         << domain << "." << name;
        ASSERT_EQ(store_a->dimension(),    store_b->dimension())    << domain << "." << name;
        ASSERT_EQ(store_a->element_size(), store_b->element_size()) << domain << "." << name;
        const std::size_t byte_count = static_cast<std::size_t>(store_a->size()) * store_a->dimension() * store_a->element_size();
        EXPECT_EQ(std::memcmp(store_a->data(), store_b->data(), byte_count), 0) << domain << "." << name << " differs";
    }
}

inline void compare_meshes(const GEO::Mesh& a, const GEO::Mesh& b)
{
    ASSERT_EQ(a.vertices.nb(),      b.vertices.nb());
    ASSERT_EQ(a.facets.nb(),        b.facets.nb());
    ASSERT_EQ(a.facet_corners.nb(), b.facet_corners.nb());
    ASSERT_EQ(a.edges.nb(),         b.edges.nb());
    for (GEO::index_t vertex = 0; vertex < a.vertices.nb(); ++vertex) {
        const GEO::vec3f pa = erhe::geometry::get_pointf(a.vertices, vertex);
        const GEO::vec3f pb = erhe::geometry::get_pointf(b.vertices, vertex);
        for (GEO::index_t i = 0; i < 3; ++i) {
            EXPECT_EQ(hex(pa[i]), hex(pb[i])) << "point " << vertex << " coordinate " << i;
        }
    }
    for (GEO::index_t facet = 0; facet < a.facets.nb(); ++facet) {
        ASSERT_EQ(a.facets.nb_vertices(facet), b.facets.nb_vertices(facet));
        ASSERT_EQ(a.facets.corners_begin(facet), b.facets.corners_begin(facet));
    }
    for (GEO::index_t corner = 0; corner < a.facet_corners.nb(); ++corner) {
        EXPECT_EQ(a.facet_corners.vertex(corner),         b.facet_corners.vertex(corner))         << "corner " << corner;
        EXPECT_EQ(a.facet_corners.adjacent_facet(corner), b.facet_corners.adjacent_facet(corner)) << "corner " << corner;
    }
    for (GEO::index_t edge = 0; edge < a.edges.nb(); ++edge) {
        EXPECT_EQ(a.edges.vertex(edge, 0), b.edges.vertex(edge, 0)) << "edge " << edge;
        EXPECT_EQ(a.edges.vertex(edge, 1), b.edges.vertex(edge, 1)) << "edge " << edge;
    }

    compare_attribute_stores(a.vertices.attributes(),      b.vertices.attributes(),      "vertices");
    compare_attribute_stores(a.facets.attributes(),        b.facets.attributes(),        "facets");
    compare_attribute_stores(a.facet_corners.attributes(), b.facet_corners.attributes(), "facet_corners");
    compare_attribute_stores(a.edges.attributes(),         b.edges.attributes(),         "edges");
}

inline void process_like_gltf_import(erhe::geometry::Geometry& geometry)
{
    // The flags finalize_imported_meshes processes a converted soup with.
    geometry.process({.flags =
        erhe::geometry::Geometry::process_flag_connect                       |
        erhe::geometry::Geometry::process_flag_build_edges                   |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals
    });
}

} // namespace erhe_primitive_test
