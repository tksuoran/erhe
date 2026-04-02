layout(location = 0) out vec2 v_texcoord;

void main()
{
    // Fullscreen triangle - UVs don't matter for solid-color test textures
    const vec4 positions[3] = vec4[3](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4( 3.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  3.0, 0.0, 1.0)
    );
    gl_Position = positions[gl_VertexID];
    v_texcoord  = vec2(0.5, 0.5); // center of texture (solid color, any UV works)
}
