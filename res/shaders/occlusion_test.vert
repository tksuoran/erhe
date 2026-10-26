// The raster occlusion test (doc/plans/occlusion_culling.md, doc/erhe/
// draw_list_renderer.md Q6 "Occlusion test"): one indexed draw over every
// entry of the frame's published draw lists (all lists back to back, the
// classifier's entry order), inside the view's render pass after its opaque
// and alpha-tested color passes. Per entry the three faces of its grown
// world AABB (erhe_occlusion_bounds.glsl) that can face the camera: a fixed
// template of the faces at the box's positive corner (+X, +Y, +Z), seven
// unique corners and 18 indices, mirrored per axis toward the camera (the
// "three faces" variant of Sebastian Aaltonen's cube rendering). The index
// buffer holds the 18-index pattern of entry e at 7 e, so the vertex index
// (gl_VertexID: the index value on every backend) names entry e = index / 7
// and template corner index % 7, and the post-transform cache shares the
// corners between the faces: 7 vertex invocations per entry.
//
// occlusion_view.clip_from_world[v]: view v's clip_from_world, as its color
// passes' camera block and its culling view are built from it.
// occlusion_view.eye[v]: view v's camera, homogeneous: (position, 1) for a
// perspective view, (the camera's +Z axis, 0) for an orthographic one. v is
// c_view_index: gl_ViewIndex in a multiview render pass (ERHE_MULTIVIEW, the
// headset's eyes, which share one culling view and so one bit), else 0. The
// faces are chosen per view: each eye rasterizes the faces toward it. The
// faces toward the camera on axis k are on the side sign(eye_k - center_k *
// eye_w) (either side when 0: the camera is in the plane, the faces of
// that axis are edge-on). Mirroring an odd number of axes reverses the
// winding: the pipeline culls no face.
// occlusion_view.info = entry_count, view group, view bit (0..63), 0.
// occlusion_test.words: the classification's per (group, entry) uvec2 of
// the views whose entry the pass rasterizes (cull_classify.comp: a bounded
// color entry inside the view's frustum and wholly beyond its near plane).
// An entry whose bit is clear is pushed outside the clip volume: the
// classification already marked it visible, or it is not this view's. A
// camera inside an entry's box sees none of these faces from the front:
// such a box reaches the near plane, so the classification marks the entry
// visible without a test.

#include "erhe_camera_view.glsl"
#include "erhe_occlusion_bounds.glsl"

layout(location = 0) flat out uint v_word;
layout(location = 1) flat out uint v_mask;

void main()
{
    uint vertex      = uint(gl_VertexID);
    uint entry       = vertex / 7u;
    // Template corner 1..7 (corner 0, the negative corner, is on no face
    // of the template): x | (y << 1) | (z << 2) of the positive faces.
    uint corner      = (vertex % 7u) + 1u;
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
    vec3 center   = center_w.xyz;
    vec3 extent   = occlusion_grown_extent(center, extent_w.xyz);
    vec4 eye      = occlusion_view.eye[c_view_index];
    vec3 toward   = eye.xyz - (center * eye.w);
    vec3 facing   = vec3(
        (toward.x >= 0.0) ? 1.0 : -1.0,
        (toward.y >= 0.0) ? 1.0 : -1.0,
        (toward.z >= 0.0) ? 1.0 : -1.0
    );
    vec3 side     = (vec3(
        float(corner & 1u),
        float((corner >> 1u) & 1u),
        float((corner >> 2u) & 1u)
    ) * 2.0 - vec3(1.0)) * facing;
    gl_Position = occlusion_view.clip_from_world[c_view_index] * vec4(center + (side * extent), 1.0);
}
