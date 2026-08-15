# Draw list renderer — results and follow-ups

Companion to `doc/draw_list_renderer_requirements.md` and
`doc/draw_list_renderer_plan.md`. Written 2026-08-15 after phases 1–6.

## What landed

| Phase | Commit | Content |
|---|---|---|
| 1 | `ab3c4079` | `Draw_list_scene` data model + registration (keys, entries, objects, pending queue, flag mirroring, rebuild) |
| 2 | `0ca35cfe` | `Scene_host` hooks, `Mesh` notifications, `Scene_root` ownership + `flush_draw_lists`, material writers via `Mesh::set_primitive_material`, MCP `get_draw_lists` / `set_draw_lists_enabled` |
| 3 | `868dfe95` | Color draw path: cached resolution, entry-based `Primitive_buffer` / `Draw_indirect_buffer`, `Forward_renderer::render_draw_lists`, `Composition_pass` routing rule, R12 material identity watch |
| 4 | `87ade69d` | Shadow draw path (depth-only / distance / cube sub-variants), coarsened-key prewarm |
| 5 | `2f17ee45` | `Editor_settings_config::use_draw_lists` (codegen v2) + Settings checkbox; timing counters + MCP `reset_composition_pass_stats`; early-out; cheap debug labels |
| 6 | (this commit) | Measurement + this document |

## Verification

- Pixel parity (headless Vulkan, `capture_screenshot`, 2304x1200): frames
  identical with the setting off vs on for the default scene, with a
  selected mesh (selected / not-selected fill passes), and with a
  shadow-casting point light added (cube sub-variant) — C1.
- Registration lifecycle over MCP: create / delete / undo keep object /
  entry counts consistent; the pending queue drains every frame; no
  negative-determinant flips (R10b) in normal use.
- Prewarm parity (R22): no `Shader_variant_cache miss` after enabling the
  path in a prewarmed scene; the only later misses are genuinely new
  variants (light-count changes), which the fallback path compiles too.
- C5: two viewports on one scene, one in shader-debug mode (fallback) and
  one on draw lists — `color_environment_change_count` and
  `lazy_resolution_count` unchanged over 60 frames.
- Bistro (`res/editor/assets/niagara_bistro/bistro.gltf`, 2909 registered
  objects, 5652 entries, 132 non-empty draw lists) — CPU time per pass, see
  below.

## Measurements

Per-pass CPU wall time inside `Composition_pass::render()` /
`Shadow_render_node::execute_rendergraph_node()`, averaged over ~120
frames via MCP `reset_composition_pass_stats` + `get_composition_passes`
(bistro loaded, default viewport, nothing selected).

### Debug (build_ninja_win_vulkan)

| Pass | classic (us) | draw lists (us) | ratio |
|---|---|---|---|
| Content fill opaque not selected | 15392 | 8003 | 1.9x |
| Content fill selected (0 selected) | 1468 | 20 | 73x |
| Content fill translucent not selected | 4585 | 1074 | 4.3x |
| Content fill translucent selected | 1375 | 5 | 275x |
| Sum of the four fill passes | 22820 | 9102 | 2.5x |
| Shadow node (7 directional shadow lights) | 1043510 | 730622 | 1.4x |

### Release (build_ninja_win_vulkan_release)

| Pass | classic (us) | draw lists (us) | ratio |
|---|---|---|---|
| Content fill opaque not selected | 846 | 369 | 2.3x |
| Content fill selected (0 selected) | 97 | 3.6 | 27x |
| Content fill translucent not selected | 254 | 88 | 2.9x |
| Content fill translucent selected | 87 | 1.3 | 67x |
| Sum of the four fill passes | 1285 | 463 | 2.8x |
| Shadow node (bistro viewport, per exec) | 51774 | 21726 | 2.4x |

(200 frames per configuration, off / on / off; the two "off" runs agree
within 1%.)

Reading: the remaining draw-list cost is the per-frame per-primitive upload
(`Primitive_buffer::write_primitive` for every visible entry — transform,
normal matrix, material / joint slots) and the GPU submit; the bucketing +
`Shader_key::derive` + bucket scan that the classic path repeats per pass
is gone (P1). Passes whose filter rejects everything now cost ~nothing
(early-out before the pass prologue).

## Deviations from the requirements doc

- R18: the graphics-preset part of the color environment is re-checked
  per color draw alongside the light partition (recompute-and-compare)
  instead of via the `graphics_settings` message-bus event. Equivalent and
  simpler.
- Preview (material / brush) and tools scene roots have no
  `Draw_list_scene` (constructed on init worker threads); their passes
  take the fallback via the null check in the routing rule.
- Everything non-skinned registers as `dynamic` (R10a): no static source
  exists yet; the API carries the flag.
- Setting default is OFF (`use_draw_lists = false`); flip to ON once the
  follow-ups below have had a round of daily use.

## Follow-ups (ordered)

1. Frustum culling on the entry AABB (Q6) — needs an AABB update path for
   dynamic objects (per-draw recompute from the node, or a hook that
   respects the threading contract).
2. Re-register on negative-determinant flip instead of the debug assert
   (R10b): route `Mesh::handle_node_transform_update`'s flag change to a
   reregister when the registered value differs.
3. Static mobility source (item flag / asset metadata) and cached static
   uploads (R9 / G4).
4. Depth-sort translucent entries (C1 note).
5. Retire `bucket_primitives` for the covered passes once the setting has
   defaulted to ON for a while; keep it for the fallback passes.
6. Shadow frustum-fit AABB gathering still walks all content meshes per
   shadow render — could read the entries' AABBs once the update path (1)
   exists.
