# Shadow map cache: remaining work

Status: in progress

The cache is built (`doc/erhe/shadows.md` "Shadow map cache"): a pass is
kept while its light, its matrix and the frame's configuration - including
the content serial of the published draw lists - are unchanged.
`scripts/shadow_map_cache_check.py` is its image gate (cache on against
every pass drawn, after caster, light, cone and camera edits). What remains
is finer invalidation.

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

1. **Per-light caster test.** Any draw-list change (a caster drag, a
   selection flag) forgets every pass. The draw-list scene is the single
   funnel for caster changes (`write_object_transform`,
   `write_object_bounds`, `set_object_flags`, `add_entries`,
   `remove_entries`, `release_object`): record the changed objects' old
   and new world bounds per frame (an empty list in a static frame), test
   them against each pass's cull view (sphere / octant for cube faces,
   frustum for spot and directional - the classification lane code) and
   forget only the passes they reach. The content serial then leaves the
   configuration and becomes the fast path (unchanged: no bounds test).
   Gate: a drag beside one point light keeps the other lights' passes
   (`get_scene_lights` kept flags), images identical to every pass drawn.
2. **Skinned casters.** Skinned objects disable the cache: joint poses have
   no serial. A pose serial at the change sites (animation player, IK and
   rigging tools) plus posed bounds for the per-light test of item 1 would
   keep the passes no animated caster reaches.
3. **Fit skip.** The shadow fit (`fit`, 0.2-0.45 ms on the render thread)
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
