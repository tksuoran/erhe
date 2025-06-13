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
# if ERHE_GLSL_VERSION < 420
    switch (texture_handle.x) {
        case  0: return texture(s_texture[0], texcoord);
        case  1: return texture(s_texture[1], texcoord);
        case  2: return texture(s_texture[2], texcoord);
        case  3: return texture(s_texture[3], texcoord);
        case  4: return texture(s_texture[4], texcoord);
        case  5: return texture(s_texture[5], texcoord);
        case  6: return texture(s_texture[6], texcoord);
        case  7: return texture(s_texture[7], texcoord);
        case  8: return texture(s_texture[8], texcoord);
        case  9: return texture(s_texture[9], texcoord);
        case 10: return texture(s_texture[0], texcoord);
        case 11: return texture(s_texture[1], texcoord);
        case 12: return texture(s_texture[2], texcoord);
        case 13: return texture(s_texture[3], texcoord);
        default: return vec4(0.0, 0.0, 0.0, 1.0);
    }
# else
    return texture(s_texture[texture_handle.x], texcoord);
# endif
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
# if ERHE_GLSL_VERSION < 420
    switch (texture_handle.x) {
        case  0: return texture(s_texture[0], texcoord, lod_bias);
        case  1: return texture(s_texture[1], texcoord, lod_bias);
        case  2: return texture(s_texture[2], texcoord, lod_bias);
        case  3: return texture(s_texture[3], texcoord, lod_bias);
        case  4: return texture(s_texture[4], texcoord, lod_bias);
        case  5: return texture(s_texture[5], texcoord, lod_bias);
        case  6: return texture(s_texture[6], texcoord, lod_bias);
        case  7: return texture(s_texture[7], texcoord, lod_bias);
        case  8: return texture(s_texture[8], texcoord, lod_bias);
        case  9: return texture(s_texture[9], texcoord, lod_bias);
        case 10: return texture(s_texture[0], texcoord, lod_bias);
        case 11: return texture(s_texture[1], texcoord, lod_bias);
        case 12: return texture(s_texture[2], texcoord, lod_bias);
        case 13: return texture(s_texture[3], texcoord, lod_bias);
        default: return vec4(0.0, 0.0, 0.0, 1.0);
    }
# else
    return texture(s_texture[texture_handle.x], texcoord, lod_bias);
# endif
#endif
}

vec2 get_texture_size(uvec2 texture_handle) {
    if (texture_handle.x == max_u32) {
        return vec2(1.0, 1.0);
    }
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return vec2(textureSize(s_texture, 0));
#else
# if ERHE_GLSL_VERSION < 420
    switch (texture_handle.x) {
        case  0: return vec2(textureSize(s_texture[ 0], 0));
        case  1: return vec2(textureSize(s_texture[ 1], 0));
        case  2: return vec2(textureSize(s_texture[ 2], 0));
        case  3: return vec2(textureSize(s_texture[ 3], 0));
        case  4: return vec2(textureSize(s_texture[ 4], 0));
        case  5: return vec2(textureSize(s_texture[ 5], 0));
        case  6: return vec2(textureSize(s_texture[ 6], 0));
        case  7: return vec2(textureSize(s_texture[ 7], 0));
        case  8: return vec2(textureSize(s_texture[ 8], 0));
        case  9: return vec2(textureSize(s_texture[ 9], 0));
        case 10: return vec2(textureSize(s_texture[10], 0));
        case 11: return vec2(textureSize(s_texture[11], 0));
        case 12: return vec2(textureSize(s_texture[12], 0));
        case 13: return vec2(textureSize(s_texture[13], 0));
        default: return vec2(0.0, 0.0);
    }
# else
    return vec2(textureSize(s_texture[texture_handle.x], 0));
# endif
#endif
}

#endif // ERHE_TEXTURE_GLSL
