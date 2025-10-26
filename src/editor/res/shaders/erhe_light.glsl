#ifndef ERHE_LIGHT_GLSL
#define ERHE_LIGHT_GLSL

#include "erhe_texture.glsl"
#extension GL_ARB_derivative_control : enable

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

#   if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2DArrayShadow s_shadow_compare    = sampler2DArrayShadow(light_block.shadow_texture_compare);
    sampler2DArray       s_shadow_no_compare = sampler2DArray      (light_block.shadow_texture_no_compare);
#   endif

    Light light                                 = light_block.lights[light_index];
    float array_layer                           = float(light_index);
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;
    vec3  position_in_light_texture             = position_in_light_texture_homogeneous.xyz / position_in_light_texture_homogeneous.w;
    if (position_in_light_texture.z <= 0.0) {
        return 1.0;
    }

    // What follows is based on https://renderdiagrams.org/2024/12/18/shadowmap-bias/
    // Note: erhe uses reverse Z.

    // First, a common part:
    vec2  dU_dXY = vec2(dFdxFine(position_in_light_texture.x), dFdyFine(position_in_light_texture.x));
    vec2  dV_dXY = vec2(dFdxFine(position_in_light_texture.y), dFdyFine(position_in_light_texture.y));
    vec2  dz_dXY = vec2(dFdxFine(position_in_light_texture.z), dFdyFine(position_in_light_texture.z));
    float detJ   = dU_dXY.x * dV_dXY.y - dV_dXY.x * dU_dXY.y;
    vec2  dz_dUV = vec2(0.0);
    if (detJ != 0.0) {
        dz_dUV.x = dot(vec2( dV_dXY.y, -dV_dXY.x), dz_dXY) / detJ;
        dz_dUV.y = dot(vec2(-dU_dXY.y,  dU_dXY.x), dz_dXY) / detJ;
    }
    vec2 shadowmap_resolution = textureSize(s_shadow_compare, 0).xy;

    // Below are three paths, one of which can be enabled at time:
    // - Path 1: PCF via textureGather(), enabled by default
    // - Path 2: Hardware depth texture comparison with nearest filter
    // - Path 3: Shader depth texture comparison

#   if 1 // Path 1: PCF via textureGather(), enabled by default
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

        // Apply biases
        surfaceZ[0] += 2.0 * max(0.0, dot(offsets[0] * (1.0 / shadowmap_resolution) * vec2(      fracCoords.x, 1.0 - fracCoords.y), dz_dUV));
        surfaceZ[1] += 2.0 * max(0.0, dot(offsets[1] * (1.0 / shadowmap_resolution) * vec2(1.0 - fracCoords.x, 1.0 - fracCoords.y), dz_dUV));
        surfaceZ[2] += 2.0 * max(0.0, dot(offsets[2] * (1.0 / shadowmap_resolution) * vec2(1.0 - fracCoords.x,       fracCoords.y), dz_dUV));
        surfaceZ[3] += 2.0 * max(0.0, dot(offsets[3] * (1.0 / shadowmap_resolution) * vec2(      fracCoords.x,       fracCoords.y), dz_dUV));

        surfaceZ += position_in_light_texture.z;

        // Compare the four surface depths with shadow map depths
        bvec4 attenuationMask = greaterThan(surfaceZ, shadowDepths);
        vec4 attenuation4 = vec4(attenuationMask) * 1.0; // convert bools to floats (0.0/1.0)

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
        //float slopeBias = dot(distanceToTexelCenterUV, dz_dUV);
        //float slopeBias = min(0, dot(distanceToTexelCenterUV, dz_dUV));
        float slopeBias = max(0, dot(distanceToTexelCenterUV, dz_dUV));

#       if 1 // Path 2: Hardware depth texture comparison with nearest filter
        {
            float D_ref_       = position_in_light_texture.z;
            float D_ref        = D_ref_ + 2.0 * slopeBias;
            //float D_ref        = floor(D_ref_ * 65535.0) / 65535.0;
            vec4  uv_ref_layer = vec4(position_in_light_texture.xy, array_layer, D_ref);
            float hw_compare   = texture(s_shadow_compare, uv_ref_layer);
            return hw_compare;
        }
#       else // Path 3: Shader depth texture comparison
        {   // Shader comparison - NOTE: Using reverse Z
            float D_ref        = position_in_light_texture.z + 2.0 * slopeBias;
            vec3  uv_layer     = vec3(position_in_light_texture.xy, array_layer);
            float depth_sample = texture(s_shadow_no_compare, uv_layer).r;
            return ceil(D_ref * 65535.0) / 65535.0 > depth_sample ? 1.0 : 0.0;
        }
#       endif
    }
#   endif // Implement PCF using textureGather()
#else // defined(ERHE_SHADOW_MAPS)
    return 1.0;
#endif
}

#endif // ERHE_LIGHT_GLSL
