#include "erhe_geometry/operation/clip_tile_tree.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"

#include <fmt/format.h>

#include <map>
#include <unordered_map>

namespace erhe::geometry::operation {

namespace {

using Weighted_sources = std::vector<std::pair<float, GEO::index_t>>;

// A clip-space vertex: either an original source vertex (records[v] for
// v < source vertex count) or a cut produced by a tree plane. Positions and
// vertex provenance are stored once; every fragment on either side of a cut
// references the same record, which is what makes the shared boundary
// binary exact.
class Record
{
public:
    GEO::vec3f       position{0.0f, 0.0f, 0.0f};
    Weighted_sources vertex_sources; // flattened over ORIGINAL source vertices
};

// Recomputes a position from flattened vertex sources with exactly the
// expression Geometry_operation::interpolate_mesh_attributes() uses
// (per-term weight / sum division, list order), so the positions used for
// plane classification here are bitwise identical to the positions the
// emitted geometry ends up with.
[[nodiscard]] auto position_from_sources(const GEO::Mesh& source_mesh, const Weighted_sources& sources) -> GEO::vec3f
{
    float sum_weights = 0.0f;
    for (const std::pair<float, GEO::index_t>& entry : sources) {
        sum_weights += entry.first;
    }
    GEO::vec3f value{0.0f, 0.0f, 0.0f};
    for (const std::pair<float, GEO::index_t>& entry : sources) {
        const float      weight    = entry.first;
        const GEO::vec3f src_value = get_pointf(source_mesh.vertices, entry.second);
        value += static_cast<GEO::vec3f>((weight / sum_weights) * src_value);
    }
    return value;
}

// One polygon corner inside a fragment: the shared vertex record plus this
// facet's corner-attribute provenance (index into the corner source pool).
class Fragment_corner
{
public:
    uint32_t record        {0};
    uint32_t corner_sources{0}; // index into corner source pool
};

class Fragment
{
public:
    GEO::index_t                 src_facet{GEO::NO_INDEX};
    std::vector<Fragment_corner> corners;
};

class Cut_key
{
public:
    uint32_t record_a{0}; // canonical: record_a < record_b
    uint32_t record_b{0};
    uint32_t node    {0};

    auto operator==(const Cut_key& other) const -> bool
    {
        return (record_a == other.record_a) && (record_b == other.record_b) && (node == other.node);
    }
};

class Cut_key_hash
{
public:
    auto operator()(const Cut_key& key) const -> std::size_t
    {
        const uint64_t packed = (static_cast<uint64_t>(key.record_a) << 32) | key.record_b;
        return std::hash<uint64_t>{}(packed) ^ (std::hash<uint32_t>{}(key.node) << 1);
    }
};

class Cut_entry
{
public:
    uint32_t record{0};
    float    t     {0.0f}; // canonical: from record_a toward record_b
};

class Clip_context
{
public:
    const GEO::Mesh&                                  source_mesh;
    const std::vector<Clip_tree_node>&                tree;
    int                                               overflow_tile;
    std::vector<Record>                               records;
    std::vector<Weighted_sources>                     corner_source_pool;
    std::unordered_map<Cut_key, Cut_entry, Cut_key_hash> cut_memo;
    std::map<int, std::vector<Fragment>>              tile_fragments; // ordered for deterministic emission

    [[nodiscard]] auto subtree_contains_tile(const int node_index, const int tile) const -> bool
    {
        if (node_index < 0) {
            return false;
        }
        const Clip_tree_node& node = tree[static_cast<std::size_t>(node_index)];
        if (node.is_leaf()) {
            return node.tile == tile;
        }
        return subtree_contains_tile(node.child[0], tile) || subtree_contains_tile(node.child[1], tile);
    }

    // Computes (or fetches) the cut record for the edge (corner_a.record,
    // corner_b.record) against the plane of tree node node_index, and this
    // fragment's corner provenance for the cut. The record (position +
    // vertex sources) is memoized per (edge, node) so every fragment - in
    // this facet, in the neighbor facet sharing the edge, on either side of
    // the plane - references the same values. Corner sources are per facet
    // (corners are facet-local) but derive from the same memoized t with a
    // canonical term order, so the two tile-side fragments of one facet get
    // identical corner source lists.
    auto make_cut(const uint32_t node_index, const Fragment_corner& corner_p, const Fragment_corner& corner_q) -> Fragment_corner
    {
        const Clip_tree_node& node = tree[node_index];
        const int             comp = node.axis; // 0 = x, 2 = z

        const bool p_is_lower = corner_p.record < corner_q.record;
        const Fragment_corner& corner_a = p_is_lower ? corner_p : corner_q;
        const Fragment_corner& corner_b = p_is_lower ? corner_q : corner_p;

        const Cut_key key{corner_a.record, corner_b.record, node_index};
        const auto    it = cut_memo.find(key);
        float    t;
        uint32_t cut_record;
        if (it != cut_memo.end()) {
            t          = it->second.t;
            cut_record = it->second.record;
        } else {
            const float pa = records[corner_a.record].position[comp];
            const float pb = records[corner_b.record].position[comp];
            t = (node.value - pa) / (pb - pa);

            Record record;
            const Weighted_sources& sources_a = records[corner_a.record].vertex_sources;
            const Weighted_sources& sources_b = records[corner_b.record].vertex_sources;
            record.vertex_sources.reserve(sources_a.size() + sources_b.size());
            for (const std::pair<float, GEO::index_t>& entry : sources_a) {
                record.vertex_sources.emplace_back((1.0f - t) * entry.first, entry.second);
            }
            for (const std::pair<float, GEO::index_t>& entry : sources_b) {
                record.vertex_sources.emplace_back(t * entry.first, entry.second);
            }
            record.position = position_from_sources(source_mesh, record.vertex_sources);

            cut_record = static_cast<uint32_t>(records.size());
            records.push_back(std::move(record));
            cut_memo.emplace(key, Cut_entry{cut_record, t});
        }

        Weighted_sources cut_corner_sources;
        const Weighted_sources& corner_sources_a = corner_source_pool[corner_a.corner_sources];
        const Weighted_sources& corner_sources_b = corner_source_pool[corner_b.corner_sources];
        cut_corner_sources.reserve(corner_sources_a.size() + corner_sources_b.size());
        for (const std::pair<float, GEO::index_t>& entry : corner_sources_a) {
            cut_corner_sources.emplace_back((1.0f - t) * entry.first, entry.second);
        }
        for (const std::pair<float, GEO::index_t>& entry : corner_sources_b) {
            cut_corner_sources.emplace_back(t * entry.first, entry.second);
        }
        const uint32_t pool_index = static_cast<uint32_t>(corner_source_pool.size());
        corner_source_pool.push_back(std::move(cut_corner_sources));
        return Fragment_corner{cut_record, pool_index};
    }

    void route(const int node_index, std::vector<Fragment>&& fragments)
    {
        if (fragments.empty()) {
            return;
        }
        const Clip_tree_node& node = tree[static_cast<std::size_t>(node_index)];
        if (node.is_leaf()) {
            std::vector<Fragment>& bucket = tile_fragments[node.tile];
            for (Fragment& fragment : fragments) {
                bucket.push_back(std::move(fragment));
            }
            return;
        }

        if (node.axis < 0) {
            // Overflow split: co-located tiles that partition by count, not
            // space - route the whole set toward the pre-assigned tile.
            const int side = subtree_contains_tile(node.child[1], overflow_tile) ? 1 : 0;
            route(node.child[side], std::move(fragments));
            return;
        }

        const int   comp  = node.axis;
        const float value = node.value;
        std::vector<Fragment> side_fragments[2];
        for (Fragment& fragment : fragments) {
            // Classify corners: -1 below plane, 0 on plane, +1 at or above.
            // On-plane corners are emitted to both sides unmodified.
            int  any_negative = 0;
            int  any_positive = 0;
            std::vector<int> signs(fragment.corners.size());
            for (std::size_t i = 0; i < fragment.corners.size(); ++i) {
                const float d = records[fragment.corners[i].record].position[comp] - value;
                const int   s = (d > 0.0f) ? 1 : (d < 0.0f) ? -1 : 0;
                signs[i] = s;
                any_negative |= static_cast<int>(s < 0);
                any_positive |= static_cast<int>(s > 0);
            }
            if (any_negative == 0) {
                side_fragments[1].push_back(std::move(fragment));
                continue;
            }
            if (any_positive == 0) {
                side_fragments[0].push_back(std::move(fragment));
                continue;
            }

            // Sutherland-Hodgman: the fragment straddles the plane. Cuts on
            // each crossing edge are computed once and shared by both sides.
            const std::size_t corner_count = fragment.corners.size();
            for (int side = 0; side < 2; ++side) {
                const int keep_sign = (side == 1) ? 1 : -1;
                Fragment out;
                out.src_facet = fragment.src_facet;
                out.corners.reserve(corner_count + 2);
                for (std::size_t i = 0; i < corner_count; ++i) {
                    const std::size_t j      = (i + 1) % corner_count;
                    const int         sign_i = signs[i];
                    const int         sign_j = signs[j];
                    if ((sign_i == 0) || (sign_i == keep_sign)) {
                        out.corners.push_back(fragment.corners[i]);
                    }
                    if (sign_i * sign_j < 0) {
                        out.corners.push_back(make_cut(static_cast<uint32_t>(node_index), fragment.corners[i], fragment.corners[j]));
                    }
                }
                // Drop consecutive duplicate records (on-plane corners can
                // repeat) and degenerate results.
                std::vector<Fragment_corner> deduped;
                deduped.reserve(out.corners.size());
                for (const Fragment_corner& corner : out.corners) {
                    if (deduped.empty() || (deduped.back().record != corner.record)) {
                        deduped.push_back(corner);
                    }
                }
                while ((deduped.size() > 1) && (deduped.front().record == deduped.back().record)) {
                    deduped.pop_back();
                }
                if (deduped.size() < 3) {
                    continue;
                }
                out.corners = std::move(deduped);
                side_fragments[side].push_back(std::move(out));
            }
        }
        fragments.clear();
        route(node.child[0], std::move(side_fragments[0]));
        route(node.child[1], std::move(side_fragments[1]));
    }
};

// Emits one tile's fragments into a destination Geometry, registering the
// shared record provenance so interpolate_mesh_attributes() reproduces
// bitwise-identical vertex and corner values in every piece that shares a
// cut record.
class Clip_emit : public Geometry_operation
{
public:
    Clip_emit(const Geometry& source, Geometry& destination)
        : Geometry_operation{source, destination}
    {
    }

    void build(const Clip_context& context, const std::vector<Fragment>& fragments)
    {
        // Batch-create the destination vertices (one per unique record used
        // by this tile) - see the geogram quadratic growth note on the
        // map_dst_* helpers.
        std::vector<GEO::index_t> record_to_dst_vertex(context.records.size(), GEO::NO_INDEX);
        GEO::index_t vertex_count = 0;
        for (const Fragment& fragment : fragments) {
            for (const Fragment_corner& corner : fragment.corners) {
                if (record_to_dst_vertex[corner.record] == GEO::NO_INDEX) {
                    record_to_dst_vertex[corner.record] = vertex_count++;
                }
            }
        }
        destination_mesh.vertices.create_vertices(vertex_count);
        for (std::size_t record_index = 0; record_index < context.records.size(); ++record_index) {
            const GEO::index_t dst_vertex = record_to_dst_vertex[record_index];
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            const Record& record = context.records[record_index];
            set_pointf(destination_mesh.vertices, dst_vertex, record.position);
            for (const std::pair<float, GEO::index_t>& entry : record.vertex_sources) {
                add_vertex_source(dst_vertex, entry.first, entry.second);
            }
        }

        GEO::Attribute<GEO::index_t> clip_source_facet{destination_mesh.facets.attributes(), "clip_source_facet"};
        for (const Fragment& fragment : fragments) {
            const GEO::index_t corner_count = static_cast<GEO::index_t>(fragment.corners.size());
            const GEO::index_t dst_facet    = destination_mesh.facets.create_polygon(corner_count);
            add_facet_source(dst_facet, 1.0f, fragment.src_facet);
            clip_source_facet[dst_facet] = fragment.src_facet;
            for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
                const Fragment_corner& corner = fragment.corners[local_corner];
                destination_mesh.facets.set_vertex(dst_facet, local_corner, record_to_dst_vertex[corner.record]);
                const GEO::index_t dst_corner = destination_mesh.facets.corner(dst_facet, local_corner);
                for (const std::pair<float, GEO::index_t>& entry : context.corner_source_pool[corner.corner_sources]) {
                    add_corner_source(dst_corner, entry.first, entry.second);
                }
            }
        }

        // Structural finalization only: every attribute channel (normals,
        // texcoords, tangents, ...) is interpolated from the shared records;
        // regenerating them would break the binary-exact boundary guarantee.
        post_processing(structural_post_process_flags);
    }
};

} // anonymous namespace

void clip_by_tile_tree(
    const erhe::geometry::Geometry&    source_world,
    const std::vector<Clip_tree_node>& tree,
    const int                          overflow_tile,
    std::vector<Clip_tile_piece>&      out_pieces)
{
    out_pieces.clear();
    if (tree.empty()) {
        return;
    }
    const GEO::Mesh& source_mesh = source_world.get_mesh();

    Clip_context context{
        .source_mesh        = source_mesh,
        .tree               = tree,
        .overflow_tile      = overflow_tile,
        .records            = {},
        .corner_source_pool = {},
        .cut_memo           = {},
        .tile_fragments     = {}
    };

    // Seed one record per source vertex; record ids below the source vertex
    // count are the source vertices themselves, so the cut memo is shared
    // across facets automatically.
    context.records.resize(source_mesh.vertices.nb());
    for (GEO::index_t vertex : source_mesh.vertices) {
        Record& record = context.records[vertex];
        record.position = get_pointf(source_mesh.vertices, vertex);
        record.vertex_sources.emplace_back(1.0f, vertex);
    }

    // Fan-triangulate source facets into initial fragments. The fan root
    // (corner 0) pool entry is shared by every fan triangle of a facet.
    std::vector<Fragment> fragments;
    fragments.reserve(source_mesh.facets.nb());
    for (const GEO::index_t src_facet : source_mesh.facets) {
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        if (corner_count < 3) {
            continue;
        }
        std::vector<uint32_t> facet_corner_pool(corner_count);
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            const GEO::index_t src_corner = source_mesh.facets.corner(src_facet, local_corner);
            facet_corner_pool[local_corner] = static_cast<uint32_t>(context.corner_source_pool.size());
            context.corner_source_pool.push_back(Weighted_sources{{1.0f, src_corner}});
        }
        const auto corner_at = [&](const GEO::index_t local_corner) -> Fragment_corner {
            const GEO::index_t src_vertex = source_mesh.facet_corners.vertex(source_mesh.facets.corner(src_facet, local_corner));
            return Fragment_corner{static_cast<uint32_t>(src_vertex), facet_corner_pool[local_corner]};
        };
        if (corner_count == 3) {
            Fragment fragment;
            fragment.src_facet = src_facet;
            fragment.corners   = {corner_at(0), corner_at(1), corner_at(2)};
            fragments.push_back(std::move(fragment));
        } else {
            for (GEO::index_t k = 2; k < corner_count; ++k) {
                Fragment fragment;
                fragment.src_facet = src_facet;
                fragment.corners   = {corner_at(0), corner_at(k - 1), corner_at(k)};
                fragments.push_back(std::move(fragment));
            }
        }
    }

    context.route(0, std::move(fragments));

    for (const auto& [tile, tile_fragments] : context.tile_fragments) {
        if (tile_fragments.empty()) {
            continue;
        }
        Clip_tile_piece piece;
        piece.tile     = tile;
        piece.geometry = std::make_shared<erhe::geometry::Geometry>(
            fmt::format("{}.tile{}", source_world.get_name(), tile)
        );
        Clip_emit emit{source_world, *piece.geometry};
        emit.build(context, tile_fragments);
        out_pieces.push_back(std::move(piece));
    }
}

} // namespace erhe::geometry::operation
