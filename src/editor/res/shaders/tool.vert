layout(location = 0) out vec3      v_position;
layout(location = 1) out vec3      v_normal;
layout(location = 2) out flat uint v_material_index;

void main() {
    mat4 world_from_node          = primitive.primitives[gl_DrawID].world_from_node;
    mat4 world_from_node_cofactor = primitive.primitives[gl_DrawID].world_from_node_cofactor;
    mat4 clip_from_world          = camera.cameras[0].clip_from_world;
    vec4 position                 = world_from_node * vec4(a_position, 1.0);

    v_position       = position.xyz;
    v_normal         = normalize(vec3(world_from_node_cofactor * vec4(a_normal, 0.0)));
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
}
