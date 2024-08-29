in vec2 v_texcoord;

bool is_nan(float val)
{
    return ( val < 0.0 || 0.0 < val || val == 0.0 ) ? false : true;
}

float fix(float value)
{
    return is_nan(value) ? 0.0 : value;
}

vec3 fix(vec3 value)
{
    return vec3(
        fix(value.r),
        fix(value.g),
        fix(value.b)
    );
}


float rcp(float _v)
{
    return 1.0 / _v;
}

vec3 rcp(vec3 _v)
{
    return vec3(1.0) / _v;
}

float luminance(vec3 _color)
{
    vec3 W = vec3(0.2125, 0.7154, 0.0721);
    return dot(_color, W);
}

// https://github.com/hughsk/glsl-luma/blob/master/index.glsl
float luma(vec3 _color)
{
    vec3 W = vec3(0.299, 0.587, 0.114);
    return dot(_color, W);
}

vec3 tonemap(vec3 _c)
{
    return _c * rcp(max(_c.r, max(_c.g, _c.b)) + 1.0);
}

vec3 tonemap_with_weight(vec3 _c, float _w)
{
    return _c * (_w * rcp(max(_c.r, max(_c.g, _c.b)) + 1.0));
}

vec3 tonemap_invert(vec3 _c)
{
    return _c * rcp(1.0 - max(_c.r, max(_c.g, _c.b)));
}

vec3 tonemap_reinhard(vec3 color)
{
    return color / (color + vec3(1.0));
}

vec3 tonemap_reinhard2(vec3 color)
{
    return color / (color + vec3(1.0));
}

vec3 tonemap_simple(vec3 x)
{
    vec3 a = vec3(
        pow(x.r, 1.25),
        pow(x.g, 1.25),
        pow(x.b, 1.25)
    );
    return a / (a + vec3(1.0));
}

vec3 tonemap_ue3(vec3 x)
{
    // Used in Unreal Engine 3 up to 4.14. (I think, might be wrong).
    // They've since moved to ACES for output on a larger variety of devices.
    // Very simple and intented for use with color-lut afterwards.
    return x / (x + vec3(0.187)) * vec3(1.035);
}

vec3 tonemap_log(vec3 x)
{
    float D_max       =  1.0;
    float D_min       =  0.0;
    float tau         =  1.0;
    float small_value =  0.5 / 65536.0; // 1.0e-6
    float Y_in        =  0.2126 * x.r + 0.7152 * x.g + 0.0722 * x.b; // bt.709
    float Y_max       =  1.5;
    float Y_min       =  0.0;
    float nominator   = log(Y_in  + tau) - log(Y_min + tau);
    float denominator = log(Y_max + tau) - log(Y_min + tau);
    float Y_out       = (D_max - D_min) * (nominator / denominator) + D_min;
    float scale       = Y_out / max(Y_in, small_value);

    return x * scale;
    //return x * scale + Y_in * 0.03;
}

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    vec4 base_color = texture(sampler2D(post_processing.source_texture[0]), v_texcoord);
#else
    vec4 base_color = texture(s_source_textures[0], v_texcoord);
#endif
    vec3 sum = base_color.rgb;
    for (uint i = 1; i < post_processing.texture_count; ++i) {
        float scale = 0.02 / float(i + 1);
#if defined(ERHE_BINDLESS_TEXTURE)
        vec3 source = texture(sampler2D(post_processing.source_texture[i]), v_texcoord).rgb;
#else
        vec3 source = texture(s_source_textures[i], v_texcoord).rgb;
#endif
        sum += fix(scale * source);
    }

    vec3 color = sum;

    //out_color.rgb = color;
    color   = max(vec3(0.0), color);
    //color.r = max(0.0, color.r);
    //color.g = max(0.0, color.g);
    //color.b = max(0.0, color.b);
    //out_color.rgb = tonemap(color);
    //out_color.rgb = tonemap_reinhard(color);
    //out_color.rgb = tonemap_simple(color);
    //out_color.rgb = tonemap_ue3(color);
    out_color.rgb = tonemap_log(color);
    //out_color.rgb = color;
    out_color.a   = base_color.a;
}
