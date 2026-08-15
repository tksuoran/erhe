# Draw list performance improvements: cached primitive records

Companion to `doc/draw_list_renderer_requirements.md` /
`doc/draw_list_renderer_results.md`. Written 2026-08-15.

## Profiling finding (input)

`Primitive_buffer::update()` (`src/erhe/scene_renderer/erhe_scene_renderer/primitive_buffer.cpp`,
`Draw_list` overload) is the most expensive operation when rendering a single
frame with draw lists on. Per pass, per chunk, for every entry that passes the
filter it calls `write_primitive()`, which re-derives the whole per-primitive
GPU record from the live scene objects: `Mesh` -> `Node` ->
`world_from_node()`, `transpose(adjugate(...))` for the normal matrix,
`Mesh_primitive` -> `Material` -> `material_buffer_index`, `Skin` ->
`joint_buffer_index`, `Buffer_mesh` -> `base_vertex`, lightmap scale/offset,
plus the (unused for draw lists) id-offset alignment bookkeeping. Bistro:
5652 entries x (4 fill passes + 7 shadow renders) = ~62k full derivations per
frame, most of them pointer chasing through cold scene objects.

## Refined task definition

### Goal

Filling the primitive buffer for a draw list chunk shall be (nearly) a memcpy
per passing entry. The normative CPU copy of the per-primitive GPU record is
owned by the draw list (`Draw_list::primitive_records`, one record per entry,
in the exact GPU layout of `Primitive_interface::primitive_struct`), kept
current by hooks / listeners, and copied out per pass. Everything below is
per `Draw_list_scene` (per `Scene_root`).

### Record fields and their update source

| Field | Source | Update mechanism |
|---|---|---|
| `world_from_node`, `normal_transform` | node world transform (+ node negative-determinant flag) | Hook `Mesh::handle_node_transform_update()` -> `Scene_host::on_mesh_transform_changed()` -> `Draw_list_scene::enqueue_transform_update()`; applied once per frame in `flush_pending()` (dynamic + skinned objects; deduplicated per object by `Node_transforms::world_from_node_serial`) |
| `material_index` | `Material::material_buffer_index` (GPU slot assigned per `Material_buffer::update()`) | GPU-slot sync at draw time: per pass, compare each watched material's slot against the value cached in the material watch; refresh the records of objects using a changed material (rare: only when the scene material list changes) |
| `base_joint_index`, `skinning_factor` | `Skin::skin_data.joint_buffer_index` | Same GPU-slot sync, over the skinned objects only |
| `lightmap_scale_offset` | `Mesh_primitive::lightmap_uv_scale_offset` | New setter `Mesh::set_primitive_lightmap_uv_scale_offset()` -> `Scene_host::on_mesh_primitive_data_changed()` -> `Draw_list_scene::enqueue_refresh()`; applied in `flush_pending()` (record rewrite for the object, no re-classification). Lightmap baker / streamer switch to the setter |
| `base_vertex` | `Buffer_mesh` | Registration (a Buffer_mesh replacement already re-registers via `on_mesh_primitives_changed`) |
| `color`, `size` | pass `Primitive_interface_settings` + entry flag bits (selected / hovered) | Patched per entry after the memcpy (pass-dependent, cannot live in the record) |

Registration / re-registration / `rebuild_all()` write the full record for
every entry (records are always complete; hooks only keep them current).

### Fast / slow path in `Primitive_buffer::update(Draw_list, ...)`

- Fast path (memcpy record + patch color / size) when the settings are
  expressible without per-mesh evaluation: `face_id_base_provider == nullptr`,
  `color_source != id_offset`, `size_source == constant_size`. Every pass that
  the `Composition_pass` routing rule sends to draw lists (polygon fill, no
  forced variant bits) and every shadow pass satisfies this.
- Otherwise the existing per-entry `write_primitive()` path stays (single
  implementation shared with the `Render_bucket` overload; no drift).

### Non-goals (explicitly out of scope, unchanged)

- Static draw lists owning static GPU primitive buffers (no per-frame upload)
  - future work; the per-list contiguous GPU-layout records are the
  precondition for it.
- `Draw_indirect_buffer::update(Draw_list, ...)` (already a 20-byte write per
  entry from entry-local data).
- Frustum culling, translucent sorting, determinant-flip re-registration
  (results-doc follow-ups 1, 2, 4).
- `Mesh::skin`, `Mesh::point_size` / `line_width` changes after registration
  (no hook exists; sampled at registration as today).

### Behavior changes to be aware of

- Node transforms mutated after `Scene_root::flush_draw_lists()` in a tick
  (i.e. after `update_transforms`) are picked up by the next frame's flush.
  The fallback path reads them live. All editor transform writers run before
  the flush (tools, viewports, headset, physics, animation, MCP).
- Requirement R12b (live per-primitive upload inputs) is superseded by this
  design; R8a's "slot cannot be baked" is handled by the draw-time slot sync.

### Acceptance

- Pixel parity (headless Vulkan `capture_screenshot`) off vs on for the
  default scene, with a selected mesh, after moving a mesh over MCP, after
  changing a material's slot (create a material), and with a shadow-casting
  light.
- `Primitive_buffer::update` (draw-list overload) drops out of the top of the
  per-frame profile; per-pass CPU timing via `reset_composition_pass_stats` /
  `get_composition_passes` improves measurably on bistro (numbers recorded
  below).
- Debug builds: existing draw-list VERIFY invariants still hold (entry/record
  parallel vectors, object counts over register / unregister / undo).

## Plan

1. `Draw_list` gets `primitive_records` (bytes, `entries.size() * stride`);
   `Draw_list_scene` takes `const Primitive_interface&` (stride + offsets),
   writes records at `add_entries`, moves them in `remove_entry` swap-remove,
   drops them in `rebuild_all`. Helper `write_entry_record()`.
   `Draw_list_object` gains `transform_serial`, `joint_slot`;
   `Material_watch` gains `slot`. Editor: `App_context::program_interface`,
   `Draw_list_scene_dependencies::primitive_interface`.
2. Transform hook: `Scene_host::on_mesh_transform_changed()`,
   `Mesh::handle_node_transform_update()` notifies, `Scene_root` enqueues,
   `Draw_list_scene::flush_pending()` applies (`Pending_op::Kind::transform`).
3. Refresh hook: `Mesh::set_primitive_lightmap_uv_scale_offset()`,
   `Scene_host::on_mesh_primitive_data_changed()`, `enqueue_refresh()`,
   `Pending_op::Kind::refresh`; lightmap baker / streamer use the setter.
4. GPU-slot sync at the start of `draw_color()` / `draw_shadow()`.
5. `Primitive_buffer::update(Draw_list, ...)` fast path (memcpy + color / size
   patch), slow path retained.
6. Build (ninja vulkan Debug + Release, headless VS Debug), MCP parity +
   timing on bistro, record results here, update results doc follow-ups.

## Results (2026-08-15)

Verification over the in-editor MCP server (headless Vulkan Debug build,
`capture_screenshot` 2304x1200, bottom-left frame-time text masked): frames
identical with the setting off vs on for the default scene; with a mesh
selected; after moving the selected mesh over MCP (`transform_update_count`
increments; the move was done with lists on, so the transform hook path
produced the frame); after deselecting; after creating a material; with a
shadow-casting point light (cube sub-variant); with two skinned controller
models imported and after deleting one of them (joint slot shift ->
`slot_sync_count` increments, records re-synced).

Per-pass CPU wall time (`reset_composition_pass_stats` +
`get_composition_passes`, bistro loaded, nothing selected, ~730 frames per
Release configuration / ~75-430 per Debug configuration, off / on / off / on;
the two runs of each configuration agree within 2%):

| Pass | build | classic (us) | draw lists before this change (us, results doc) | draw lists now (us) |
|---|---|---|---|---|
| Content fill opaque not selected | Release ninja | 1413 | 369 | 272 |
| Content fill selected (0 selected) | Release ninja | 175 | 3.6 | 5.6 |
| Content fill translucent not selected | Release ninja | 501 | 88 | 104 |
| Content fill translucent selected | Release ninja | 165 | 1.3 | 1.6 |
| Sum of the four fill passes | Release ninja | 2254 | 463 | 383 |
| Shadow node (per exec) | Release ninja | 9450 | - | 2513 |
| Content fill opaque not selected | Debug VS headless | 50000 | - | 3250 |
| Sum of the four fill passes | Debug VS headless | 74000 | - | 5425 |
| Shadow node (per exec) | Debug VS headless | 348000 | - | 48000 |

(The classic and "before" columns are not from the same session / light
setup as the results doc, so compare ratios: fill sum classic -> draw lists
went from 2.8x to 5.9x, shadow node from 2.4x to 3.7x. Debug: 15x for the
opaque fill, 7x for the shadow node.)

What remains in the draw-list cost is per-list overhead (pipeline lookup,
`fmt::format` debug label, ring-buffer acquire per chunk, indirect command
write per entry) rather than per-primitive derivation.

Deviations from the plan: none. `slot_sync_count` is non-zero right after a
scene load (records are written at registration before the first
`Material_buffer::update()` assigned the slots) - expected, one-time.

Follow-ups enabled by this change: static lists can now upload their
`primitive_records` block once (G4 / R9); the per-list debug label could be
cached per entry count.
