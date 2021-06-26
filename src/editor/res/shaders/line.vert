//out vec4 v_color;
//out vec4 v_position;
//
//void main()
//{
//    //gl_Position = model.clip_from_world * vec4(a_position.xyz, 1.0);
//    gl_Position = view.clip_from_world * vec4(a_position.xyz, 1.0);
//    v_position  = a_position;
//    v_color     = a_color;
//}

out vec4  vs_color;
out float vs_line_width;

void main()
{
    mat4  clip_from_world        = view.clip_from_world;
    vec4  position               = vec4(a_position.xyz, 1.0);
    vec3  view_position_in_world = view.view_position_in_world.xyz;
    float fov_left               = view.fov[0];
    float fov_right              = view.fov[1];
    float fov_up                 = view.fov[2];
    float fov_down               = view.fov[3];
    float fov_width              = fov_right - fov_left;
    float fov_height             = fov_up    - fov_down;
    float viewport_width         = view.viewport[2];
    float viewport_height        = view.viewport[2];
    float d                      = distance(view_position_in_world, position.xyz);
    float max_size               = min(4.0 * a_position.w, 2.0);

    gl_Position   = clip_from_world * position;
    vs_color      = a_color;
    vs_line_width = (1.0 / 1024.0) * viewport_width * max(max_size / d, 1.0) / fov_width; //primitive.primitives[gl_DrawID].size;
}
