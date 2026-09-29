// Shadow_tie fragment pass (src/erhe/scene_renderer/test/test_shadow_gpu.cpp,
// doc/erhe/shadows.md "Shadow sampling GPU tests").
//
// The target is split into vertical bands SHADOW_TIE_BAND_WIDTH pixels wide.
// Band b evaluates sample_light_visibility() of light slot 0 at receiver points
// on the station-local plane y = SHADOW_TIE_PLANE_Y, x and z in
// [-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT], placed relative to the view
// camera's view origin by SHADOW_TIE_VIEW_RELATIVE_FROM_STATION (the
// view-relative positions standard.vert produces), with the reference depth
// offset by
// k = b - SHADOW_TIE_MAX_ULPS float ulps. Every band covers the same receiver
// points. The band width is even, so no 2x2 quad straddles two bands and the
// screen-space derivatives of the receiver position see one plane.
//
// Output: r = visibility, g = k.

int shadow_tie_reference_depth_ulps()
{
    int band = int(gl_FragCoord.x) / SHADOW_TIE_BAND_WIDTH;
    return band - SHADOW_TIE_MAX_ULPS;
}

// Test-only hook read by sample_light_visibility() (erhe_light.glsl).
#define ERHE_SHADOW_TEST_REFERENCE_DEPTH_ULPS shadow_tie_reference_depth_ulps()

#include "erhe_light.glsl"

void main()
{
    int   band   = int(gl_FragCoord.x) / SHADOW_TIE_BAND_WIDTH;
    float band_x = gl_FragCoord.x - float(band * SHADOW_TIE_BAND_WIDTH);
    vec2  uv     = vec2(
        band_x          / float(SHADOW_TIE_BAND_WIDTH),
        gl_FragCoord.y  / float(SHADOW_TIE_HEIGHT)
    );
    vec4 position = SHADOW_TIE_VIEW_RELATIVE_FROM_STATION * vec4(
        mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.x),
        SHADOW_TIE_PLANE_Y,
        mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.y),
        1.0
    );
    // The vertex rounding bound the same way standard.vert takes it: the
    // matrix above stands for view_relative_from_node (its upper 3x3 and its
    // translation), the station point for the node-space vertex.
    float vertex_rounding = get_vertex_position_rounding(
        mat3(SHADOW_TIE_VIEW_RELATIVE_FROM_STATION),
        vec3(mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.x), SHADOW_TIE_PLANE_Y, mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.y)),
        SHADOW_TIE_VIEW_RELATIVE_FROM_STATION[3].xyz
    );
    // The receiver plane the same way standard.frag takes it: from the
    // screen-space derivatives of the view-relative position (position is
    // linear in gl_FragCoord inside a band, and no quad straddles two bands).
    vec4  receiver_plane  = get_receiver_geometric_normal(position.xyz, vertex_rounding);
    float visibility      = sample_light_visibility(position.xyz, vertex_rounding, 0u, receiver_plane);
    out_color = vec4(visibility, float(shadow_tie_reference_depth_ulps()), 0.0, 1.0);
}
