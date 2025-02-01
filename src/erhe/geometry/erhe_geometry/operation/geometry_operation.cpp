#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

namespace erhe::geometry::operation {

auto Geometry_operation::get_size_to_include(std::size_t size, std::size_t i) -> size_t
{
    if (size == 0) {
        size = 20;
    }
    for (;;) {
        if (i < size) {
            return size;
        }
        size += size / 2 + size / 4;
    }
}

void Geometry_operation::make_dst_vertices_from_src_vertices()
{
    GEO::index_t src_vertex_count = source_mesh.vertices.nb();
    m_vertex_src_to_dst.reserve(src_vertex_count);

    for (GEO::index_t src_vertex = 0; src_vertex < src_vertex_count; ++src_vertex) {
        make_new_dst_vertex_from_src_vertex(src_vertex);
    }
}

void Geometry_operation::make_facet_centroids()
{
    GEO::index_t src_facet_count = source_mesh.facets.nb();
    m_src_facet_centroid_to_dst_vertex.reserve(src_facet_count);
    for (GEO::index_t src_facet : source_mesh.facets) {
        make_new_dst_vertex_from_src_facet_centroid(src_facet);
    }
}

void Geometry_operation::build_edge_to_facets_map(
    const GEO::Mesh& mesh, 
    std::unordered_map<
        std::pair<GEO::index_t, GEO::index_t>,
        std::vector<GEO::index_t>,
        pair_hash
    >& edge_to_facets
)
{
    for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
        for (GEO::index_t corner = mesh.facets.corners_begin(facet), end = mesh.facets.corners_end(facet); corner < end; ++corner) {
            GEO::index_t next_corner = mesh.facets.next_corner_around_facet(facet, corner);
            GEO::index_t v1 = mesh.facet_corners.vertex(corner);
            GEO::index_t v2 = mesh.facet_corners.vertex(next_corner);
            if (v1 > v2) {
                std::swap(v1, v2);
            }
            edge_to_facets[std::make_pair(v1, v2)].push_back(facet);
        }
    }
}

void Geometry_operation::make_edge_midpoints(const std::initializer_list<float> relative_positions)
{
    geo_assert(relative_positions.size() != 0);
    GEO::MeshVertices& dst_vertices = destination_mesh.vertices;
    const GEO::index_t split_count  = static_cast<GEO::index_t>(relative_positions.size());
    ERHE_VERIFY(m_src_edge_to_dst_vertex.empty());
    const GEO::index_t new_dst_vertex_count = split_count * source_mesh.edges.nb();
    const GEO::index_t new_dst_vertex_start = dst_vertices.create_vertices(new_dst_vertex_count);
    const GEO::index_t new_dst_vertex_end   = new_dst_vertex_start + new_dst_vertex_count;
    GEO::index_t new_dst_vertex = new_dst_vertex_start;
    for (GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>&            src_edge_facets = source.get_edge_facets(src_edge);
        const GEO::index_t                          src_vertex_a    = source_mesh.edges.vertex(src_edge, 0);
        const GEO::index_t                          src_vertex_b    = source_mesh.edges.vertex(src_edge, 1);
        const std::pair<GEO::index_t, GEO::index_t> src_edge_key    = std::make_pair(src_vertex_a, src_vertex_b);
        ERHE_VERIFY(src_vertex_a < src_vertex_b);
        GEO::index_t split_slot = 0;
        for (auto t : relative_positions) {
            const float weight_a = t;
            const float weight_b = 1.0f - t;
            log_operation->trace(
                "creating edge midpoint: src edge {} slot {}: w0 = {}, v0 = {}, w1 = {} v1 = {}, new dst vertex = {}",
                src_edge, split_slot, weight_a, src_vertex_a, weight_b, src_vertex_b, new_dst_vertex
            );
            m_src_edge_to_dst_vertex[src_edge_key].push_back(new_dst_vertex);
            for (const GEO::index_t src_facet : src_edge_facets) {
                const GEO::index_t local_src_corner_a = source_mesh.facets.find_vertex(src_facet, src_vertex_a);
                const GEO::index_t local_src_corner_b = source_mesh.facets.find_vertex(src_facet, src_vertex_b);
                const GEO::index_t src_corner_a       = source_mesh.facets.corner(src_facet, local_src_corner_a);
                const GEO::index_t src_corner_b       = source_mesh.facets.corner(src_facet, local_src_corner_b);
                add_vertex_source       (new_dst_vertex, weight_a, src_vertex_a);
                add_vertex_source       (new_dst_vertex, weight_b, src_vertex_b);
                add_vertex_corner_source(new_dst_vertex, weight_a, src_corner_a);
                add_vertex_corner_source(new_dst_vertex, weight_b, src_corner_b);
            }
            ++new_dst_vertex;
            ++split_slot;
        }
    }
    ERHE_VERIFY(new_dst_vertex == new_dst_vertex_end);
}

auto Geometry_operation::get_src_edge_new_vertex(GEO::index_t src_vertex_a, GEO::index_t src_vertex_b, GEO::index_t vertex_split_position) const -> GEO::index_t
{
    const bool swapped = src_vertex_a > src_vertex_b;
    if (swapped) {
        std::swap(src_vertex_a, src_vertex_b);
    }

    const std::pair<GEO::index_t, GEO::index_t> src_edge_key = std::make_pair(src_vertex_a, src_vertex_b);
    const auto i = m_src_edge_to_dst_vertex.find(src_edge_key);
    ERHE_VERIFY(i != m_src_edge_to_dst_vertex.end());
    const std::vector<GEO::index_t>& dst_vertices = i->second;
    ERHE_VERIFY(vertex_split_position < dst_vertices.size());
    return dst_vertices[!swapped ? dst_vertices.size() - vertex_split_position - 1 : vertex_split_position];
}

auto Geometry_operation::make_new_dst_vertex_from_src_vertex(const float vertex_weight, const GEO::index_t src_vertex) -> GEO::index_t
{
    const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(1);

    add_vertex_source(new_dst_vertex, vertex_weight, src_vertex);
    const std::size_t i = static_cast<std::size_t>(src_vertex);
    const std::size_t old_size = m_vertex_src_to_dst.size();
    if (old_size <= i) {
        m_vertex_src_to_dst.resize(get_size_to_include(old_size, i));
    }
    m_vertex_src_to_dst[i] = new_dst_vertex;
    return new_dst_vertex;
}

auto Geometry_operation::make_new_dst_vertex_from_src_vertex(const GEO::index_t src_vertex) -> GEO::index_t
{
    const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(1);

    add_vertex_source(new_dst_vertex, 1.0f, src_vertex);
    const std::size_t i = static_cast<std::size_t>(src_vertex);
    const std::size_t old_size = m_vertex_src_to_dst.size();
    if (old_size <= i) {
        m_vertex_src_to_dst.resize(get_size_to_include(old_size, i));
    }
    m_vertex_src_to_dst[i] = new_dst_vertex;
    return new_dst_vertex;
}

auto Geometry_operation::make_new_dst_vertex_from_src_facet_centroid(const GEO::index_t src_facet) -> GEO::index_t
{
    const GEO::index_t new_dst_vertex = destination_mesh.vertices.create_vertices(1);
    const std::size_t i = static_cast<size_t>(src_facet);
    const std::size_t old_size = m_src_facet_centroid_to_dst_vertex.size();
    if (old_size <= i) {
        m_src_facet_centroid_to_dst_vertex.resize(get_size_to_include(old_size, i));
    }
    m_src_facet_centroid_to_dst_vertex[i] = new_dst_vertex;
    add_facet_centroid(new_dst_vertex, 1.0f, src_facet);
    return new_dst_vertex;
}

void Geometry_operation::add_facet_centroid(const GEO::index_t dst_vertex, const float facet_weight, const GEO::index_t src_facet)
{
    log_operation->trace(
        "add_facet_centroid(dst_vertex = {}, facet_weight = {}, src_facet = {})",
        dst_vertex, facet_weight, src_facet
    );
    for (GEO::index_t src_corner : source_mesh.facets.corners(src_facet)) {
        const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(src_corner);
        add_vertex_corner_source(dst_vertex, facet_weight, src_corner);
        add_vertex_source(dst_vertex, facet_weight, src_vertex);
    }
}

void Geometry_operation::add_vertex_ring(const GEO::index_t dst_vertex, const float vertex_weight, const GEO::index_t src_vertex)
{
    for (GEO::index_t src_corner : source.get_vertex_corners(src_vertex)) {
        const GEO::index_t src_facet       = source.get_corner_facet(src_corner);
        const GEO::index_t next_src_corner = source_mesh.facets.next_corner_around_facet(src_facet, src_corner);
        const GEO::index_t next_src_vertex = source_mesh.facet_corners.vertex(next_src_corner);
        add_vertex_source(dst_vertex, vertex_weight, next_src_vertex);
    }
}

auto Geometry_operation::make_new_dst_facet_from_src_facet(const GEO::index_t src_facet, const GEO::index_t corner_count) -> GEO::index_t
{
    const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(corner_count);
    add_facet_source(new_dst_facet, 1.0f, src_facet);
    const std::size_t i = static_cast<std::size_t>(src_facet);
    const std::size_t old_size = m_facet_src_to_dst.size();
    if (old_size <= i) {
        m_facet_src_to_dst.resize(get_size_to_include(old_size, i));
    }
    m_facet_src_to_dst[i] = new_dst_facet;
    return new_dst_facet;
}

auto Geometry_operation::make_new_dst_corner_from_src_facet_centroid(
    const GEO::index_t dst_facet,
    const GEO::index_t dst_local_facet_corner,
    const GEO::index_t src_facet
) -> GEO::index_t
{
    geo_assert(m_src_facet_centroid_to_dst_vertex.size() >= source_mesh.facets.nb());
    const GEO::index_t dst_vertex = m_src_facet_centroid_to_dst_vertex[src_facet];
    destination_mesh.facets.set_vertex(dst_facet, dst_local_facet_corner, dst_vertex);
    const GEO::index_t new_dst_corner = destination_mesh.facets.corner(dst_facet, dst_local_facet_corner);
    distribute_corner_sources(new_dst_corner, 1.0f, dst_vertex);
    return new_dst_corner;
}

auto Geometry_operation::make_new_dst_corner_from_dst_vertex(
    const GEO::index_t dst_facet,
    const GEO::index_t dst_local_facet_corner,
    const GEO::index_t dst_vertex
) -> GEO::index_t
{
    destination_mesh.facets.set_vertex(dst_facet, dst_local_facet_corner, dst_vertex);
    const GEO::index_t dst_corner = destination_mesh.facets.corner(dst_facet, dst_local_facet_corner);
    distribute_corner_sources(dst_corner, 1.0f, dst_vertex);
    return dst_corner;
}

auto Geometry_operation::make_new_dst_corner_from_src_corner(
    const GEO::index_t dst_facet,
    const GEO::index_t dst_local_facet_corner,
    const GEO::index_t src_corner
) -> GEO::index_t
{
    const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(src_corner);
    const GEO::index_t dst_vertex = m_vertex_src_to_dst[src_vertex];
    destination_mesh.facets.set_vertex(dst_facet, dst_local_facet_corner, dst_vertex);
    const GEO::index_t new_dst_corner = destination_mesh.facets.corner(dst_facet, dst_local_facet_corner);
    add_corner_source(new_dst_corner, 1.0f, src_corner);
    return new_dst_corner;
}

void Geometry_operation::add_facet_corners(const GEO::index_t dst_facet, const GEO::index_t src_facet)
{
    const GEO::index_t local_facet_corner_count = source_mesh.facets.nb_vertices(src_facet);
    for (GEO::index_t local_facet_corner = 0; local_facet_corner < local_facet_corner_count; ++local_facet_corner) {
        const GEO::index_t src_corner = source_mesh.facets.corner(src_facet, local_facet_corner);
        const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(src_corner);
        const GEO::index_t dst_vertex = m_vertex_src_to_dst[src_vertex];
        destination_mesh.facets.set_vertex(dst_facet, local_facet_corner, dst_vertex);
        const GEO::index_t new_dst_corner = destination_mesh.facets.corner(dst_facet, local_facet_corner);
        add_corner_source(new_dst_corner, 1.0f, src_corner);
    }
}

void Geometry_operation::add_vertex_source(const GEO::index_t dst_vertex, const float vertex_weight, const GEO::index_t src_vertex)
{
    const std::size_t i = static_cast<std::size_t>(dst_vertex);
    const std::size_t old_size = m_dst_vertex_sources.size();
    if (old_size <= i) {
        m_dst_vertex_sources.resize(get_size_to_include(old_size, i));
    }
    m_dst_vertex_sources[i].emplace_back(vertex_weight, src_vertex);
}

void Geometry_operation::add_vertex_corner_source(const GEO::index_t dst_vertex, const float corner_weight, const GEO::index_t src_corner)
{
    log_operation->trace(
        "add_vertex_corner_source(dst_vertex= {}, weight = {}, src_corner = {})",
        dst_vertex, corner_weight, src_corner
    );

    const std::size_t i = static_cast<std::size_t>(dst_vertex);
    const std::size_t old_size = m_dst_vertex_corner_sources.size();
    if (old_size <= i) {
        m_dst_vertex_corner_sources.resize(get_size_to_include(old_size, i));
    }
    m_dst_vertex_corner_sources[dst_vertex].emplace_back(corner_weight, src_corner);
}

void Geometry_operation::add_corner_source(const GEO::index_t dst_corner, const float corner_weight, const GEO::index_t src_corner)
{
    const std::size_t i = static_cast<std::size_t>(dst_corner);
    const std::size_t old_size = m_dst_corner_sources.size();
    if (old_size <= i) {
        m_dst_corner_sources.resize(get_size_to_include(old_size, i));
    }
    m_dst_corner_sources[i].emplace_back(corner_weight, src_corner);
}

void Geometry_operation::distribute_corner_sources(const GEO::index_t dst_corner, const float vertex_weight, const GEO::index_t dst_vertex)
{
    const auto vertex_corner_sources = m_dst_vertex_corner_sources[dst_vertex];
    for (const auto& vertex_corner_source : vertex_corner_sources) {
        const float        corner_weight = vertex_weight * vertex_corner_source.first;
        const GEO::index_t corner        = vertex_corner_source.second;
        add_corner_source(dst_corner, corner_weight, corner);
    }
}

void Geometry_operation::add_facet_source(const GEO::index_t dst_facet, const float facet_weight, const GEO::index_t src_facet)
{
    const std::size_t i = static_cast<std::size_t>(dst_facet);
    const std::size_t old_size = m_dst_facet_sources.size();
    if (old_size <= i) {
        m_dst_facet_sources.resize(get_size_to_include(old_size, i));
    }
    m_dst_facet_sources[i].emplace_back(facet_weight, src_facet);
}

void Geometry_operation::add_edge_source(const GEO::index_t dst_edge, const float edge_weight, const GEO::index_t src_edge)
{
    const std::size_t i = static_cast<std::size_t>(dst_edge);
    const std::size_t old_size = m_dst_edge_sources.size();
    if (old_size) {
        m_dst_edge_sources.resize(get_size_to_include(old_size, i));
    }
    m_dst_edge_sources[i].emplace_back(edge_weight, src_edge);
}

void Geometry_operation::interpolate_mesh_attributes()
{
    m_dst_vertex_sources.resize(destination_mesh.vertices.nb());
    m_dst_facet_sources .resize(destination_mesh.facets.nb());
    m_dst_corner_sources.resize(destination_mesh.facet_corners.nb());
    m_dst_edge_sources  .resize(destination_mesh.edges.nb());

    const Mesh_attributes& s = source.get_attributes();
    Mesh_attributes&       d = destination.get_attributes();

    for (GEO::index_t vertex : destination_mesh.vertices) {
        const std::vector<std::pair<float, GEO::index_t>>& src_keys = m_dst_vertex_sources[vertex];
        float sum_weights{0.0f};
        for (auto j : src_keys) {
            //const GEO::index_t src_key = j.second;
            sum_weights += j.first;
        }

        if (sum_weights == 0.0f) {
            continue;
        }

        GEO::vec3 dst_value{0.0f, 0.0f, 0.0f};
        for (auto j : src_keys) {
            const GEO::index_t src_key = j.second;

            const float     weight    = j.first;
            const GEO::vec3 src_value = source_mesh.vertices.point(src_key);
            dst_value += static_cast<GEO::vec3>((weight / sum_weights) * src_value);
        }

        destination_mesh.vertices.point(vertex) = dst_value;
    }

    // Recompute facet_id, facet_centroid, facet_normal
    interpolate_attribute<GEO::vec4f>(s.facet_color_0,          d.facet_color_0,          m_dst_facet_sources);
    interpolate_attribute<GEO::vec4f>(s.facet_color_1,          d.facet_color_1,          m_dst_facet_sources);
    interpolate_attribute<GEO::vec2f>(s.facet_aniso_control,    d.facet_aniso_control,    m_dst_facet_sources);
    interpolate_attribute<GEO::vec3f>(s.vertex_normal,          d.vertex_normal,          m_dst_vertex_sources);
    interpolate_attribute<GEO::vec3f>(s.vertex_normal_smooth,   d.vertex_normal_smooth,   m_dst_vertex_sources);
    interpolate_attribute<GEO::vec2f>(s.vertex_texcoord_0,      d.vertex_texcoord_0,      m_dst_vertex_sources);
    interpolate_attribute<GEO::vec2f>(s.vertex_texcoord_1,      d.vertex_texcoord_1,      m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4f>(s.vertex_tangent,         d.vertex_tangent,         m_dst_vertex_sources);
    interpolate_attribute<GEO::vec3f>(s.vertex_bitangent,       d.vertex_bitangent,       m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4f>(s.vertex_color_0,         d.vertex_color_0,         m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4f>(s.vertex_color_1,         d.vertex_color_1,         m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4f>(s.vertex_joint_weights_0, d.vertex_joint_weights_0, m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4f>(s.vertex_joint_weights_1, d.vertex_joint_weights_1, m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4u>(s.vertex_joint_indices_0, d.vertex_joint_indices_0, m_dst_vertex_sources);
    interpolate_attribute<GEO::vec4u>(s.vertex_joint_indices_1, d.vertex_joint_indices_1, m_dst_vertex_sources);
    interpolate_attribute<GEO::vec2f>(s.vertex_aniso_control,   d.vertex_aniso_control,   m_dst_vertex_sources);
    interpolate_attribute<GEO::vec3f>(s.corner_normal,          d.corner_normal,          m_dst_corner_sources);
    interpolate_attribute<GEO::vec2f>(s.corner_texcoord_0,      d.corner_texcoord_0,      m_dst_corner_sources);
    interpolate_attribute<GEO::vec2f>(s.corner_texcoord_1,      d.corner_texcoord_1,      m_dst_corner_sources);
    interpolate_attribute<GEO::vec4f>(s.corner_tangent,         d.corner_tangent,         m_dst_corner_sources);
    interpolate_attribute<GEO::vec3f>(s.corner_bitangent,       d.corner_bitangent,       m_dst_corner_sources);
    interpolate_attribute<GEO::vec4f>(s.corner_color_0,         d.corner_color_0,         m_dst_corner_sources);
    interpolate_attribute<GEO::vec4f>(s.corner_color_1,         d.corner_color_1,         m_dst_corner_sources);
    interpolate_attribute<GEO::vec2f>(s.corner_aniso_control,   d.corner_aniso_control,   m_dst_corner_sources);

    // TODO edge attributes
}

void Geometry_operation::copy_mesh_attributes()
{
    const Mesh_attributes& s = source.get_attributes();
    Mesh_attributes&       d = destination.get_attributes();

    copy_attribute<GEO::vec4f>(s.facet_color_0,          d.facet_color_0          );
    copy_attribute<GEO::vec4f>(s.facet_color_1,          d.facet_color_1          );
    copy_attribute<GEO::vec2f>(s.facet_aniso_control,    d.facet_aniso_control    );
    copy_attribute<GEO::vec3f>(s.vertex_normal,          d.vertex_normal          );
    copy_attribute<GEO::vec3f>(s.vertex_normal_smooth,   d.vertex_normal_smooth   );
    copy_attribute<GEO::vec2f>(s.vertex_texcoord_0,      d.vertex_texcoord_0      );
    copy_attribute<GEO::vec2f>(s.vertex_texcoord_1,      d.vertex_texcoord_1      );
    copy_attribute<GEO::vec4f>(s.vertex_tangent,         d.vertex_tangent         );
    copy_attribute<GEO::vec3f>(s.vertex_bitangent,       d.vertex_bitangent       );
    copy_attribute<GEO::vec4f>(s.vertex_color_0,         d.vertex_color_0         );
    copy_attribute<GEO::vec4f>(s.vertex_color_1,         d.vertex_color_1         );
    copy_attribute<GEO::vec4f>(s.vertex_joint_weights_0, d.vertex_joint_weights_0 );
    copy_attribute<GEO::vec4f>(s.vertex_joint_weights_1, d.vertex_joint_weights_1 );
    copy_attribute<GEO::vec4u>(s.vertex_joint_indices_0, d.vertex_joint_indices_0 );
    copy_attribute<GEO::vec4u>(s.vertex_joint_indices_1, d.vertex_joint_indices_1 );
    copy_attribute<GEO::vec2f>(s.vertex_aniso_control,   d.vertex_aniso_control   );
    copy_attribute<GEO::vec3f>(s.corner_normal,          d.corner_normal          );
    copy_attribute<GEO::vec2f>(s.corner_texcoord_0,      d.corner_texcoord_0      );
    copy_attribute<GEO::vec2f>(s.corner_texcoord_1,      d.corner_texcoord_1      );
    copy_attribute<GEO::vec4f>(s.corner_tangent,         d.corner_tangent         );
    copy_attribute<GEO::vec3f>(s.corner_bitangent,       d.corner_bitangent       );
    copy_attribute<GEO::vec4f>(s.corner_color_0,         d.corner_color_0         );
    copy_attribute<GEO::vec4f>(s.corner_color_1,         d.corner_color_1         );
    copy_attribute<GEO::vec2f>(s.corner_aniso_control,   d.corner_aniso_control   );
}

void Geometry_operation::post_processing()
{
    //destination.sanity_check();
    //destination.compute_tangents();

    interpolate_mesh_attributes();

    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    destination.process(flags);
}

} // namespace erhe::geometry::operation
