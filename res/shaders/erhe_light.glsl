#ifndef ERHE_LIGHT_GLSL
#define ERHE_LIGHT_GLSL

#include "erhe_camera_view.glsl"
#include "erhe_texture.glsl"

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
// preset's Shadow_technique_mode). depth = sample the depth map and apply the
// receiver-plane bias here; distance = sample the R32F distance map the caster
// already biased (fwidth) and compare with no receiver-side bias. The two share
// the ERHE_SHADOW_FILTER kernel sizes. See doc/shadows.md.
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

#if defined(ERHE_FRAGMENT_SHADER)
// Geometric normal of the receiver plane from the screen-space derivatives of
// the world position: exact for a planar triangle (both derivatives lie in its
// plane) and independent of smooth vertex normals and normal maps. The
// orientation follows the screen-space winding and is not meaningful;
// sample_light_visibility() accepts either. Call it in uniform control flow
// (before any per-light branch) so the quad's helper lanes take part.
vec3 get_receiver_geometric_normal(vec3 world_position) {
    return normalize(cross(ERHE_DFDX(world_position), ERHE_DFDY(world_position)));
}
#endif

// position: receiver world position (w = 1). receiver_normal: unit geometric
// normal of the receiver plane in world space, either orientation (see
// get_receiver_geometric_normal()); only the depth technique reads it.
float sample_light_visibility(vec4 position, uint light_index, vec3 receiver_normal) {
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
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;
    vec3  position_in_light_texture             = position_in_light_texture_homogeneous.xyz / position_in_light_texture_homogeneous.w;
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
    // Out-of-range XY is covered by the one texel empty border the shadow
    // pass scissor keeps around the map (clamp_to_edge then compares
    // against the far clear value, which always resolves to lit).

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

#if ERHE_SHADOW_TECHNIQUE == ERHE_SHADOW_TECHNIQUE_DISTANCE
    // Distance technique: the caster already baked the fwidth slope bias into
    // the stored distances, so compare the (unbiased, range-clamped) receiver
    // depth against the R32F distance map. No dz_dUV / receiver-plane work here.
    {
        float ref = clamp(position_in_light_texture.z, 0.0, 1.0);
        vec2  res = vec2(textureSize(s_shadow_distance, 0).xy);
#   if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
        float stored = textureLod(s_shadow_distance, vec3(position_in_light_texture.xy, array_layer), 0.0).r;
        return ((-cdd) * ref >= (-cdd) * stored) ? 1.0 : 0.0;
#   else
        // KxK PCF on the distance map: gather, compare unbiased, average. The
        // 2x2 mode uses K = 2 (one gather); wider modes use ERHE_SHADOW_FILTER.
        const int  K       = (ERHE_SHADOW_FILTER < 2) ? 2 : ERHE_SHADOW_FILTER;
        const int  gathers = K / 2;
        const vec2 sub[4]  = vec2[4](vec2(-0.5, 0.5), vec2(0.5, 0.5), vec2(0.5, -0.5), vec2(-0.5, -0.5));
        float visibility = 0.0;
        for (int gj = 0; gj < gathers; ++gj) {
            for (int gi = 0; gi < gathers; ++gi) {
                vec2 gather_texel = (vec2(float(gi), float(gj)) - float(gathers - 1) * 0.5) * 2.0;
                vec2 guv          = position_in_light_texture.xy + gather_texel / res;
                vec4 stored4      = textureGather(s_shadow_distance, vec3(guv, array_layer), 0);
                for (int t = 0; t < 4; ++t) {
                    visibility += ((-cdd) * ref >= (-cdd) * stored4[t]) ? 1.0 : 0.0;
                }
            }
        }
        return visibility / float(K * K);
#   endif
    }
#else

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
    mat4  world_from_texture  = light.world_from_texture;
    vec3  depth_axis_in_world = normalize(world_from_texture[2].xyz - position.xyz * world_from_texture[2].w);
    float receiver_cos        = dot(receiver_normal, depth_axis_in_world);
    const float grazing_cos   = 0.05;
    vec3  plane_normal        = receiver_normal;
    if (abs(receiver_cos) < grazing_cos) {
        vec3  tangent_direction = normalize(receiver_normal - (receiver_cos * depth_axis_in_world));
        float side              = (receiver_cos < 0.0) ? -1.0 : 1.0;
        plane_normal = (side * grazing_cos) * depth_axis_in_world + sqrt(1.0 - (grazing_cos * grazing_cos)) * tangent_direction;
    }
    vec4 plane_in_texture = transpose(world_from_texture) * vec4(plane_normal, -dot(plane_normal, position.xyz));
    vec2 dz_dUV           = -plane_in_texture.xy / plane_in_texture.z;
    vec2 shadowmap_resolution = textureSize(s_shadow_no_compare, 0).xy;

    // Texel geometry of the sample point, in texels. The hardware rounds
    // texel coordinates to its sub-texel precision (subTexelPrecisionBits,
    // typically 8) before it picks texels, so a coordinate within 1/512 texel
    // below a texel boundary picks the texel above it; the + 1.0 / 512.0 inside
    // floor() reproduces that selection
    // (https://www.reedbeta.com/blog/texture-gathers-and-coordinate-precision/).
    // The offsets below are measured from the unrounded coordinate, so they
    // are the exact distances to the centres of the texels actually fetched.
    //  - texel_offset_nearest: sample point -> centre of the texel a nearest
    //    fetch reads (texel centres at integer + 0.5).
    //  - gather_fraction: position of the sample point relative to the
    //    lower-left texel centre of the 2x2 set textureGather() reads
    //    (texel centres at integers); in [-1/512, 1 + 1/512).
    vec2 sample_texel           = position_in_light_texture.xy * shadowmap_resolution;
    vec2 texel_offset_nearest   = floor(sample_texel + 1.0 / 512.0) + 0.5 - sample_texel;
    vec2 gather_texel_position  = sample_texel - 0.5;
    vec2 gather_fraction        = gather_texel_position - floor(gather_texel_position + 1.0 / 512.0);

    // Caster vertex snap. The rasterizer snaps the caster's vertices to its
    // sub-pixel grid (subPixelPrecisionBits, typically 8) before it
    // interpolates depth, so the stored plane is the caster plane displaced by
    // at most one sub-texel step along each map axis, and its depth at a texel
    // centre differs by at most (|dz/du| + |dz/dv|) * step. Every reference
    // moves by that much toward the light.
    const float caster_snap_texels = 1.0 / 256.0;
    float snap_bias = (-cdd) * caster_snap_texels * dot(abs(dz_dUV), 1.0 / shadowmap_resolution);

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
        float D_ref_       = position_in_light_texture.z + slopeBias + snap_bias;
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
        vec4  uv_ref_layer = vec4(position_in_light_texture.xy, array_layer, D_ref);
        return texture(s_shadow_compare, uv_ref_layer);
    }
#   elif ERHE_SHADOW_FILTER == 2
    {
        // 2x2 PCF: a single textureGather() with bilinear weighting of the
        // four comparison results, for a smooth sub-texel-accurate edge.
        vec4 shadowDepths = textureGather(s_shadow_no_compare, vec3(position_in_light_texture.xy, array_layer));

        // textureGather() returns the four texels in counter-clockwise order
        // starting from the upper-left: (0, 1), (1, 1), (1, 0), (0, 0) relative
        // to the lower-left texel of the set. Each reference is offset by the
        // one-sided slope bias to its own texel centre (direction-aware:
        // reverse-Z biases positive, forward-Z biases negative).
        const vec2 corners[4] = vec2[4](vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0));
        vec4 surfaceZ = vec4(0.0);
        for (int t = 0; t < 4; ++t) {
            vec2 texel_uv_offset = (corners[t] - gather_fraction) / shadowmap_resolution;
            surfaceZ[t] = (-cdd) * max(0.0, (-cdd) * dot(texel_uv_offset, dz_dUV)) + snap_bias;
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
                // Gather centers tile the KxK block, spaced 2 texels apart and
                // centered on the sample point. The spacing is whole texels, so
                // every gather selects its set with the gather_fraction of the
                // sample point.
                vec2 gather_texel = (vec2(float(gi), float(gj)) - float(gathers - 1) * 0.5) * 2.0;
                vec2 guv          = position_in_light_texture.xy + gather_texel / shadowmap_resolution;
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
                    float ref = clamp(position_in_light_texture.z + dot(texel_uv_offset, dz_dUV) + snap_bias, 0.0, 1.0);
#       else // ERHE_SHADOW_BIAS_SLOPE_SCALED
                    // Previous method: a one-sided slope bias scaled by the
                    // tap's offset from the sample. The bias grows with the
                    // kernel radius, which detaches the contact shadow
                    // (peter-panning) on wide kernels.
                    float bias = (-cdd) * max(0.0, (-cdd) * dot(texel_uv_offset, dz_dUV));
                    float ref  = clamp(position_in_light_texture.z + bias + snap_bias, 0.0, 1.0);
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
#endif // ERHE_SHADOW_TECHNIQUE
#else // defined(ERHE_SHADOW_MAPS)
    return 1.0;
#endif
}

// Omnidirectional point-light shadow lookup. The caster stored the raw radial
// distance from the light into an R32F cube-map array (one cube / 6 faces per
// shadow-casting point light); here we sample it by the fragment->light
// direction (the samplerCubeArray selects the face automatically) at the
// light's cube layer and compare the stored nearest-occluder distance against
// this fragment's distance. A small world-space slope+constant bias avoids
// self-shadow acne without detaching the contact shadow. Returns 1.0 (lit) when
// shadow maps are disabled. See doc/forge-erhe.md / forge-point-light-shadows.
float sample_point_light_visibility(vec3 world_position, vec3 light_position, float cube_index)
{
#if defined(ERHE_SHADOW_MAPS)
    vec3  light_to_frag = world_position - light_position;
    float current       = length(light_to_frag);
    float bias          = max(0.05, 0.02 * current);
    float stored        = texture(s_shadow_cube, vec4(light_to_frag, cube_index)).r;
    return (current - bias > stored) ? 0.0 : 1.0;
#else
    return 1.0;
#endif
}

#endif // ERHE_LIGHT_GLSL
