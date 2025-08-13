layout(location = 0) out vec4      v_position;
layout(location = 1) out vec2      v_texcoord;
layout(location = 6) out vec3      v_N;
layout(location = 7) out flat uint v_material_index;

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

    world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
    world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal,      0.0)));
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    v_N              = normal;
    v_position       = position;
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
    v_texcoord       = a_texcoord_0;
}
