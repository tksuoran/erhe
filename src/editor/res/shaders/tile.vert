out      vec2  v_texcoord;
out      vec4  v_color;
out flat uvec2 v_texture;

void main()
{
    gl_Position = projection.clip_from_window * vec4(a_position, 1.0);
    v_texture   = projection.texture;
    v_texcoord  = a_texcoord_0;
    v_color     = a_color_0;
}

