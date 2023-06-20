#if 0
#include "renderers/frustum_tiler.hpp"
#include "editor_log.hpp"

#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <sstream>

// Warning: This code is highly experimental
//          This code is currently not in use

namespace editor
{

namespace
{

auto sign(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) -> float
{
    return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

// auto point_in_triangle(glm::vec2 pt, glm::vec2 v1, glm::vec2 v2, glm::vec2 v3) -> bool
// {
//     const float d1 = sign(pt, v1, v2);
//     const float d2 = sign(pt, v2, v3);
//     const float d3 = sign(pt, v3, v1);
// 
//     const bool has_neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
//     const bool has_pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
// 
//     return !(has_neg && has_pos);
// }

auto orientation(glm::vec2 p1, glm::vec2 p2, glm::vec2 p3) -> int
{
    const float s = sign(p1, p2, p3);

    if (s == 0.0f) {
        return 0; // colinear
    }

    return (s > 0.0f) ? 1 : 2; // clock or counterclock wise
}

// Given three collinear points p, q, r, the function checks if
// point q lies on line segment 'pr'
auto on_segment(glm::vec2 p, glm::vec2 q, glm::vec2 r) -> bool
{
    return
        q.x <= std::max(p.x, r.x) &&
        q.x >= std::min(p.x, r.x) &&
        q.y <= std::max(p.y, r.y) &&
        q.y >= std::min(p.y, r.y);
}

// The main function that returns true if line segment 'p1q1'
// and 'p2q2' intersect.
auto line_line_intersection(
    glm::vec2 p1,
    glm::vec2 q1,
    glm::vec2 p2,
    glm::vec2 q2
) -> bool
{
    // Find the four orientations needed for general and
    // special cases
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    // General case
    if ((o1 != o2) && (o3 != o4)) {
        return true;
    }

    // Special Cases
    // p1, q1 and p2 are collinear and p2 lies on segment p1q1
    if ((o1 == 0) && on_segment(p1, p2, q1)) {
        return true;
    }

    // p1, q1 and q2 are collinear and q2 lies on segment p1q1
    if ((o2 == 0) && on_segment(p1, q2, q1)) {
        return true;
    }

    // p2, q2 and p1 are collinear and p1 lies on segment p2q2
    if ((o3 == 0) && on_segment(p2, p1, q2)) {
        return true;
    }

    // p2, q2 and q1 are collinear and q1 lies on segment p2q2
    if ((o4 == 0) && on_segment(p2, q1, q2)) {
        return true;
    }

    return false; // Doesn't fall in any of the above cases
}

auto polygon_polygon_intersection(
    const std::vector<glm::vec2>& polygon1,
    const std::vector<glm::vec2>& polygon2
) -> bool
{
    const std::size_t polygon1_size = polygon1.size();
    const std::size_t polygon2_size = polygon2.size();

    for (size_t i = 0; i < polygon1_size; ++i) {
        const glm::vec2 a = polygon1[i];
        const glm::vec2 b = polygon1[(i + 1) % polygon1_size];
        for (size_t j = 0; j < polygon2_size; ++j) {
            const glm::vec2 c = polygon2[j];
            const glm::vec2 d = polygon2[(j + 1) % polygon2_size];
            const bool intersection = line_line_intersection(a, b, c, d);
            if (intersection) {
                return true;
            }
        }
    }
    return false;
}

// returns true if the three points make a counter-clockwise turn
auto ccw(const glm::vec2 a, const glm::vec2 b, const glm::vec2 c) -> bool
{
    return ((b.x - a.x) * (c.y - a.y)) > ((b.y - a.y) * (c.x - a.x));
}

auto convex_hull(std::array<glm::vec2, 8>& p) -> std::vector<glm::vec2>
{
    if (p.size() == 0) {
        return {};
    }

    std::sort(
        p.begin(),
        p.end(),
        [](glm::vec2& lhs, glm::vec2& rhs) {
            return lhs.x < rhs.x;
        }
    );

    std::vector<glm::vec2> hull;

    // lower hull
    for (const glm::vec2& pt : p) {
        while (
            (hull.size() >= 2) &&
            !ccw(
                hull.at(hull.size() - 2),
                hull.at(hull.size() - 1),
                pt
            )
        ) {
            hull.pop_back();
        }
        hull.push_back(pt);
    }

    // upper hull
    const auto t = hull.size() + 1;
    for (
        auto it = p.crbegin();
        it != p.crend();
        it = std::next(it)
    ) {
        glm::vec2 pt = *it;
        while (
            (hull.size() >= t) &&
            !ccw(
                hull.at(hull.size() - 2),
                hull.at(hull.size() - 1),
                pt
            )
        ) {
            hull.pop_back();
        }
        hull.push_back(pt);
    }

    hull.pop_back();
    return hull;
}

// auto area(const std::array<glm::vec2, 8>& points) -> float
// {
//     float sum = 0.0f;
// 
//     std::size_t j = points.size() - 1;
//     for (std::size_t i = 0, end = points.size(); i < end; i++)
//     {
//         sum += (points[j].x + points[i].x) * (points[j].y - points[i].y);
//         j = i;
//     }
// 
//     return std::abs(sum / 2.0f);
// }

// auto convex_hull_o(const std::array<glm::vec2, 8>& points) -> std::vector<glm::vec2>
// {
//     std::vector<glm::vec2> result;
// 
//     // Find left most point
//     std::size_t left_most_index = 0;
//     for (std::size_t i = 1; i < 8; i++)
//     {
//         if (points[i].x < points[left_most_index].x)
//         {
//             left_most_index = i;
//         }
//     }
// 
//     std::size_t p = left_most_index;
//     do
//     {
//         result.push_back(points[p]);
//         std::size_t q = (p + 1) % 8;
//         for (std::size_t i = 0; i < 8; i++)
//         {
//             if (orientation(points[p], points[i], points[q]) == 2)
//             {
//                 q = i;
//             }
//             p = q;
//         }
//     }
//     while (p != left_most_index);
// 
//     return result;
// }

int point_inside_polygon(const std::vector<glm::vec2>& points, glm::vec2 p)
{
    bool result{false};
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        if (
            (
                (points[i].y > p.y) != (points[j].y > p.y)
            ) &&
            (
                p.x < (
                    points[j].x - points[i].x
                ) * (p.y - points[i].y) / (points[j].y - points[i].y) + points[i].x
            )
        ) {
            result = !result;
        }
    }
    return result;
}

}

Frustum_tiler::Frustum_tiler(
    const erhe::graphics::Texture& texture
)
    : m_texture     {texture}
    , m_tile_size   {texture.get_sparse_tile_size()}
    , m_tile_count_x{m_tile_size.x > 0 ? (texture.width()  / m_tile_size.x) : 0}
    , m_tile_count_y{m_tile_size.y > 0 ? (texture.height() / m_tile_size.y) : 0}
    , m_viewport {
        .x      = 0,
        .y      = 0,
        .width  = texture.width(),
        .height = texture.height()
    }
{
}

auto Frustum_tiler::point_to_tile(glm::vec2 p) const -> Tile
{
    return Tile{
        .x = static_cast<int>(p.x / static_cast<float>(m_tile_size.x)),
        .y = static_cast<int>(p.y / static_cast<float>(m_tile_size.y))
    };
}

auto Frustum_tiler::clamp(Tile tile) const -> Tile
{
    return Tile {
        .x = std::min(std::max(tile.x, 0), m_tile_count_x - 1),
        .y = std::min(std::max(tile.y, 0), m_tile_count_y - 1)
    };
}

auto Frustum_tiler::get_center(const std::vector<glm::vec2>& points) -> glm::vec2
{
    glm::vec2 center{0.0f, 0.0f};
    for (const glm::vec2 p : points) {
        center += p;
    }
    return center / static_cast<float>(points.size());
}

void Frustum_tiler::get_frustum_hull(
    const glm::mat4&              light_clip_from_world,
    const erhe::scene::Camera&    view_camera,
    const erhe::toolkit::Viewport view_camera_viewport
)
{
    static constexpr std::array<glm::vec3, 8> clip_space_points = {
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{ 1.0f, -1.0f, 0.0f},
        glm::vec3{ 1.0f,  1.0f, 0.0f},
        glm::vec3{-1.0f,  1.0f, 0.0f},
        glm::vec3{-1.0f, -1.0f, 1.0f},
        glm::vec3{ 1.0f, -1.0f, 1.0f},
        glm::vec3{ 1.0f,  1.0f, 1.0f},
        glm::vec3{-1.0f,  1.0f, 1.0f}
    };

    // From view camera clip space to light camera clip
    const glm::mat4  world_from_view_camera_clip = view_camera.projection_transforms(view_camera_viewport).clip_from_world.get_inverse_matrix();
    const glm::mat4& m = light_clip_from_world * world_from_view_camera_clip;
    const float w = static_cast<float>(m_viewport.width);
    const float h = static_cast<float>(m_viewport.height);

    std::array<glm::vec2, 8> window_space_points = {
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[0], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[1], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[2], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[3], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[4], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[5], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[6], w, h),
        erhe::toolkit::project_to_screen_space_2d<float>(m, clip_space_points[7], w, h)
    };


    {
        std::stringstream ss;
        for (const glm::vec2 p : window_space_points) {
            ss << fmt::format("{}, {}; ", p.x, p.y);
        }
        SPDLOG_LOGGER_TRACE(
            log_render,
            "frustum convex points (area = {}) : {} ",
            area(window_space_points),
            ss.str()
        );
    }
    m_frustum_hull_points = convex_hull(window_space_points);
    m_frustum_hull_center = get_center(m_frustum_hull_points);

    {
        std::stringstream ss;
        for (const glm::vec2 p : m_frustum_hull_points) {
            ss << fmt::format("{} ", p);
        }
        SPDLOG_LOGGER_TRACE(
            log_render,
            "frustum convex hull points = {}",
            ss.str()
        );
    }

    SPDLOG_LOGGER_TRACE(
        log_render,
        "camera view frustum hull center = {}",
        m_frustum_hull_center
    );

}

auto Frustum_tiler::get_tile_hull(Tile tile) const -> std::vector<glm::vec2>
{
    float left   = static_cast<float>(tile.x * m_tile_size.x);
    float bottom = static_cast<float>(tile.y * m_tile_size.y);
    float right  = left   + static_cast<float>(m_tile_size.x);
    float top    = bottom + static_cast<float>(m_tile_size.y);
    std::vector<glm::vec2> result;
    result.emplace_back(left, bottom);
    result.emplace_back(right, bottom);
    result.emplace_back(right, top);
    result.emplace_back(left, top);
    return result;
}

auto Frustum_tiler::get_tile_center(Tile tile) const -> glm::vec2
{
    return glm::vec2{
        (static_cast<float>(tile.x) + 0.5f) * static_cast<float>(m_tile_size.x),
        (static_cast<float>(tile.y) + 0.5f) * static_cast<float>(m_tile_size.y)
    };
}

auto Frustum_tiler::frustum_tile_intersection(Tile tile) const -> bool
{
    const std::vector<glm::vec2> tile_hull   = get_tile_hull  (tile);
    const glm::vec2              tile_center = get_tile_center(tile);

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
    float left   = static_cast<float>(tile.x * m_tile_size.x);
    float bottom = static_cast<float>(tile.y * m_tile_size.y);
    float right  = left   + static_cast<float>(m_tile_size.x);
    float top    = bottom + static_cast<float>(m_tile_size.y);

    SPDLOG_LOGGER_TRACE(
        log_render,
        "considering tile {}, {} = {}, {} .. {}, {} | center = {}",
        tile.x,
        tile.y,
        left,
        bottom,
        right,
        top,
        tile_center
    );
#endif

    // 1. tile is outside of frustum hull
    // 2. tile intersects frustum hull
    // 3. tile is inside of frustum hull
    // 4. frustum hull is inside of Rect

    // Test tile center inside frustum - rule out case 3
    const bool tile_center_in_frustum = point_inside_polygon(m_frustum_hull_points, tile_center);
    if (tile_center_in_frustum) {
        SPDLOG_LOGGER_TRACE(log_render, "    tile center is in frustum");
        return true;
    }

    // Test frustum center inside tile - rule out case 4
    const bool frustum_center_in_tile = point_inside_polygon(tile_hull, m_frustum_hull_center);
    if (frustum_center_in_tile) {
        SPDLOG_LOGGER_TRACE(log_render, "    frustum center is in tile");
        return true;
    }

    const bool has_edge_intersection = polygon_polygon_intersection(tile_hull, m_frustum_hull_points);
    if (has_edge_intersection) {
        SPDLOG_LOGGER_TRACE(log_render, "    there is edge intersection");
    } else {
        SPDLOG_LOGGER_TRACE(log_render, "    there is no edge intersection");
    }

    return has_edge_intersection;
}

void Frustum_tiler::update(
    int                           texture_z,
    const glm::mat4&              clip_from_world,
    const erhe::scene::Camera&    view_camera,
    const erhe::toolkit::Viewport view_camera_viewport
)
{
    if (
        (m_tile_size.x == 0) ||
        (m_tile_size.y == 0)
    ) {
        SPDLOG_LOGGER_TRACE(log_render, "sparse texture not enabled");
        return;
    }

    // Uncommit the whole slice
    gl::texture_page_commitment_ext(
        m_texture.gl_name(),
        0,          // level
        0,          // x offset
        0,          // y offset,
        texture_z,  // z offset
        m_texture.width(),
        m_texture.height(),
        1,
        GL_FALSE
    );

    get_frustum_hull(
        clip_from_world,
        view_camera,
        view_camera_viewport
    );

    glm::vec2 min_corner_point = m_frustum_hull_points.front();
    glm::vec2 max_corner_point = m_frustum_hull_points.front();
    for (const glm::vec2 p : m_frustum_hull_points) {
        min_corner_point.x = std::min(min_corner_point.x, p.x);
        min_corner_point.y = std::min(min_corner_point.y, p.y);
        max_corner_point.x = std::max(max_corner_point.x, p.x);
        max_corner_point.y = std::max(max_corner_point.y, p.y);
    }
    const Tile min_corner_tile = clamp(point_to_tile(min_corner_point));
    const Tile max_corner_tile = clamp(point_to_tile(max_corner_point));

    SPDLOG_LOGGER_TRACE(
        log_render,
        "tile bbox min = {}, {} - max = {}, {}",
        min_corner_tile.x,
        min_corner_tile.y,
        max_corner_tile.x,
        max_corner_tile.y
    );

    const Tile start_tile  = clamp(point_to_tile(m_frustum_hull_points.front()));
    int  top_tile_y        = start_tile.y;
    int  bottom_tile_y     = start_tile.y;
    bool top_going_up      = true;
    bool bottom_going_down = true;

    // Scan tile from left to right
    int tile_count = 0;
    for (int x = min_corner_tile.x; x <= max_corner_tile.x; ++x) {
        // Scan top
        if (top_going_up) {
            if (!frustum_tile_intersection(Tile{x, top_tile_y})) {
                top_going_up = false;
            } else {
                while (
                    ((top_tile_y + 1) <= max_corner_tile.y) &&
                    frustum_tile_intersection(Tile{x, top_tile_y + 1})
                ) {
                    ++top_tile_y;
                }
            }
        }
        if (!top_going_up) {
            while (
                ((top_tile_y - 1) >= min_corner_tile.y) &&
                !frustum_tile_intersection(Tile{x, top_tile_y - 1})
            ) {
                --top_tile_y;
            }
        }

        // Scan bottom
        if (bottom_going_down) {
            if (!frustum_tile_intersection(Tile{x, bottom_tile_y})) {
                bottom_going_down = false;
            } else {
                while (
                    ((bottom_tile_y - 1) >= min_corner_tile.y) &&
                    frustum_tile_intersection(Tile{x, bottom_tile_y - 1})
                ) {
                    --bottom_tile_y;
                }
            }
        }
        if (!bottom_going_down) {
            while (
                ((bottom_tile_y + 1) <= max_corner_tile.y) &&
                !frustum_tile_intersection(Tile{x, bottom_tile_y + 1})
            ) {
                ++bottom_tile_y;
            }
        }

        if (bottom_tile_y <= top_tile_y) {
            const GLint   x_offset        = x * m_tile_size.x;
            const GLint   y_offset        = bottom_tile_y * m_tile_size.y;
            const GLsizei width           = m_tile_size.x;
            const GLsizei height          = (top_tile_y - bottom_tile_y + 1) * m_tile_size.y;
            const int     span_tile_count = (top_tile_y - bottom_tile_y + 1);
            SPDLOG_LOGGER_TRACE(
                log_render,
                "include tile x {} y {} - {}: offset {}, {} size {}, {} ({} tiles)",
                x,
                top_tile_y,
                bottom_tile_y,
                x_offset,
                y_offset,
                width,
                height,
                span_tile_count
            );

            tile_count += span_tile_count;
            gl::texture_page_commitment_ext(
                m_texture.gl_name(),
                0,          // level
                x_offset,
                y_offset,
                texture_z,  // z offset
                width,
                height,
                1,          // depth
                GL_TRUE     // commitment
            );
        }
    }

    const int   texture_area = m_texture.width() * m_texture.height();
    const int   used_area    = tile_count * m_tile_size.x * m_tile_size.y;
    const float usage        = 100.0f * static_cast<float>(used_area) / static_cast<float>(texture_area);
    log_render->info(
        "committed {} tiles, usage {}%",
        tile_count,
        usage
    );
}

} // namespace editor
#endif