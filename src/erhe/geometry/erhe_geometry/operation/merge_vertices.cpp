#include "erhe_geometry/operation/merge_vertices.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/octree.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// Carries the source selection through the operation's provenance.
void remap_selection(const Geometry_operation& operation, Component_remap* remap)
{
    if ((remap == nullptr) || (remap->source == nullptr) || (remap->destination == nullptr)) {
        return;
    }
    operation.remap_component_selection(*remap->source, *remap->destination);
}

class Union_find
{
public:
    explicit Union_find(const std::size_t count)
        : m_parent(count)
    {
        std::iota(m_parent.begin(), m_parent.end(), GEO::index_t{0});
    }

    auto find(GEO::index_t x) -> GEO::index_t
    {
        while (m_parent[x] != x) {
            m_parent[x] = m_parent[m_parent[x]];
            x = m_parent[x];
        }
        return x;
    }

    void unite(const GEO::index_t a, const GEO::index_t b)
    {
        const GEO::index_t root_a = find(a);
        const GEO::index_t root_b = find(b);
        if (root_a != root_b) {
            m_parent[std::max(root_a, root_b)] = std::min(root_a, root_b);
        }
    }

private:
    std::vector<GEO::index_t> m_parent;
};

// One group of vertices merged into a survivor.
class Merge_cluster
{
public:
    GEO::index_t              survivor{GEO::NO_INDEX};
    std::vector<GEO::index_t> members; // includes the survivor
    std::optional<GEO::vec3f> position;           // new survivor position, when it moves
    bool                      average_attributes{false}; // survivor provenance = the cluster, equal weights
    bool                      merge_uvs{false};          // corner texcoords snap to the cluster's extent midpoint
};

// Shared machinery: the derived vertex set of a selection, and the weld of a
// set of clusters with provenance, remap and UV handling.
class Merge_operation_base : public Edit_mesh_operation
{
public:
    Merge_operation_base(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

protected:
    // Flags (indexed by vertex) of the selection's vertices, the endpoints of
    // its edges and the vertices of its facets; every live vertex for nullptr.
    [[nodiscard]] auto get_selected_vertex_flags(const Geometry_component_selection* selection) const -> std::vector<std::uint8_t>
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        std::vector<std::uint8_t> selected(vertex_slot_count, 0);
        const auto mark = [&](const GEO::index_t vertex) {
            if ((vertex < vertex_slot_count) && m_edit_mesh.is_vertex_alive(vertex)) {
                selected[vertex] = 1;
            }
        };
        if (selection == nullptr) {
            for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
                mark(vertex);
            }
            return selected;
        }
        for (const GEO::index_t vertex : selection->vertices) {
            mark(vertex);
        }
        for (const std::pair<GEO::index_t, GEO::index_t>& edge : selection->edges) {
            mark(edge.first);
            mark(edge.second);
        }
        for (const GEO::index_t facet : selection->facets) {
            if ((facet >= m_edit_mesh.get_facet_slot_count()) || !m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                mark(corner.vertex);
            }
        }
        return selected;
    }

    [[nodiscard]] auto get_mean_position(const std::span<const GEO::index_t> vertices) const -> GEO::vec3f
    {
        GEO::vec3f sum{0.0f, 0.0f, 0.0f};
        for (const GEO::index_t vertex : vertices) {
            sum += m_edit_mesh.get_position(vertex);
        }
        return vertices.empty() ? sum : (sum / static_cast<float>(vertices.size()));
    }

    // Welds every cluster, applies positions and provenance, emits, then
    // makes each cluster's source vertices map to the survivor (so the remap
    // selects it) and writes merged UVs.
    void weld_and_emit(const std::vector<Merge_cluster>& clusters)
    {
        std::vector<std::pair<GEO::index_t, GEO::index_t>> merges;
        std::vector<std::vector<Edit_source>>              cluster_sources(clusters.size());
        for (std::size_t i = 0; i < clusters.size(); ++i) {
            // Averaged: every member with an equal weight. Otherwise the
            // survivor keeps its weights and the other members are listed with
            // weight 0, so the provenance names the whole cluster (the remap
            // selects the survivor for any selected member) while the
            // attributes stay the survivor's.
            const Merge_cluster& cluster = clusters[i];
            const float average_weight = 1.0f / static_cast<float>(cluster.members.size());
            for (const GEO::index_t member : cluster.members) {
                if (member != cluster.survivor) {
                    merges.emplace_back(member, cluster.survivor);
                }
                const float scale = cluster.average_attributes ? average_weight : ((member == cluster.survivor) ? 1.0f : 0.0f);
                for (const Edit_source& source_entry : m_edit_mesh.get_vertex(member).sources) {
                    cluster_sources[i].emplace_back(scale * source_entry.first, source_entry.second);
                }
            }
        }

        m_edit_mesh.weld_vertices(merges);

        for (std::size_t i = 0; i < clusters.size(); ++i) {
            const Merge_cluster& cluster = clusters[i];
            if (cluster.position.has_value()) {
                m_edit_mesh.set_position(cluster.survivor, cluster.position.value());
            }
            m_edit_mesh.set_vertex_sources(cluster.survivor, cluster_sources[i]);
        }

        emit();

        for (std::size_t i = 0; i < clusters.size(); ++i) {
            const Merge_cluster& cluster = clusters[i];
            const GEO::index_t dst_vertex = get_emitted_vertex(cluster.survivor);
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            for (const Edit_source& source_entry : cluster_sources[i]) {
                const std::size_t src_vertex = static_cast<std::size_t>(source_entry.second);
                const std::size_t old_size   = m_vertex_src_to_dst.size();
                if (old_size <= src_vertex) {
                    m_vertex_src_to_dst.resize(get_size_to_include(old_size, src_vertex));
                }
                m_vertex_src_to_dst[src_vertex] = dst_vertex;
            }
        }

        write_merged_uvs(clusters, cluster_sources);
    }

private:
    // Per texcoord channel and per UV-merging cluster: the midpoint of the
    // per-component extent of the source corners at the cluster's source
    // vertices, written to every destination corner of the survivor.
    void write_merged_uvs(const std::vector<Merge_cluster>& clusters, const std::vector<std::vector<Edit_source>>& cluster_sources)
    {
        constexpr GEO::index_t no_cluster = GEO::NO_INDEX;
        const GEO::index_t src_vertex_count = source_mesh.vertices.nb();
        const GEO::index_t dst_vertex_count = destination_mesh.vertices.nb();
        std::vector<GEO::index_t> src_vertex_cluster(src_vertex_count, no_cluster);
        std::vector<GEO::index_t> dst_vertex_cluster(dst_vertex_count, no_cluster);
        bool any = false;
        for (std::size_t i = 0; i < clusters.size(); ++i) {
            const Merge_cluster& cluster = clusters[i];
            if (!cluster.merge_uvs) {
                continue;
            }
            const GEO::index_t dst_vertex = get_emitted_vertex(cluster.survivor);
            if ((dst_vertex == GEO::NO_INDEX) || (dst_vertex >= dst_vertex_count)) {
                continue;
            }
            dst_vertex_cluster[dst_vertex] = static_cast<GEO::index_t>(i);
            for (const Edit_source& source_entry : cluster_sources[i]) {
                if (source_entry.second < src_vertex_count) {
                    src_vertex_cluster[source_entry.second] = static_cast<GEO::index_t>(i);
                }
            }
            any = true;
        }
        if (!any) {
            return;
        }

        const Mesh_attributes& src_attributes = source.get_attributes();
        Mesh_attributes&       dst_attributes = destination.get_attributes();
        const std::array<const Attribute_present<GEO::vec2f>*, 3> src_channels{
            &src_attributes.corner_texcoord_0, &src_attributes.corner_texcoord_1, &src_attributes.corner_texcoord_2
        };
        const std::array<Attribute_present<GEO::vec2f>*, 3> dst_channels{
            &dst_attributes.corner_texcoord_0, &dst_attributes.corner_texcoord_1, &dst_attributes.corner_texcoord_2
        };

        class Extent
        {
        public:
            GEO::vec2f min{ std::numeric_limits<float>::max(),  std::numeric_limits<float>::max()};
            GEO::vec2f max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            bool       found{false};
        };
        std::vector<Extent> extents;
        const GEO::index_t src_corner_count = source_mesh.facet_corners.nb();
        const GEO::index_t dst_corner_count = destination_mesh.facet_corners.nb();
        for (std::size_t channel = 0; channel < src_channels.size(); ++channel) {
            const Attribute_present<GEO::vec2f>& src_channel = *src_channels[channel];
            Attribute_present<GEO::vec2f>&       dst_channel = *dst_channels[channel];
            extents.assign(clusters.size(), Extent{});
            bool channel_found = false;
            for (GEO::index_t corner = 0; corner < src_corner_count; ++corner) {
                const GEO::index_t cluster = src_vertex_cluster[source_mesh.facet_corners.vertex(corner)];
                if ((cluster == no_cluster) || !src_channel.has(corner)) {
                    continue;
                }
                const GEO::vec2f texcoord = src_channel.get(corner);
                Extent& extent = extents[cluster];
                extent.min.x = std::min(extent.min.x, texcoord.x);
                extent.min.y = std::min(extent.min.y, texcoord.y);
                extent.max.x = std::max(extent.max.x, texcoord.x);
                extent.max.y = std::max(extent.max.y, texcoord.y);
                extent.found = true;
                channel_found = true;
            }
            if (!channel_found) {
                continue;
            }
            for (GEO::index_t corner = 0; corner < dst_corner_count; ++corner) {
                const GEO::index_t cluster = dst_vertex_cluster[destination_mesh.facet_corners.vertex(corner)];
                if ((cluster == no_cluster) || !extents[cluster].found) {
                    continue;
                }
                const Extent& extent = extents[cluster];
                dst_channel.set(corner, 0.5f * (extent.min + extent.max));
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

class Merge_vertices : public Merge_operation_base
{
public:
    Merge_vertices(const Geometry& source, Geometry& destination, const Geometry_component_selection& selection, const Merge_vertices_options& options)
        : Merge_operation_base{source, destination}
        , m_selection         {selection}
        , m_options           {options}
    {
    }

    void build()
    {
        std::vector<Merge_cluster> clusters;
        if (m_options.type == Merge_type::collapse) {
            make_collapse_clusters(clusters);
        } else {
            make_single_cluster(clusters);
        }
        weld_and_emit(clusters);
    }

private:
    void make_single_cluster(std::vector<Merge_cluster>& clusters) const
    {
        const std::vector<std::uint8_t> selected = get_selected_vertex_flags(&m_selection);
        Merge_cluster cluster;
        for (GEO::index_t vertex = 0; vertex < static_cast<GEO::index_t>(selected.size()); ++vertex) {
            if (selected[vertex] != 0) {
                cluster.members.push_back(vertex);
            }
        }
        if (cluster.members.size() < 2) {
            return;
        }
        switch (m_options.type) {
            case Merge_type::at_center: {
                cluster.survivor           = cluster.members.front();
                cluster.position           = get_mean_position(cluster.members);
                cluster.average_attributes = true;
                cluster.merge_uvs          = m_options.merge_uvs;
                break;
            }
            case Merge_type::at_position: {
                cluster.survivor           = cluster.members.front();
                cluster.position           = m_options.position;
                cluster.average_attributes = true;
                cluster.merge_uvs          = m_options.merge_uvs;
                break;
            }
            case Merge_type::at_first: {
                cluster.survivor = cluster.members.front();
                break;
            }
            case Merge_type::at_last: {
                cluster.survivor = cluster.members.back();
                break;
            }
            default: {
                return;
            }
        }
        clusters.push_back(std::move(cluster));
    }

    // Islands: connected components of the selected edges (the selection's
    // edges, the edges of its facets, the edges between two selected
    // vertices). Each island collapses to its lowest vertex at its mean.
    void make_collapse_clusters(std::vector<Merge_cluster>& clusters)
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        Union_find                union_find{vertex_slot_count};
        std::vector<std::uint8_t> in_edge(vertex_slot_count, 0);
        const auto add_edge = [&](const GEO::index_t a, const GEO::index_t b) {
            if ((a >= vertex_slot_count) || (b >= vertex_slot_count) || (a == b)) {
                return;
            }
            if (m_edit_mesh.find_edge(a, b) == GEO::NO_INDEX) {
                return;
            }
            union_find.unite(a, b);
            in_edge[a] = 1;
            in_edge[b] = 1;
        };
        for (const std::pair<GEO::index_t, GEO::index_t>& edge : m_selection.edges) {
            add_edge(edge.first, edge.second);
        }
        for (const GEO::index_t facet : m_selection.facets) {
            if ((facet >= m_edit_mesh.get_facet_slot_count()) || !m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            for (std::size_t i = 0; i < corners.size(); ++i) {
                add_edge(corners[i].vertex, corners[(i + 1) % corners.size()].vertex);
            }
        }
        for (const GEO::index_t vertex : m_selection.vertices) {
            if ((vertex >= vertex_slot_count) || !m_edit_mesh.is_vertex_alive(vertex)) {
                continue;
            }
            const std::span<const GEO::index_t> edges = m_edit_mesh.get_vertex_edges(vertex);
            const std::vector<GEO::index_t> vertex_edges{edges.begin(), edges.end()};
            for (const GEO::index_t edge : vertex_edges) {
                const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(edge, vertex);
                if (m_selection.vertices.contains(other)) {
                    add_edge(vertex, other);
                }
            }
        }

        std::vector<GEO::index_t> root_to_cluster(vertex_slot_count, GEO::NO_INDEX);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            if (in_edge[vertex] == 0) {
                continue;
            }
            const GEO::index_t root = union_find.find(vertex);
            if (root_to_cluster[root] == GEO::NO_INDEX) {
                root_to_cluster[root] = static_cast<GEO::index_t>(clusters.size());
                clusters.emplace_back();
            }
            clusters[root_to_cluster[root]].members.push_back(vertex); // ascending
        }
        for (Merge_cluster& cluster : clusters) {
            cluster.survivor           = cluster.members.front();
            cluster.position           = get_mean_position(cluster.members);
            cluster.average_attributes = true;
            cluster.merge_uvs          = m_options.merge_uvs;
        }
    }

    const Geometry_component_selection& m_selection;
    Merge_vertices_options              m_options;
};

////////////////////////////////////////////////////////////////////////////////

class Merge_by_distance : public Merge_operation_base
{
public:
    Merge_by_distance(const Geometry& source, Geometry& destination, const Geometry_component_selection* selection, const Merge_by_distance_options& options)
        : Merge_operation_base{source, destination}
        , m_selection         {selection}
        , m_options           {options}
    {
    }

    void build()
    {
        std::vector<Merge_cluster> clusters;
        make_clusters(clusters);
        weld_and_emit(clusters);
    }

private:
    void make_clusters(std::vector<Merge_cluster>& clusters)
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        if ((vertex_slot_count == 0) || !(m_options.threshold > 0.0f)) {
            return;
        }
        const std::vector<std::uint8_t> selected = get_selected_vertex_flags(m_selection);

        std::vector<GEO::vec3f> points(vertex_slot_count);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            points[vertex] = m_edit_mesh.get_position(vertex);
        }
        unibn::Octree<GEO::vec3f> octree;
        octree.initialize(points);

        Union_find                union_find{vertex_slot_count};
        std::vector<std::uint8_t> clustered(vertex_slot_count, 0);
        std::vector<GEO::index_t> nearby;
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            if (selected[vertex] == 0) {
                continue;
            }
            nearby.clear();
            octree.radiusNeighbors<unibn::L2Distance<GEO::vec3f>>(points[vertex], m_options.threshold, nearby);
            for (const GEO::index_t other : nearby) {
                if ((other == vertex) || !m_edit_mesh.is_vertex_alive(other)) {
                    continue;
                }
                if ((selected[other] == 0) && !m_options.include_unselected) {
                    continue;
                }
                union_find.unite(vertex, other);
                clustered[vertex] = 1;
                clustered[other]  = 1;
            }
        }

        std::vector<GEO::index_t> root_to_cluster(vertex_slot_count, GEO::NO_INDEX);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            if (clustered[vertex] == 0) {
                continue;
            }
            const GEO::index_t root = union_find.find(vertex);
            if (root_to_cluster[root] == GEO::NO_INDEX) {
                root_to_cluster[root] = static_cast<GEO::index_t>(clusters.size());
                clusters.emplace_back();
            }
            clusters[root_to_cluster[root]].members.push_back(vertex); // ascending
        }

        std::vector<GEO::index_t> pool;
        for (Merge_cluster& cluster : clusters) {
            const GEO::vec3f centroid = get_mean_position(cluster.members);
            pool.clear();
            for (const GEO::index_t member : cluster.members) {
                if (selected[member] == 0) {
                    pool.push_back(member);
                }
            }
            const bool survivor_is_unselected = !pool.empty();
            if (pool.empty()) {
                pool = cluster.members;
            }
            if ((pool.size() == 1) || (cluster.members.size() == 2)) {
                cluster.survivor = pool.front(); // the lower index
            } else {
                float best_distance = std::numeric_limits<float>::max();
                for (const GEO::index_t member : pool) {
                    const float distance = GEO::length2(m_edit_mesh.get_position(member) - centroid);
                    if (distance < best_distance) {
                        best_distance    = distance;
                        cluster.survivor = member;
                    }
                }
            }
            if (m_options.use_centroid && !survivor_is_unselected) {
                cluster.position           = centroid;
                cluster.average_attributes = true;
            }
        }
    }

    const Geometry_component_selection* m_selection;
    Merge_by_distance_options           m_options;
};

} // anonymous namespace

void merge_vertices(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    const Merge_vertices_options        options,
    Component_remap*                    remap
)
{
    Merge_vertices operation{source, destination, selection, options};
    operation.build();
    remap_selection(operation, remap);
}

void merge_by_distance(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection* selection,
    const Merge_by_distance_options     options,
    Component_remap*                    remap
)
{
    Merge_by_distance operation{source, destination, selection, options};
    operation.build();
    remap_selection(operation, remap);
}

} // namespace erhe::geometry::operation
