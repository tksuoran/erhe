out vec3      v_position;
out vec3      v_normal;
out flat uint v_material_index;

void main()
{
    mat4 world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
    mat4 world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;
    mat4 clip_from_world        = camera.cameras[0].clip_from_world;
    vec4 position               = world_from_node * vec4(a_position, 1.0);

    v_position       = position.xyz;
    v_normal         = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
}
