#ifndef ERHE_LIGHT_GLSL
#define ERHE_LIGHT_GLSL

#include "erhe_camera_view.glsl"
#include "erhe_texture.glsl"
#include "erhe_point_shadow.glsl"
#include "erhe_shadow_distance.glsl"

#if __VERSION__ >= 450
#   define ERHE_DFDX dFdxFine
#   define ERHE_DFDY dFdyFine
#else
#   define ERHE_DFDX dFdx
#   define ERHE_DFDY dFdy
#endif

// Shadow map filtering selected by the ERHE_SHADOW_FILTER compile-time variant
// axis (Shader_int::SHADOW_FILTER, set from the graphics preset's
// Shadow_filter_mode enum). The value IS the PCF kernel width in texels:
// 0 = hard (single hardware compare), 2 = 2x2, 4 = 4x4, 6 = 6x6, ... A kernel
// of width K is fetched with (K/2)^2 textureGather() calls.
#define ERHE_SHADOW_FILTER_HARD 0

// Depth-bias method for the wide-kernel (>= 4) PCF path, selected by the
// ERHE_SHADOW_BIAS compile-time variant axis (set from the graphics preset's
// Shadow_bias_mode enum). Exposed for live comparison; the hard and 2x2 paths
// always use their own fixed bias and ignore this axis.
#define ERHE_SHADOW_BIAS_SLOPE_SCALED   0
#define ERHE_SHADOW_BIAS_RECEIVER_PLANE 1

// Shadow technique (ERHE_SHADOW_TECHNIQUE compile-time axis, from the graphics
// preset's Shadow_technique_mode), for directional and spot lights (point
// lights always use their distance cube). depth = sample the depth map and
// compare the receiver plane's depth at each texel centre; distance = sample
// the R32F distance map of caster plane distances on the texel centre rays
// and compare the receiver plane's distance on the same rays. Both apply
// derived error bounds and share the ERHE_SHADOW_FILTER kernel sizes. See
// doc/erhe/shadows.md.
#define ERHE_SHADOW_TECHNIQUE_DEPTH    0
#define ERHE_SHADOW_TECHNIQUE_DISTANCE 1

// Cusp-free windowed inverse-square falloff (Esoterica "AttenuationNoCusp" /
// Frostbite): saturate(1 - (d/range)^4)^2 / (d^2 + 1). Squaring the window
// removes the derivative discontinuity at d = range that the previous
// glTF-recommended curve had, and the +1 keeps the response finite as d -> 0.
// range <= 0 means unlimited (window = 1).
float get_range_attenuation(float range, float distance) {
    float inverse_square = 1.0 / (distance * distance + 1.0);
    if (range <= 0.0) {
        return inverse_square;
    }
    float k      = distance / range;
    float k2     = k * k;
    float window = clamp(1.0 - k2 * k2, 0.0, 1.0);
    return window * window * inverse_square;
}

float get_spot_attenuation(vec3 point_to_light, vec3 spot_direction, float outer_cone_coss, float inner_cone_cos) {
    float actual_cos = dot(normalize(spot_direction), normalize(-point_to_light));
    if (actual_cos > outer_cone_coss) {
        if (actual_cos < inner_cone_cos) {
            return smoothstep(outer_cone_coss, inner_cone_cos, actual_cos);
        }
        return 1.0;
    }
    return 0.0;
}

// Unit roundoff of fp32 (round to nearest): a rounded operation returns
// x * (1 + d) with |d| <= 2^-24. The receiver's minimum shadow bias (D1,
// doc/erhe/shadows.md "Minimum bias") is built from bounds in these units.
const float erhe_fp32_unit_roundoff = 1.0 / 16777216.0;

// Positions in the shadow code are view-relative (doc/erhe/shadows.md
// "View-relative positions"): standard.vert computes every vertex relative to
// its pass's view origin (camera.cameras[].view_origin), the camera for a
// forward pass and the light camera for a shadow pass, with a
// view_relative_from_node whose translation the CPU subtracted in double. The
// rounding bounds below therefore take the distance from the view origin, not
// from the world origin.
//
// Bound on the fp32 rounding of a vertex position, as a vector length. Each
// component is a four-term fp32 sum (a row of view_relative_from_node times
// the homogeneous vertex), at most gamma_4 = 4u times the magnitudes it adds,
// plus the one rounding of the row's translation when the CPU composed it:
// 5u. The position's own distance from the view origin stands in for the
// magnitudes; the three components combine to sqrt(3) * 5u * |p|.
float get_vertex_position_rounding(vec3 view_relative_position) {
    return (sqrt(3.0) * 5.0 * erhe_fp32_unit_roundoff) * length(view_relative_position);
}

// Bound on the rounding of an interpolated view-relative position (a
// fragment's receiver point, a caster fragment's point): its triangle's
// vertices (get_vertex_position_rounding()) plus the barycentric
// interpolation, a four-term sum per component (4u): sqrt(3) * 9u * |p|.
float get_position_rounding(vec3 view_relative_position) {
    return (sqrt(3.0) * 9.0 * erhe_fp32_unit_roundoff) * length(view_relative_position);
}

#if defined(ERHE_SHADOW_MAPS)
// A receiver position relative to a light's shadow view origin (the light
// block's view_origin, the view origin of that light's shadow passes), from
// its position relative to this pass's view origin. Both origins are exact
// fp32 values; the small offset between them is formed first and then added
// (precise, so the compiler cannot reassociate the sum into one of large
// magnitudes).
vec3 get_light_relative_position(vec3 view_relative_position, vec3 light_view_origin) {
    ERHE_SHADOW_DISTANCE_PRECISE vec3 origin_offset           = camera.cameras[c_view_index].view_origin.xyz - light_view_origin;
    ERHE_SHADOW_DISTANCE_PRECISE vec3 light_relative_position = view_relative_position + origin_offset;
    return light_relative_position;
}

// Bound on the distance between the light-relative receiver point of
// get_light_relative_position() and the receiver surface in real
// arithmetic: the interpolated view-relative point (get_position_rounding()),
// the rounding of the origin offset (u |offset| per component) and of the
// sum (u |result| per component).
float get_light_relative_receiver_rounding(vec3 view_relative_position, vec3 light_relative_position, vec3 light_view_origin) {
    vec3 origin_offset = camera.cameras[c_view_index].view_origin.xyz - light_view_origin;
    return get_position_rounding(view_relative_position) +
        ((sqrt(3.0) * erhe_fp32_unit_roundoff) * (length(origin_offset) + length(light_relative_position)));
}
#endif

#if defined(ERHE_FRAGMENT_SHADER)
// Geometric normal of the receiver plane from the screen-space derivatives of
// the view-relative position: exact for a planar triangle (both derivatives
// lie in its plane) and independent of smooth vertex normals and normal maps.
// The orientation follows the screen-space winding and is not meaningful;
// sample_light_visibility() accepts either. Call it in uniform control flow
// (before any per-light branch) so the quad's helper lanes take part.
//
// Returns xyz = the unit normal, w = a bound on its error in radians. Each
// derivative is the difference of two rounded positions, so it carries at
// most e = 2 * get_position_rounding() (the vertex rounding counts too: the
// casters' vertices are rounded in another pass's view-relative space, so
// the receiver's triangle is bounded against the true surface, not against
// theirs); the cross product then moves by at most e * (|dp_dx| + |dp_dy| + e),
// and the unit normal tilts by at most that over |dp_dx x dp_dy|. The bound
// grows as the pixel footprint shrinks toward the position's fp32
// resolution, which scales with the distance from the camera.
//
// Undetermined plane: when the derivatives do not span a plane (their cross
// product is exactly zero - the quad's rounded positions are equal or
// collinear, which needs a pixel footprint at or below the fp32 resolution of
// the position) there is no receiver plane to take. The function then returns
// vec4(0.0); sample_light_visibility() treats the receiver as head-on to the
// light and covers every plane R1 admits (doc/erhe/shadows.md "Undetermined
// receiver plane").
vec4 get_receiver_geometric_normal(vec3 view_relative_position) {
    vec3  dp_dx    = ERHE_DFDX(view_relative_position);
    vec3  dp_dy    = ERHE_DFDY(view_relative_position);
    vec3  n        = cross(dp_dx, dp_dy);
    float n_length = length(n);
    if (!(n_length > 0.0)) {
        return vec4(0.0);
    }
    float derivative_error = 2.0 * get_position_rounding(view_relative_position);
    float normal_error     = (derivative_error * (length(dp_dx) + length(dp_dy) + derivative_error)) / n_length;
    return vec4(n / n_length, normal_error);
}
#endif

#if defined(ERHE_SHADOW_MAPS) && (ERHE_SHADOW_TECHNIQUE == ERHE_SHADOW_TECHNIQUE_DISTANCE)
// One tap of the distance technique (doc/erhe/shadows.md "The distance
// technique"): the texel with integer-valued index `texel`, its centre ray
// (get_shadow_distance_ray(), the same ray the caster stored on), and the
// receiver plane through receiver_point (relative to the light's view origin;
// plane_normal, unit, clamped to the R1 grazing limit as in
// sample_light_visibility(); plane_determined false: every plane within the
// grazing limit of head-on).
//
// Returns x = the receiver plane's light distance on the ray (for an
// undetermined plane the smallest over the admissible planes), y = the
// receiver point's own light distance (the one-sided cap: a centre whose
// plane point is farther from the light compares the point itself), z = the
// bias: the sum of error bounds that moves the reference toward the light,
// in world units along the ray (u = 2^-24, e = 2 get_position_rounding(P)):
//  - snap (bias_terms.x, precomputed): the caster coverage snap.
//  - receiver gradient: a normal error dN moves a plane's ray distance by
//    dN . (P - X) / (N . d), X the plane point on the ray.
//  - caster gradient: the caster normal's error bound (bias_terms.y) over
//    half the texel diagonal on the plane (bias_terms.z / |N . d|), the
//    distance from the caster's interpolated point to its ray.
//  - position: the rounding of the receiver point and of the caster's
//    interpolated point (their sum in bias_terms.w) along the plane normal.
//  - evaluation: each side's N . (p - O) / (N . d): the subtraction and the
//    three-term dot product 4u |p - O|, the dot N . d 3u |s| / |N . d|, the
//    divide u |s|; plus the receiver's own length() / dot, 13u |P - O|.
// light_block.shadow_bias_scales: x scales the two gradient terms, y the
// position and evaluation terms; the snap is unscaled. The ray itself is not
// a term: both passes compute it bit for bit alike (`precise`), and R32F
// stores the caster's value exactly.
vec3 get_shadow_distance_tap(
    vec2  texel,
    float resolution,
    mat4  view_relative_from_texture,
    vec3  light_direction,
    bool  is_directional,
    vec3  receiver_point,
    vec3  plane_normal,
    bool  plane_determined,
    float normal_error,
    vec4  bias_terms
)
{
    const float u           = erhe_fp32_unit_roundoff;
    const float grazing_cos = 0.05;
    const float grazing_tan = sqrt(1.0 - (grazing_cos * grazing_cos)) / grazing_cos;
    vec3 ray_origin;
    vec3 ray_direction;
    get_shadow_distance_ray(view_relative_from_texture, light_direction, is_directional, get_shadow_distance_texel_centre(texel, resolution), ray_origin, ray_direction);
    vec3  origin_to_receiver = receiver_point - ray_origin;
    float receiver_length    = length(origin_to_receiver);
    float current            = is_directional ? dot(origin_to_receiver, ray_direction) : receiver_length;

    float ray_cos;
    float plane_distance;
    float receiver_gradient;
    if (plane_determined) {
        float signed_cos = dot(plane_normal, ray_direction);
        ray_cos           = abs(signed_cos);
        plane_distance    = dot(plane_normal, origin_to_receiver) / signed_cos;
        float lateral     = length(origin_to_receiver - (plane_distance * ray_direction));
        receiver_gradient = (normal_error * lateral) / (ray_cos - normal_error);
    } else {
        // Planes through P tilted from head-on (the light ray at P) by up to
        // the grazing limit: directional, the ray lies at lateral distance w
        // from P, so the nearest plane point is |w| tan(alpha_max) nearer;
        // spot, as for the point-light cube (point_light_shadows.md).
        ray_cos = grazing_cos;
        if (is_directional) {
            plane_distance = current - (grazing_tan * length(origin_to_receiver - (current * ray_direction)));
        } else {
            vec3 receiver_direction = origin_to_receiver / receiver_length;
            plane_distance = current / (dot(receiver_direction, ray_direction) + (grazing_tan * length(cross(receiver_direction, ray_direction))));
        }
        receiver_gradient = 0.0; // the reference already takes every admissible plane
    }

    float caster_lateral   = bias_terms.z / ray_cos;
    float caster_gradient  = (bias_terms.y * caster_lateral) / max(ray_cos - bias_terms.y, erhe_shadow_distance_plane_cos_min);
    float position_error   = bias_terms.w / ray_cos;
    float abs_distance     = abs(plane_distance);
    float evaluation_error = u * (
        (((4.0 * (receiver_length + abs_distance + caster_lateral)) + (6.0 * abs_distance)) / ray_cos) +
        (2.0 * abs_distance) +
        (13.0 * receiver_length)
    );
    vec2  bias_scales = light_block.shadow_bias_scales;
    float bias        = bias_terms.x + (bias_scales.x * (receiver_gradient + caster_gradient)) + (bias_scales.y * (position_error + evaluation_error));
    return vec3(plane_distance, current, bias);
}
#endif

// view_relative_position: the receiver position relative to this pass's view
// origin (standard.vert v_view_relative_position). receiver_plane: xyz = unit
// geometric normal of the receiver plane, either orientation, or 0 when
// undetermined; w = the normal's error bound in radians (see
// get_receiver_geometric_normal()). Every light-space computation below is
// relative to the light's view origin (light block view_origin), with the
// light's view-relative matrices, so their fp32 rounding scales with the
// distances from the camera and from the light camera, not from the world
// origin (doc/erhe/shadows.md "View-relative positions").
float sample_light_visibility(vec3 view_relative_position, uint light_index, vec4 receiver_plane) {
#if defined(ERHE_SHADOW_MAPS)
    if (light_block.shadow_texture_compare.x == max_u32) {
        return 1.0;
    }

    // s_shadow_compare and s_shadow_no_compare are declared as uniforms
    // in the generated shader preamble (from default_uniform_block) and
    // bound via Render_command_encoder::set_sampled_image() for all backends.

    Light light                                 = light_block.lights[light_index];
    // shadow_index_packed.x is the dense shadow-array-layer the
    // Shadow_renderer wrote this light's depth map into. It differs
    // from light_index whenever an earlier type bucket has any
    // non-shadow lights (those skip the shadow array layer); without
    // this indirection mixed shadow/non-shadow scenes sample the wrong
    // layer.
    float array_layer                           = float(light.shadow_index_packed.x);
    vec3  receiver_point                        = get_light_relative_position(view_relative_position, light.view_origin.xyz);
    vec4  position_in_light_texture_homogeneous = light.texture_from_view_relative * vec4(receiver_point, 1.0);
    vec3  position_in_light_texture             = position_in_light_texture_homogeneous.xyz / position_in_light_texture_homogeneous.w;
    float reference_depth                       = position_in_light_texture.z; // before the test offset below
#if defined(ERHE_SHADOW_TEST_REFERENCE_DEPTH_ULPS)
    // Test-only entry point, never defined by production shaders: offsets the
    // reference depth by a signed number of float ulps before any bias, so the
    // Shadow_tie GPU test (src/erhe/scene_renderer/test/shaders/shadow_tie.frag)
    // can show that no verdict depends on last-bit rounding.
    position_in_light_texture.z = uintBitsToFloat(uint(int(floatBitsToUint(position_in_light_texture.z)) + (ERHE_SHADOW_TEST_REFERENCE_DEPTH_ULPS)));
#endif
    // Receivers outside the light-space [0, 1] depth range are NOT rejected
    // here. With tight shadow frustum fitting (Shadow_frustum_fit_settings,
    // e.g. fit_to_casters) the far plane hugs the casters, so a visible
    // receiver can lie beyond it while every caster above it is in the map.
    // Clamping the reference depth to [0, 1] (after biasing, below) resolves
    // those receivers correctly with the non-strict depth comparison:
    //  - beyond the far plane: reference clamps to the far value; caster
    //    texels compare shadowed (all mapped casters are nearer the light),
    //    empty texels compare lit (equal to the far clear value).
    //  - nearer than the near plane: reference clamps to the near value and
    //    compares lit (nothing in the map is nearer than the near plane).
    // The explicit clamp is required because hardware only clamps the
    // comparison reference for unorm depth formats, not float ones.
    // Out-of-range XY is covered by the empty border the shadow pass
    // scissor keeps around the map, as wide as the filter's tap reach
    // (clamp_to_edge then compares against the far clear value, which
    // always resolves to lit); see doc/erhe/shadows.md "Empty border and
    // receiver coverage".

    // What follows is based on https://renderdiagrams.org/2024/12/18/shadowmap-bias/
    // Notes:
    //  - dz_dUV comes from the receiver plane, not from the screen-space
    //    Jacobian of the article, so it has no det == 0 case: a receiver
    //    edge-on to the camera keeps its gradient.
    //  - HLSL code has been converted to GLSL
    //  - clip_depth_direction: -1.0 for reverse Z, 1.0 for forward Z
    //      - bias and comparisons are adjusted accordingly
    //  - The slope terms carry no scale factor: for a planar receiver the
    //    stored depth of a texel differs from the reference at the sample point
    //    by exactly dot(texel_center_uv - sample_uv, dz_dUV), so each tap is
    //    offset to its own texel centre, located from the texel set the
    //    hardware selects, plus the caster vertex snap term (snap_bias below);
    //    see doc/erhe/shadows.md "Bias technique".

    float cdd = camera.cameras[c_view_index].clip_depth_direction; // -1.0 reverse Z, 1.0 forward Z

    // Receiver depth gradient dz_dUV from the receiver plane (derivation in
    // doc/erhe/shadows.md "Receiver depth gradient"). The world plane
    // (N, -dot(N, P)) maps to the homogeneous texture-space plane
    // transpose(world_from_texture) * (N, -dot(N, P)) = (a, b, c, e); since
    // a plane through the origin of homogeneous space is the same plane
    // after the perspective divide, a * u + b * v + c * z + e = 0 holds in
    // post-divide texture space, so dz/du = -a / c and dz/dv = -b / c for
    // orthographic and perspective light projections alike. The orientation
    // of N cancels in the ratios.
    //
    // c = dot(N, D), D = world_from_texture[2].xyz - P * world_from_texture[2].w
    // the world direction along which the texture depth changes at P (the
    // light ray through P), so |c| / |D| is |N . L|. A receiver edge-on to
    // the light (c -> 0) has an unbounded gradient; below the R1 grazing
    // limit N . L = 0.05 the plane is tilted about its line through P, on
    // the side it already faces, to exactly N . L = 0.05, which clamps
    // |dz_dUV| to the slope at that limit and keeps c away from 0.
    //
    // D_u, D_v and D are the world directions along which u, v and z change
    // at P (columns of world_from_texture applied at P, up to the common
    // positive scale h.w); the minimum bias below reads them too. The matrix
    // is the light's view-relative one and P the light-relative receiver
    // point; directions do not depend on the translation.
    mat4  world_from_texture  = light.view_relative_from_texture;
    vec3  D_u                 = world_from_texture[0].xyz - receiver_point * world_from_texture[0].w;
    vec3  D_v                 = world_from_texture[1].xyz - receiver_point * world_from_texture[1].w;
    vec3  D                   = world_from_texture[2].xyz - receiver_point * world_from_texture[2].w;
    float D_length            = length(D);
    vec3  depth_axis_in_world = D / D_length;
    const float grazing_cos   = 0.05;
    const float grazing_tan   = sqrt(1.0 - (grazing_cos * grazing_cos)) / grazing_cos;
    vec3  receiver_normal     = receiver_plane.xyz;
    float normal_error        = receiver_plane.w;
    float receiver_cos        = dot(receiver_normal, depth_axis_in_world);
    // The plane is determined when get_receiver_geometric_normal() found one
    // and its error bound tilts it by less than half of its own |N . L| (the
    // clamped one), so the first-order gradient error bound below holds with
    // at least half of c left. Otherwise the receiver is taken head-on to the
    // light (doc/erhe/shadows.md "Undetermined receiver plane").
    bool  plane_determined    = (dot(receiver_normal, receiver_normal) > 0.0) && (normal_error < (0.5 * max(abs(receiver_cos), grazing_cos)));
    vec3  plane_normal        = plane_determined ? receiver_normal : depth_axis_in_world;
    if (plane_determined && (abs(receiver_cos) < grazing_cos)) {
        vec3  tangent_direction = normalize(receiver_normal - (receiver_cos * depth_axis_in_world));
        float side              = (receiver_cos < 0.0) ? -1.0 : 1.0;
        plane_normal = (side * grazing_cos) * depth_axis_in_world + sqrt(1.0 - (grazing_cos * grazing_cos)) * tangent_direction;
    }

#if ERHE_SHADOW_TECHNIQUE == ERHE_SHADOW_TECHNIQUE_DISTANCE
    // Distance technique (doc/erhe/shadows.md "The distance technique"). Each
    // texel of the R32F distance map holds the light distance of its caster's
    // plane on the texel's centre ray (standard.frag, VARIANT_SHADOW_DISTANCE;
    // the ray from erhe_shadow_distance.glsl). Every tap compares the
    // receiver plane's distance on the same ray, which equals the stored
    // value for the receiver's own surface in real arithmetic, moved toward
    // the light by the error bounds of get_shadow_distance_tap(). The texels
    // are picked from the sample point and fetched at their centres / shared
    // gather corners, as for the depth technique ("Tap offsets").
    // shadow_index_packed.z is 0 for a spot light whose map is too coarse
    // for its cone (Light_shadow_limits::distance_rays_valid: the tap rays
    // would fan out past what the grazing limit leaves over the normal
    // error, doc/erhe/shadows.md "The distance technique", validity); that
    // light is sampled with the depth technique below, from the depth map
    // the same caster pass writes.
    if (light.shadow_index_packed.z != 0u) {
        const float caster_snap_texels = 1.0 / 256.0;
        bool  is_directional  = (light_index < light_block.directional_light_count);
        float resolution      = float(textureSize(s_shadow_distance, 0).x);
        vec2  sample_texel    = position_in_light_texture.xy * resolution;
        vec2  nearest_texel   = floor(sample_texel);
        vec2  gather_base     = floor(sample_texel - 0.5);
        vec2  gather_fraction = (sample_texel - 0.5) - gather_base;

        // One texel step along u / v at the receiver point, perpendicular to
        // the light ray there: dX/du at fixed texture depth is D_u * h.w, less
        // its component along the ray.
        float h_w              = abs(position_in_light_texture_homogeneous.w);
        vec3  texel_u_world    = (D_u * h_w) / resolution;
        vec3  texel_v_world    = (D_v * h_w) / resolution;
        vec3  lateral_u        = texel_u_world - (depth_axis_in_world * dot(depth_axis_in_world, texel_u_world));
        vec3  lateral_v        = texel_v_world - (depth_axis_in_world * dot(depth_axis_in_world, texel_v_world));
        float lateral_u_length = length(lateral_u);
        float lateral_v_length = length(lateral_v);

        // Coverage snap: a caster covers texel centres up to 1/256 texel past
        // its true edge, and there the stored plane is that caster's,
        // extended. Bounded with the receiver plane's distance slope per
        // texel, |N . lateral| / |N . ray| per axis (an undetermined plane:
        // the grazing-limit slope). Unscaled, like the depth technique's
        // snap_bias.
        float snap_bias = plane_determined
            ? ((caster_snap_texels * (abs(dot(plane_normal, lateral_u)) + abs(dot(plane_normal, lateral_v)))) / abs(dot(plane_normal, depth_axis_in_world)))
            : (caster_snap_texels * grazing_tan * (lateral_u_length + lateral_v_length));

        // The caster's plane normal comes from the derivatives of its
        // interpolated position over one map pixel, relative to the light
        // camera (the shadow pass's view origin), each off by up to
        // e = 2 get_position_rounding(P), so it tilts by at most
        // e (|l_u| + |l_v| + e) / |l_u x l_v| (l_u, l_v the pixel's extents
        // perpendicular to the ray; the caster taken at the receiver's
        // distance, which is where the tie is).
        float caster_rounding     = get_position_rounding(receiver_point);
        float receiver_rounding   = get_light_relative_receiver_rounding(view_relative_position, receiver_point, light.view_origin.xyz);
        float derivative_error    = 2.0 * caster_rounding;
        float caster_normal_error = (derivative_error * (lateral_u_length + lateral_v_length + derivative_error)) / length(cross(lateral_u, lateral_v));
        float texel_half_diagonal = 0.5 * sqrt((lateral_u_length * lateral_u_length) + (lateral_v_length * lateral_v_length));
        vec4  bias_terms          = vec4(snap_bias, caster_normal_error, texel_half_diagonal, receiver_rounding + caster_rounding);

        vec3 light_direction = light.direction_and_outer_spot_cos.xyz;

#   if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
        // One nearest fetch at the centre of nearest_texel. One-sided: a
        // centre whose plane point is farther from the light than the
        // receiver point compares the receiver point's own distance.
        vec3  tap    = get_shadow_distance_tap(nearest_texel, resolution, world_from_texture, light_direction, is_directional, receiver_point, plane_normal, plane_determined, normal_error, bias_terms);
        float stored = textureLod(s_shadow_distance, vec3(get_shadow_distance_texel_centre(nearest_texel, resolution), array_layer), 0.0).r;
        return ((min(tap.x, tap.y) - tap.z) > stored) ? 0.0 : 1.0;
#   elif ERHE_SHADOW_FILTER == 2
        // One textureGather() at the corner the gather_base set shares,
        // one-sided per tap, bilinearly weighted by gather_fraction.
        const vec2 corners[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));
        vec4 stored4      = textureGather(s_shadow_distance, vec3((gather_base + 1.0) / resolution, array_layer), 0);
        vec4 attenuation4 = vec4(0.0);
        for (int t = 0; t < 4; ++t) {
            vec3 tap = get_shadow_distance_tap(gather_base + corners[t], resolution, world_from_texture, light_direction, is_directional, receiver_point, plane_normal, plane_determined, normal_error, bias_terms);
            attenuation4[t] = ((min(tap.x, tap.y) - tap.z) > stored4[t]) ? 0.0 : 1.0;
        }
        vec2 fracCoords = clamp(gather_fraction, 0.0, 1.0);
        return mix(
            mix(attenuation4[3], attenuation4[2], fracCoords.x),
            mix(attenuation4[0], attenuation4[1], fracCoords.x),
            fracCoords.y
        );
#   else
        // KxK box PCF from (K/2)^2 gathers at the shared corners of their
        // sets, as in the depth technique's wide path. receiver_plane: the
        // reference is the receiver plane's distance on each tap's ray
        // (signed); slope_scaled: one-sided per tap.
        const int  K          = ERHE_SHADOW_FILTER;
        const int  gathers    = K / 2;
        const vec2 corners[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));
        float visibility = 0.0;
        for (int gj = 0; gj < gathers; ++gj) {
            for (int gi = 0; gi < gathers; ++gi) {
                vec2 gather_texel = (vec2(float(gi), float(gj)) - float(gathers - 1) * 0.5) * 2.0;
                vec4 stored4      = textureGather(s_shadow_distance, vec3((gather_base + gather_texel + 1.0) / resolution, array_layer), 0);
                for (int t = 0; t < 4; ++t) {
                    vec3 tap = get_shadow_distance_tap(gather_base + gather_texel + corners[t], resolution, world_from_texture, light_direction, is_directional, receiver_point, plane_normal, plane_determined, normal_error, bias_terms);
#       if ERHE_SHADOW_BIAS == ERHE_SHADOW_BIAS_RECEIVER_PLANE
                    float reference = tap.x;
#       else
                    float reference = min(tap.x, tap.y);
#       endif
                    visibility += ((reference - tap.z) > stored4[t]) ? 0.0 : 1.0;
                }
            }
        }
        return visibility / float(K * K);
#   endif
    }
#endif // ERHE_SHADOW_TECHNIQUE == ERHE_SHADOW_TECHNIQUE_DISTANCE

    vec4 plane_in_texture = transpose(world_from_texture) * vec4(plane_normal, -dot(plane_normal, receiver_point));
    vec2 dz_dUV           = -plane_in_texture.xy / plane_in_texture.z;
    vec2 shadowmap_resolution = textureSize(s_shadow_no_compare, 0).xy;

    // Texel geometry of the sample point, in texels. The shader picks the
    // texels itself and fetches them at coordinates the hardware selects
    // without ambiguity: a nearest fetch at the texel's centre, a
    // textureGather() at the corner its 2x2 set shares. The hardware rounds
    // texel coordinates to its sub-texel precision (subTexelPrecisionBits,
    // typically 8) before it selects texels, so a coordinate that lies on or
    // near a rounding boundary (a sample point within 1/512 texel of a texel
    // boundary, or a gather coordinate whose fp32 sum with the kernel offset
    // lands there) selects a texel the shader cannot predict; a centre or a
    // shared corner is half a texel from every boundary. The offsets are
    // measured from the sample point, so they are the exact distances to the
    // centres of the texels fetched.
    //  - nearest_texel / texel_offset_nearest: the texel containing the sample
    //    point, and sample point -> its centre (texel centres at integer +
    //    0.5), in (-0.5, 0.5].
    //  - gather_base / gather_fraction: the lower-left texel of the 2x2 set
    //    around the sample point, and the sample point's position relative to
    //    that texel's centre (texel centres at integers), in [0, 1).
    vec2 sample_texel           = position_in_light_texture.xy * shadowmap_resolution;
    vec2 nearest_texel          = floor(sample_texel);
    vec2 texel_offset_nearest   = nearest_texel + 0.5 - sample_texel;
    vec2 gather_texel_position  = sample_texel - 0.5;
    vec2 gather_base            = floor(gather_texel_position);
    vec2 gather_fraction        = gather_texel_position - gather_base;

    // Caster vertex snap. The rasterizer snaps the caster's vertices to its
    // sub-pixel grid (subPixelPrecisionBits, typically 8) before it
    // interpolates depth, so the stored plane is the caster plane displaced by
    // at most one sub-texel step along each map axis, and its depth at a texel
    // centre differs by at most (|dz/du| + |dz/dv|) * step. Every reference
    // moves by that much toward the light.
    const float caster_snap_texels = 1.0 / 256.0;
    float snap_bias = (-cdd) * caster_snap_texels * dot(abs(dz_dUV), 1.0 / shadowmap_resolution);

    // Minimum bias (D1; derivation in doc/erhe/shadows.md "Minimum bias").
    // The offsets above are exact for the receiver plane in real arithmetic;
    // what is left is where the stored and the reference depth of the same
    // surface come from different fp32 evaluations. Each term bounds one error
    // source in texture depth units, and their sum moves every reference
    // toward the light:
    //  - projection: z = (T_z . p) / (T_w . p) evaluated in fp32, once for
    //    the reference at P and once per caster vertex in the shadow pass
    //    (the same rows of the same matrix; the caster vertices are
    //    evaluated at P as their stand-in): a four-term dot product per row
    //    (gamma_4, plus one rounding of the composed depth row), the divide.
    //  - position: the rounding of the receiver point moves it off the
    //    surface by up to get_light_relative_receiver_rounding(), and the
    //    rounding of the caster's vertices (computed relative to the light
    //    camera in the shadow pass, get_vertex_position_rounding()) moves the
    //    stored plane; at fixed (u, v) that is a depth offset of their sum
    //    over |c * h.w|.
    //  - raster: the rasterizer's fp32 interpolation of the caster
    //    primitive's vertex depths at the texel centre, 4u times the largest
    //    |vertex depth|, light.view_origin.w (Light_shadow_limits::
    //    raster_vertex_depth). Clipping keeps every vertex depth inside the
    //    clip volume's [0, 1], and a primitive clipped at the depth-1 plane
    //    (the near plane under reverse-Z, the far plane under forward-Z) has
    //    vertices at depth 1 however small the texel's own depth is, so the
    //    bound is 1. Under depth clamp (directional passes with
    //    Shadow_frustum_fit_settings::depth_clamp) nothing clips at the depth
    //    planes, the interpolation takes the unclamped vertex depths and the
    //    clamp to [0, 1] follows it, so the bound is the largest |depth| over
    //    the caster bounds' corners.
    //  - gradient: the normal's error bound tilts the plane about P, which
    //    moves the plane depth at a tap by up to
    //    normal_error * (|o_u| |D_u| + |o_v| |D_v| + |dz_dUV . o| |D|) / (|c| - normal_error |D|)
    //    for the tap offset o (uv units), bounded by the filter's reach.
    //  - format: one quantum of the stored depth (D0): 1 / (2^bits - 1) for
    //    UNORM, one float ulp of z for a float map. The hard path's UNORM
    //    reference is rounded up onto the stored grid below instead.
    // light_block.shadow_bias_scales: x scales the gradient term
    // (shadow_bias_texel_scale), y the projection + position + raster terms
    // (shadow_bias_origin_scale); 1 is the derived bound.
    const float u = erhe_fp32_unit_roundoff;
#   if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
    const float tap_reach_texels = 0.5;
#   elif ERHE_SHADOW_FILTER == 2
    const float tap_reach_texels = 1.0;
#   else
    const float tap_reach_texels = 0.5 * float(ERHE_SHADOW_FILTER);
#   endif
    mat4  texture_from_world = light.texture_from_view_relative;
    vec4  abs_receiver_point = vec4(abs(receiver_point), 1.0);
    float row_z_magnitude    = dot(abs(vec4(texture_from_world[0].z, texture_from_world[1].z, texture_from_world[2].z, texture_from_world[3].z)), abs_receiver_point);
    float row_w_magnitude    = dot(abs(vec4(texture_from_world[0].w, texture_from_world[1].w, texture_from_world[2].w, texture_from_world[3].w)), abs_receiver_point);
    float h_w                = abs(position_in_light_texture_homogeneous.w);
    float abs_z              = abs(reference_depth);
    float projection_error   = 2.0 * (((((5.0 * u) * row_z_magnitude) + ((4.0 * u) * abs_z * row_w_magnitude)) / h_w) + (u * abs_z));
    float plane_c            = plane_determined ? abs(plane_in_texture.z) : (grazing_cos * D_length);
    float position_rounding  = get_light_relative_receiver_rounding(view_relative_position, receiver_point, light.view_origin.xyz) + get_vertex_position_rounding(receiver_point);
    float position_error     = position_rounding / (plane_c * h_w);
    float raster_error       = (4.0 * u) * light.view_origin.w;
    vec2  tap_reach_uv       = (tap_reach_texels + caster_snap_texels) / shadowmap_resolution;
    float tap_reach_lateral  = (tap_reach_uv.x * length(D_u)) + (tap_reach_uv.y * length(D_v));
    float gradient_error     = plane_determined
        ? ((normal_error * (tap_reach_lateral + (dot(abs(dz_dUV), tap_reach_uv) * D_length))) / (abs(plane_in_texture.z) - (normal_error * D_length)))
        : ((grazing_tan * tap_reach_lateral) / D_length);
#   if (ERHE_SHADOW_DEPTH_BITS >= 16) && (ERHE_SHADOW_DEPTH_BITS < 32)
#       if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
    const float format_error = 0.0;
#       else
    const float format_error = 1.0 / (exp2(float(ERHE_SHADOW_DEPTH_BITS)) - 1.0);
#       endif
#   else
    float format_error = (2.0 * u) * abs_z;
#   endif
    vec2  bias_scales  = light_block.shadow_bias_scales;
    float minimum_bias = (bias_scales.x * gradient_error) + (bias_scales.y * (projection_error + position_error + raster_error)) + format_error;
    // Added to every tap reference, like snap_bias.
    float tap_bias     = snap_bias + ((-cdd) * minimum_bias);

    // Shadow filtering method selected at compile time via the
    // ERHE_SHADOW_FILTER variant axis (set from the graphics preset's
    // Shadow_filter_mode; see shader_key.hpp / forward_renderer.cpp). The
    // value is the PCF kernel width in texels (0 = hard).
#   if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
    {
        // Single hardware depth comparison with nearest filter -- hard 0/1 edges.
        // One-sided slope bias to the fetched texel's centre (toward the
        // light only: a texel centre farther from the light already compares
        // lit). Direction-aware: reverse-Z keeps positive, forward-Z keeps negative.
        float slopeBias    = (-cdd) * max(0.0, (-cdd) * dot(texel_offset_nearest / shadowmap_resolution, dz_dUV));
        float D_ref_       = position_in_light_texture.z + slopeBias + tap_bias;
        // Direction-aware rounding: reverse-Z uses ceil (toward near=1),
        // forward-Z uses floor (toward near=0). Snap the reference to the shadow
        // depth format's quantization grid so it matches the hardware comparison
        // sampler's rounding (the RPDB article's D16_UNORM fix). The
        // ERHE_SHADOW_DEPTH_BITS variant axis carries the shadow map's depth bit
        // count: a fixed-point (UNORM) format is snapped to its 2^bits-1 levels
        // (65535 for 16-bit); D32_SFLOAT (bits >= 32, there is no 32-bit UNORM
        // depth) and bits == 0 (axis unset) have no coarse grid, so snapping
        // would only add bias -- skip it there.
        // Clamp into the shadow map depth range so receivers outside the
        // fitted range compare correctly (see comment above).
#       if (ERHE_SHADOW_DEPTH_BITS >= 16) && (ERHE_SHADOW_DEPTH_BITS < 32)
        const float shadow_depth_levels = exp2(float(ERHE_SHADOW_DEPTH_BITS)) - 1.0; // 65535 for 16-bit
        float D_ref        = clamp((-cdd) * ceil((-cdd) * D_ref_ * shadow_depth_levels) / shadow_depth_levels, 0.0, 1.0);
#       else
        float D_ref        = clamp(D_ref_, 0.0, 1.0);
#       endif
        // Fetched at the centre of nearest_texel, so the hardware selects
        // exactly the texel texel_offset_nearest measures to.
        vec4  uv_ref_layer = vec4((nearest_texel + 0.5) / shadowmap_resolution, array_layer, D_ref);
        return texture(s_shadow_compare, uv_ref_layer);
    }
#   elif ERHE_SHADOW_FILTER == 2
    {
        // 2x2 PCF: a single textureGather() with bilinear weighting of the
        // four comparison results, for a smooth sub-texel-accurate edge.
        // Gathered at the corner the four texels of the gather_base set share,
        // so the hardware selects exactly that set.
        vec4 shadowDepths = textureGather(s_shadow_no_compare, vec3((gather_base + 1.0) / shadowmap_resolution, array_layer));

        // textureGather() returns the four texels in counter-clockwise order
        // starting from the upper-left: (0, 1), (1, 1), (1, 0), (0, 0) relative
        // to the lower-left texel of the set. Each reference is offset by the
        // one-sided slope bias to its own texel centre (direction-aware:
        // reverse-Z biases positive, forward-Z biases negative).
        const vec2 corners[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));
        vec4 surfaceZ = vec4(0.0);
        for (int t = 0; t < 4; ++t) {
            vec2 texel_uv_offset = (corners[t] - gather_fraction) / shadowmap_resolution;
            surfaceZ[t] = (-cdd) * max(0.0, (-cdd) * dot(texel_uv_offset, dz_dUV)) + tap_bias;
        }
        vec2 fracCoords = clamp(gather_fraction, 0.0, 1.0);

        // Clamp into the shadow map depth range so receivers outside the
        // fitted range compare correctly (see comment above).
        surfaceZ = clamp(surfaceZ + position_in_light_texture.z, 0.0, 1.0);

        // Compare the four surface depths with shadow map depths
        // (direction-aware, non-strict to match the gequal / lequal
        // hardware comparison so clamped references resolve to lit on
        // far-clear texels)
        bvec4 attenuationMask = greaterThanEqual((-cdd) * surfaceZ, (-cdd) * shadowDepths);
        vec4 attenuation4 = vec4(attenuationMask); // convert bools to floats (0.0/1.0)

        // Bilinear interpolation between the four samples
        float attenuation = mix(
            mix(attenuation4[3], attenuation4[2], fracCoords.x),
            mix(attenuation4[0], attenuation4[1], fracCoords.x),
            fracCoords.y
        );

        return attenuation;
    }
#   else
    {
        // KxK box PCF (K = ERHE_SHADOW_FILTER, even) built from (K/2)^2
        // textureGather() calls on the non-comparison shadow sampler. Each of
        // the K*K texels is compared against a slope-following reference depth
        // and the binary results are averaged. K and the loop bounds are
        // compile-time constants so the loops unroll.
        const int  K       = ERHE_SHADOW_FILTER;
        const int  gathers = K / 2;
        // Texel positions of the four components textureGather() returns,
        // relative to the lower-left texel of its 2x2 set, in the GL order
        // (top-left, top-right, bottom-right, bottom-left).
        const vec2 corners[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));

        float visibility = 0.0;
        for (int gj = 0; gj < gathers; ++gj) {
            for (int gi = 0; gi < gathers; ++gi) {
                // Gather sets tile the KxK block, spaced 2 texels apart and
                // centred on the gather_base set. Each is gathered at the
                // corner its four texels share, so the hardware selects
                // exactly the set gather_texel names, and every tap's offset
                // is measured from the sample point with gather_fraction.
                vec2 gather_texel = (vec2(float(gi), float(gj)) - float(gathers - 1) * 0.5) * 2.0;
                vec2 guv          = (gather_base + gather_texel + 1.0) / shadowmap_resolution;
                vec4 depths       = textureGather(s_shadow_no_compare, vec3(guv, array_layer));
                for (int t = 0; t < 4; ++t) {
                    // Sample point -> centre of this tap's texel.
                    vec2 texel_uv_offset = (gather_texel + corners[t] - gather_fraction) / shadowmap_resolution;
#       if ERHE_SHADOW_BIAS == ERHE_SHADOW_BIAS_RECEIVER_PLANE
                    // Receiver-plane depth bias: follow the receiver's depth
                    // gradient (signed, in depth space) to this texel's centre,
                    // so the reference is the receiver plane's depth there.
                    // Unlike an offset-scaled one-sided bias it adds no net
                    // bias, so the contact shadow stays attached no matter how
                    // wide the kernel is.
                    float ref = clamp(position_in_light_texture.z + dot(texel_uv_offset, dz_dUV) + tap_bias, 0.0, 1.0);
#       else // ERHE_SHADOW_BIAS_SLOPE_SCALED
                    // Previous method: a one-sided slope bias scaled by the
                    // tap's offset from the sample. The bias grows with the
                    // kernel radius, which detaches the contact shadow
                    // (peter-panning) on wide kernels.
                    float bias = (-cdd) * max(0.0, (-cdd) * dot(texel_uv_offset, dz_dUV));
                    float ref  = clamp(position_in_light_texture.z + bias + tap_bias, 0.0, 1.0);
#       endif
                    // Non-strict, direction-aware comparison to match the
                    // gequal / lequal hardware sampler.
                    visibility += ((-cdd) * ref >= (-cdd) * depths[t]) ? 1.0 : 0.0;
                }
            }
        }
        return visibility / float(K * K);
    }
#   endif // ERHE_SHADOW_FILTER
#else // defined(ERHE_SHADOW_MAPS)
    return 1.0;
#endif
}

// Omnidirectional point-light shadow lookup (derivation in
// doc/erhe/point_light_shadows.md "Receiver bias"). The caster rasterizes
// both faces (cull_none) and stores, per cube texel, the radial distance of
// the nearest caster plane on the texel's centre ray (standard.frag,
// VARIANT_SHADOW_CUBE): the lit receiver's own surface is in the cube, so
// the comparison is a tie.
//
// The lookup is a single nearest fetch at the centre of the texel that
// contains the receiver direction (get_point_shadow_texel_centre()), which
// the hardware selects without ambiguity. The reference is the receiver
// plane's radial distance on that same centre ray, one-sided (a centre whose
// plane point is farther from the light already compares lit): in real
// arithmetic it equals what the caster stored for the receiver's own
// surface, like the tap offsets of sample_light_visibility(). The plane is
// the receiver's geometric normal (receiver_plane, from
// get_receiver_geometric_normal()), tilted to the R1 grazing limit
// N . L = 0.05 below it; an undetermined plane takes the smallest centre-ray
// distance of every plane R1 admits.
//
// The reference then moves toward the light by the sum of error bounds, in
// world units along the ray (u = 2^-24, e = 2 get_position_rounding(P), the
// error of one position derivative; P is relative to the light, the cube
// passes' view origin, and |P| stands in for the caster's position
// magnitudes):
//  - gradient: a normal error dN moves a plane's centre-ray distance by
//    dN . (X - X_c) / (N' . d_c), X the point the plane was taken through and
//    X_c its point on the centre ray. Receiver: its normal's error bound over
//    the computed |P - X_c|. Caster: its normal's error bound
//    e (2a + e) / a^2, a = 2 r / (resolution |q|^2) the smallest world size
//    of a cube pixel at the centre q, over half the texel diagonal on the
//    plane, sqrt(2) r / (resolution |q| |N . d_c|); the caster stores its
//    plane only at a computed |N . d_c| >= erhe_point_shadow_plane_cos_min,
//    which bounds the denominator.
//  - position: the rounding of the receiver point
//    (get_light_relative_receiver_rounding()) and of the caster's
//    interpolated point (get_position_rounding()) moves each plane along its
//    normal, the centre-ray distance by that over |N . d_c|.
//  - evaluation: each side's plane distance |N . v| / |N . d_c| (v and d_c
//    rounded, dots gamma_3, the divide): 12u / |N . d_c| + 5u relative; the
//    receiver's own length() when it is the reference, 13u.
// The rasterizer's placement of the caster's interpolated point (vertex
// snap, barycentric precision) moves it within the caster's plane only, so
// it does not reach the stored distance; R32F stores the caster's fp32 value
// exactly; the caster's position is relative to the light (its cube passes'
// view origin) and so is the receiver's, and both select the same texel
// centre. What the vertex snap does change is coverage: a caster
// triangle covers pixel centres up to one snap step (1/256 pixel per face
// axis) past its true edge, and there the stored plane is that triangle's,
// extended, not the receiver's. At a convex crease of the receiver's own
// mesh the extended neighbour is nearer the light, by up to the step times
// the two planes' radial-distance slopes; snap_bias covers the receiver's
// slope, as snap_bias does for the 2D maps. A neighbour steeper than the
// receiver is covered by the caster, which stores the farther of its plane
// distance and its interpolated point's own distance (standard.frag,
// VARIANT_SHADOW_CUBE). light_block.shadow_bias_scales applies as for
// the 2D maps: x scales the gradient term, y the position + evaluation
// terms; snap_bias is unscaled. Returns 1.0 (lit) when shadow maps are
// disabled.
//
// view_relative_position: the receiver relative to this pass's view origin;
// light_view_origin: the light block's view_origin of the light (its
// position, the view origin of its cube passes, which store relative to it).
float sample_point_light_visibility(vec3 view_relative_position, vec3 light_view_origin, float cube_index, vec4 receiver_plane)
{
#if defined(ERHE_SHADOW_MAPS)
    const float u            = erhe_fp32_unit_roundoff;
    const float grazing_cos  = 0.05;
    const float grazing_tan  = sqrt(1.0 - (grazing_cos * grazing_cos)) / grazing_cos;
    vec3  light_to_receiver  = get_light_relative_position(view_relative_position, light_view_origin);
    float current            = length(light_to_receiver);
    vec3  receiver_direction = light_to_receiver / current;

    float resolution       = float(textureSize(s_shadow_cube, 0).x);
    vec3  centre           = get_point_shadow_texel_centre(light_to_receiver, resolution);
    float centre_length    = length(centre);
    vec3  centre_direction = centre / centre_length;
    float stored           = textureLod(s_shadow_cube, vec4(centre, cube_index), 0.0).r;

    // Receiver plane, determined and clamped as in sample_light_visibility().
    vec3  receiver_normal  = receiver_plane.xyz;
    float normal_error     = receiver_plane.w;
    float receiver_cos     = dot(receiver_normal, receiver_direction);
    bool  plane_determined = (dot(receiver_normal, receiver_normal) > 0.0) && (normal_error < (0.5 * max(abs(receiver_cos), grazing_cos)));
    vec3  plane_normal     = plane_determined ? receiver_normal : receiver_direction;
    if (plane_determined && (abs(receiver_cos) < grazing_cos)) {
        vec3  tangent_direction = normalize(receiver_normal - (receiver_cos * receiver_direction));
        float side              = (receiver_cos < 0.0) ? -1.0 : 1.0;
        plane_normal = ((side * grazing_cos) * receiver_direction) + (sqrt(1.0 - (grazing_cos * grazing_cos)) * tangent_direction);
    }

    // Radial distance of the plane on the centre ray. The minimum
    // point_shadow_resolution keeps the half-texel diagonal (sqrt(2) /
    // resolution rad at a face centre) below the grazing angle asin(0.05), so
    // the centre ray meets every plane R1 admits in front of the light.
    // Undetermined plane: a plane through P tilted from head-on by alpha
    // toward the centre ray meets it at
    // current / (d_r . d_c + tan(alpha) |d_r x d_c|), smallest at the grazing
    // limit.
    float centre_cos;
    float centre_distance;
    if (plane_determined) {
        centre_cos      = abs(dot(plane_normal, centre_direction));
        centre_distance = abs(dot(plane_normal, light_to_receiver)) / centre_cos;
    } else {
        centre_cos      = grazing_cos;
        centre_distance = current / (dot(receiver_direction, centre_direction) + (grazing_tan * length(cross(receiver_direction, centre_direction))));
    }
    float reference = min(current, centre_distance);

    // Caster coverage snap (see the function comment): the derivative of the
    // plane's radial distance with respect to the face coordinates q,
    // r (q / |q|^2 - N / (N . q)) at the centre, over the two face axes, times
    // the 1/256 pixel step (a texel is 2 / resolution face units). An
    // undetermined plane bounds each face-axis component by
    // r (1 / |q| + 1 / (0.05 |q|)).
    const float caster_snap_texels = 1.0 / 256.0;
    float snap_step      = caster_snap_texels * (2.0 / resolution);
    vec3  distance_slope = plane_determined
        ? (centre_distance * abs((centre / (centre_length * centre_length)) - (plane_normal / dot(plane_normal, centre))))
        : vec3((centre_distance * (1.0 + (1.0 / grazing_cos))) / centre_length);
    distance_slope[get_point_shadow_major_axis(centre)] = 0.0;
    float snap_bias      = snap_step * (distance_slope.x + distance_slope.y + distance_slope.z);

    // Minimum bias (see the function comment).
    float caster_rounding    = get_position_rounding(light_to_receiver);
    float receiver_rounding  = get_light_relative_receiver_rounding(view_relative_position, light_to_receiver, light_view_origin);
    float derivative_error   = 2.0 * caster_rounding;
    float receiver_lateral   = length(light_to_receiver - (centre_distance * centre_direction));
    // The undetermined reference already takes every admissible receiver plane.
    float receiver_gradient  = plane_determined ? ((normal_error * receiver_lateral) / (centre_cos - normal_error)) : 0.0;
    float cube_pixel_size    = (2.0 * centre_distance) / (resolution * centre_length * centre_length);
    float caster_normal_error = (derivative_error * ((2.0 * cube_pixel_size) + derivative_error)) / (cube_pixel_size * cube_pixel_size);
    float caster_lateral     = (sqrt(2.0) * centre_distance) / (resolution * centre_length * centre_cos);
    float caster_gradient    = (caster_normal_error * caster_lateral) / max(centre_cos - caster_normal_error, erhe_point_shadow_plane_cos_min);
    float position_error     = (receiver_rounding + caster_rounding) / centre_cos;
    float evaluation_error   = u * ((((24.0 / centre_cos) + 10.0) * centre_distance) + (13.0 * current));
    vec2  bias_scales        = light_block.shadow_bias_scales;
    float minimum_bias       = (bias_scales.x * (receiver_gradient + caster_gradient)) + (bias_scales.y * (position_error + evaluation_error));

    // A reference no farther from the light than the stored distance is lit.
    return ((reference - snap_bias - minimum_bias) > stored) ? 0.0 : 1.0;
#else
    return 1.0;
#endif
}

#endif // ERHE_LIGHT_GLSL
