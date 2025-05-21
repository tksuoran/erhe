#ifndef ERHE_LIGHT_GLSL
#define ERHE_LIGHT_GLSL

#include "erhe_texture.glsl"

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

#if defined(ERHE_BINDLESS_TEXTURE)
    //sampler2DArray s_shadow = sampler2DArray(light_block.shadow_texture_no_compare);
    sampler2DArrayShadow s_shadow_compare = sampler2DArrayShadow(light_block.shadow_texture_compare);
#endif

    Light light       = light_block.lights[light_index];
    float array_layer = float(light_index);
    //vec4  position_in_light_clip    = light.clip_from_world * position;
    //vec3  position_in_light_ndc     = position_in_light_clip.xyz / position_in_light_clip.w;
    //vec4  position_in_light_texture = texture_from_ndc * vec4(position_in_light_ndc, 1.0);
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;
    vec3  position_in_light_texture = position_in_light_texture_homogeneous.xyz / position_in_light_texture_homogeneous.w;
    if (position_in_light_texture.z <= 0.0) {
        return 1.0;
    }

    // TODO compute sqrt(1.0 - N_dot_L * N_dot_L) / N_dot_L in outer scope
    float bias_        = light_block.shadow_bias_scale * sqrt(1.0 - N_dot_L * N_dot_L) / N_dot_L;
    float bias         = clamp(bias_, light_block.shadow_min_bias, light_block.shadow_max_bias);

    float D_ref        = position_in_light_texture.z + bias;
    //vec4  uv_ref_layer = vec4(position_in_light_texture.xy, D_ref, array_layer);
    vec4  uv_ref_layer = vec4(position_in_light_texture.xy, array_layer, D_ref);
    //vec3  uv_layer     = vec3(position_in_light_texture.xy, array_layer);
    //float D_t          = texture(s_shadow_no_compare, uv_layer).x;
    //float compare      = (D_ref >= D_t) ? 1.0 : 0.0;
    float hw_compare   = texture(s_shadow_compare, uv_ref_layer);
    return hw_compare;
    // float is_in = 1.0;
    // if (
    //     (position_in_light_texture.x < 0.0) ||
    //     (position_in_light_texture.x > 1.0) ||
    //     (position_in_light_texture.y < 0.0) ||
    //     (position_in_light_texture.y > 1.0)
    // ) {
    //     is_in = 0.1;
    // }

    //out_color = vec4(hw_compare, hw_compare, hw_compare, 1.0);

    //vec4  uv_reference_layer = vec4(
    //    position_in_light_texture.xy,
    //    position_in_light_texture.z,
    //    float(light_index)
    //);
    //return texture(s_shadow_compare, uv_reference_layer);
    ////float bias = max(light_block.shadow_bias_scale * sqrt(1.0 - N_dot_L * N_dot_L) / N_dot_L, light_block.shadow_min_bias);
    //vec4  uv_reference_layer = vec4(
    //    position_in_light_texture.xy,
    //    position_in_light_texture.z// - bias,
    //    float(light_index)
    //);
    //return texture(s_shadow_compare, uv_reference_layer);
#else
    return 1.0;
#endif
}

#endif // ERHE_LIGHT_GLSL
