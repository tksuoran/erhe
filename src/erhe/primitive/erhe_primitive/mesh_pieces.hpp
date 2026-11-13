#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace erhe::primitive {

// One piece's indices within one level's indices (doc/erhe/primitive.md
// "Pieces"): first_index is relative to the level's first index.
class Piece_span
{
public:
    uint32_t first_index{0};
    uint32_t index_count{0};
};

// The spatial pieces of a prepared primitive's triangles: per piece its
// bounding box over every level (in the core's space), and the spans of the
// pieces in each level, level-major (spans[level * piece_count + piece]),
// the pieces of a level back to back in piece order.
class Mesh_pieces
{
public:
    [[nodiscard]] auto get_piece_count() const -> std::size_t { return bounding_boxes.size(); }
    [[nodiscard]] auto is_empty       () const -> bool        { return bounding_boxes.empty(); }
    [[nodiscard]] auto get_span       (std::size_t level, std::size_t piece) const -> Piece_span;

    std::vector<erhe::math::Aabb> bounding_boxes;
    std::vector<Piece_span>       spans;
    std::size_t                   level_count{0};
    float                         max_extent {0.0f};
};

// The split tree partition_triangles() built over a triangle set: the piece
// of every input triangle, and find_piece() for the triangles of other
// levels over the same vertices.
class Piece_partition
{
public:
    class Node
    {
    public:
        uint32_t axis {3}; // 0..2 a split along the axis, 3 a leaf
        float    split{0.0f};
        uint32_t left {0}; // split: the node of centroids below split
        uint32_t right{0}; // split: the node of centroids at or above
        uint32_t piece{0}; // leaf: the piece
    };

    [[nodiscard]] auto find_piece(const glm::vec3& centroid) const -> uint32_t;

    std::vector<uint32_t> triangle_piece; // per input triangle
    std::vector<Node>     nodes;          // nodes[0] is the root
    std::size_t           piece_count{0};
};

// Partitions the triangles `indices` (TRIANGLES) over `positions_xyz` into
// pieces: a recursive split of the triangle centroids' box at the median
// centroid along its longest axis, until a piece's centroid box is at most
// max_extent on every axis and holds at most max_triangles triangles. The
// pieces are numbered in depth-first order, so pieces near in number are
// near in space. A triangle belongs whole to the piece of its centroid, so
// the pieces' triangle boxes may overlap.
[[nodiscard]] auto partition_triangles(
    std::span<const float>    positions_xyz,
    std::span<const uint32_t> indices,
    float                     max_extent,
    std::size_t               max_triangles
) -> Piece_partition;

// Reorders the triangles of `level_indices` so that each piece's triangles
// follow each other in piece order (stable within a piece), each triangle
// assigned by find_piece() of its centroid over `positions_xyz`. Writes one
// span per piece to `out_spans` (relative to the level) and grows
// `bounding_boxes` (one per piece) by the triangles' corners.
void order_level_by_pieces(
    const Piece_partition&         partition,
    std::span<const float>         positions_xyz,
    std::span<uint32_t>            level_indices,
    std::vector<Piece_span>&       out_spans,
    std::vector<erhe::math::Aabb>& bounding_boxes
);

// The checks a loaded Mesh_pieces passes: at least two pieces, level_count
// levels each tiled exactly by its pieces' spans in piece order (back to
// back from 0, counts multiples of 3, their sum the level's index count
// `level_index_counts[level]`), finite boxes with min <= max. Returns an
// empty string when it passes, otherwise the reason (a fixed phrase without
// numbers).
[[nodiscard]] auto validate_mesh_pieces(const Mesh_pieces& pieces, std::span<const std::size_t> level_index_counts) -> std::string;

} // namespace erhe::primitive
