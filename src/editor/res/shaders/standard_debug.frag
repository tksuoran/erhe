in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in mat3      v_TBN;
in flat uint v_material_index;
in float     v_tangent_scale;
in float     v_line_width;

float srgb_to_linear(float x)
{
    if (x <= 0.04045) {
        return x / 12.92;
    } else {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 V)
{
    return vec3(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g),
        srgb_to_linear(V.b)
    );
}

vec2 srgb_to_linear(vec2 V)
{
    return vec2(
        srgb_to_linear(V.r),
        srgb_to_linear(V.g)
    );
}


void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V     = normalize(view_position_in_world - v_position.xyz);
    vec3  T0    = normalize(v_TBN[0]);
    vec3  B0    = normalize(v_TBN[1]);
    vec3  N     = normalize(v_TBN[2]);

    Light light          = light_block.lights[0];
    vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
    vec3  L              = normalize(point_to_light);

    Material material = material.materials[v_material_index];

    vec2  T_circular                    = normalize(v_texcoord);
    float circular_anisotropy_magnitude = pow(length(v_texcoord) * 8.0, 0.25);
    // Vertex red channel is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Vertex color green channel is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    float tangent_space_control = v_color.g;
    float anisotropy_strength = mix(
        1.0,
        min(1.0, circular_anisotropy_magnitude),
        tangent_space_control
    ) * v_color.r;
    // Mix tangent space geometric .. texcoord generated
    vec3  T                   = mix(T0, T_circular.x * T0 + T_circular.y * B0, tangent_space_control);
    vec3  B                   = mix(B0, T_circular.y * T0 - T_circular.x * B0, tangent_space_control);
    float isotropic_roughness = 0.5 * material.roughness.x + 0.5 * material.roughness.y;
    // Mix roughness based on anisotropy_strength
    float roughness_x         = mix(isotropic_roughness, material.roughness.x, anisotropy_strength);
    float roughness_y         = mix(isotropic_roughness, material.roughness.y, anisotropy_strength);

    mat3 TBN   = mat3(T0, B0, N);
    mat3 TBN_t = transpose(TBN);
    vec3 wo    = normalize(TBN_t * V);
    vec3 wi    = normalize(TBN_t * L);
    vec3 wg    = normalize(TBN_t * N);

#if defined(ERHE_DEBUG_NORMAL)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * N);
#endif
#if defined(ERHE_DEBUG_TANGENT)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * T0);
#endif
#if defined(ERHE_DEBUG_BITANGENT)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * B0);
#endif
#if defined(ERHE_DEBUG_OMEGA_O)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * wo);
    out_color.r = 1.0;
#endif
#if defined(ERHE_DEBUG_OMEGA_I)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * wi);
    out_color.g = 1.0;
#endif
#if defined(ERHE_DEBUG_OMEGA_G)
    out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * wg);
    out_color.b = 1.0;
#endif
#if defined(ERHE_DEBUG_TEXCOORD)
    out_color.rgb = srgb_to_linear(vec3(v_texcoord, 0.0));
#endif
#if defined(ERHE_DEBUG_VERTEX_COLOR_RGB)
    out_color.rgb = srgb_to_linear(v_color.rgb);
#endif
#if defined(ERHE_DEBUG_VERTEX_COLOR_ALPHA)
    out_color.rgb = srgb_to_linear(vec3(v_color.a));
#endif
#if defined(ERHE_DEBUG_MISC)
    // Show Draw ID
    const vec3 palette[24] = vec3[24](
        vec3(0.0, 0.0, 0.0), // 0
        vec3(1.0, 0.0, 0.0), // 1
        vec3(0.0, 1.0, 0.0), // 2
        vec3(0.0, 0.0, 1.0), // 3
        vec3(1.0, 1.0, 0.0), // 4
        vec3(0.0, 1.0, 1.0), // 5
        vec3(1.0, 0.0, 1.0), // 6
        vec3(1.0, 1.0, 1.0), // 7
        vec3(0.5, 0.0, 0.0), // 8
        vec3(0.0, 0.5, 0.0), // 9
        vec3(0.0, 0.0, 0.5), // 10
        vec3(0.5, 0.5, 0.0), // 11
        vec3(0.0, 0.5, 0.5), // 12
        vec3(0.5, 0.0, 0.5), // 13
        vec3(0.5, 0.5, 0.5), // 14
        vec3(1.0, 0.5, 0.0), // 15
        vec3(1.0, 0.0, 0.5), // 16
        vec3(0.5, 1.0, 0.0), // 17
        vec3(0.0, 1.0, 0.5), // 18
        vec3(0.5, 0.0, 1.0), // 19
        vec3(0.0, 0.5, 1.0), // 20
        vec3(1.0, 1.0, 0.5), // 21
        vec3(0.5, 1.0, 1.0), // 22
        vec3(1.0, 0.5, 1.0)  // 23
    );


    // Show Directional light L . N
    out_color.rgb = srgb_to_linear(palette[v_material_index % 24]);

    float N_dot_L = dot(N, L);
    float N_dot_V = dot(N, V);
    float N_dot_T = dot(N, T);
    float N_dot_B = dot(N, B);
    float T_dot_B = dot(T, B);

    out_color.rgb = srgb_to_linear(vec3(N_dot_T, N_dot_B, T_dot_B));

    // Show material
    //Material material = material.materials[v_material_index];
    //out_color.rgb = srgb_to_linear(material.base_color.rgb);

#endif
    out_color.a = 1.0;
}
