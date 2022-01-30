out      vec2  v_texcoord;
out flat uvec2 v_texture;

void main()
{
    mat4 world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world = camera.cameras[0].clip_from_world;

    vec4 position = world_from_node * vec4(a_position, 1.0);
    gl_Position   = clip_from_world * position;
    v_texture     = material.materials[gl_DrawID].texture;
    v_texcoord    = a_texcoord;
}
