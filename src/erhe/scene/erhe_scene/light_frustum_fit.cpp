// Tight, modular shadow frustum fit for directional lights.
//
// Implements the optional tightening steps of Light::projection_transforms()
// (see Shadow_frustum_fit_settings in light.hpp). The base case with all
// tightening steps disabled is Light::stable_directional_light_projection_transforms()
// in light.cpp; this file is only entered when at least one tightening step
// is enabled.
//
// Pipeline (each step optional):
//  1. Build the main camera view frustum F_main (planes and corners),
//     truncated to the maximum shadow distance (Camera::get_shadow_range()).
//  2. fit_to_casters: cull the per-caster world AABBs to those intersecting
//     the shadow caster volume F_shadow (F_main extruded toward the light),
//     clip each surviving box to F_shadow individually, and use the union of
//     the clipped point sets for fitting.
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
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <span>
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
    const std::span<const glm::vec3> points_in_world,
    const glm::mat3&                 light_from_world,
    const glm::mat2&                 rolled_from_light_xy,
    const glm::vec3&                 light_direction_in_light
) -> Light_space_box
{
    ERHE_PROFILE_FUNCTION();

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

// Index of the first plane with the whole AABB outside (the plane
// aabb_in_convex_volume rejects on; same center + extents form so the
// verdicts match exactly), or -1 when no single plane rejects.
// Debug diagnostics only.
[[nodiscard]] auto first_rejecting_plane(const std::span<const glm::vec4> planes, const erhe::math::Aabb& aabb) -> int
{
    const glm::vec3 center = aabb.center();
    const glm::vec3 extent = 0.5f * aabb.diagonal();
    for (std::size_t plane_index = 0; plane_index < planes.size(); ++plane_index) {
        const glm::vec3 normal{planes[plane_index]};
        const float center_distance  = glm::dot(normal, center) + planes[plane_index].w;
        const float projected_extent = glm::dot(glm::abs(normal), extent);
        if ((center_distance + projected_extent) < 0.0f) {
            return static_cast<int>(plane_index);
        }
    }
    return -1;
}

// Fills the light-independent part of the receiver cull volume build: the
// in-frustum receiver corner set and (when use_hull) the convex hull of those
// corners clipped to the view frustum and re-hulled. Shared by all lights of
// one Light_projections::apply() pass via the cache - none of this depends on
// the light. No-op when the cache is already valid for this pass.
//
// Across passes the cache reuses its previous result when the inputs repeat
// (skipped while collecting debug data, which must refill the debug vectors):
// an unchanged corner set keeps the corner hull - it depends on nothing else -
// and an unchanged view frustum on top keeps the clip, the re-hull and
// hull_valid as well. A pass over an unchanged scene then pays for the
// in-frustum gather and the comparison, not for the QuickHull runs.
void ensure_receiver_cache(
    Shadow_fit_receiver_cache&              cache,
    const std::span<const erhe::math::Aabb> receiver_world_aabbs,
    const std::array<glm::vec4, 6>&         main_frustum_planes,
    const std::array<glm::vec3, 8>&         main_frustum_corners,
    const bool                              use_hull,
    const bool                              collect_debug
)
{
    if (cache.valid) {
        return;
    }
    ERHE_PROFILE_FUNCTION();

    cache.valid = true;

    // Corners of the receiver AABBs that intersect the view frustum (world
    // space), gathered into a scratch so the previous pass's corner set stays
    // available for the reuse comparison below.
    std::vector<glm::vec3>& gathered = cache.gather_scratch;
    gathered.clear();
    gathered.reserve(receiver_world_aabbs.size() * 8);
    if (collect_debug) {
        cache.receiver_boxes.clear();
    }
    for (const erhe::math::Aabb& aabb : receiver_world_aabbs) {
        const bool in_frustum =
            aabb.is_valid() &&
            erhe::math::aabb_in_frustum(main_frustum_planes, main_frustum_corners, aabb);
        if (collect_debug) {
            cache.receiver_boxes.push_back(Shadow_frustum_fit_debug_data::Receiver_box{aabb, in_frustum});
        }
        if (!in_frustum) {
            continue;
        }
        const glm::vec3& a = aabb.min;
        const glm::vec3& b = aabb.max;
        gathered.push_back(glm::vec3{a.x, a.y, a.z});
        gathered.push_back(glm::vec3{a.x, a.y, b.z});
        gathered.push_back(glm::vec3{a.x, b.y, a.z});
        gathered.push_back(glm::vec3{a.x, b.y, b.z});
        gathered.push_back(glm::vec3{b.x, a.y, a.z});
        gathered.push_back(glm::vec3{b.x, a.y, b.z});
        gathered.push_back(glm::vec3{b.x, b.y, a.z});
        gathered.push_back(glm::vec3{b.x, b.y, b.z});
    }

    const bool same_corner_set =
        cache.has_result &&
        !collect_debug &&
        (use_hull == cache.result_use_hull) &&
        (gathered == cache.receiver_points);
    if (same_corner_set && (main_frustum_corners == cache.result_frustum_corners)) {
        return; // the clip and re-hull inputs are also unchanged: the whole cached result stands
    }

    cache.receiver_points.swap(gathered);
    cache.has_result             = true;
    cache.result_use_hull        = use_hull;
    cache.result_frustum_corners = main_frustum_corners;

    cache.hull_valid = false;
    cache.clipped_hull.clear();
    if (collect_debug) {
        cache.receiver_hull_points.clear();
        cache.receiver_clipped_points.clear();
        cache.receiver_clipped_hull_points.clear();
    }

    // Tight convex-hull body (fit_to_receivers_hull): the convex intersection of
    // the receiver-corner hull with the view frustum (complete, via
    // clip_convex_hull_points_to_frustum, so near receivers are kept even when the
    // frustum apex is inside the hull). Left invalid - the per-light code then
    // falls back to the bounding box - when the receiver points are degenerate
    // (fewer than 4, or coplanar) or the clipped body is degenerate.
    if (use_hull && (cache.receiver_points.size() >= 4)) {
        erhe::math::Convex_hull& hull = cache.receiver_hull_scratch;
        if (!same_corner_set) {
            // same_corner_set implies this branch also ran on the previous
            // pass (same size, same use_hull), so the stored hull already is
            // the hull of exactly this corner set.
            erhe::math::calculate_bounding_convex_hull(cache.receiver_points, hull);
        }
        if (collect_debug) {
            cache.receiver_hull_points = hull.points;
        }
        if (!hull.triangle_indices.empty()) {
            std::vector<glm::vec3>& clipped = cache.clipped_points_scratch;
            erhe::math::clip_convex_hull_points_to_frustum(hull, main_frustum_planes, main_frustum_corners, clipped);
            if (collect_debug) {
                cache.receiver_clipped_points = clipped;
            }
            if (clipped.size() >= 4) {
                // Computed directly into the cache; hull_valid (not the hull
                // contents) is the consumed-or-not signal on the failure paths.
                erhe::math::calculate_bounding_convex_hull(clipped, cache.clipped_hull);
                if (collect_debug) {
                    cache.receiver_clipped_hull_points = cache.clipped_hull.points;
                }
                if (!cache.clipped_hull.triangle_indices.empty()) {
                    cache.hull_valid = true;
                }
            }
        }
    }
}

// Builds the inward-facing filter planes of the receiver-aware caster cull
// volume: the receiver bounds that touch the view frustum, bounded in light
// space and extruded toward the light. A caster outside this volume can only
// shadow empty space, so it is rejected (in addition to the F_shadow cull).
// Leaves out_planes empty when no receiver bound touches the frustum (the
// caller then skips the receiver cull and keeps today's frustum-only behavior).
// The light-independent inputs come from the (lazily filled) cache; only the
// silhouette projection and plane sweep run per light.
void build_receiver_cull_planes(
    Shadow_fit_receiver_cache&              cache,
    const std::span<const erhe::math::Aabb> receiver_world_aabbs,
    const std::array<glm::vec4, 6>&         main_frustum_planes,
    const std::array<glm::vec3, 8>&         main_frustum_corners,
    const glm::mat3&                        light_from_world,
    const glm::mat3&                        world_from_light,
    const glm::vec3&                        light_direction,
    const bool                              use_hull,
    std::vector<glm::vec4>&                 out_planes,
    std::vector<glm::vec3>*                 out_far_plane_hull,
    Shadow_frustum_fit_debug_data*          debug_out
)
{
    ERHE_PROFILE_FUNCTION();

    out_planes.clear();
    ensure_receiver_cache(cache, receiver_world_aabbs, main_frustum_planes, main_frustum_corners, use_hull, debug_out != nullptr);
    if (debug_out != nullptr) {
        debug_out->receiver_boxes               = cache.receiver_boxes;
        debug_out->receiver_hull_points         = cache.receiver_hull_points;
        debug_out->receiver_clipped_points      = cache.receiver_clipped_points;
        debug_out->receiver_clipped_hull_points = cache.receiver_clipped_hull_points;
    }
    if (cache.receiver_points.empty()) {
        return;
    }

    if (cache.hull_valid) {
        erhe::math::build_shadow_caster_cull_planes_from_hull(cache.clipped_hull, light_direction, out_planes, out_far_plane_hull);
        if (!out_planes.empty()) {
            if (debug_out != nullptr) {
                debug_out->receiver_hull_path_used = true;
            }
            return;
        }
        // Degenerate silhouette (receivers edge-on to the light): fall
        // through to the bounding box below.
    }

    // Bounding-box body (fit_to_receivers): the light-space AABB of the receiver
    // points, extruded toward the light. A box is 6 planes + 8 corners, so it
    // reuses the frustum builders directly.
    if (debug_out != nullptr) {
        debug_out->receiver_box_path_used = true;
    }
    glm::vec3 receiver_min{std::numeric_limits<float>::max()};
    glm::vec3 receiver_max{std::numeric_limits<float>::lowest()};
    for (const glm::vec3& p : cache.receiver_points) {
        const glm::vec3 p_in_light = light_from_world * p;
        receiver_min = glm::min(receiver_min, p_in_light);
        receiver_max = glm::max(receiver_max, p_in_light);
    }
    // Avoid a zero-thickness box (coplanar receivers) so the extruded volume and
    // its silhouette planes stay well defined.
    receiver_max = glm::max(receiver_max, receiver_min + glm::vec3{min_box_extent});

    // World-space inward planes and corners of the light-space box, in the
    // left/right/bottom/top/near/far plane order and the corner bit-numbering
    // (bit0=x, bit1=y, bit2=z) that build_shadow_caster_silhouette() expects.
    const glm::vec3 ex = world_from_light * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 ey = world_from_light * glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 ez = world_from_light * glm::vec3{0.0f, 0.0f, 1.0f};
    std::array<glm::vec3, 8> box_corners{};
    for (std::size_t i = 0; i < 8; ++i) {
        const float x = ((i & 1u) != 0u) ? receiver_max.x : receiver_min.x;
        const float y = ((i & 2u) != 0u) ? receiver_max.y : receiver_min.y;
        const float z = ((i & 4u) != 0u) ? receiver_max.z : receiver_min.z;
        box_corners[i] = world_from_light * glm::vec3{x, y, z};
    }
    const std::array<glm::vec4, 6> box_planes{
        glm::vec4{ ex, -glm::dot( ex, box_corners[0])}, // left   (x = min)
        glm::vec4{-ex, -glm::dot(-ex, box_corners[1])}, // right  (x = max)
        glm::vec4{ ey, -glm::dot( ey, box_corners[0])}, // bottom (y = min)
        glm::vec4{-ey, -glm::dot(-ey, box_corners[2])}, // top    (y = max)
        glm::vec4{ ez, -glm::dot( ez, box_corners[0])}, // near   (z = min)
        glm::vec4{-ez, -glm::dot(-ez, box_corners[4])}  // far    (z = max)
    };
    const erhe::math::Shadow_volume_planes     volume     = erhe::math::build_shadow_caster_volume_planes(box_planes, light_direction);
    const erhe::math::Shadow_caster_silhouette silhouette = erhe::math::build_shadow_caster_silhouette(box_planes, box_corners, light_direction);
    const std::span<const glm::vec4> volume_planes     = volume.planes_span();
    const std::span<const glm::vec4> silhouette_planes = silhouette.planes_span();
    out_planes.assign(volume_planes.begin(), volume_planes.end());
    out_planes.insert(out_planes.end(), silhouette_planes.begin(), silhouette_planes.end());
}

} // anonymous namespace

auto Light::tight_directional_light_projection_transforms(const Light_projection_parameters& parameters) const -> Light_projection_transforms
{
    ERHE_PROFILE_FUNCTION();
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

    // Persistent scratch buffers (see Shadow_fit_scratch): cleared (capacity
    // kept) at point of use, so steady-state fits perform no heap
    // allocations. A local instance keeps callers without one (single-light
    // previews) working at the cost of allocating per call.
    Shadow_fit_scratch local_scratch{};
    Shadow_fit_scratch& scratch = (parameters.fit_scratch != nullptr) ? *parameters.fit_scratch : local_scratch;

    // Light frame - rotation only transform, same convention as the stable fit.
    // light_direction points from the scene toward the light (node +Z axis).
    const glm::vec3 light_direction = glm::vec3{light_node->direction_in_world()};
    const glm::vec3 light_up_vector = glm::vec3{light_node->world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    const glm::mat3 world_from_light{light_node->world_from_node()};
    const glm::mat3 light_from_world{light_node->node_from_world()};
    const glm::vec3 light_direction_in_light = light_from_world * light_direction; // == +Z for a pure rotation light node

    // Step 1: Main camera view frustum F_main, truncated to the maximum shadow
    // distance. Receivers past Camera::get_shadow_range() are never shadowed
    // (the fit is capped to that range and the shadow map does not cover them),
    // so the shadow-relevant view frustum ends there. Truncating the far plane
    // keeps F_shadow - and the caster set fitted to it - tight; the
    // extrude-toward-light step below still keeps casters past the range whose
    // shadows fall back into the truncated frustum (the far cap is dropped
    // exactly when its inward normal opposes the light direction).
    const float shadow_range = parameters.view_camera->get_shadow_range();
    Projection truncated_projection = *parameters.view_camera->projection();
    truncated_projection.z_far = std::min(
        truncated_projection.z_far,
        std::max(shadow_range, truncated_projection.z_near + min_box_extent)
    );
    const Transform main_clip_from_node  = truncated_projection.clip_from_node_transform(
        parameters.main_camera_viewport, parameters.reverse_depth, parameters.depth_range, parameters.conventions
    );
    const glm::mat4 main_clip_from_world = main_clip_from_node.get_matrix()    * view_camera_node->node_from_world();
    const glm::mat4 world_from_main_clip = view_camera_node->world_from_node() * main_clip_from_node.get_inverse_matrix();
    const std::array<glm::vec4, 6> main_frustum_planes  = erhe::math::extract_frustum_planes (main_clip_from_world, 0.0f, 1.0f);
    const std::array<glm::vec3, 8> main_frustum_corners = erhe::math::extract_frustum_corners(world_from_main_clip, 0.0f, 1.0f);
    const std::span<const glm::vec3> frustum_corner_points{main_frustum_corners};

    // Steps 2-3: Caster point set - filter the per-caster world AABBs to those
    // that can shadow a visible receiver, and clip each survivor to the shadow
    // caster volume F_shadow.
    std::vector<glm::vec3>& caster_points = scratch.caster_points;
    caster_points.clear();
    bool casters_available = false;
    if (settings.fit_to_casters && !parameters.caster_world_aabbs.empty()) {
        ERHE_PROFILE_SCOPE("fit: casters");

        // F_shadow: the (truncated) main camera view frustum extruded toward
        // the light. Two plane sets with different requirements are derived
        // from it (see build_shadow_caster_volume_planes / _silhouette):
        //  - shadow_volume.planes (open, no lateral closure): used only to clip
        //    the caster convex hull, which is already bounded laterally by the
        //    casters themselves.
        //  - filter_planes (open planes + silhouette side planes = the exact
        //    extruded-frustum planes): used for per-caster culling, which needs
        //    the lateral closure or it would keep casters off to the side that
        //    cannot shadow anything visible. The open set alone collapses to a
        //    single plane when the light is near-antiparallel to the view.
        const erhe::math::Shadow_volume_planes      shadow_volume = erhe::math::build_shadow_caster_volume_planes(main_frustum_planes, light_direction);
        const erhe::math::Shadow_caster_silhouette  silhouette    = erhe::math::build_shadow_caster_silhouette(main_frustum_planes, main_frustum_corners, light_direction);
        const std::span<const glm::vec4> shadow_volume_planes = shadow_volume.planes_span();
        const std::span<const glm::vec4> silhouette_planes    = silhouette.planes_span();
        std::vector<glm::vec4>& filter_planes = scratch.filter_planes;
        filter_planes.assign(shadow_volume_planes.begin(), shadow_volume_planes.end());
        filter_planes.insert(filter_planes.end(), silhouette_planes.begin(), silhouette_planes.end());

        // fit_to_receivers: a caster can only shadow a visible receiver if it
        // lies in the receiver region (the view frustum intersected with the
        // receiver bounds) extruded toward the light. Build that volume's filter
        // planes and require casters to pass it in addition to F_shadow; this
        // rejects casters whose shadows fall only on empty space and tightens
        // the fit through the smaller surviving caster set.
        std::vector<glm::vec4>& receiver_filter_planes = scratch.receiver_filter_planes;
        receiver_filter_planes.clear();
        std::vector<glm::vec3>& receiver_far_plane_hull = scratch.receiver_far_plane_hull;
        receiver_far_plane_hull.clear();
        if (settings.fit_to_receivers && !parameters.receiver_world_aabbs.empty()) {
            // Shared cache for the light-independent receiver work; a local
            // instance keeps callers without one (single-light previews)
            // working at the cost of refilling per light.
            Shadow_fit_receiver_cache local_receiver_cache{};
            Shadow_fit_receiver_cache& receiver_cache = (parameters.receiver_cache != nullptr) ? *parameters.receiver_cache : local_receiver_cache;
            build_receiver_cull_planes(
                receiver_cache,
                parameters.receiver_world_aabbs,
                main_frustum_planes,
                main_frustum_corners,
                light_from_world,
                world_from_light,
                light_direction,
                settings.fit_to_receivers_hull,
                receiver_filter_planes,
                (debug_out != nullptr) ? &receiver_far_plane_hull : nullptr,
                debug_out
            );
        }

        if (debug_out != nullptr) {
            // Store the planes per-caster culling actually tests against. The
            // debug visualization classifies casters against these and
            // reconstructs the bounded volume from them; appending the receiver
            // cull planes makes the reconstructed volume F_shadow intersected
            // with the receiver volume, and the culled/affecting classification
            // match the receiver-aware cull.
            debug_out->shadow_volume_planes = filter_planes;
            debug_out->shadow_volume_planes.insert(
                debug_out->shadow_volume_planes.end(),
                receiver_filter_planes.begin(),
                receiver_filter_planes.end()
            );
            debug_out->receiver_far_plane_hull = receiver_far_plane_hull;
            debug_out->receiver_filter_planes  = receiver_filter_planes;
        }

        // Surviving casters: those whose bounds can shadow a visible receiver.
        std::vector<erhe::math::Aabb>& surviving_casters = scratch.surviving_caster_aabbs;
        surviving_casters.clear();
        {
            ERHE_PROFILE_SCOPE("fit: filter casters");
            for (const erhe::math::Aabb& aabb : parameters.caster_world_aabbs) {
                if (!aabb.is_valid()) {
                    continue;
                }
                const bool in_shadow_volume = erhe::math::aabb_in_convex_volume(filter_planes, aabb);
                // The receiver verdict only matters when the shadow volume keeps the
                // caster; with diagnostics on it is evaluated unconditionally so the
                // dump shows both verdicts for every caster.
                bool in_receiver_volume = true;
                if (!receiver_filter_planes.empty() && (in_shadow_volume || (debug_out != nullptr))) {
                    in_receiver_volume = erhe::math::aabb_in_convex_volume(receiver_filter_planes, aabb);
                }
                if (debug_out != nullptr) {
                    Shadow_frustum_fit_debug_data::Caster_box caster_box{};
                    caster_box.aabb                      = aabb;
                    caster_box.culled_by_shadow_volume   = !in_shadow_volume;
                    caster_box.culled_by_receiver_volume = !in_receiver_volume;
                    if (!in_shadow_volume) {
                        caster_box.rejecting_plane = first_rejecting_plane(filter_planes, aabb);
                    } else if (!in_receiver_volume) {
                        caster_box.rejecting_plane = first_rejecting_plane(receiver_filter_planes, aabb);
                    }
                    debug_out->caster_boxes.push_back(caster_box);
                }
                if (!in_shadow_volume || !in_receiver_volume) {
                    continue; // outside F_shadow, or cannot shadow any visible receiver
                }
                surviving_casters.push_back(aabb);
            }
        }

        // Fit point set: each surviving box clipped to the **open** F_shadow
        // individually (the box is bounded laterally by itself, so the
        // silhouette side planes are not needed). A box is its own convex hull
        // with fixed topology, so no hull build is involved; and the union of
        // the per-box clips is a subset of the previously used clipped
        // whole-set hull - which also covered the empty bridge regions between
        // separated casters - so the fit can only get tighter while still
        // covering every caster.
        if (!surviving_casters.empty()) {
            ERHE_PROFILE_SCOPE("fit: clip casters");

            // Box hull topology for the corner order built below (bit 0 = z,
            // bit 1 = y, bit 2 = x). Triangle winding is irrelevant: this hull
            // is used only for Sutherland-Hodgman clipping, which does not
            // derive planes from it.
            static constexpr std::array<std::array<std::size_t, 3>, 12> box_triangles{{
                {0, 1, 3}, {0, 3, 2}, // x = min
                {4, 5, 7}, {4, 7, 6}, // x = max
                {0, 1, 5}, {0, 5, 4}, // y = min
                {2, 3, 7}, {2, 7, 6}, // y = max
                {0, 2, 6}, {0, 6, 4}, // z = min
                {1, 3, 7}, {1, 7, 5}  // z = max
            }};
            erhe::math::Convex_hull& box_hull = scratch.caster_box_hull;
            box_hull.triangle_indices.assign(box_triangles.begin(), box_triangles.end());
            std::vector<glm::vec3>& box_clip_points      = scratch.box_clip_points;
            std::vector<glm::vec3>& caster_corner_points = scratch.caster_corner_points;
            caster_corner_points.clear();
            unsigned int frustum_corners_added = 0u; // bit per F_main corner already in the fit point set

            for (const erhe::math::Aabb& aabb : surviving_casters) {
                box_hull.points.clear();
                for (std::size_t i = 0; i < 8; ++i) {
                    box_hull.points.push_back(
                        glm::vec3{
                            ((i & 4u) != 0u) ? aabb.max.x : aabb.min.x,
                            ((i & 2u) != 0u) ? aabb.max.y : aabb.min.y,
                            ((i & 1u) != 0u) ? aabb.max.z : aabb.min.z
                        }
                    );
                }
                if (debug_out != nullptr) {
                    caster_corner_points.insert(caster_corner_points.end(), box_hull.points.begin(), box_hull.points.end());
                }

                // Fast path: a box entirely inside the volume clips to itself,
                // and its corners are the complete vertex set of the
                // intersection (interior F_shadow vertices cannot be extremal
                // against the corner span, so the check below is not needed).
                const glm::vec3 center = aabb.center();
                const glm::vec3 extent = 0.5f * aabb.diagonal();
                bool fully_inside = true;
                for (const glm::vec4& plane : shadow_volume_planes) {
                    const glm::vec3 normal{plane};
                    const float center_distance  = glm::dot(normal, center) + plane.w;
                    const float projected_extent = glm::dot(glm::abs(normal), extent);
                    if (center_distance < projected_extent) {
                        fully_inside = false;
                        break;
                    }
                }
                if (fully_inside) {
                    caster_points.insert(caster_points.end(), box_hull.points.begin(), box_hull.points.end());
                    continue;
                }

                erhe::math::clip_convex_hull_points_by_planes(box_hull, shadow_volume_planes, box_clip_points);
                caster_points.insert(caster_points.end(), box_clip_points.begin(), box_clip_points.end());

                // The surface clip cannot produce F_shadow vertices interior
                // to the box (a box strictly containing the whole volume even
                // clips to nothing), so add the F_main corners the box
                // contains: they lie in F_shadow, hence in box ^ F_shadow.
                // Deliberately not gated on the clip output being non-empty -
                // empty output is exactly the box-contains-volume case.
                for (std::size_t corner_index = 0; corner_index < 8; ++corner_index) {
                    if ((frustum_corners_added & (1u << corner_index)) != 0u) {
                        continue;
                    }
                    const glm::vec3& corner = main_frustum_corners[corner_index];
                    if (
                        (corner.x >= aabb.min.x) && (corner.x <= aabb.max.x) &&
                        (corner.y >= aabb.min.y) && (corner.y <= aabb.max.y) &&
                        (corner.z >= aabb.min.z) && (corner.z <= aabb.max.z)
                    ) {
                        caster_points.push_back(corner);
                        frustum_corners_added |= (1u << corner_index);
                    }
                }
            }
            casters_available = !caster_points.empty();
            if ((debug_out != nullptr) && (caster_corner_points.size() >= 4)) {
                // Visualization only: the hull of the surviving corners no
                // longer participates in the fit itself.
                erhe::math::calculate_bounding_convex_hull(caster_corner_points, debug_out->caster_hull);
            }
        }
    }

    if (!casters_available && !settings.fit_to_view_frustum) {
        // fit_to_casters was requested but produced nothing to fit to
        return stable_directional_light_projection_transforms(parameters);
    }

    const std::span<const glm::vec3> primary_fit_points = casters_available ? std::span<const glm::vec3>{caster_points} : frustum_corner_points;

    // Step 4: Optional roll around the light direction (rotating calipers)
    glm::mat2 rolled_from_light_xy{1.0f};
    erhe::math::Min_area_obb_2d obb{};
    std::vector<glm::vec2>& light_plane_hull = scratch.light_plane_hull;
    light_plane_hull.clear(); // also read on the debug path when optimize_rotation is off
    if (settings.optimize_rotation) {
        ERHE_PROFILE_SCOPE("fit: optimize rotation (calipers)");
        std::vector<glm::vec2>& projected_points = scratch.projected_points;
        projected_points.clear();
        projected_points.reserve(primary_fit_points.size());
        for (const glm::vec3& p_in_world : primary_fit_points) {
            const glm::vec3 p_in_light = light_from_world * p_in_world;
            projected_points.push_back(glm::vec2{p_in_light});
        }
        erhe::math::calculate_bounding_convex_hull(projected_points, light_plane_hull);
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

    // Safety cap: never exceed the stable shadow-range box (shadow_range was
    // resolved above for the F_main truncation)
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
        debug_out->fit_points.assign(primary_fit_points.begin(), primary_fit_points.end());
        debug_out->light_plane_hull   = light_plane_hull;
        debug_out->obb                = obb;
        debug_out->view_frustum_planes        = main_frustum_planes;
        debug_out->view_frustum_corners       = main_frustum_corners;
        debug_out->view_frustum_corners_valid = true;

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
