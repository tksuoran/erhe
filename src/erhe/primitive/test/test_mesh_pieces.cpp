#include "erhe_primitive/mesh_pieces.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

// A grid of unit quads (two triangles each) in the xz plane, n x n quads
// from the origin.
class Quad_grid
{
public:
    explicit Quad_grid(const std::size_t n)
    {
        for (std::size_t z = 0; z <= n; ++z) {
            for (std::size_t x = 0; x <= n; ++x) {
                positions.push_back(static_cast<float>(x));
                positions.push_back(0.0f);
                positions.push_back(static_cast<float>(z));
            }
        }
        const auto vertex = [n](const std::size_t x, const std::size_t z) -> uint32_t { return static_cast<uint32_t>((z * (n + 1)) + x); };
        for (std::size_t z = 0; z < n; ++z) {
            for (std::size_t x = 0; x < n; ++x) {
                indices.insert(indices.end(), {vertex(x, z), vertex(x + 1, z), vertex(x + 1, z + 1)});
                indices.insert(indices.end(), {vertex(x, z), vertex(x + 1, z + 1), vertex(x, z + 1)});
            }
        }
    }
    std::vector<float>    positions;
    std::vector<uint32_t> indices;
};

} // anonymous namespace

TEST(Mesh_pieces, partition_respects_extent_and_tiles_the_level)
{
    const Quad_grid grid{16}; // 16 x 16 m, 512 triangles
    const erhe::primitive::Piece_partition partition = erhe::primitive::partition_triangles(grid.positions, grid.indices, 4.0f, 1000000);
    EXPECT_GE(partition.piece_count, 16u); // at least a 4 x 4 split of centroids
    EXPECT_EQ(partition.triangle_piece.size(), grid.indices.size() / 3);

    std::vector<uint32_t> ordered = grid.indices;
    std::vector<erhe::primitive::Piece_span> spans;
    std::vector<erhe::math::Aabb> boxes(partition.piece_count);
    erhe::primitive::order_level_by_pieces(partition, grid.positions, ordered, spans, boxes);
    ASSERT_EQ(spans.size(), partition.piece_count);
    uint32_t next = 0;
    for (std::size_t piece = 0; piece < spans.size(); ++piece) {
        EXPECT_EQ(spans[piece].first_index, next);
        EXPECT_EQ(spans[piece].index_count % 3u, 0u);
        EXPECT_GT(spans[piece].index_count, 0u);
        next += spans[piece].index_count;
        // A triangle box of a 4 m centroid cell is at most 5 m wide (one quad past the cell).
        const glm::vec3 extent = boxes[piece].max - boxes[piece].min;
        EXPECT_LE(extent.x, 5.0f + 1e-5f);
        EXPECT_LE(extent.z, 5.0f + 1e-5f);
    }
    EXPECT_EQ(next, grid.indices.size());
    // The same triangles, reordered: every triangle of the input is in the output once.
    std::vector<std::array<uint32_t, 3>> a, b;
    for (std::size_t t = 0; t < grid.indices.size() / 3; ++t) {
        a.push_back({grid.indices[3 * t], grid.indices[(3 * t) + 1], grid.indices[(3 * t) + 2]});
        b.push_back({ordered[3 * t], ordered[(3 * t) + 1], ordered[(3 * t) + 2]});
    }
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    EXPECT_EQ(a, b);
    // Every triangle finds the piece it was ordered into.
    for (std::size_t piece = 0; piece < spans.size(); ++piece) {
        for (uint32_t i = spans[piece].first_index; i < spans[piece].first_index + spans[piece].index_count; i += 3) {
            glm::vec3 centroid{0.0f};
            for (std::size_t c = 0; c < 3; ++c) {
                const uint32_t v = ordered[i + c];
                centroid += glm::vec3{grid.positions[3 * v], grid.positions[(3 * v) + 1], grid.positions[(3 * v) + 2]};
            }
            EXPECT_EQ(partition.find_piece(centroid / 3.0f), piece);
        }
    }

    erhe::primitive::Mesh_pieces pieces{.bounding_boxes = boxes, .spans = spans, .level_count = 1, .max_extent = 4.0f};
    const std::array<std::size_t, 1> counts{grid.indices.size()};
    EXPECT_TRUE(erhe::primitive::validate_mesh_pieces(pieces, counts).empty());
    pieces.spans[1].first_index += 3;
    EXPECT_FALSE(erhe::primitive::validate_mesh_pieces(pieces, counts).empty());
}

TEST(Mesh_pieces, small_mesh_is_one_piece)
{
    const Quad_grid grid{2};
    const erhe::primitive::Piece_partition partition = erhe::primitive::partition_triangles(grid.positions, grid.indices, 8.0f, 1000000);
    EXPECT_EQ(partition.piece_count, 1u);
}

TEST(Mesh_pieces, triangle_cap_splits_dense_meshes)
{
    const Quad_grid grid{8}; // 128 triangles within 8 m
    const erhe::primitive::Piece_partition partition = erhe::primitive::partition_triangles(grid.positions, grid.indices, 100.0f, 32);
    EXPECT_GE(partition.piece_count, 4u);
    for (std::size_t piece = 0; piece < partition.piece_count; ++piece) {
        std::size_t count = 0;
        for (const uint32_t p : partition.triangle_piece) {
            count += (p == piece) ? 1 : 0;
        }
        EXPECT_LE(count, 32u);
    }
}
