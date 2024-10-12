layout(location = 0) out      vec2  v_texcoord;
layout(location = 1) out      vec4  v_color;
layout(location = 2) out flat uvec2 v_texture;

void main() {
    gl_Position = projection.clip_from_window * vec4(a_position, 1.0);
    v_texture   = projection.texture;
    v_texcoord  = a_texcoord;
    v_color     = a_color;
}
