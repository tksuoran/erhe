#pragma once

#include "erhe_math/aabb.hpp"
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

    // Per-receiver world AABB the fit received (parameters.receiver_world_aabbs)
    // and whether it passed the aabb_in_frustum filter (and so contributed its
    // corners to the receiver hull).
    class Receiver_box
    {
    public:
        erhe::math::Aabb aabb      {};
        bool             in_frustum{false};
    };

    // Per-caster world AABB the fit received (parameters.caster_world_aabbs)
    // and its cull verdict. rejecting_plane is the index of the first plane
    // with all 8 AABB corners outside: into shadow_volume filter planes when
    // culled_by_shadow_volume, into receiver_filter_planes when
    // culled_by_receiver_volume (there 0 is the far cap and i >= 1 is the
    // silhouette side plane swept from receiver_far_plane_hull edge i-1 -> i).
    class Caster_box
    {
    public:
        erhe::math::Aabb aabb                     {};
        bool             culled_by_shadow_volume  {false};
        bool             culled_by_receiver_volume{false};
        int              rejecting_plane          {-1};
    };

    std::vector<glm::vec4>      shadow_volume_planes;         // caster-cull filter planes, world space, inward-facing; the debug viz classifies caster AABBs against these and reconstructs the bounded volume from them
    std::vector<glm::vec3>      receiver_far_plane_hull;      // world-space silhouette polygon (2D hull of the clipped receiver hull projected along the light onto the flat far cap, lifted to 3D); the loop the receiver caster-cull volume side planes are swept from; empty unless fit_to_receivers_hull built a valid silhouette

    // Receiver-cull diagnostics (fit_to_receivers): the fit's actual receiver
    // input set, the intermediate point sets of the cull volume build, and the
    // per-caster cull verdicts - for diagnosing receivers dropped from the
    // receiver hull / casters wrongly culled by the receiver volume.
    std::vector<Receiver_box>   receiver_boxes;               // every receiver AABB the fit received, with its in-frustum verdict
    std::vector<glm::vec3>      receiver_hull_points;         // unclipped receiver hull vertices (hull of the in-frustum receiver AABB corners)
    std::vector<glm::vec3>      receiver_clipped_points;      // clip_convex_hull_points_to_frustum output (before re-hull)
    std::vector<glm::vec3>      receiver_clipped_hull_points; // vertices of the re-hulled clipped point set (the body the silhouette is projected from)
    std::vector<glm::vec4>      receiver_filter_planes;       // the receiver cull volume planes alone (also appended to shadow_volume_planes); plane 0 is the far cap, the rest are silhouette side planes in receiver_far_plane_hull edge order
    bool                        receiver_hull_path_used{false}; // the hull path produced the planes
    bool                        receiver_box_path_used {false}; // the light-space bounding box fallback produced the planes
    std::vector<Caster_box>     caster_boxes;                 // every caster AABB the fit received, with its cull verdict
    erhe::math::Convex_hull     caster_hull;                  // convex hull around the surviving caster bounds (visualization only; the fit clips per box and builds this hull just for this debug data)
    std::array<glm::vec4, 6>    view_frustum_planes{};        // main camera view frustum planes (truncated to shadow range), world space, inward-facing; receivers are filtered against these
    std::array<glm::vec3, 8>    view_frustum_corners{};       // main camera view frustum corners (truncated to shadow range), world space
    bool                        view_frustum_corners_valid{false};
    std::vector<glm::vec3>      fit_points;                   // world space point set the light box was fitted to (per-box clipped caster points or view frustum corners)
    std::vector<glm::vec2>      light_plane_hull;             // 2D convex hull of fit_points on the light plane; coordinates in world_from_light_plane space
    erhe::math::Min_area_obb_2d obb{};                        // rotating calipers result on light_plane_hull (when optimize_rotation is enabled)
    glm::mat4                   world_from_light_plane{1.0f}; // maps light plane points (x, y, 0, 1) to world space
    glm::mat4                   world_from_fit_frame  {1.0f}; // maps step_boxes coordinates (x, y, s, 1) to world space
    std::array<Step_box, static_cast<std::size_t>(Shadow_fit_step::count)> step_boxes{};
    bool                        valid{false};
};

// Cross-light cache for the light-independent part of the receiver cull
// volume build: the in-frustum receiver AABB corner set and (on the hull
// path) the convex hull of those corners clipped to the view frustum and
// re-hulled. None of this depends on the light, so Light_projections::apply()
// resets one instance per pass (valid = false; buffers keep their capacity)
// and every tight directional fit of that pass shares it via
// Light_projection_parameters::receiver_cache. Only the silhouette projection
// and plane sweep remain per light. Across passes the cache additionally
// fingerprints its inputs (see the temporal-reuse fields below) so the
// receiver hulls are recomputed only when the corner set or frustum actually
// changed. The debug intermediates are filled only when the filling fit
// collects debug data (collect_debug is uniform across the lights of one
// pass).
class Shadow_fit_receiver_cache
{
public:
    bool                    valid     {false}; // filled this pass
    bool                    hull_valid{false}; // clipped_hull holds a usable hull (hull path)
    std::vector<glm::vec3>  receiver_points;   // corners of the in-frustum receiver AABBs (box fallback input)
    erhe::math::Convex_hull clipped_hull;      // re-hulled (receiver hull intersected with view frustum); meaningful only when hull_valid

    // Previous-pass fingerprint for temporal reuse (see ensure_receiver_cache):
    // with an unchanged in-frustum corner set the corner hull is reused, and
    // with an unchanged view frustum on top the whole cached result is.
    // has_result guards the very first fill; result_frustum_corners fully
    // identifies the frustum (the planes derive from the same matrix).
    bool                     has_result     {false};
    bool                     result_use_hull{false};
    std::array<glm::vec3, 8> result_frustum_corners{};

    // The unclipped receiver corner hull. Scratch of the cache fill, but also
    // reused across passes when the corner set is unchanged (temporal reuse
    // above), so it must stay the hull of receiver_points between fills.
    erhe::math::Convex_hull receiver_hull_scratch;

    // Pure scratch (persist only so capacity carries across passes): the
    // clipped point set the receiver hull is re-hulled from, and the gather
    // buffer the corner set is collected into before it is compared against /
    // swapped into receiver_points.
    std::vector<glm::vec3>  clipped_points_scratch;
    std::vector<glm::vec3>  gather_scratch;

    // Debug intermediates, copied into each light's Shadow_frustum_fit_debug_data
    std::vector<Shadow_frustum_fit_debug_data::Receiver_box> receiver_boxes;
    std::vector<glm::vec3>  receiver_hull_points;
    std::vector<glm::vec3>  receiver_clipped_points;
    std::vector<glm::vec3>  receiver_clipped_hull_points;
};

// Persistent scratch buffers for the per-light tight fit
// (Light::tight_directional_light_projection_transforms in
// light_frustum_fit.cpp). Each buffer is cleared (capacity kept) at its point
// of use, so per-frame heap traffic stops once steady-state capacity is
// reached. Owned by Light_projections alongside Shadow_fit_receiver_cache and
// shared by all fits of one pass via
// Light_projection_parameters::fit_scratch; no state carries over between
// fits.
class Shadow_fit_scratch
{
public:
    std::vector<glm::vec4>        filter_planes;           // F_shadow per-caster cull planes (volume + silhouette)
    std::vector<glm::vec4>        receiver_filter_planes;  // receiver cull volume planes
    std::vector<glm::vec3>        receiver_far_plane_hull; // receiver cull silhouette polygon; filled only when collect_debug is on
    std::vector<erhe::math::Aabb> surviving_caster_aabbs;  // casters that passed the F_shadow (and receiver volume) cull
    erhe::math::Convex_hull       caster_box_hull;         // one caster box as a fixed-topology hull (8 corners, constant 12-triangle table), reused per box
    std::vector<glm::vec3>        box_clip_points;         // per-box Sutherland-Hodgman clip output
    std::vector<glm::vec3>        caster_corner_points;    // surviving caster AABB corners; filled only when collect_debug is on (debug caster hull input)
    std::vector<glm::vec3>        caster_points;           // fit point set: union of the per-box clipped caster points
    std::vector<glm::vec2>        projected_points;        // fit points projected onto the light plane
    std::vector<glm::vec2>        light_plane_hull;        // 2D hull of projected_points (rotating calipers input)
};

} // namespace erhe::scene
