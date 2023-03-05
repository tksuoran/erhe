in      vec2  v_texcoord;
in flat uvec2 v_texture;

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
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return texture(s_texture, v_texcoord);
#else
    return texture(s_texture[v_texture.x], v_texcoord);
#endif
}

vec4 sample_texture_lod_bias(vec2 texcoord, float lod_bias)
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return texture(s_texture, v_texcoord, lod_bias);
#else
    return texture(s_texture[v_texture.x], v_texcoord, lod_bias);
#endif
}

vec2 get_texture_size()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(v_texture);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[v_texture.x], 0);
#endif
}

void main()
{
    // This does not seem to give better image quality.
    // I observed only very little difference, except
    // when image was viewed in really steep angle.

    //const float lod_bias = -1.0;
    //const vec2  offsets  = vec2(0.125, 0.375); // rotated grid
    //const vec2  dx       = dFdx(v_texcoord);
    //const vec2  dy       = dFdy(v_texcoord);
    //const vec4  sample0  = sample_texture_lod_bias(v_texcoord + offsets.x * dx + offsets.y * dy, lod_bias);
    //const vec4  sample1  = sample_texture_lod_bias(v_texcoord - offsets.x * dx - offsets.y * dy, lod_bias);
    //const vec4  sample2  = sample_texture_lod_bias(v_texcoord + offsets.y * dx - offsets.x * dy, lod_bias);
    //const vec4  sample3  = sample_texture_lod_bias(v_texcoord - offsets.y * dx + offsets.x * dy, lod_bias);
    //const vec4  super    = 0.25 * (sample0 + sample1 + sample2 + sample3);
    //const vec4  normal   = sample_texture(v_texcoord);
    //out_color = vec4(diff.rgb * 100.0, 1.0);
    //out_color = sample0;
    //out_color = sample1;
    //out_color = vec4(dx * 20.0, 0.0, 1.0);
    //out_color = sample2;
    //out_color = super;
    //out_color = normal;
    out_color = sample_texture(v_texcoord);
}
