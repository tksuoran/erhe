
out vec3 v_normal;
out vec3 v_tangent;
out vec3 v_view_direction;
out vec2 v_texcoord;
out vec4 v_position;

out flat uint v_material_index;

// out float v_r;
// out float v_p;
// out vec3  v_Cd;
// out vec3  v_C;

void main()
{
    mat4 world_from_model = model.models[gl_DrawID].world_from_model;
    mat4 clip_from_world  = camera.cameras[0].clip_from_world;
    v_normal      = vec3(world_from_model * vec4(a_normal,      0.0));
    v_tangent     = vec3(world_from_model * vec4(a_tangent.xyz, 0.0)) * a_tangent.w;
    v_position    =      world_from_model * vec4(a_position, 1.0);
    gl_Position   =      clip_from_world  * v_position;
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_view[3][0],
        camera.cameras[0].world_from_view[3][1],
        camera.cameras[0].world_from_view[3][2]
    );

    v_view_direction = view_position_in_world - v_position.xyz;
    v_texcoord       = a_texcoord;
    uint material_index = model.models[gl_DrawID].material_index;

    v_material_index = material_index;

    // v_r  = material.materials[material_index].roughness;
    // v_p  = material.materials[material_index].isotropy;
    // v_Cd = material.materials[material_index].diffuse_color.rgb;
    // v_C  = material.materials[material_index].specular_color.rgb;
}
