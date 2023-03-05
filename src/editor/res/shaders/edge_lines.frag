in      vec3 v_position;
in flat uint v_material_index;

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );
    float d = distance(view_position_in_world, v_position);

    vec4 base_color = material.materials[v_material_index].base_color;
    //out_color.rgb = vec3(0.0, 0.0, 1.0);
    //out_color.a = 1.0;
    float thickness = 6.0;
    float distance_scale = min(thickness / d, 1.0);
    float alpha = distance_scale;
    out_color.rgb = vec3(0.01, 0.05, 0.1) * alpha;
    out_color.a = alpha;
}
