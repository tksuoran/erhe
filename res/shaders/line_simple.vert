#include "erhe_camera_view.glsl"

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = view.cameras[c_view_index].clip_from_world * vec4(a_position.xyz, 1.0);
    v_color = a_color_0;
}
