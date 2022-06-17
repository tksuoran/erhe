in vec2 v_texcoord;

float fix(float value)
{
    return isnan(value) || isinf(value)
        ? 0.0
        : value;
}

vec3 fix(vec3 value)
{
    return vec3(
        fix(value.r),
        fix(value.g),
        fix(value.b)
    );
}

void main()
{
#if defined(ERHE_BINDLESS_TEXTURE)
    sampler2D s_source = sampler2D(post_processing.source_texture[0]);
#endif

    float offset_1 = 1.41176471 * post_processing.texel_scale;
    float offset_2 = 3.29411765 * post_processing.texel_scale;
    float offset_3 = 5.17647059 * post_processing.texel_scale;
    float offset_4 = 7.05882353 * post_processing.texel_scale;
    float weight_0 = 0.19638062;
    float weight_1 = 0.29675293;
    float weight_2 = 0.09442139;
    float weight_3 = 0.01037598;
    float weight_4 = 0.00025940;

    vec3 prev_4 = weight_4 * texture(s_source, v_texcoord - vec2(0.0, offset_4)).rgb;
    vec3 prev_3 = weight_3 * texture(s_source, v_texcoord - vec2(0.0, offset_3)).rgb;
    vec3 prev_2 = weight_2 * texture(s_source, v_texcoord - vec2(0.0, offset_2)).rgb;
    vec3 prev_1 = weight_1 * texture(s_source, v_texcoord - vec2(0.0, offset_1)).rgb;
    vec3 center = weight_0 * texture(s_source, v_texcoord).rgb;
    vec3 next_1 = weight_1 * texture(s_source, v_texcoord + vec2(0.0, offset_1)).rgb;
    vec3 next_2 = weight_2 * texture(s_source, v_texcoord + vec2(0.0, offset_2)).rgb;
    vec3 next_3 = weight_3 * texture(s_source, v_texcoord + vec2(0.0, offset_3)).rgb;
    vec3 next_4 = weight_3 * texture(s_source, v_texcoord + vec2(0.0, offset_4)).rgb;
    prev_4 = fix(prev_4);
    prev_3 = fix(prev_3);
    prev_2 = fix(prev_2);
    prev_1 = fix(prev_1);
    center = fix(center);
    next_1 = fix(next_1);
    next_2 = fix(next_2);
    next_3 = fix(next_3);
    next_4 = fix(next_4);

    out_color.rgb  = fix(prev_4 + prev_3 + prev_2 + prev_1 + center + next_1 + next_2 + next_3 + next_4);
    out_color.a    = 1.0;
}
