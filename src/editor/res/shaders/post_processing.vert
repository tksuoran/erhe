out vec2 v_texcoord;

void main()
{
         if (gl_VertexID == 0) { gl_Position = vec4(-1.0, -1.0, 0.0, 1.0); v_texcoord = vec2(0.0, 0.0); }
    else if (gl_VertexID == 1) { gl_Position = vec4( 1.0, -1.0, 0.0, 1.0); v_texcoord = vec2(1.0, 0.0); }
    else if (gl_VertexID == 2) { gl_Position = vec4( 1.0,  1.0, 0.0, 1.0); v_texcoord = vec2(1.0, 1.0); }
    else                       { gl_Position = vec4(-1.0,  1.0, 0.0, 1.0); v_texcoord = vec2(0.0, 1.0); }
}