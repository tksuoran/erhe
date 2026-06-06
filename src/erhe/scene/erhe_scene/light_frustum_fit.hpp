#pragma once

#include "erhe_math/math_util.hpp"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace erhe::scene {

// Stages of the tight shadow frustum fit whose intermediate light space box
// is recorded into Shadow_frustum_fit_debug_data::step_boxes.
enum class Shadow_fit_step : unsigned int {
    fit_points = 0,     // box fitted around the fit point set
    frustum_constraint, // after view frustum intersection / near from view frustum
    shadow_range_cap,   // after cap by shadow range
    stabilized,         // after quantize extents + texel snap; matches the final light projection
    count
};

// Per-light intermediate results of the tight shadow frustum fit
// (Light::tight_directional_light_projection_transforms() in
// light_frustum_fit.cpp), collected only when
// Shadow_frustum_fit_settings::collect_debug is enabled, for use by debug
// visualization.
class Shadow_frustum_fit_debug_data
{
public:
    // Light space box snapshot in world_from_fit_frame space:
    // x / y on the (optionally rolled) light plane, z along the light direction.
    class Step_box
    {
    public:
        glm::vec3 min  {0.0f};
        glm::vec3 max  {0.0f};
        bool      valid{false}; // false when the corresponding step did not run
    };

    std::vector<glm::vec4>      shadow_volume_planes;         // F_shadow planes, world space, inward-facing
    std::vector<glm::vec3>      caster_aabb_points;           // world space caster bounds, 8 AABB corners per caster (min corner first, max corner last)
    erhe::math::Convex_hull     caster_hull;                  // convex hull around caster_aabb_points, before clipping to F_shadow
    std::array<glm::vec3, 8>    receiver_corners{};           // main camera view frustum (receiver volume) corners, world space
    bool                        receiver_corners_valid{false};
    std::vector<glm::vec3>      fit_points;                   // world space point set the light box was fitted to (clipped caster hull or view frustum corners)
    std::vector<glm::vec2>      light_plane_hull;             // 2D convex hull of fit_points on the light plane; coordinates in world_from_light_plane space
    erhe::math::Min_area_obb_2d obb{};                        // rotating calipers result on light_plane_hull (when optimize_rotation is enabled)
    glm::mat4                   world_from_light_plane{1.0f}; // maps light plane points (x, y, 0, 1) to world space
    glm::mat4                   world_from_fit_frame  {1.0f}; // maps step_boxes coordinates (x, y, s, 1) to world space
    std::array<Step_box, static_cast<std::size_t>(Shadow_fit_step::count)> step_boxes{};
    bool                        valid{false};
};

} // namespace erhe::scene
