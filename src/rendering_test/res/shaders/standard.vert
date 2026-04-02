layout(location = 0) out vec4 v_color;

void main()
{
    mat4 world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    mat4 world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    mat4 clip_from_world        = camera.cameras[0].clip_from_world;
    vec4 position               = world_from_node * vec4(a_position, 1.0);
    vec3 world_normal           = normalize(mat3(world_from_node_normal) * a_normal);

    // Map normal xyz from [-1,1] to [0,1] for visualization
    v_color     = vec4(world_normal * 0.5 + 0.5, 1.0);
    gl_Position = clip_from_world * position;
}
