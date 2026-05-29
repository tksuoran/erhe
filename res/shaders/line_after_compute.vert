#include "erhe_camera_view.glsl"

layout(location = 0) out float v_line_width;
layout(location = 1) out vec4  v_color;
layout(location = 2) out vec4  v_start_end;

// Read pre-transformed triangle vertices directly from the compute output
// SSBO. The compute pass writes one slab of stride_per_view vertices per
// view (laid out [view][line][vertex]); the multiview build of this
// shader indexes the slab by gl_ViewIndex (via c_view_index) so a single
// draw inside a multiview render pass produces correct stereo output.
// Non-multiview builds use c_view_index = 0 and the (unique) slab written
// for the single-view dispatch.
//
// triangle_vertex_buffer matches the layout written by
// compute_before_content_line.comp; `view` mirrors the per-renderer view
// UBO whose stride_per_view field the C++ side fills with
// padded_edge_count * 6 (vertices).
void main()
{
    uint idx = uint(gl_VertexID) + c_view_index * view.stride_per_view;
    vec4 a_position_0 = triangle_vertex_buffer.vertices[idx].position_0;
    vec4 a_color_0_   = triangle_vertex_buffer.vertices[idx].color_0;
    vec4 a_custom_0_  = triangle_vertex_buffer.vertices[idx].custom_0;

    gl_Position  = vec4(a_position_0.xyz, 1.0);
    v_line_width = a_position_0.w;
    v_color      = a_color_0_;
    v_start_end  = a_custom_0_;
}
