in vec4 a_position_texcoord;

void main()
{
    gl_Position = primitives.clip_from_model * vec4(a_position_texcoord.xy, 0.5, 1.0);
}
