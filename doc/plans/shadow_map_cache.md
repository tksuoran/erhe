# Shadow map cache: remaining work

Status: in progress

The cache is built (`doc/erhe/shadows.md` "Shadow map cache"): a pass is
kept while its light, its matrix, its culling view and the frame's
configuration are unchanged and no draw-list change or skinned pose change
since it was drawn reaches it (the per-light caster test).
`scripts/shadow_map_cache_check.py` is its image gate (cache on against
every pass drawn, after caster, light, cone and camera edits; casters
dragged, added, removed and moved beside one of two point lights keep the
other light's faces; a static skinned figure keeps every face, a joint
rotation redraws only the faces of the light beside it). What remains is
the fit.

## Bistro measurements (Release Metal, M4 Pro, Dell at 120 Hz)

`scripts/bistro_frame_bench.py` on the Tracy tree, with a Metal System Trace
attached during the settle for the per-pass GPU split. Bistro shadow-maps
8 of its 43 lights under the Medium preset: 4 point (24 cube faces at
1024^2) and 4 spot (2048^2 layers); no directional light.

| per frame | shadows live | shadows frozen (`set_shadow_maps_frozen`) |
|---|---|---|
| GPU work at the N=1 clock (span x clock ratio) | 6.3-6.8 ms | 4.1-4.5 ms |
| render thread R | 2.15 ms | 1.09 ms |
| main thread work | 0.38 ms | 0.47 ms |

GPU passes with shadows live (Metal System Trace, profiler-attached clock):
main viewport 4.3 ms (vertex 1.8, fragment 2.2), point shadow faces 2.15 ms,
spot shadows 0.3 ms, post processing 0.4 ms, ImGui host 0.27 ms, overlay
0.18 ms. With the cache built (doc/erhe/shadows.md "Shadow map cache") the static
scene's GPU span is 4.3 ms and render-thread `execute` 0.8 ms, the frozen
upper bound.

The heavy bench scene (`config/editor/commands_bench_heavy.json`, four
directional cascades) spends 1.3 ms of its 5.5 ms GPU frame on the
directional passes and 0.6 ms on point faces; its directional maps are
camera-dependent: they are kept only while the camera is still.

## Remaining work

1. **Fit skip.** The shadow fit (`fit`, 0.2-0.45 ms on the render thread)
   still runs every frame; for spot and point lights whose snapshot is
   unchanged the projections could be reused
   (`doc/plans/shadows.md` "Temporal whole-fit skip").

## Related finding: tier M cadence on Bistro

Tier M held Bistro at N=2 (60 fps on the 120 Hz Dell) although the frames
fit one refresh: N=1's GPU clock record came from the empty editor before
the scene opened. Claim C29 (`doc/frame_pacing/algorithm.md` deviation 23)
fixes the cause; its live gate is open in
[frame_pacing_metal.md](frame_pacing_metal.md) (Log). The cache's GPU
savings apply at either cadence.
