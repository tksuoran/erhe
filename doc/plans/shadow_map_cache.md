# Shadow map cache: skip unchanged shadow passes

Status: proposed

Every frame each shadow render node re-renders every shadow-mapped light's
map (`doc/erhe/shadows.md`), although in a scene where neither the light nor
its casters change the map comes out identical. This plan keeps a map from
the frame it was drawn in and redraws only the lights whose inputs changed.

## Measured potential (Bistro, Release Metal, M4 Pro, Dell at 120 Hz)

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
0.18 ms. A perfect cache removes about a third of the GPU frame and half of
the render thread (`draw_shadow` ~0.95 ms, its share of `execute`).

The heavy bench scene (`config/editor/commands_bench_heavy.json`, four
directional cascades) spends 1.3 ms of its 5.5 ms GPU frame on the
directional passes and 0.6 ms on point faces; its directional maps are
camera-dependent (below).

## What a map depends on

- **Spot and point lights**: the projection is built from the light's frame,
  range, spot angles and the map resolution only (`Light_snapshot::
  projection_transforms`, `light.cpp`), and the passes use the light as the
  view origin. A camera move never changes these maps. Bistro has only these.
- **Directional lights**: the stable fit centres on the snapped camera
  position and the tight fit clips to the view frustum and the receivers
  (`doc/erhe/shadow_tight_fit.md`), so a camera move changes the fitted
  `clip_from_world` and with it the map. Cached directional maps pay off only
  while the camera is still (editing with a fixed view); the texel snap keeps
  the matrix bitwise unchanged for sub-texel camera motion only along the
  stable fit.
- **Receivers** matter only through the directional fit; comparing the
  fitted matrices covers them.

## Design

The maps already persist: one `texture_2d_array` and one
`texture_cube_map_array` per `Shadow_render_node`, no per-frame-in-flight
copies, cleared only by the pass's own `Load_action::Clear`
(`shadow_render_node.cpp`). A pass that is not begun keeps its layer.

Per shadow layer (2D layer or cube face) the node keeps the key the layer
was drawn with and redraws when any part differs:

1. **Light and projection**: the light id assigned to the layer, and the
   fitted `clip_from_world` / per-face matrices, compared bitwise (the fit's
   output; spot and point outputs are deterministic, and the directional
   comparison is the camera-move invalidation).
2. **Configuration**: the shadow technique, depth format, resolution, cull
   mode, depth-bias settings, `Light_set` generation (slot layout, which
   decides `shadow_index` / `point_shadow_index`), and a texture generation
   bumped by `reconfigure()`.
3. **Casters**: "did a caster of this layer change since it was drawn".
   The draw-list scene is the single funnel for caster changes:
   `write_object_transform`, `write_object_bounds`, `set_object_flags`,
   `add_entries`, `remove_entries` and `release_object` all bump
   `m_shadow_bounds_serial` (`draw_list_scene.cpp`). The cache needs it per
   layer instead of scene-wide:
   - the funnel records the changed objects' old and new world bounds in a
     per-frame change list (empty in a static frame, so the steady-state
     cost is zero);
   - after the frame's cull views exist, each changed bound is tested
     against each layer's cull view (sphere / octant for cube faces, frustum
     for spot and directional - the classification lane code); a hit on the
     old or the new bound invalidates that layer. Cost: changed objects x
     shadow layers, once per frame.
   - the global serial stays the fast path: unchanged since the newest
     cached layer means no caster test at all.
4. **Skinned casters**: they carry no bounds and survive every view, and
   `Joint_poses::capture` re-uploads every frame without a serial. A layer
   whose cull view admits a skinned caster redraws every frame until skinned
   entries get posed bounds and a pose serial (the animation player and the
   IK tools are the change sites).

On a hit the layer redraws with the pass's clear as today. Point cube faces
are independent layers, so a drag beside a point light redraws only the
faces that see it.

Steady state: the comparison is a few matrices per layer and one serial
test - no per-frame allocation, no per-object work in a static frame. The
shadow fit (`fit`, ~0.2-0.45 ms on the render thread) still runs every
frame; skipping it for spot / point lights whose key is unchanged is the
follow-up (`doc/plans/shadows.md` "Temporal whole-fit skip").

## Verification

- `set_shadow_maps_frozen` (MCP, `doc/agents/mcp_server_usage.md`) is the
  upper bound; the cache must reach it on static Bistro.
- Image gate: every capture of the render-thread gates and `shadow_verify`
  byte-identical with the cache on and off; a drag of a caster, a light edit,
  a preset change and a light add / remove each invalidate exactly the
  layers they touch (a per-layer redraw counter in `get_shadow_fit_debug`).
- Skinned and physics scenes: no stale shadow over a 600-frame soak with
  animation playing and bodies falling.

## Related finding: Bistro stays at two refreshes per frame

Tier M holds Bistro at N=2 (60 fps on the 120 Hz Dell) although the frames
fit one refresh: with the pacer off the same build displays 99 % of frames
after one refresh with a 6.4 ms GPU span, and with frozen shadows (4.1-4.5 ms
of GPU work at the N=1 clock) the pacer still stays at N=2. The scene open
drives the cadence 1 -> 2 -> 3 -> 2; at N=2 the GPU clocks down to about
0.5, the window statistic `W` reads about 10 ms and 15-24 self-misses per
600 frames reset the upshift dwell, so the pre-warmed upshift never starts.
This belongs to [frame_pacing_metal.md](frame_pacing_metal.md); it is the
larger Bistro win (60 -> 120 fps with no rendering change) and is
independent of the cache.
