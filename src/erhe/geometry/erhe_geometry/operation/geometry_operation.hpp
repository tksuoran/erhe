#pragma once

#include "erhe_geometry/geometry.hpp"
#include <geogram/mesh/mesh.h>

#include <cctype>
#include <utility>
#include <vector>
#include <unordered_map>

struct pair_hash {
    template <typename T1, typename T2>
    auto operator() (const std::pair<T1, T2>& p) const -> std::size_t {
        auto hash1 = std::hash<T1>{}(p.first);
        auto hash2 = std::hash<T2>{}(p.second);
        return hash1 ^ (hash2 << 1);
    }
};

namespace erhe::geometry::operation {

class Geometry_operation
{
public:
    Geometry_operation(const erhe::geometry::Geometry& source, erhe::geometry::Geometry& destination)
        : source          {source}
        , lhs             {source}
        , rhs             {nullptr}
        , destination     {destination}
        , source_mesh     {source.get_mesh()}
        , lhs_mesh        {source.get_mesh()}
        , rhs_mesh        {nullptr}
        , destination_mesh{destination.get_mesh()}
    {
    }

    Geometry_operation(const erhe::geometry::Geometry& lhs, const erhe::geometry::Geometry& rhs, erhe::geometry::Geometry& destination)
        : source          {lhs}
        , lhs             {lhs}
        , rhs             {&rhs}
        , destination     {destination}
        , source_mesh     {source.get_mesh()}
        , lhs_mesh        {source.get_mesh()}
        , rhs_mesh        {&rhs.get_mesh()}
        , destination_mesh{destination.get_mesh()}
    {
    }

protected:
    void post_processing();

    const erhe::geometry::Geometry&                          source;
    const erhe::geometry::Geometry&                          lhs;
    const erhe::geometry::Geometry*                          rhs;
    erhe::geometry::Geometry&                                destination;

    const GEO::Mesh&                                         source_mesh;
    const GEO::Mesh&                                         lhs_mesh;
    const GEO::Mesh*                                         rhs_mesh;
    GEO::Mesh&                                               destination_mesh;

    std::vector<GEO::index_t>                                m_vertex_src_to_dst;
    std::vector<GEO::index_t>                                m_facet_src_to_dst;
    //  std::vector<GEO::index_t>                                m_corner_src_to_dst;
    std::vector<GEO::index_t>                                m_edge_src_to_dst;
    std::vector<GEO::index_t>                                m_src_facet_centroid_to_dst_vertex;
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_dst_vertex_sources;
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_dst_vertex_corner_sources;
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_dst_corner_sources;
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_dst_facet_sources;
    std::vector<std::vector<std::pair<float, GEO::index_t>>> m_dst_edge_sources;

    std::unordered_map<
        std::pair<GEO::index_t, GEO::index_t>,
        std::vector<GEO::index_t>,
        pair_hash
    > m_src_edge_to_dst_vertex;

    void make_dst_vertices_from_src_vertices();
    void make_facet_centroids               ();
    void build_edge_to_facets_map(
        const GEO::Mesh& mesh, 
        std::unordered_map<
            std::pair<GEO::index_t, GEO::index_t>,
            std::vector<GEO::index_t>,
            pair_hash
        >& edge_to_facets
    );

    //// [[nodiscard]] auto get_dst_vertex_from_src_edge(GEO::index_t src_edge) -> GEO::index_t;

    void make_edge_midpoints(std::initializer_list<float> relative_positions);

    [[nodiscard]] auto get_src_edge_new_vertex(GEO::index_t src_vertex_a, GEO::index_t src_vertex_b, GEO::index_t vertex_split_position) const -> GEO::index_t;

    // Creates a new vertex to Destination from given src_vertex in Source and
    // registers src_vertex as source for the new vertex with specified weight.
    //
    // weight      Weight for old vertex as source.
    // src_vertex  Vertex in source used as source for the new vertex.
    // returns     The new vertex in destination.
    auto make_new_dst_vertex_from_src_vertex(float vertex_weight, GEO::index_t src_vertex) -> GEO::index_t;

    // Creates a new vertex to Destination from given src_vertex in Source and
    // registers src_vertex as source for the new vertex with weight 1.0.
    //
    // src_vertex  Vertex in source used as source for the new vertex.
    // returns     The new vertex in destination.
    auto make_new_dst_vertex_from_src_vertex(GEO::index_t src_vertex) -> GEO::index_t;

    // Creates a new vertex to Destination from centroid of given src_vertex in Source and
    // registers each corner of the src_facet as source for the new vertex with weight 1.0.
    auto make_new_dst_vertex_from_src_facet_centroid(GEO::index_t src_facet) -> GEO::index_t;

    void add_facet_centroid(GEO::index_t dst_vertex, float facet_weight, GEO::index_t src_facet);

    void add_vertex_ring(GEO::index_t dst_vertex, float vertex_weight, GEO::index_t src_vertex);

    auto make_new_dst_facet_from_src_facet(GEO::index_t src_facet, GEO::index_t corner_count) -> GEO::index_t;

    auto make_new_dst_corner_from_src_facet_centroid(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t src_facet) -> GEO::index_t;

    auto make_new_dst_corner_from_dst_vertex(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t dst_point) -> GEO::index_t;

    auto make_new_dst_corner_from_src_corner(GEO::index_t dst_facet, GEO::index_t dst_local_facet_corner, GEO::index_t src_corner) -> GEO::index_t;

    void add_facet_corners(GEO::index_t dst_facet, GEO::index_t src_facet);

    void add_vertex_source(GEO::index_t dst_vertex, float point_weight, GEO::index_t src_vertex);

    void add_vertex_corner_source(GEO::index_t dst_vertex, float corner_weight, GEO::index_t src_corner);

    void add_corner_source(GEO::index_t dst_corner, float corner_weight, GEO::index_t src_corner);

    // Inherit vertex sources to corner
    void distribute_corner_sources(GEO::index_t dst_corner, float point_weight, GEO::index_t dst_vertex);

    void add_facet_source(GEO::index_t dst_facet, float polygon_weight, GEO::index_t src_facet);

    void add_edge_source(GEO::index_t dst_edge, float edge_weight, GEO::index_t src_edge);

    void interpolate_mesh_attributes();

    void copy_mesh_attributes();

protected:
    [[nodiscard]] static auto get_size_to_include(std::size_t old_size, std::size_t i) -> size_t;
};

} // namespace namespace geometry
