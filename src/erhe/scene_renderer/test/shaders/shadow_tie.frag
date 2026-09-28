// Shadow_tie fragment pass (src/erhe/scene_renderer/test/test_shadow_gpu.cpp,
// doc/plans/shadow_robustness.md T7).
//
// The target is split into vertical bands SHADOW_TIE_BAND_WIDTH pixels wide.
// Band b evaluates sample_light_visibility() of light slot 0 at receiver points
// on the station-local plane y = SHADOW_TIE_PLANE_Y, x and z in
// [-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT], placed in the world by
// SHADOW_TIE_WORLD_FROM_STATION, with the reference depth offset by
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
    vec4 position = SHADOW_TIE_WORLD_FROM_STATION * vec4(
        mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.x),
        SHADOW_TIE_PLANE_Y,
        mix(-SHADOW_TIE_EXTENT, SHADOW_TIE_EXTENT, uv.y),
        1.0
    );
    // The receiver plane the same way standard.frag takes it: from the
    // screen-space derivatives of the world position (position is linear in
    // gl_FragCoord inside a band, and no quad straddles two bands).
    vec3  receiver_normal = get_receiver_geometric_normal(position.xyz);
    float visibility      = sample_light_visibility(position, 0u, receiver_normal);
    out_color = vec4(visibility, float(shadow_tie_reference_depth_ulps()), 0.0, 1.0);
}
