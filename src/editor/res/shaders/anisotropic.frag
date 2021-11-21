in vec3 v_view_direction;
in vec3 v_normal;
in vec3 v_tangent;
in vec2 v_texcoord;
in vec4 v_position;

in flat uint v_material_index;

// in float v_r;
// in float v_p;
// in vec3  v_Cd;
// in vec3  v_C;

vec3 ColorFn1Ddiv(float y) {
    y = clamp(y, -1.0, 1.0);
    //float r = 0.569 + ( 0.396 + 0.834   * y) * sin(2.15  + 0.93 * y);
    //float g = 0.911 + (-0.060 - 0.863   * y) * sin(0.181 + 1.3  * y);
    //float b = 0.939 + (-0.309 - 0.705   * y) * sin(0.125 + 2.18 * y);
    float r = 0.484 + ( 0.432 - 0.104   * y) * sin(1.29  + 2.53 * y);
    float g = 0.334 + ( 0.585 + 0.00332 * y) * sin(1.82  + 1.95 * y);
    float b = 0.517 + ( 0.406 - 0.0348  * y) * sin(1.23  + 2.49 * y);
    return vec3 (r, g, b);
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045)
    {
        return x / 12.92;
    }
    else
    {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 v)
{
    return vec3(
        srgb_to_linear(v.x),
        srgb_to_linear(v.y),
        srgb_to_linear(v.z)
    );
}

struct Surface {
    vec3 N;
    vec3 B;
    vec3 T;
    vec3 V;
    vec3 s;
    vec3 L;

    float r;
    float p;
    vec3  Cd;
    vec3  C;

    float p2;
    float one_minus_r;
    float one_minus_p2;
    vec3  one_minus_C;
};

struct Light_setup {
    float vn;
    float vn_clamped;
    float gvn_denom;
    vec3  S;
    float ln;
};

struct Light_final {
    float ln_clamped;
    vec3  H;
    float hn;
    float hv;
    float hv_clamped;
    float ht;
    float hn2;
    float ht2;
    float aht_denom;
    float gln_denom;
    float zhn0;
    float D;
};

float sample_light_visibility(vec4 position, uint light_index) {
    vec4  position_in_light_texture_homogeneous = light.lights[light_index].texture_from_world * position;

    vec4  position_in_light_texture = position_in_light_texture_homogeneous / position_in_light_texture_homogeneous.w;
    float depth_in_light_texture    = position_in_light_texture.z;
    float sampled_depth             = texture(
        s_shadow,
        vec3(position_in_light_texture.xy,
            float(light_index)
        )
    ).x;

    if (depth_in_light_texture - 0.005 <= sampled_depth)
    {
        return 1.0;
    }
    return 0.0;
}

Light_setup common_light_setup(Surface s) {
    Light_setup l;
    l.vn          = dot(s.V, s.N);
    l.vn_clamped  = max(0.0, l.vn);
    l.gvn_denom   = s.r + s.one_minus_r * l.vn_clamped;
    l.gvn_denom   = clamp(l.gvn_denom, 0.05, 1.0);
    l.S           = s.C + s.one_minus_C * pow(1.0 - l.vn_clamped, 5.0);
    l.ln          = dot(s.L, s.N);
    return l;
}

Light_final common_light(Surface s, Light_setup l) {
    Light_final f;
    f.H          = normalize(s.L + s.V);
    f.ln_clamped = max(0.0, l.ln);
    f.hn         = max(0.0, dot(f.H, s.N));
    f.hv         = max(0.0, dot(f.H, s.V));
    f.hv_clamped = max(0.0, f.hv);
    f.ht         = dot(f.H - f.hn * s.N, s.T);
    f.hn2        = f.hn * f.hn;
    f.ht2        = f.ht * f.ht;
    f.aht_denom  = sqrt(s.p2 + s.one_minus_p2 * f.ht2);
    f.gln_denom  = s.r + (s.one_minus_r * l.ln);
    f.zhn0       = 1.0 - (s.one_minus_r * f.hn * f.hn);
    f.D          = (l.ln * s.r * s.p2) / (l.gvn_denom * f.gln_denom * f.zhn0 * f.zhn0 * f.aht_denom);
    return f;
}

void main(void) {
    Surface s;

#if 1
    s.r            = material.materials[v_material_index].roughness;
    s.p            = material.materials[v_material_index].isotropy;
    s.Cd           = material.materials[v_material_index].diffuse_color.rgb;
    s.C            = material.materials[v_material_index].specular_color.rgb;
#else
    s.r            = v_r;
    s.p            = v_p;
    s.Cd           = v_Cd;
    s.C            = v_C;
#endif

    s.one_minus_C  = vec3(1.0, 1.0, 1.0) - s.C;
    s.p2           = s.p * s.p;
    s.one_minus_r  = 1.0 - s.r;
    s.one_minus_p2 = 1.0 - s.p2;

    s.N = normalize(v_normal);
    s.B = normalize(v_tangent);
    s.T = normalize(cross(s.B, s.N));
    s.V = normalize(v_view_direction);

    vec3 exitant_radiance         = vec3(0.0);
    uint directional_light_count  = light.directional_light_count;
    uint spot_light_count         = light.spot_light_count;
    uint directional_light_offset = 0;
    uint spot_light_offset        = directional_light_count;
    for (uint i = 0; i < directional_light_count; ++i)
    {
        uint light_index = directional_light_offset + i;

        s.L = light.lights[light_index].direction;

        Light_setup l = common_light_setup(s);

        if (l.ln > 0.0)
        {
            Light_final f = common_light(s, l);

            float light_visibility  = sample_light_visibility(v_position, light_index);
            vec3  incident_radiance = light.lights[light_index].radiance * light_visibility;

            exitant_radiance += incident_radiance * s.Cd * f.ln_clamped; // diffuse
            exitant_radiance += incident_radiance * l.S  * f.D;          // specular
            //exitant_radiance =+ vec3(l.ln);
        }
    }

    for (uint i = 0; i < spot_light_count; ++i)
    {
        uint light_index = spot_light_offset + i;
        vec3 to_light    = light.lights[light_index].position - v_position.xyz;
        s.L              = normalize(to_light);
        vec3  spot       = normalize(light.lights[light_index].direction.xyz);
        float ls         = dot(s.L, spot);
        float ls_clamped = max(0.0, ls);

        Light_setup l = common_light_setup(s);

        if (l.ln > 0.0)
        {
            float spot_cutoff = light.lights[light_index].spot_cutoff;
            if (ls_clamped > spot_cutoff)
            {
                Light_final f = common_light(s, l);

                float light_distance    = length(to_light);
                float light_attenuation = 1.0 / (light_distance * light_distance);

                float t = ls - spot_cutoff;
                t /= (1.0 - spot_cutoff);
                light_attenuation *= t;

                float light_visibility  = sample_light_visibility(v_position, light_index);
                vec3  incident_radiance = light.lights[light_index].radiance * light_visibility * light_attenuation;

                exitant_radiance += incident_radiance * s.Cd * f.ln_clamped; // diffuse
                exitant_radiance += incident_radiance * l.S  * f.D;          // specular
            }
        }
    }

    vec3 linear = exitant_radiance;
    float exposure = camera.cameras[0].exposure;
    //float depth = texture(s_shadow, v_texcoord).r;
    //out_color.rgb = vec3(1.0) - exp(-linear * exposure);
    out_color.rgb = linear;
    //out_color.b += depth * 0.5;

//    {
//        vec3  position_in_light_texture = v_position_in_light_texture.xyz / v_position_in_light_texture.w;
//        float depth_in_light_texture    = position_in_light_texture.z;
//        float sampled_depth             = texture(s_shadow, position_in_light_texture.xy).x;
//        //out_color.rgb = ColorFn1Ddiv(v_shadow.z * 0.0001);
//        //out_color.rgb = vec3(fragment_depth * 0.1, fragment_depth * 0.1, 0.0);
//        //out_color.rgb = position_in_light_texture;
//        //out_color.rgb = position_in_light_texture;
//        out_color.rgb = ColorFn1Ddiv(depth_in_light_texture * 0.01);
//    }
    //out_color.rgb = material.materials[v_material_index].color.rgb;
    // out_color.rgb = v_Cd;
    // out_color.rgb = vec3(v_r);
    //out_color.rgb = srgb_to_linear(vec3(s.N * 0.5 + 0.5));

}
