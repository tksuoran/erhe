#ifndef ERHE_OVERDRAW_GLSL
#define ERHE_OVERDRAW_GLSL

// Overdraw counting (doc/plans/overdraw_visualization.md O1-O3): the
// ERHE_OVERDRAW variants of a shader add 1 to the pixel's uint in
// overdraw_count.counts[] for every invocation, row by row over the view's
// viewport (camera.cameras[c_view_index].viewport). Under multiview the
// views' counts follow each other: view v's rows start at v * height rows.
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
// - ERHE_OVERDRAW_COST: as shaded, adding ERHE_OVERDRAW_WEIGHT (the static
//   cost of the variant's OVERDRAW off twin, doc/plans/shader_cost.md B2)
//   instead of 1.
// Helper invocations perform no stores, so they never count.

#include "erhe_camera_view.glsl"

#if ((ERHE_OVERDRAW == ERHE_OVERDRAW_SHADED) || (ERHE_OVERDRAW == ERHE_OVERDRAW_COST)) && !defined(ERHE_FRAGMENT_MAY_DISCARD)
layout(early_fragment_tests) in;
#endif

#if ERHE_OVERDRAW == ERHE_OVERDRAW_COST
#   define ERHE_OVERDRAW_INCREMENT uint(ERHE_OVERDRAW_WEIGHT)
#else
#   define ERHE_OVERDRAW_INCREMENT 1u
#endif

void erhe_count_overdraw()
{
    const ivec4 viewport = ivec4(camera.cameras[c_view_index].viewport);
    const ivec2 pixel    = ivec2(gl_FragCoord.xy) - viewport.xy;
    if ((pixel.x >= 0) && (pixel.y >= 0) && (pixel.x < viewport.z) && (pixel.y < viewport.w)) {
        const uint row = (c_view_index * uint(viewport.w)) + uint(pixel.y);
        atomicAdd(overdraw_count.counts[(row * uint(viewport.z)) + uint(pixel.x)], ERHE_OVERDRAW_INCREMENT);
    }
}

#endif // ERHE_OVERDRAW_GLSL
