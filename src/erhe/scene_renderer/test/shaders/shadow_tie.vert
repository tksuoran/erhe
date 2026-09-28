// Fullscreen triangle for the Shadow_tie fragment pass
// (src/erhe/scene_renderer/test/test_shadow_gpu.cpp). The receiver points are
// generated from gl_FragCoord in shadow_tie.frag; the vertex stage only covers
// the target.

void main()
{
    const vec4 positions[3] = vec4[3](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4( 3.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  3.0, 0.0, 1.0)
    );
    gl_Position = positions[gl_VertexID];
}
