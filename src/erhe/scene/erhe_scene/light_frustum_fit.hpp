#pragma once

#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene {

// Per-light intermediate results of the tight shadow frustum fit
// (Light::tight_directional_light_projection_transforms() in
// light_frustum_fit.cpp), collected only when
// Shadow_frustum_fit_settings::collect_debug is enabled, for use by debug
// visualization.
class Shadow_frustum_fit_debug_data
{
public:
    std::vector<glm::vec4>      shadow_volume_planes;         // F_shadow planes, world space, inward-facing
    std::vector<glm::vec3>      fit_points;                   // world space point set the light box was fitted to (clipped caster hull or view frustum corners)
    std::vector<glm::vec2>      light_plane_hull;             // 2D convex hull of fit_points on the light plane; coordinates in world_from_light_plane space
    erhe::math::Min_area_obb_2d obb{};                        // rotating calipers result on light_plane_hull (when optimize_rotation is enabled)
    glm::mat4                   world_from_light_plane{1.0f}; // maps light plane points (x, y, 0, 1) to world space
    bool                        valid{false};
};

} // namespace erhe::scene
