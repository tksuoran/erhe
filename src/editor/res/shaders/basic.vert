out vec3 v_normal;
out vec4 v_color;
out vec2 v_texcoord;

void main()
{
    mat4 world_from_model  = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world   = camera.cameras[0].clip_from_world;
    vec4 position_in_world = world_from_model * vec4(a_position, 1.0);
    gl_Position = clip_from_world * position_in_world;
    v_normal    = a_normal;
}

