#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <span>

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

class Surface_offset
{
public:
    bool  found {false};
    float height{0.0f}; // the base's rise against the snap direction; negative when it moves along it
};

// U23 / U30 / U31: the nearest height within max_distance where a base point rests on a surface hit and no line has a hit
// strictly inside the piece's span; of equal distances up and down the one along the snap direction (down) wins.
[[nodiscard]] inline auto find_snap_height(const std::span<const Surface_line> lines, const float max_distance) -> Surface_offset
{
    Surface_offset best{};
    for (const Surface_line& candidate_line : lines) {
        for (const Surface_hit_point& hit : candidate_line.hits) {
            const float height = hit.height;
            if (!hit.surface || (std::abs(height) > (max_distance + c_surface_snap_tolerance))) {
                continue;
            }
            if (best.found && ((std::abs(height) > std::abs(best.height)) || ((std::abs(height) == std::abs(best.height)) && (height >= best.height)))) {
                continue;
            }
            bool fits = true;
            for (const Surface_line& line : lines) {
                if (!line_is_free(line, height, height + line.span)) {
                    fits = false;
                    break;
                }
            }
            if (fits) {
                best = Surface_offset{.found = true, .height = height};
            }
        }
    }
    return best;
}

} // namespace editor
