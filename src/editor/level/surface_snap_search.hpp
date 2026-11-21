#pragma once

#include "config/generated/surface_support.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <span>

namespace erhe::scene { class Mesh; }

// The snap to surface search of D7.3 surface placement, free of rays (doc/editor/level_tools.md "Surface placement").
namespace editor {

// A move smaller than this is no move; a hit this close to a span's end is not inside it.
inline constexpr float c_surface_snap_tolerance = 1.0e-4f;

// A face on the line through one base point; height is above the base, against the snap direction.
class Surface_hit_point
{
public:
    float     height {0.0f};
    bool      surface{false}; // the face's normal is against the snap direction: a piece can rest on it
    glm::vec3 normal {0.0f, 1.0f, 0.0f};
    const erhe::scene::Mesh* mesh{nullptr}; // the mesh owning the face; null where the caller traces no scene
};

// The hits on one base point's line within the search window, and the piece's extent above the base point.
class Surface_line
{
public:
    std::span<const Surface_hit_point> hits;
    float                              span{0.0f};
};

// No hit of the line lies strictly inside lo - hi (by more than the tolerance at either end).
[[nodiscard]] inline auto line_is_free(const Surface_line& line, const float lo, const float hi) -> bool
{
    for (const Surface_hit_point& hit : line.hits) {
        if ((hit.height > (lo + c_surface_snap_tolerance)) && (hit.height < (hi - c_surface_snap_tolerance))) {
            return false;
        }
    }
    return true;
}

// No line has a hit strictly inside its span from the height.
[[nodiscard]] inline auto lines_are_free(const std::span<const Surface_line> lines, const float height) -> bool
{
    for (const Surface_line& line : lines) {
        if (!line_is_free(line, height, height + line.span)) {
            return false;
        }
    }
    return true;
}

class Surface_offset
{
public:
    bool  found {false};
    float height{0.0f}; // the base's rise against the snap direction; negative when it moves along it
    std::size_t line{0}; // the supporting hit, lines[line].hits[hit], when found
    std::size_t hit {0};
};

// The search window of one snap (U51): heights from forward_distance along the snap direction to backward_distance against it.
class Surface_search_window
{
public:
    float forward_distance {0.0f}; // meters along the snap direction (down by default)
    float backward_distance{0.0f}; // meters against it
};

// U23 / U31 / U51: the nearest height within the window where a base point of a support line rests on a surface hit and
// no blocking line has a hit strictly inside the piece's span; of equal distances the one along the snap direction wins.
[[nodiscard]] inline auto find_snap_height(
    const std::span<const Surface_line> support_lines,
    const std::span<const Surface_line> blocking_lines,
    const Surface_search_window         window
) -> Surface_offset
{
    Surface_offset best{};
    for (std::size_t line_index = 0, line_end = support_lines.size(); line_index < line_end; ++line_index) {
        const std::span<const Surface_hit_point> hits = support_lines[line_index].hits;
        for (std::size_t hit_index = 0, hit_end = hits.size(); hit_index < hit_end; ++hit_index) {
            const Surface_hit_point& hit    = hits[hit_index];
            const float              height = hit.height;
            if (
                !hit.surface ||
                (height < -(window.forward_distance + c_surface_snap_tolerance)) ||
                (height > (window.backward_distance + c_surface_snap_tolerance))
            ) {
                continue;
            }
            if (best.found && ((std::abs(height) > std::abs(best.height)) || ((std::abs(height) == std::abs(best.height)) && (height >= best.height)))) {
                continue;
            }
            if (lines_are_free(blocking_lines, height)) {
                best = Surface_offset{.found = true, .height = height, .line = line_index, .hit = hit_index};
            }
        }
    }
    return best;
}

// Every line both supports and blocks.
[[nodiscard]] inline auto find_snap_height(const std::span<const Surface_line> lines, const Surface_search_window window) -> Surface_offset
{
    return find_snap_height(lines, lines, window);
}

// S2.2: the supporting hit moves to the first line, the base center's first, with a surface hit at the place.
[[nodiscard]] inline auto with_first_supporting_line(const std::span<const Surface_line> lines, const Surface_offset offset) -> Surface_offset
{
    if (!offset.found) {
        return offset;
    }
    for (std::size_t line_index = 0; line_index < offset.line; ++line_index) {
        const std::span<const Surface_hit_point> hits = lines[line_index].hits;
        for (std::size_t hit_index = 0, hit_end = hits.size(); hit_index < hit_end; ++hit_index) {
            if (hits[hit_index].surface && (std::abs(hits[hit_index].height - offset.height) <= c_surface_snap_tolerance)) {
                return Surface_offset{.found = true, .height = offset.height, .line = line_index, .hit = hit_index};
            }
        }
    }
    return offset;
}

// U52: the kit layout support rule over the base lines, the base center's line first; the offset names the supporting hit.
[[nodiscard]] inline auto find_support_height(
    const std::span<const Surface_line> lines,
    const Surface_search_window         window,
    const Surface_support               support
) -> Surface_offset
{
    if (lines.empty()) {
        return Surface_offset{};
    }
    switch (support) {
        case Surface_support::five_lines: {
            return with_first_supporting_line(lines, find_snap_height(lines, lines, window));
        }
        case Surface_support::center_blocks: {
            return with_first_supporting_line(lines, find_snap_height(lines, lines.first(1), window));
        }
        case Surface_support::center_fallback: {
            // The nearest place of any line, taken when it fits; no farther place is searched.
            const Surface_offset nearest = find_snap_height(lines, std::span<const Surface_line>{}, window);
            if (!nearest.found || lines_are_free(lines, nearest.height)) {
                return with_first_supporting_line(lines, nearest);
            }
            // Refused: the nearest surface on the base center's line along the direction, with no fit test.
            const Surface_line center_rest{.hits = lines.front().hits, .span = 0.0f};
            return find_snap_height(
                std::span<const Surface_line>{&center_rest, 1},
                Surface_search_window{.forward_distance = window.forward_distance, .backward_distance = 0.0f}
            );
        }
    }
    return Surface_offset{};
}

class Point_rest
{
public:
    bool      found   {false};
    float     height  {0.0f};  // the rise against the direction; negative when the point moves along it
    glm::vec3 position{0.0f};  // the point moved along the direction onto the surface
    float     distance{0.0f};  // how far it moved along the direction; negative when against it
    std::size_t hit   {0};     // the line's supporting hit, when found
};

// U51 (Transform tool drags): a point's rest on its one line (span 0: nothing to fit); a point already within the tolerance
// of a surface stays there bit for bit.
[[nodiscard]] inline auto find_point_rest(
    const glm::vec3             point,
    const glm::vec3             direction,
    const Surface_line&         line,
    const Surface_search_window window
) -> Point_rest
{
    const Surface_offset offset = find_snap_height(std::span<const Surface_line>{&line, 1}, window);
    if (!offset.found) {
        return Point_rest{};
    }
    if (std::abs(offset.height) <= c_surface_snap_tolerance) {
        return Point_rest{.found = true, .height = offset.height, .position = point, .distance = 0.0f, .hit = offset.hit};
    }
    return Point_rest{.found = true, .height = offset.height, .position = point - (offset.height * direction), .distance = -offset.height, .hit = offset.hit};
}

} // namespace editor
