#ifndef ERHE_OVERDRAW_GLSL
#define ERHE_OVERDRAW_GLSL

// Overdraw counting (doc/plans/overdraw_visualization.md O1-O3): the
// ERHE_OVERDRAW variants of a shader add 1 to the pixel's uint in
// overdraw_count.counts[] for every invocation, row by row over the view's
// viewport (camera.cameras[0].viewport; single view only).
//
// ERHE_OVERDRAW values (erhe::scene_renderer::Overdraw_count, the
// ERHE_OVERDRAW_* constants of erhe_standard_variant.glsl):
// - ERHE_OVERDRAW_SHADED: the invocations the real shader runs. Storage
//   writes turn off a fragment shader's implicit early depth test, so a
//   variant that cannot discard declares early fragment tests: it runs where
//   the real shader passes the depth and stencil tests, as with early-Z. One
//   that can discard (ERHE_FRAGMENT_MAY_DISCARD, erhe_standard_variant.glsl)
//   keeps the late tests, as the GPU does for the real discarding shader.
// - ERHE_OVERDRAW_RASTERIZED: every rasterized fragment. No early fragment
//   tests, so the shader runs before the depth test, and it counts before
//   any discard: what the draws rasterize, whatever the depth buffer holds.
// Helper invocations perform no stores, so they never count.

#if (ERHE_OVERDRAW == ERHE_OVERDRAW_SHADED) && !defined(ERHE_FRAGMENT_MAY_DISCARD)
layout(early_fragment_tests) in;
#endif

void erhe_count_overdraw()
{
    const ivec4 viewport = ivec4(camera.cameras[0].viewport);
    const ivec2 pixel    = ivec2(gl_FragCoord.xy) - viewport.xy;
    if ((pixel.x >= 0) && (pixel.y >= 0) && (pixel.x < viewport.z) && (pixel.y < viewport.w)) {
        atomicAdd(overdraw_count.counts[(uint(pixel.y) * uint(viewport.z)) + uint(pixel.x)], 1u);
    }
}

#endif // ERHE_OVERDRAW_GLSL
