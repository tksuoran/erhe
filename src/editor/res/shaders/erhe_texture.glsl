#ifndef ERHE_TEXTURE_GLSL
#define ERHE_TEXTURE_GLSL

// NOTE: Requires s_texture

const uint max_u32 = 4294967295u;

vec4 sample_texture(uvec2 texture_handle, vec2 texcoord) {
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, texcoord);
#else
    return texture(s_texture[texture_handle.x], texcoord);
#endif
}

vec4 sample_texture_lod_bias(uvec2 texture_handle, vec2 texcoord, float lod_bias) {
    if (texture_handle.x == max_u32) {
        return vec4(1.0, 1.0, 1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return texture(s_texture, texcoord, lod_bias);
#else
    return texture(s_texture[texture_handle.x], texcoord, lod_bias);
#endif
}

vec2 get_texture_size(uvec2 texture_handle) {
    if (texture_handle.x == max_u32) {
        return vec2(1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[texture_handle.x], 0);
#endif
}

#endif // ERHE_TEXTURE_GLSL
