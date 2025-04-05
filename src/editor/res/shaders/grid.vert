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

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = vertex[gl_VertexID];

    gl_Position = clip_from_world * position;
    v_position = position;
}

