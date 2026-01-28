void main()
{
    mat4 world_from_node   = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    mat4 clip_from_world   = light_block.lights[light_control_block.light_index].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position = clip_from_world * position_in_world;
}

