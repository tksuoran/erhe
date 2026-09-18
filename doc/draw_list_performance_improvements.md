# Draw list performance improvements: cached primitive records

Stability: mostly stable

Filling the primitive buffer for a draw list chunk is nearly a memcpy per
passing entry. The normative CPU copy of the per-primitive GPU record is owned
by the draw list (`Draw_list::primitive_records`, one record per entry, in the
exact GPU layout of `Primitive_interface::primitive_struct`), kept current by
hooks, and copied out per pass. Everything here is per `Draw_list_scene`, that
is per `Scene_root`; `doc/draw_list_renderer.md` owns the renderer those lists
belong to, and section 10 of it owns the measured effect of both changes
together.

## Why the records exist

`Primitive_buffer::update()` (`Draw_list` overload) is otherwise the most
expensive operation in a frame rendered from draw lists. Per pass, per chunk,
for every entry that passes the filter it calls `write_primitive()`, which
re-derives the whole per-primitive GPU record from the live scene objects:
`Mesh` to `Node` to `world_from_node()`, `transpose(adjugate(...))` for the
normal matrix, `Mesh_primitive` to `Material` to its slot, `Skin` to
`joint_buffer_index`, `Buffer_mesh` to `base_vertex`, the lightmap scale and
offset, plus the id-offset alignment bookkeeping that draw lists do not use.
On the Niagara bistro scene that is 5652 entries times (4 fill passes plus 7
shadow renders), roughly 62k full derivations per frame, most of them pointer
chasing through cold scene objects.

## Record fields and their update source

| Field | Source | Update mechanism |
|---|---|---|
| `world_from_node`, `normal_transform` | node world transform (plus the node negative-determinant flag) | `Mesh::handle_node_transform_update()` -> `Scene_host::on_mesh_transform_changed()` -> `Draw_list_scene::enqueue_transform_update()`; applied once per frame in `flush_pending()` (dynamic and skinned objects; deduplicated per object by `Node_transforms::world_from_node_serial`) |
| `material_index` | the material's slot in the draw-list `Material_set` (`doc/draw_list_material_set.md`) | Baked at registration and kept by the material set: a slot is stable for as long as anything references the material in that set, so the record needs no per-pass repair |
| `base_joint_index`, `skinning_factor` | `Skin::skin_data.joint_buffer_index` | GPU-slot sync at draw time, over the skinned objects only: the joint slot is assigned per `Joint_buffer::update` call, so each pass compares the current slot against the cached one and refreshes the records of objects whose slot moved |
| `lightmap_scale_offset` | `Mesh_primitive::lightmap_uv_scale_offset` | `Mesh::set_primitive_lightmap_uv_scale_offset()` -> `Scene_host::on_mesh_primitive_data_changed()` -> `Draw_list_scene::enqueue_refresh()`; applied in `flush_pending()` as a record rewrite for the object, with no re-classification. The lightmap baker and streamer use that setter |
| `base_vertex` | `Buffer_mesh` | Registration; a `Buffer_mesh` replacement re-registers through `on_mesh_primitives_changed` |
| `color`, `size` | the pass's `Primitive_interface_settings` plus the entry flag bits (selected / hovered) | Patched per entry after the memcpy: pass-dependent, so it cannot live in the record |

Registration, re-registration and `rebuild_all()` write the full record for
every entry: records are always complete, and the hooks only keep them current.

## Fast and slow path in `Primitive_buffer::update(Draw_list, ...)`

- Fast path (memcpy the record, then patch color and size) when the settings
  are expressible without per-mesh evaluation: `face_id_base_provider ==
  nullptr`, `color_source != id_offset`, `size_source == constant_size`. Every
  pass that the `Composition_pass` routing rule sends to draw lists (polygon
  fill, no forced variant bits) and every shadow pass satisfies this.
- Otherwise the per-entry `write_primitive()` path runs. It is a single
  implementation shared with the `Render_bucket` overload, so the two cannot
  drift.

## What this does not cover

- Static draw lists owning static GPU primitive buffers, with no per-frame
  upload. The per-list contiguous GPU-layout records are the precondition for
  it; the work itself is future work.
- `Draw_indirect_buffer::update(Draw_list, ...)`, which is already a 20-byte
  write per entry from entry-local data.
- `Mesh::skin` and `Mesh::point_size` / `line_width` changes after
  registration: no hook exists, and both are sampled at registration.

## Consequences to know

- Node transforms mutated after `Scene_root::flush_draw_lists()` within a tick
  (that is, after transform propagation) are picked up by the next frame's
  flush. The fallback path reads them live. Every editor transform writer -
  tools, viewports, headset, physics, animation, MCP - runs before the flush.
- These records are what R12b of `doc/draw_list_renderer.md` means by a hook
  keeping a cached value current instead of reading it live at upload time.
- The joint-slot sync counter is non-zero right after a scene load, because
  records are written at registration before the first `Joint_buffer::update`
  assigns the slots. That is one-time and expected.

## Verification

Pixel parity over the in-editor MCP server (headless Vulkan,
`capture_screenshot` at 2304x1200, bottom-left frame-time text masked): frames
identical with the draw-list setting off and on for the default scene; with a
mesh selected; after moving the selected mesh over MCP (the move with lists on
exercises the transform hook and increments `transform_update_count`); after
deselecting; after creating a material; with a shadow-casting point light (cube
sub-variant); and with two skinned models imported and one of them deleted,
which shifts a joint slot and increments the slot-sync counter. Debug builds
additionally check the draw-list verify invariants (entry and record vectors
stay parallel, object counts survive register / unregister / undo).

## Future work

- [plans/draw_list_renderer.md](plans/draw_list_renderer.md): static lists
  uploading their record block once, and the per-list draw overhead that is
  left.
