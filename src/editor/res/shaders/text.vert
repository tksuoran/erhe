out vec2 v_texcoord;
out vec4 v_color;

void main()
{
    gl_Position = projection.clip_from_window * vec4(a_position, 1.0);
    v_texcoord  = a_texcoord;
    v_color     = a_color;
}

