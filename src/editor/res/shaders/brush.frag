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

    vec3  V = normalize(view_position_in_world - v_position.xyz);
    vec3  N = normalize(v_TBN[2]);

    float N_dot_V = dot(N, V);

    Material material = material.materials[v_material_index];
    out_color.rgb = srgb_to_linear(0.5 * vec3(N_dot_V) * material.base_color.rgb * material.opacity);
    out_color.a = 0.5 * material.opacity;
}
