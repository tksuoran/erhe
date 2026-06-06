// Tight, modular shadow frustum fit for directional lights.
//
// Implements the optional tightening steps of Light::projection_transforms()
// (see Shadow_frustum_fit_settings in light.hpp). The base case with all
// tightening steps disabled is Light::stable_directional_light_projection_transforms()
// in light.cpp; this file is only entered when at least one tightening step
// is enabled.
//
// Pipeline (each step optional):
//  1. Build the main camera view frustum F_main (planes and corners).
//  2. fit_to_casters: build a convex hull around the shadow caster bounds,
//     clip it to the shadow caster volume F_shadow (F_main extruded toward
//     the light), and use the clipped point set for fitting.
//  3. fit_to_view_frustum: constrain the fit with the F_main corner point
//     box (intersection when casters are also fitted).
//  4. optimize_rotation: rotating calipers on the fit points projected onto
//     the light plane; rolls the light camera around the light direction to
//     minimize the covered area.
//  5. near_from_main_frustum: pull the light-space near plane down to the
//     view frustum extent; casters closer to the light are preserved by
//     depth clamping in the shadow pass (Shadow_frustum_fit_settings::depth_clamp).
//  6. Stabilization: quantize_extents rounds the box size up to discrete
//     steps, texel_snap snaps the box to shadow map texels.

#include "erhe_scene/light_frustum_fit.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace erhe::scene {

namespace {

// Axis aligned box in the (optionally rolled) light frame:
// xy on the light plane, s along the light direction (toward the light).
class Light_space_box
{
public:
    glm::vec2 xy_min{0.0f};
    glm::vec2 xy_max{0.0f};
    float     s_min {0.0f};
    float     s_max {0.0f};
    bool      valid {false};
};

[[nodiscard]] auto fit_box(
    const std::vector<glm::vec3>& points_in_world,
    const glm::mat3&              light_from_world,
    const glm::mat2&              rolled_from_light_xy,
    const glm::vec3&              light_direction_in_light
) -> Light_space_box
{
    Light_space_box box{};
    if (points_in_world.empty()) {
        return box;
    }
    box.xy_min = glm::vec2{std::numeric_limits<float>::max()};
    box.xy_max = glm::vec2{std::numeric_limits<float>::lowest()};
    box.s_min  = std::numeric_limits<float>::max();
    box.s_max  = std::numeric_limits<float>::lowest();
    for (const glm::vec3& p_in_world : points_in_world) {
        const glm::vec3 p_in_light  = light_from_world * p_in_world;
        const glm::vec2 p_in_rolled = rolled_from_light_xy * glm::vec2{p_in_light};
        const float     s           = glm::dot(p_in_light, light_direction_in_light);
        box.xy_min = glm::min(box.xy_min, p_in_rolled);
        box.xy_max = glm::max(box.xy_max, p_in_rolled);
        box.s_min  = std::min(box.s_min, s);
        box.s_max  = std::max(box.s_max, s);
    }
    box.valid = true;
    return box;
}

constexpr float min_box_extent = 0.01f; // meters; keeps degenerate (flat) fits renderable

} // anonymous namespace

auto Light::tight_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_VERIFY(parameters.view_camera != nullptr);
    ERHE_VERIFY(parameters.fit_settings != nullptr);
    const Shadow_frustum_fit_settings& settings = *parameters.fit_settings;

    const Node* const light_node = get_node();
    ERHE_VERIFY(light_node != nullptr);
    const Node* const view_camera_node = parameters.view_camera->get_node();
    ERHE_VERIFY(view_camera_node != nullptr);

    Shadow_frustum_fit_debug_data* const debug_out = settings.collect_debug ? parameters.fit_debug_out : nullptr;
    if (debug_out != nullptr) {
        *debug_out = Shadow_frustum_fit_debug_data{};
    }

    // Light frame - rotation only transform, same convention as the stable fit.
    // light_direction points from the scene toward the light (node +Z axis).
    const glm::vec3 light_direction = glm::vec3{light_node->direction_in_world()};
    const glm::vec3 light_up_vector = glm::vec3{light_node->world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::mat3 world_from_light{light_node->world_from_node()};
    const glm::mat3 light_from_world{light_node->node_from_world()};
    const glm::vec3 light_direction_in_light = light_from_world * light_direction; // == +Z for a pure rotation light node

    // Step 1: Main camera view frustum F_main
    const auto camera_projection_transforms = parameters.view_camera->projection_transforms(
        parameters.main_camera_viewport, parameters.reverse_depth, parameters.depth_range, parameters.conventions
    );
    const glm::mat4 main_clip_from_world = camera_projection_transforms.clip_from_world.get_matrix();
    const glm::mat4 world_from_main_clip = camera_projection_transforms.clip_from_world.get_inverse_matrix();
    const std::array<glm::vec4, 6> main_frustum_planes  = erhe::math::extract_frustum_planes (main_clip_from_world, 0.0f, 1.0f);
    const std::array<glm::vec3, 8> main_frustum_corners = erhe::math::extract_frustum_corners(world_from_main_clip, 0.0f, 1.0f);
    const std::vector<glm::vec3> frustum_corner_points{main_frustum_corners.begin(), main_frustum_corners.end()};

    // Steps 2-3: Caster point set - convex hull of the caster bounds clipped
    // to the shadow caster volume F_shadow
    std::vector<glm::vec3> caster_points;
    bool casters_available = false;
    if (settings.fit_to_casters && (parameters.caster_world_points.size() >= 4)) {
        erhe::math::Point_vector_bounding_volume_source point_source{parameters.caster_world_points.size()};
        for (const glm::vec3& p : parameters.caster_world_points) {
            point_source.add(p);
        }
        const erhe::math::Convex_hull caster_hull = erhe::math::calculate_bounding_convex_hull(point_source);
        if (!caster_hull.triangle_indices.empty()) {
            const erhe::math::Shadow_volume_planes shadow_volume = erhe::math::build_shadow_caster_volume_planes(main_frustum_planes, light_direction);
            caster_points = erhe::math::clip_convex_hull_points_by_planes(caster_hull, shadow_volume.planes);
            // The clip produces only points on the caster hull surface; when
            // the hull contains F_shadow corner regions (e.g. one large caster
            // enclosing the whole view frustum) the F_main corners inside the
            // hull restore the missing pure plane intersection vertices.
            for (const glm::vec3& corner : main_frustum_corners) {
                if (erhe::math::point_in_convex_hull(caster_hull, corner)) {
                    caster_points.push_back(corner);
                }
            }
            casters_available = !caster_points.empty();
            if (debug_out != nullptr) {
                debug_out->shadow_volume_planes = shadow_volume.planes;
                debug_out->caster_hull          = caster_hull;
            }
        }
    }

    if (!casters_available && !settings.fit_to_view_frustum) {
        // fit_to_casters was requested but produced nothing to fit to
        return stable_directional_light_projection_transforms(parameters);
    }

    const std::vector<glm::vec3>& primary_fit_points = casters_available ? caster_points : frustum_corner_points;

    // Step 4: Optional roll around the light direction (rotating calipers)
    glm::mat2 rolled_from_light_xy{1.0f};
    erhe::math::Min_area_obb_2d obb{};
    std::vector<glm::vec2> light_plane_hull;
    if (settings.optimize_rotation) {
        std::vector<glm::vec2> projected_points;
        projected_points.reserve(primary_fit_points.size());
        for (const glm::vec3& p_in_world : primary_fit_points) {
            const glm::vec3 p_in_light = light_from_world * p_in_world;
            projected_points.push_back(glm::vec2{p_in_light});
        }
        light_plane_hull = erhe::math::calculate_bounding_convex_hull(projected_points);
        if (light_plane_hull.size() >= 3) {
            obb = erhe::math::calculate_min_area_obb_2d(light_plane_hull);
            rolled_from_light_xy = obb.edge_from_original;
        }
    }

    // Records the light space box after a pipeline stage for debug visualization
    auto record_step_box = [debug_out](const Shadow_fit_step step, const Light_space_box& b) {
        if (debug_out == nullptr) {
            return;
        }
        Shadow_frustum_fit_debug_data::Step_box& out = debug_out->step_boxes[static_cast<std::size_t>(step)];
        out.min   = glm::vec3{b.xy_min.x, b.xy_min.y, b.s_min};
        out.max   = glm::vec3{b.xy_max.x, b.xy_max.y, b.s_max};
        out.valid = b.valid;
    };

    // Fit the light space box to the active point sets
    Light_space_box box = fit_box(primary_fit_points, light_from_world, rolled_from_light_xy, light_direction_in_light);
    record_step_box(Shadow_fit_step::fit_points, box);
    if (casters_available && (settings.fit_to_view_frustum || settings.near_from_main_frustum)) {
        const Light_space_box frustum_box = fit_box(frustum_corner_points, light_from_world, rolled_from_light_xy, light_direction_in_light);
        if (settings.fit_to_view_frustum) {
            // Receivers are inside the view frustum: laterally (and below) the
            // fit only needs to cover the caster / frustum intersection.
            box.xy_min = glm::max(box.xy_min, frustum_box.xy_min);
            box.xy_max = glm::min(box.xy_max, frustum_box.xy_max);
            box.s_min  = std::max(box.s_min,  frustum_box.s_min);
        }
        if (settings.near_from_main_frustum) {
            // Step 5: near plane from the view frustum; casters closer to the
            // light rely on depth clamping in the shadow pass.
            box.s_max = std::min(box.s_max, frustum_box.s_max);
        }
        record_step_box(Shadow_fit_step::frustum_constraint, box);
    }

    // Safety cap: never exceed the stable shadow-range box
    const float shadow_range = parameters.view_camera->get_shadow_range();
    if (settings.cap_by_shadow_range) {
        const glm::vec3 view_camera_position_in_light = light_from_world * glm::vec3{view_camera_node->position_in_world()};
        const glm::vec2 view_camera_xy_in_rolled      = rolled_from_light_xy * glm::vec2{view_camera_position_in_light};
        const float     view_camera_s                 = glm::dot(view_camera_position_in_light, light_direction_in_light);
        box.xy_min = glm::max(box.xy_min, view_camera_xy_in_rolled - glm::vec2{shadow_range});
        box.xy_max = glm::min(box.xy_max, view_camera_xy_in_rolled + glm::vec2{shadow_range});
        box.s_min  = std::max(box.s_min, view_camera_s - shadow_range);
        box.s_max  = std::min(box.s_max, view_camera_s + shadow_range);
        record_step_box(Shadow_fit_step::shadow_range_cap, box);
    }

    // Disjoint constraints mean no visible receiver / caster overlap;
    // fall back to the stable fit instead of producing a degenerate box.
    if (!box.valid || (box.xy_min.x > box.xy_max.x) || (box.xy_min.y > box.xy_max.y) || (box.s_min > box.s_max)) {
        return stable_directional_light_projection_transforms(parameters);
    }

    // Step 6: Stabilization - quantize extents, then snap to shadow map texels
    glm::vec2 box_size = glm::max(box.xy_max - box.xy_min, glm::vec2{min_box_extent});
    if (settings.quantize_extents) {
        const float quantize_step = (settings.quantize_step > 0.0f)
            ? settings.quantize_step
            : (2.0f * shadow_range) / 16.0f;
        box_size.x = std::ceil(box_size.x / quantize_step) * quantize_step;
        box_size.y = std::ceil(box_size.y / quantize_step) * quantize_step;
    }
    glm::vec2 box_xy_min = box.xy_min;
    if (settings.texel_snap) {
        // Pad by two texels so snapping the min corner down never drops
        // coverage at the max edge, then snap on the padded texel grid. The
        // padded size and grid stay constant whenever box_size is constant
        // (see quantize_extents), which is what makes the snap effective.
        const glm::vec2 viewport_size{
            static_cast<float>(parameters.shadow_map_viewport.width),
            static_cast<float>(parameters.shadow_map_viewport.height)
        };
        box_size += 2.0f * (box_size / viewport_size);
        const glm::vec2 texel_size = box_size / viewport_size;
        box_xy_min = glm::floor(box_xy_min / texel_size) * texel_size;
    }
    const glm::vec2 box_xy_max = box_xy_min + box_size;
    const float     s_extent   = std::max(box.s_max - box.s_min, min_box_extent);

    // Assemble the light camera: centered on the box, on its light-most face,
    // looking toward the scene, with the optionally rolled up vector.
    const glm::vec2 center_in_rolled   = 0.5f * (box_xy_min + box_xy_max);
    const glm::vec2 center_in_light_xy = glm::transpose(rolled_from_light_xy) * center_in_rolled;
    const glm::vec3 eye_in_light       = glm::vec3{center_in_light_xy.x, center_in_light_xy.y, 0.0f} + box.s_max * light_direction_in_light;
    const glm::vec3 eye_in_world       = world_from_light * eye_in_light;
    const glm::vec3 target_in_world    = eye_in_world - light_direction;

    glm::vec3 up_in_world = light_up_vector;
    if (settings.optimize_rotation) {
        const glm::vec2 up_in_light_xy = glm::transpose(rolled_from_light_xy) * glm::vec2{0.0f, 1.0f};
        up_in_world = world_from_light * glm::vec3{up_in_light_xy.x, up_in_light_xy.y, 0.0f};
    }

    const glm::mat4 world_from_light_camera = erhe::math::create_look_at(eye_in_world, target_in_world, up_in_world);
    const glm::mat4 light_camera_from_world = glm::inverse(world_from_light_camera);

    const Projection light_projection{
        .projection_type = Projection::Type::orthogonal,
        .z_near          = 0.0f,
        .z_far           = s_extent,
        .ortho_width     = box_size.x,
        .ortho_height    = box_size.y
    };

    if (debug_out != nullptr) {
        debug_out->fit_points         = primary_fit_points;
        debug_out->light_plane_hull   = light_plane_hull;
        debug_out->obb                = obb;
        debug_out->caster_aabb_points.assign(parameters.caster_world_points.begin(), parameters.caster_world_points.end());
        debug_out->receiver_corners       = main_frustum_corners;
        debug_out->receiver_corners_valid = true;

        glm::mat4 world_from_light_plane{world_from_light};
        world_from_light_plane[3] = glm::vec4{world_from_light * (box.s_max * light_direction_in_light), 1.0f};
        debug_out->world_from_light_plane = world_from_light_plane;

        // Fit frame: x / y on the rolled light plane, z along the light direction
        const glm::mat2 light_xy_from_rolled = glm::transpose(rolled_from_light_xy);
        const glm::vec2 x_axis_in_light_xy   = light_xy_from_rolled * glm::vec2{1.0f, 0.0f};
        const glm::vec2 y_axis_in_light_xy   = light_xy_from_rolled * glm::vec2{0.0f, 1.0f};
        debug_out->world_from_fit_frame = glm::mat4{
            glm::vec4{world_from_light * glm::vec3{x_axis_in_light_xy.x, x_axis_in_light_xy.y, 0.0f}, 0.0f},
            glm::vec4{world_from_light * glm::vec3{y_axis_in_light_xy.x, y_axis_in_light_xy.y, 0.0f}, 0.0f},
            glm::vec4{world_from_light * light_direction_in_light, 0.0f},
            glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}
        };

        // Final (stabilized) box; matches the assembled light projection
        Light_space_box stabilized_box{};
        stabilized_box.xy_min = box_xy_min;
        stabilized_box.xy_max = box_xy_max;
        stabilized_box.s_min  = box.s_max - s_extent;
        stabilized_box.s_max  = box.s_max;
        stabilized_box.valid  = true;
        record_step_box(Shadow_fit_step::stabilized, stabilized_box);

        debug_out->valid = true;
    }

    return assemble_directional_light_projection_transforms(parameters, light_projection, world_from_light_camera, light_camera_from_world);
}

} // namespace erhe::scene
