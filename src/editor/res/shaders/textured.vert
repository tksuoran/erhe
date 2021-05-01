in vec4 a_position_texcoord;

out vec2 v_texcoord;

void main()
{
    gl_Position = primitives.clip_from_model * vec4(a_position_texcoord.xy, 0.5, 1.0);
    v_texcoord = a_position_texcoord.zw;
}
