#pragma once

#include <memory>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// One node of an axis-aligned kd split tree over world-space X / Z.
// Mirrors the lightmap baker's spatial tile partition so a mesh can be
// clipped into exactly the same tiles the baker packs regions into.
class Clip_tree_node
{
public:
    int   axis    {-1};       // 0 = X (YZ plane), 2 = Z (XY plane), -1 = overflow split (no plane)
    float value   {0.0f};     // world-space plane constant; child[0] = side < value, child[1] = side >= value
    int   child   [2]{-1, -1};// indices into the tree vector; -1 = none (leaf)
    int   tile    {-1};       // leaf: tile index; interior: -1

    [[nodiscard]] auto is_leaf() const -> bool { return (child[0] < 0) && (child[1] < 0); }
};

class Clip_tile_piece
{
public:
    int                                       tile{-1};
    std::shared_ptr<erhe::geometry::Geometry> geometry; // world-space positions, all attributes interpolated
};

// Clips a world-space source geometry down the kd tile tree, producing one
// piece geometry per overlapped leaf tile.
//
// Guarantees:
// - Each split plane is applied to each triangle fragment exactly once while
//   descending the tree, and the two output sides reference the same cut
//   record: positions and every interpolated vertex/corner attribute of a
//   clip vertex are binary exact (bitwise identical) in both pieces that
//   share the plane.
// - A vertex lying exactly on a plane is emitted to both sides unmodified
//   (no cut with t == 0 or t == 1 is ever produced).
// - N-gon facets are fan-triangulated (fan root = corner 0) before clipping;
//   fragments that survive clipping keep their polygon corner count.
// - Facet provenance is stored on each piece as facet attribute
//   "clip_source_facet" (GEO::index_t, source facet id).
//
// overflow_tile: leaves created by the baker's co-located "overflow" split
// (Clip_tree_node::axis == -1) partition regions by count, not space; when
// the tree contains such a node the whole fragment set is routed toward the
// caller's pre-assigned overflow_tile (-1 when the mesh has no overflow
// assignment; routing then falls back to child[0]).
//
// Thread safety: safe to call concurrently from multiple threads on
// DISTINCT source/destination geometries with no caller lock. The clipper
// reaches no Geogram algorithm (pure per-invocation clipping state +
// mesh-local attribute work; GEO attribute stores are per-mesh with
// spinlocked observer registration and read-only type registries at the
// geogram pin), and the piece post_processing self-locks through
// Geometry::process() (see erhe::geometry::geogram_lock()). Concurrent
// reads of one shared source geometry are fine; nothing mutates it.
void clip_by_tile_tree(
    const erhe::geometry::Geometry&    source_world,
    const std::vector<Clip_tree_node>& tree,
    int                                overflow_tile,
    std::vector<Clip_tile_piece>&      out_pieces);

} // namespace erhe::geometry::operation
