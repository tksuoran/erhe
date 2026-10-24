// The raster occlusion test (doc/plans/occlusion_culling.md, doc/erhe/
// draw_list_renderer.md Q6 "Occlusion test"): one non-indexed draw of 36
// vertices per entry of the frame's published draw lists (all lists back to
// back, the classifier's entry order), inside the view's render pass after
// its opaque and alpha-tested color passes. Entry e is vertices
// [36 e, 36 e + 36): the twelve triangles of its grown world AABB
// (erhe_occlusion_bounds.glsl).
//
// occlusion_view.clip_from_world: the view's clip_from_world, as its color
// passes' camera block and its culling view are built from it.
// occlusion_view.info = entry_count, view group, view bit (0..63), 0.
// occlusion_test.words: the classification's per (group, entry) uvec2 of
// the views whose entry the pass rasterizes (cull_classify.comp: a bounded
// color entry inside the view's frustum and wholly beyond its near plane).
// An entry whose bit is clear is pushed outside the clip volume: the
// classification already marked it visible, or it is not this view's.

#include "erhe_occlusion_bounds.glsl"

layout(location = 0) flat out uint v_word;
layout(location = 1) flat out uint v_mask;

//   6-------7   corner = x | (y << 1) | (z << 2)
//  /|      /|
// 2-------3 |
// | 4-----|-5
// |/      |/
// 0-------1
const uint c_box_corners[36] = uint[36](
    1u, 7u, 3u,  7u, 1u, 5u, // +X
    0u, 6u, 4u,  6u, 0u, 2u, // -X
    2u, 7u, 6u,  7u, 2u, 3u, // +Y
    0u, 5u, 1u,  5u, 0u, 4u, // -Y
    4u, 7u, 5u,  7u, 4u, 6u, // +Z
    0u, 3u, 2u,  3u, 0u, 1u  // -Z
);

void main()
{
    uint vertex      = uint(gl_VertexID);
    uint entry       = vertex / 36u;
    uint corner      = c_box_corners[vertex % 36u];
    uint entry_count = occlusion_view.info.x;
    uint group       = occlusion_view.info.y;
    uint bit         = occlusion_view.info.z;
    uint word        = (2u * ((group * entry_count) + entry)) + (bit / 32u);
    uint mask        = 1u << (bit % 32u);
    v_word = word;
    v_mask = mask;
    if ((occlusion_test.words[word] & mask) == 0u) {
        // Outside the left clip plane: the triangle has no fragments.
        gl_Position = vec4(-2.0, -2.0, -2.0, 1.0);
        return;
    }
    vec4 center_w = cull_bounds.bounds[(2u * entry) + 0u];
    vec4 extent_w = cull_bounds.bounds[(2u * entry) + 1u];
    vec3 extent   = occlusion_grown_extent(center_w.xyz, extent_w.xyz);
    vec3 side     = vec3(
        float(corner & 1u),
        float((corner >> 1u) & 1u),
        float((corner >> 2u) & 1u)
    ) * 2.0 - vec3(1.0);
    gl_Position = occlusion_view.clip_from_world * vec4(center_w.xyz + (side * extent), 1.0);
}
