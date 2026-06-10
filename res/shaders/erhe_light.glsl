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

float get_range_attenuation(float range, float distance) {
    if (range <= 0.0) {
        return 1.0 / pow(distance, 2.0); // negative range means unlimited
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
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

float sample_light_visibility(vec4 position, uint light_index, float N_dot_L) {
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
    //  - "What if the det is zero?" - I don't know. If you do, please let me know.
    //      - I can only hope dz_dUV being left to zero is ok.
    //  - HLSL code has been converted to GLSL
    //  - clip_depth_direction: -1.0 for reverse Z, 1.0 for forward Z
    //      - bias and comparisons are adjusted accordingly
    //  - For reasons I do not fully yet understand, I had to scale the bias
    //      - Code uses 2.0 at the moment, but smaller values seem to also work.

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

    // First, a common part:
    vec2  dU_dXY = vec2(ERHE_DFDX(position_in_light_texture.x), ERHE_DFDY(position_in_light_texture.x));
    vec2  dV_dXY = vec2(ERHE_DFDX(position_in_light_texture.y), ERHE_DFDY(position_in_light_texture.y));
    vec2  dz_dXY = vec2(ERHE_DFDX(position_in_light_texture.z), ERHE_DFDY(position_in_light_texture.z));
    float detJ   = dU_dXY.x * dV_dXY.y - dV_dXY.x * dU_dXY.y;
    vec2  dz_dUV = vec2(0.0);
    if (detJ != 0.0) {
        dz_dUV.x = dot(vec2( dV_dXY.y, -dV_dXY.x), dz_dXY) / detJ;
        dz_dUV.y = dot(vec2(-dU_dXY.y,  dU_dXY.x), dz_dXY) / detJ;
    }
    vec2 shadowmap_resolution = textureSize(s_shadow_no_compare, 0).xy;

    // Shadow filtering method selected at compile time via the
    // ERHE_SHADOW_FILTER variant axis (set from the graphics preset's
    // Shadow_filter_mode; see shader_key.hpp / forward_renderer.cpp). The
    // value is the PCF kernel width in texels (0 = hard).
#   if ERHE_SHADOW_FILTER == ERHE_SHADOW_FILTER_HARD
    {
        // Single hardware depth comparison with nearest filter -- hard 0/1 edges.
        vec2  distanceToTexelCenterUV = vec2(0.5) - fract(position_in_light_texture.xy * shadowmap_resolution);
        distanceToTexelCenterUV      *= 1.0 / shadowmap_resolution;
        // Direction-aware slope bias: reverse-Z keeps positive, forward-Z keeps negative
        float slopeBias    = (-cdd) * max(0.0, (-cdd) * dot(distanceToTexelCenterUV, dz_dUV));
        float D_ref_       = position_in_light_texture.z + 2.0 * slopeBias;
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
        // Note: adding 1/512 to make sure fract() and textureGather() work on the same set of texels
        // See: https://www.reedbeta.com/blog/texture-gathers-and-coordinate-precision/
        vec2 fracCoords = fract(position_in_light_texture.xy * shadowmap_resolution - 0.5 + 1.0 / 512.0);

        // Sample the shadow atlas using textureGather.
        // Equivalent to HLSL's Gather, returns four depth samples.
        vec4 shadowDepths = textureGather(s_shadow_no_compare, vec3(position_in_light_texture.xy, array_layer));

        // The four samples that would contribute to filtering are placed into xyzw
        // in counter-clockwise order starting from the lower-left sample.
        // Offsets correspond to deltas at (-,+),(+,+),(+,-),(-,-)
        const vec2 offsets[4] = vec2[4](
            vec2(-1.0,  1.0),
            vec2( 1.0,  1.0),
            vec2( 1.0, -1.0),
            vec2(-1.0, -1.0)
        );

        vec4 surfaceZ = vec4(0.0);

        // Apply biases (direction-aware: reverse-Z biases positive, forward-Z biases negative)
        surfaceZ[0] += 2.0 * (-cdd) * max(0.0, (-cdd) * dot(offsets[0] * (1.0 / shadowmap_resolution) * vec2(      fracCoords.x, 1.0 - fracCoords.y), dz_dUV));
        surfaceZ[1] += 2.0 * (-cdd) * max(0.0, (-cdd) * dot(offsets[1] * (1.0 / shadowmap_resolution) * vec2(1.0 - fracCoords.x, 1.0 - fracCoords.y), dz_dUV));
        surfaceZ[2] += 2.0 * (-cdd) * max(0.0, (-cdd) * dot(offsets[2] * (1.0 / shadowmap_resolution) * vec2(1.0 - fracCoords.x,       fracCoords.y), dz_dUV));
        surfaceZ[3] += 2.0 * (-cdd) * max(0.0, (-cdd) * dot(offsets[3] * (1.0 / shadowmap_resolution) * vec2(      fracCoords.x,       fracCoords.y), dz_dUV));

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
        // CCW texel offsets of the four components textureGather() returns,
        // matching the GL order (top-left, top-right, bottom-right, bottom-left).
        const vec2 sub[4]  = vec2[4](vec2(-0.5, 0.5), vec2(0.5, 0.5), vec2(0.5, -0.5), vec2(-0.5, -0.5));

#       if ERHE_SHADOW_BIAS == ERHE_SHADOW_BIAS_RECEIVER_PLANE
        // The ONLY additive bias: a small slope bias toward the texel center,
        // identical to the hard / 2x2 paths and independent of the kernel
        // radius. Keeping it kernel-size-independent is what stops wide
        // kernels from peter-panning (an offset-scaled bias would detach the
        // contact shadow as K grows).
        vec2  distance_to_texel_center = (vec2(0.5) - fract(position_in_light_texture.xy * shadowmap_resolution)) / shadowmap_resolution;
        float center_bias = 2.0 * (-cdd) * max(0.0, (-cdd) * dot(distance_to_texel_center, dz_dUV));
        float base_ref    = position_in_light_texture.z + center_bias;
#       endif

        float visibility = 0.0;
        for (int gj = 0; gj < gathers; ++gj) {
            for (int gi = 0; gi < gathers; ++gi) {
                // Gather centers tile the KxK block, spaced 2 texels apart and
                // centered on the sample point.
                vec2 gather_texel = (vec2(float(gi), float(gj)) - float(gathers - 1) * 0.5) * 2.0;
                vec2 guv          = position_in_light_texture.xy + gather_texel / shadowmap_resolution;
                vec4 depths       = textureGather(s_shadow_no_compare, vec3(guv, array_layer));
                for (int t = 0; t < 4; ++t) {
                    vec2 texel_uv_offset = (gather_texel + sub[t]) / shadowmap_resolution;
#       if ERHE_SHADOW_BIAS == ERHE_SHADOW_BIAS_RECEIVER_PLANE
                    // Receiver-plane depth bias: follow the receiver's depth
                    // gradient (signed, in depth space) to this texel so the
                    // reference tracks the surface plane. Unlike an
                    // offset-scaled one-sided bias it adds no net bias, so the
                    // contact shadow stays attached no matter how wide the
                    // kernel is.
                    float ref = clamp(base_ref + dot(texel_uv_offset, dz_dUV), 0.0, 1.0);
#       else // ERHE_SHADOW_BIAS_SLOPE_SCALED
                    // Previous method: a one-sided slope bias scaled by the
                    // tap's offset from the sample. The bias grows with the
                    // kernel radius, which detaches the contact shadow
                    // (peter-panning) on wide kernels.
                    float bias = 2.0 * (-cdd) * max(0.0, (-cdd) * dot(texel_uv_offset, dz_dUV));
                    float ref  = clamp(position_in_light_texture.z + bias, 0.0, 1.0);
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

#endif // ERHE_LIGHT_GLSL
