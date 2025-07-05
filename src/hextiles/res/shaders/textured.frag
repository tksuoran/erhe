layout(location = 0) in      vec2  v_texcoord;
layout(location = 1) in flat uvec2 v_texture;

float srgb_to_linear(float x)
{
    if (x <= 0.04045) {
        return x / 12.92;
    } else {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec4 srgb_to_linear(vec4 v)
{
    return vec4(
        srgb_to_linear(v.r),
        srgb_to_linear(v.g),
        srgb_to_linear(v.b),
        v.a
    );
}

vec4 sample_texture(vec2 texcoord)
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return texture(s_texture, v_texcoord);
#else
    return texture(s_texture[v_texture.x], v_texcoord);
#endif
}

vec4 sample_texture_lod_bias(vec2 texcoord, float lod_bias)
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return texture(s_texture, texcoord, lod_bias);
#else
    return texture(s_texture[v_texture.x], texcoord, lod_bias);
#endif
}

vec2 get_texture_size()
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[v_texture.x], 0);
#endif
}

void main()
{
    out_color = sample_texture(v_texcoord);
}
