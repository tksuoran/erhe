out      vec3 v_position;
out flat uint v_material_index;

void main()
{
    mat4 world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position, 1.0);
    v_position       = position.xyz;
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
}
