in vec2 v_texcoord;

void main()
{
    sampler2D s_source = sampler2D(post_processing.source_texture[0]);

    float offset_1 = 1.46153846 * post_processing.texel_scale;
    float offset_2 = 3.30769231 * post_processing.texel_scale;
    float offset_3 = 5.15384615 * post_processing.texel_scale;
    float weight_0 = 0.19345383;
    float weight_1 = 0.41914998;
    float weight_2 = 0.17464582;
    float weight_3 = 0.01905227;
    vec3  prev_3   = weight_3 * texture(s_source, v_texcoord - vec2(0.0, offset_3)).rgb;
    vec3  prev_2   = weight_2 * texture(s_source, v_texcoord - vec2(0.0, offset_2)).rgb;
    vec3  prev_1   = weight_1 * texture(s_source, v_texcoord - vec2(0.0, offset_1)).rgb;
    vec3  center   = weight_0 * texture(s_source, v_texcoord).rgb;
    vec3  next_1   = weight_1 * texture(s_source, v_texcoord + vec2(0.0, offset_1)).rgb;
    vec3  next_2   = weight_2 * texture(s_source, v_texcoord + vec2(0.0, offset_2)).rgb;
    vec3  next_3   = weight_3 * texture(s_source, v_texcoord + vec2(0.0, offset_3)).rgb;
    vec3  sum_a    = prev_3 + prev_2;
    vec3  sum_b    = prev_1 + next_1;
    vec3  sum_c    = next_2 + next_3;
    vec3  sum_d    = sum_a + sum_b;
    vec3  sum_e    = sum_c + center;
    vec3  sum      = sum_d + sum_e;
    out_color      = vec4(sum_d + sum_e, 1.0);
    //out_color      = vec4(center, 1.0);
    //out_color      = vec4(v_texcoord, 0.0, 1.0);
    //out_color      = vec4(v_texcoord, 0.0, 1.0);
    //out_color      = texture(s_source, v_texcoord);
}
