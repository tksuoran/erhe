#include "level/level_region.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace editor {

auto region_intersects(const Level_region& region, const erhe::math::Aabb& bounds) -> bool
{
    if (!bounds.is_valid_3d()) {
        return false;
    }
    switch (region.shape) {
        case Level_region_shape::box: {
            for (int axis = 0; axis < 3; ++axis) {
                if ((bounds.max[axis] < region.min[axis]) || (bounds.min[axis] > region.max[axis])) {
                    return false;
                }
            }
            return true;
        }
        case Level_region_shape::sphere: {
            const glm::vec3 closest = glm::clamp(region.center, bounds.min, bounds.max);
            const glm::vec3 offset  = closest - region.center;
            return glm::dot(offset, offset) <= (region.radius * region.radius);
        }
        default: {
            return false;
        }
    }
}

auto make_height_map_grid(const glm::vec3& min, const glm::vec3& max, const float cell_size) -> Height_map_grid
{
    Height_map_grid grid{.min = min, .max = max, .cell_size = cell_size};
    const glm::vec3 extent = max - min;
    if (!(cell_size > 0.0f) || !(extent.x > 0.0f) || !(extent.z > 0.0f) || !(extent.y >= 0.0f)) {
        return grid;
    }
    // A tiny relative slack keeps an exact multiple from gaining a sliver cell.
    const double slack = 1.0e-4;
    grid.columns = static_cast<std::size_t>(std::max(1.0, std::ceil((static_cast<double>(extent.x) / cell_size) - slack)));
    grid.rows    = static_cast<std::size_t>(std::max(1.0, std::ceil((static_cast<double>(extent.z) / cell_size) - slack)));
    return grid;
}

auto get_cell_center(const Height_map_grid& grid, const std::size_t column, const std::size_t row) -> glm::vec2
{
    return glm::vec2{
        grid.min.x + ((static_cast<float>(column) + 0.5f) * grid.cell_size),
        grid.min.z + ((static_cast<float>(row)    + 0.5f) * grid.cell_size)
    };
}

auto get_height_range(const std::span<const Height_map_cell> cells) -> Height_range
{
    Height_range range{};
    for (const Height_map_cell& cell : cells) {
        if (!cell.hit) {
            continue;
        }
        if (!range.valid) {
            range = Height_range{.min = cell.height, .max = cell.height, .valid = true};
            continue;
        }
        range.min = std::min(range.min, cell.height);
        range.max = std::max(range.max, cell.height);
    }
    return range;
}

auto height_digit(const float height, const Height_range& range) -> char
{
    const float span = range.max - range.min;
    if (!(span > 0.0f)) {
        return '0';
    }
    const float t     = std::clamp((height - range.min) / span, 0.0f, 1.0f);
    const int   digit = static_cast<int>(std::lround(t * 9.0f));
    return static_cast<char>('0' + digit);
}

void write_height_text(const Height_map_grid& grid, const std::span<const Height_map_cell> cells, std::string& out_text)
{
    out_text.clear();
    if (cells.size() != (grid.columns * grid.rows)) {
        return;
    }
    const Height_range range = get_height_range(cells);
    out_text.reserve(grid.rows * (grid.columns + 1));
    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const Height_map_cell& cell = cells[(row * grid.columns) + column];
            out_text.push_back(cell.hit ? height_digit(cell.height, range) : '.');
        }
        out_text.push_back('\n');
    }
}

void write_island_text(const Height_map_grid& grid, const std::span<const Height_map_cell> cells, std::string& out_text)
{
    out_text.clear();
    if (cells.size() != (grid.columns * grid.rows)) {
        return;
    }
    constexpr const char* c_island_chars = "0123456789abcdefghijklmnopqrstuvwxyz";
    out_text.reserve(grid.rows * (grid.columns + 1));
    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const Height_map_cell& cell = cells[(row * grid.columns) + column];
            char c = '.';
            if (cell.hit) {
                c = (cell.walkable && (cell.island >= 0)) ? c_island_chars[cell.island % 36] : '#';
            }
            out_text.push_back(c);
        }
        out_text.push_back('\n');
    }
}

auto get_bounds_overlap(const erhe::math::Aabb& lhs, const erhe::math::Aabb& rhs) -> float
{
    if (!lhs.is_valid_3d() || !rhs.is_valid_3d()) {
        return -1.0f;
    }
    float overlap = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        overlap = std::min(overlap, std::min(lhs.max[axis], rhs.max[axis]) - std::max(lhs.min[axis], rhs.min[axis]));
    }
    return overlap;
}

auto get_bounds_overlap_center(const erhe::math::Aabb& lhs, const erhe::math::Aabb& rhs) -> glm::vec3
{
    return 0.5f * (glm::max(lhs.min, rhs.min) + glm::min(lhs.max, rhs.max));
}

void find_bounds_pairs(
    const std::span<const erhe::math::Aabb> bounds,
    const float                             tolerance,
    std::vector<std::size_t>&               scratch_order,
    std::vector<Bounds_pair>&               out_pairs
)
{
    out_pairs.clear();
    scratch_order.clear();
    for (std::size_t i = 0, end = bounds.size(); i < end; ++i) {
        if (bounds[i].is_valid_3d()) {
            scratch_order.push_back(i);
        }
    }
    std::sort(
        scratch_order.begin(), scratch_order.end(),
        [bounds](const std::size_t lhs, const std::size_t rhs) -> bool {
            return (bounds[lhs].min.x < bounds[rhs].min.x) || ((bounds[lhs].min.x == bounds[rhs].min.x) && (lhs < rhs));
        }
    );
    for (std::size_t i = 0, end = scratch_order.size(); i < end; ++i) {
        const erhe::math::Aabb& first = bounds[scratch_order[i]];
        for (std::size_t j = i + 1; j < end; ++j) {
            const erhe::math::Aabb& second = bounds[scratch_order[j]];
            // Sorted by min x: no later bounds overlap the first along x by more than the tolerance.
            if ((first.max.x - second.min.x) <= tolerance) {
                break;
            }
            if (get_bounds_overlap(first, second) > tolerance) {
                out_pairs.push_back(Bounds_pair{
                    .a = std::min(scratch_order[i], scratch_order[j]),
                    .b = std::max(scratch_order[i], scratch_order[j])
                });
            }
        }
    }
    std::sort(
        out_pairs.begin(), out_pairs.end(),
        [](const Bounds_pair& lhs, const Bounds_pair& rhs) -> bool {
            return (lhs.a < rhs.a) || ((lhs.a == rhs.a) && (lhs.b < rhs.b));
        }
    );
}

auto has_any_tag(const std::set<std::string>& tags, const std::span<const std::string> listed) -> bool
{
    for (const std::string& tag : listed) {
        if (tags.contains(tag)) {
            return true;
        }
    }
    return false;
}

auto c_str(const Level_finding_kind kind) -> const char*
{
    switch (kind) {
        case Level_finding_kind::unsupported:           return "unsupported";
        case Level_finding_kind::overlap:               return "overlap";
        case Level_finding_kind::unreachable_island:    return "unreachable_island";
        case Level_finding_kind::unwalkable_parameters: return "unwalkable_parameters";
        case Level_finding_kind::unreadable_piece:      return "unreadable_piece";
        default:                                        return "?";
    }
}

} // namespace editor
