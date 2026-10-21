# Bistro main viewport GPU cost

Status: proposed

Bistro's main viewport pass (`Viewport window`) is the largest GPU item of
the frame once the shadow map cache keeps the static shadow passes
(`doc/erhe/shadows.md` "Shadow map cache"). This plan records where its time
goes and the options, for a user decision.

## Measurements (Release, headless Metal, M4 Pro)

The headless Metal tree with `frame_pacing_tier` off and `force_disable_vsync`
keeps the GPU saturated, so its clock holds and per-pass GPU timers
(`get_gpu_timers`) repeat within 0.15 ms between runs; the windowed build
paces and the governor moves the clock with the load, which makes per-pass
timings of different configurations incomparable. Bistro camera of
`scripts/bistro_frame_bench.py`, viewport 1334x1120, light counts and
filters set per run with `set_graphics_preset` (session-only):

| configuration | `Viewport window` |
|---|---|
| no lights (geometry + material) | 1.47 ms |
| 8 unshadowed lights, shadow maps off | 2.32 ms |
| 8 shadow-mapped lights, none unshadowed | 3.1 ms |
| 16 lights (Medium preset: 4+4 spot, 4+4 point), PCF 4x4 | 3.4-3.5 ms |
| the same, hard filter | 3.2-3.3 ms |

A Metal System Trace of the same runs: with no lights the pass is 0.87 ms of
vertex (tiling) work and 0.60 ms of fragment work; with 16 lights 1.54 ms
vertex and 1.96 ms fragment - the vertex shader does no light work, the
vertex interval grows because vertex and fragment work share the shader
cores. Lighting is about 60 % of the pass, geometry about 0.9 ms.

## Light selection is by scene order

`Light_set::resolve()` hands out the light slots per type in the scene's
light order, up to the preset's limits, independent of the view. Bistro has
43 lights (33 point, 10 spot); the Medium preset shades 16 and shadow-maps
8. At the bench camera every one of the 36 lights within reach of the view
frustum (range sphere against the frustum) is a candidate, yet:

- the 4 shadow-mapped point lights are 97-130 m from the camera, at the far
  end of the street; their 24 cube faces cost 2.15 ms of GPU time per frame
  when drawn (before the cache), for a small part of the image;
- the shaded spot lights are 30-41 m away;
- 10 point lights within 6-25 m of the camera (range 15 m, so they light
  the visible street) are not shaded at all.

The per-fragment light loops skip a light outside its range or facing away
(`standard.frag`), but a SIMD group pays for a light when any of its lanes
is in range, and the loop runs every shaded light.

## Options

1. **View-relevant light selection.** Rank the candidate lights per view by
   their influence on the image (range sphere against the view frustum,
   distance, intensity), hand the shadow and shading slots out by rank,
   with hysteresis so a camera move does not churn the assignment (a slot
   change redraws that light's shadow maps; with the cache, a stable
   assignment keeps them). Changes what is lit; a quality fix as much as a
   cost one. Cost: a per-view sort of the candidates when the camera or a
   light moves (a few dozen lights).
2. **Clustered (or tiled) light culling.** A compute pass bins the lights
   into view-space clusters each frame; the fragment shader loops only the
   lights of its cluster. Lets every light be shaded, not only the preset's
   16, at a cost proportional to the overlap. The standard path for many
   lights; touches the light buffer layout and the forward shaders, and
   shadow-mapped lights stay limited by the shadow slots (option 1 still
   picks those).
3. **Geometry.** About 0.9 ms of vertex / tiling work for 1155 entries after
   frustum culling (2569 culled). Occlusion culling (`doc/plans/culling.md`
   phase 7, not started) and LODs are the levers; larger work.

Option 1 is the smallest change with a visible effect on both quality and
cost and composes with option 2; option 2 is the scalable answer for scenes
with hundreds of lights.
