#if 0
#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <vector>

// Warning: This code is highly experimental
//          This code is currently not in use

namespace erhe::graphics {
    class Texture;
};
namespace erhe::scene {
    class Camera;
};

namespace editor
{

class Tile
{
public:
    int x;
    int y;
};

class Frustum_tiler
{
public:
    const erhe::graphics::Texture&  m_texture;
    const erhe::graphics::Tile_size m_tile_size;
    const int                       m_tile_count_x;
    const int                       m_tile_count_y;
    const erhe::math::Viewport      m_viewport;
    std::vector<glm::vec2>          m_frustum_hull_points;
    glm::vec2                       m_frustum_hull_center;

    explicit Frustum_tiler(
        const erhe::graphics::Texture& texture
    );

    auto point_to_tile(glm::vec2 p) const -> Tile;
    auto clamp        (Tile tile) const -> Tile;

    static auto get_center(const std::vector<glm::vec2>& points) -> glm::vec2;

    void get_frustum_hull(
        const glm::mat4&           light_clip_from_world,
        const erhe::scene::Camera& view_camera,
        const erhe::math::Viewport view_camera_viewport
    );

    auto get_tile_hull  (Tile tile) const -> std::vector<glm::vec2>;
    auto get_tile_center(Tile tile) const -> glm::vec2;

    auto frustum_tile_intersection(Tile tile) const -> bool;

    void update(
        int                        texture_z,
        const glm::mat4&           clip_from_world,
        const erhe::scene::Camera& view_camera,
        const erhe::math::Viewport view_camera_viewport
    );
};

} // namespace editor
#endif
