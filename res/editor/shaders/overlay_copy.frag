layout(location = 0) in vec2 v_texcoord;

// Fullscreen 1:1 copy of s_input into the current render target. Used by the
// post-processing overlay pass to seed the (multisampled) overlay color buffer
// with the already post-processed image before the tool gizmo / hotbar
// rendertarget meshes are drawn on top of it. See issue #230.
void main()
{
    out_color = textureLod(s_input, v_texcoord, 0.0);
}
