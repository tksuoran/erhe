#include "erhe_camera_view.glsl"

layout(location = 0) out highp vec4 v_position;

void main()
{
    //  Vertices of a fullscreen triangle:          .
    //                                              .
    //  gl_Position           v_texcoord            .
    //                                              .
    //   3  c                 2  c                  .
    //      |\                   |\                 .
    //   2  |  \                 |  \               .
    //      |    \               |    \             .
    //   1  +-----\           1  +-----\            .
    //      |     | \            |     | \          .
    //   0  |  +  |  \           |     |  \         .
    //      |     |    \         |     |    \       .
    //  -1  a-----+-----b     0  a-----+-----b      .
    //                                              .
    //     -1  0  1  2  3        0     1     2      .
    const vec4 positions[3] = vec4[3](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4( 3.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  3.0, 0.0, 1.0)
    );
    const vec2 texcoords[3] = vec2[3](
        vec2(0.0, 0.0),
        vec2(2.0, 0.0),
        vec2(0.0, 2.0)
    );

    mat4 world_from_clip = camera.cameras[c_view_index].world_from_clip;

    // Place the fullscreen triangle at the far plane so it composites behind the
    // scene. The far-plane clip-space z depends on the depth convention: 0 for
    // reverse-Z, 1 for forward-Z. clip_depth_direction is -1 for reverse-Z and
    // +1 for forward-Z, so pick the far value accordingly (previously hard-coded
    // to 0, which is the near plane under forward-Z -> the sky was rejected).
    float far_clip_z = (camera.cameras[c_view_index].clip_depth_direction < 0.0) ? 0.0 : 1.0;
    vec4 clip_position = vec4(positions[gl_VertexID].xy, far_clip_z, 1.0);

    gl_Position = clip_position;
    v_position = world_from_clip * clip_position;
}
