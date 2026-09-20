#include "erhe_camera_view.glsl"

layout(location = 0) out vec4 v_position;

void main()
{
    const vec4 vertex[12] = vec4[12](
        vec4( 0.0, 0.0,  0.0, 1.0), // 0
        vec4( 1.0, 0.0,  0.0, 0.0), // 1
        vec4( 0.0, 0.0,  1.0, 0.0), // 2
        vec4( 0.0, 0.0,  0.0, 1.0), // 0
        vec4( 0.0, 0.0,  1.0, 0.0), // 2
        vec4(-1.0, 0.0,  0.0, 0.0), // 3
        vec4( 0.0, 0.0,  0.0, 1.0), // 0
        vec4(-1.0, 0.0,  0.0, 0.0), // 3
        vec4( 0.0, 0.0, -1.0, 0.0), // 4
        vec4( 0.0, 0.0,  0.0, 1.0), // 0
        vec4( 0.0, 0.0, -1.0, 0.0), // 4
        vec4( 1.0, 0.0,  0.0, 0.0)  // 1
    );

    v_position  = camera.cameras[c_view_index].world_from_grid * vertex[gl_VertexID];
    gl_Position = camera.cameras[c_view_index].clip_from_world_for_grid * v_position;
    if (camera.cameras[c_view_index].grid_flags.z > 0.5) {
        // Behind all content: the far plane, which the grid pipeline's
        // depth test passes only where nothing has been drawn.
        float far_depth = (camera.cameras[c_view_index].clip_depth_direction < 0.0) ? 0.0 : 1.0;
        gl_Position.z = far_depth * gl_Position.w;
    }
}

