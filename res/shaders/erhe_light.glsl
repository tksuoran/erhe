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

    // Below are three paths, one of which can be enabled at time:
    // - Path 1: PCF via textureGather(), enabled by default
    // - Path 2: Hardware depth texture comparison with nearest filter
    // - Path 3: Shader depth texture comparison

#   if 0 // Path 1: PCF via textureGather(), enabled by default
    {
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
        vec2 distanceToTexelCenterUV = vec2(0.5) - fract(position_in_light_texture.xy * shadowmap_resolution);
        distanceToTexelCenterUV *= 1.0 / shadowmap_resolution;
        // Direction-aware slope bias: reverse-Z keeps positive, forward-Z keeps negative
        float slopeBias = (-cdd) * max(0.0, (-cdd) * dot(distanceToTexelCenterUV, dz_dUV));

#       if 1 // Path 2: Hardware depth texture comparison with nearest filter
        {
            float D_ref_       = position_in_light_texture.z + 2.0 * slopeBias;
            // Direction-aware rounding: reverse-Z uses ceil (toward near=1), forward-Z uses floor (toward near=0)
            // Clamp into the shadow map depth range so receivers outside the
            // fitted range compare correctly (see comment above).
            float D_ref        = clamp((-cdd) * ceil((-cdd) * D_ref_ * 65535.0) / 65535.0, 0.0, 1.0);
            vec4  uv_ref_layer = vec4(position_in_light_texture.xy, array_layer, D_ref);
            float hw_compare   = texture(s_shadow_compare, uv_ref_layer);
            return hw_compare;
        }
#       else // Path 3: Shader depth texture comparison
        {   // Shader comparison - direction-aware
            float D_ref         = position_in_light_texture.z + 2.0 * slopeBias;
            vec3  uv_layer      = vec3(position_in_light_texture.xy, array_layer);
            float depth_sample  = texture(s_shadow_no_compare, uv_layer).r;
            // Clamp into the shadow map depth range so receivers outside the
            // fitted range compare correctly (see comment above). Non-strict
            // comparison to match the gequal / lequal hardware sampler.
            float D_ref_rounded = clamp((-cdd) * ceil((-cdd) * D_ref * 65535.0) / 65535.0, 0.0, 1.0);
            return ((-cdd) * D_ref_rounded) >= ((-cdd) * depth_sample) ? 1.0 : 0.0;
        }
#       endif
    }
#   endif // Implement PCF using textureGather()
#else // defined(ERHE_SHADOW_MAPS)
    return 1.0;
#endif
}

#endif // ERHE_LIGHT_GLSL
