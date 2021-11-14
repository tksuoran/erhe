in vec3      v_position;
in vec3      v_normal;
in flat uint v_material_index;

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
        srgb_to_linear(v.r),
        srgb_to_linear(v.g),
        srgb_to_linear(v.b)
    );
}

vec2 srgb_to_linear(vec2 v)
{
    return vec2(srgb_to_linear(v.r), srgb_to_linear(v.g));
}

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  v     = normalize(view_position_in_world - v_position);
    vec3  n     = normalize(v_normal);
    float NdotV = max(dot(n, v), 0.0);
    out_color.rgb = srgb_to_linear(material.materials[v_material_index].base_color.rgb * (0.5 + pow(NdotV, 5.0) * 2.5));
    out_color.a = 1.0;
}
