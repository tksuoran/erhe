#include "erhe_primitive/mesh_pieces.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace erhe::primitive {

auto Mesh_pieces::get_span(const std::size_t level, const std::size_t piece) const -> Piece_span
{
    const std::size_t piece_count = get_piece_count();
    ERHE_VERIFY((level < level_count) && (piece < piece_count));
    return spans[(level * piece_count) + piece];
}

auto Piece_partition::find_piece(const glm::vec3& centroid) const -> uint32_t
{
    ERHE_VERIFY(!nodes.empty());
    uint32_t node_index = 0;
    for (;;) {
        const Node& node = nodes[node_index];
        if (node.axis == 3u) {
            return node.piece;
        }
        node_index = (centroid[static_cast<glm::vec3::length_type>(node.axis)] < node.split) ? node.left : node.right;
    }
}

namespace {

[[nodiscard]] auto triangle_centroid(const std::span<const float> positions_xyz, const std::span<const uint32_t> indices, const std::size_t triangle) -> glm::vec3
{
    glm::vec3 sum{0.0f};
    for (std::size_t corner = 0; corner < 3; ++corner) {
        const uint32_t vertex = indices[(3 * triangle) + corner];
        sum += glm::vec3{positions_xyz[(3 * vertex) + 0], positions_xyz[(3 * vertex) + 1], positions_xyz[(3 * vertex) + 2]};
    }
    return sum / 3.0f;
}

class Partition_builder
{
public:
    Partition_builder(const std::vector<glm::vec3>& centroids, const float max_extent, const std::size_t max_triangles, Piece_partition& out)
        : m_centroids    {centroids}
        , m_max_extent   {max_extent}
        , m_max_triangles{std::max<std::size_t>(max_triangles, 1)}
        , m_out          {out}
    {
    }

    // Builds the subtree over triangles [begin, end) of m_order; returns its node.
    auto build(const std::size_t begin, const std::size_t end, std::vector<uint32_t>& order) -> uint32_t
    {
        erhe::math::Aabb box{};
        for (std::size_t i = begin; i < end; ++i) {
            box.include(m_centroids[order[i]]);
        }
        const glm::vec3   extent     = box.max - box.min;
        const std::size_t count      = end - begin;
        const bool        small      = (extent.x <= m_max_extent) && (extent.y <= m_max_extent) && (extent.z <= m_max_extent);
        const bool        few        = count <= m_max_triangles;
        const bool        splittable = (count >= 2) && ((extent.x > 0.0f) || (extent.y > 0.0f) || (extent.z > 0.0f));
        const uint32_t    node_index = static_cast<uint32_t>(m_out.nodes.size());
        m_out.nodes.push_back(Piece_partition::Node{});
        if ((small && few) || !splittable) {
            const uint32_t piece = static_cast<uint32_t>(m_out.piece_count++);
            m_out.nodes[node_index] = Piece_partition::Node{.axis = 3u, .piece = piece};
            for (std::size_t i = begin; i < end; ++i) {
                m_out.triangle_piece[order[i]] = piece;
            }
            return node_index;
        }
        uint32_t axis = 0;
        if (extent.y > extent[static_cast<glm::vec3::length_type>(axis)]) { axis = 1; }
        if (extent.z > extent[static_cast<glm::vec3::length_type>(axis)]) { axis = 2; }
        const glm::vec3::length_type a = static_cast<glm::vec3::length_type>(axis);
        const std::size_t middle = begin + (count / 2);
        std::nth_element(
            order.begin() + static_cast<std::ptrdiff_t>(begin),
            order.begin() + static_cast<std::ptrdiff_t>(middle),
            order.begin() + static_cast<std::ptrdiff_t>(end),
            [this, a](const uint32_t lhs, const uint32_t rhs) -> bool { return m_centroids[lhs][a] < m_centroids[rhs][a]; }
        );
        // The split value separates the two halves exactly: every centroid
        // below `middle` is below it, every one from `middle` on at or
        // above it. Equal values across the boundary move to the right.
        const float split = m_centroids[order[middle]][a];
        std::size_t left_end = middle;
        while ((left_end > begin) && (m_centroids[order[left_end - 1]][a] >= split)) {
            --left_end;
        }
        if (left_end == begin) {
            // Every centroid at or above the median value: split below it instead.
            std::size_t right_begin = middle;
            while ((right_begin < end) && (m_centroids[order[right_begin]][a] <= split)) {
                ++right_begin;
            }
            if (right_begin == end) {
                // All equal on this axis (extent on another axis): a leaf.
                const uint32_t piece = static_cast<uint32_t>(m_out.piece_count++);
                m_out.nodes[node_index] = Piece_partition::Node{.axis = 3u, .piece = piece};
                for (std::size_t i = begin; i < end; ++i) {
                    m_out.triangle_piece[order[i]] = piece;
                }
                return node_index;
            }
            const float next_split = m_centroids[order[right_begin]][a];
            std::nth_element(
                order.begin() + static_cast<std::ptrdiff_t>(begin),
                order.begin() + static_cast<std::ptrdiff_t>(right_begin),
                order.begin() + static_cast<std::ptrdiff_t>(end),
                [this, a](const uint32_t lhs, const uint32_t rhs) -> bool { return m_centroids[lhs][a] < m_centroids[rhs][a]; }
            );
            const uint32_t left  = build(begin, right_begin, order);
            const uint32_t right = build(right_begin, end, order);
            m_out.nodes[node_index] = Piece_partition::Node{.axis = axis, .split = next_split, .left = left, .right = right};
            return node_index;
        }
        const uint32_t left  = build(begin, left_end, order);
        const uint32_t right = build(left_end, end, order);
        m_out.nodes[node_index] = Piece_partition::Node{.axis = axis, .split = split, .left = left, .right = right};
        return node_index;
    }

private:
    const std::vector<glm::vec3>& m_centroids;
    float                         m_max_extent;
    std::size_t                   m_max_triangles;
    Piece_partition&              m_out;
};

} // anonymous namespace

auto partition_triangles(
    const std::span<const float>    positions_xyz,
    const std::span<const uint32_t> indices,
    const float                     max_extent,
    const std::size_t               max_triangles
) -> Piece_partition
{
    Piece_partition   partition{};
    const std::size_t triangle_count = indices.size() / 3;
    partition.triangle_piece.assign(triangle_count, 0u);
    if (triangle_count == 0) {
        partition.nodes.push_back(Piece_partition::Node{.axis = 3u, .piece = 0u});
        partition.piece_count = 1;
        return partition;
    }
    std::vector<glm::vec3> centroids(triangle_count);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        centroids[triangle] = triangle_centroid(positions_xyz, indices, triangle);
    }
    std::vector<uint32_t> order(triangle_count);
    std::iota(order.begin(), order.end(), 0u);
    Partition_builder builder{centroids, std::max(max_extent, 0.0f), max_triangles, partition};
    static_cast<void>(builder.build(0, triangle_count, order));
    return partition;
}

void order_level_by_pieces(
    const Piece_partition&         partition,
    const std::span<const float>   positions_xyz,
    const std::span<uint32_t>      level_indices,
    std::vector<Piece_span>&       out_spans,
    std::vector<erhe::math::Aabb>& bounding_boxes
)
{
    const std::size_t piece_count    = partition.piece_count;
    const std::size_t triangle_count = level_indices.size() / 3;
    ERHE_VERIFY(bounding_boxes.size() == piece_count);
    std::vector<uint32_t> piece_of(triangle_count);
    std::vector<uint32_t> counts(piece_count, 0u);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const uint32_t piece = partition.find_piece(triangle_centroid(positions_xyz, level_indices, triangle));
        piece_of[triangle] = piece;
        ++counts[piece];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t vertex = level_indices[(3 * triangle) + corner];
            bounding_boxes[piece].include(glm::vec3{positions_xyz[(3 * vertex) + 0], positions_xyz[(3 * vertex) + 1], positions_xyz[(3 * vertex) + 2]});
        }
    }
    out_spans.resize(piece_count);
    std::vector<uint32_t> cursor(piece_count, 0u);
    uint32_t first = 0;
    for (std::size_t piece = 0; piece < piece_count; ++piece) {
        out_spans[piece] = Piece_span{.first_index = first, .index_count = 3u * counts[piece]};
        cursor[piece]    = first;
        first           += 3u * counts[piece];
    }
    std::vector<uint32_t> ordered(level_indices.size());
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        uint32_t& destination = cursor[piece_of[triangle]];
        ordered[destination + 0] = level_indices[(3 * triangle) + 0];
        ordered[destination + 1] = level_indices[(3 * triangle) + 1];
        ordered[destination + 2] = level_indices[(3 * triangle) + 2];
        destination += 3;
    }
    std::copy(ordered.begin(), ordered.end(), level_indices.begin());
}

auto validate_mesh_pieces(const Mesh_pieces& pieces, const std::span<const std::size_t> level_index_counts) -> std::string
{
    const std::size_t piece_count = pieces.get_piece_count();
    if (piece_count < 2) {
        return "fewer than two pieces";
    }
    if ((pieces.level_count == 0) || (pieces.level_count != level_index_counts.size())) {
        return "level count mismatch";
    }
    if (pieces.spans.size() != (pieces.level_count * piece_count)) {
        return "span count mismatch";
    }
    if (!std::isfinite(pieces.max_extent) || (pieces.max_extent < 0.0f)) {
        return "malformed max_extent";
    }
    for (const erhe::math::Aabb& box : pieces.bounding_boxes) {
        for (glm::vec3::length_type i = 0; i < 3; ++i) {
            if (!std::isfinite(box.min[i]) || !std::isfinite(box.max[i]) || (box.min[i] > box.max[i])) {
                return "malformed bounding box";
            }
        }
    }
    for (std::size_t level = 0; level < pieces.level_count; ++level) {
        std::size_t next = 0;
        for (std::size_t piece = 0; piece < piece_count; ++piece) {
            const Piece_span span = pieces.spans[(level * piece_count) + piece];
            if ((span.first_index != next) || ((span.index_count % 3u) != 0u)) {
                return "spans do not tile the level";
            }
            next += span.index_count;
        }
        if (next != level_index_counts[level]) {
            return "spans do not tile the level";
        }
    }
    return {};
}

} // namespace erhe::primitive
