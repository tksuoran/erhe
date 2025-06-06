layout(location = 0) highp out vec4 v_position;

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

    mat4 world_from_clip = camera.cameras[0].world_from_clip;

    gl_Position = positions[gl_VertexID];
    v_position = world_from_clip * gl_Position;
}
