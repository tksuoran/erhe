# Reloadable asset loads: drop on undo, re-read on redo

Stability: mostly stable

An undone glTF import keeps only what it needs to redo the load, and re-reads
the file from disk on redo. Without that, loading a large glTF, undoing it and
then loading something else keeps the first scene's memory alive for as long
as the undo entry exists: the undone import is at once the owner of every
imported object, a declared asset user, and a history pin that
`Asset_manager::unload_record` refuses on with "undo/redo history (clear
history to release)".

The scope is `Import glTF`. `Scene_open_operation` still keeps its whole
`Scene_root` across undo; see "Future work".

## What dropping the payload releases

| memory | released |
|---|---|
| CPU: triangle soups, `GEO::Mesh`, element mappings, CPU BVH (about 110-140 B per triangle) | yes |
| CPU: `Gltf_image_source::encoded_bytes` (the original PNG / JPEG kept for byte-exact re-export) | yes. A plain import holds it once, on the attach operations; the asset-manager container-record and prefab cases hold it twice |
| texture VRAM | yes, deferred to frame completion (`vmaDestroyImage` from a completion handler) |
| mesh vertex / index VRAM | into the pool free list only, and frame-deferred. `Buffer_pool` never destroys a block, so the committed footprint stays at its high-water mark |

The mesh-VRAM limit is the point of reclaiming pool space rather than an
aside: the pools are capped at `max_buffers_per_pool` (64) and exceeding the
cap makes a mesh build fail gracefully - `Buffer_pool` returns `{}` and the
caller sets `build_failed` - so a second scene silently loses meshes if the
first scene's ranges are never returned.

Redo is much cheaper than the first load: the BVH disk cache is keyed by a
content hash of the triangle positions, so a re-import hits it and skips the
build. A redo re-parses synchronously, which is safe because
`current_command_buffer` is live across `Operation_stack::update()` - what
`get_or_load_container` and the inline parse's `image_residency.drain()`
require - and nothing in the import build path re-enters the stack except
`queue()`, which is legal while the stack is executing. Redoing a
Bistro-sized import stalls for seconds.

Identity is not remapped: `erhe::Unique_id` is a process-global counter, never
persisted and not copied by a clone, so a rebuild produces the same names with
different ids. Cached references to the old objects are cleared by
`Items_removed_message` (doc/editor/import_undo_reference_clearing.md).

## The lossless gate

A payload may only be dropped when nothing recorded after the load survives in
the redo stack. Undo is LIFO, so by the time a load is undone, everything
recorded after it is already in `m_undone` holding raw `shared_ptr`s to the
loaded objects, and redoing one of those against re-created content aborts -
`Item_parent_change_operation` asserts that the child's parent is the one it
recorded.

The decision belongs to the stack, not to the operation. `Operation_stack::undo()`
calls `Operation::on_lossless_undo()` on the top-level entry it just popped,
and only when `m_undone.size() == 1`:

```cpp
// operation_stack.cpp, in undo(), after m_undone.push_back(operation):
if (m_undone.size() == 1) {
    // Nothing recorded after this entry survives in the redo stack, so an
    // operation that can rebuild itself may release what it is holding.
    operation->on_lossless_undo(m_context);
}
```

A self-query (`m_undone.empty()` from inside `undo()`) would be unsound under
nesting: `Compound_operation::undo()` iterates its children, and a child sees
the stack's redo state rather than its siblings'. `Operation::on_lossless_undo()`
is a no-op by default and `Compound_operation` deliberately does not forward
it, so a nested import keeps its payload - conservative and correct.

With async loading on, a batched import is not nested: `import_gltf` queues
its operation from a completion callback frames after `end_group` has closed,
so it lands as its own top-level entry and is gated normally. The nesting case
exists on the synchronous path (`load.async_gltf_load: false`), where
`end_group` really does wrap the import in a `Compound_operation`.

One related quirk interacts with the hook: `undo()` does not check
`!m_grouping`, and `queue()` inside a group neither clears nor consults
`m_undone`, so an MCP `batch` of `[X, undo]` can reach `m_undone.size() == 1`
and drop the payload of an entry that the already-executed sibling `X` was
recorded after. There is no redo hazard, because `end_group` discards the
entry entirely - but "the stack drives the hook only for the top-level entry
it popped" is not the whole story.

## `Import_gltf_operation`: recipe and payload

`src/editor/operations/import_gltf_operation.{hpp,cpp}` splits what the import
IS from what it HOLDS:

```cpp
class Gltf_import_recipe
{
public:
    std::filesystem::path     path;
    std::weak_ptr<Scene_root> scene_root;
    bool materials_as_references{false};
    bool fit_view_to_content   {false};
    // Decisions the first import derived from the target scene's live state
    // (an existing camera / a non-empty light layer suppresses the defaults).
    // Recorded, not re-derived - otherwise a redo after the user added a
    // camera produces a different node set than the original import.
    bool add_default_camera{false};
    bool add_default_light {false};
};
```

`Import_gltf_operation` holds the recipe, the `Prepared_gltf_parse` the first
`execute()` consumes, and the compound operation that is the payload; a null
compound means the payload was dropped, and `execute()` then rebuilds from the
recipe. `collect_item_references()` forwards to the compound when it is
present and reports nothing when it is not, which is what stops
`unload_record` refusing.

`make_import_gltf_operation` takes a non-const `Gltf_import_recipe*`, used in
and out: the first build records the derived default-camera / default-light
decisions into the recipe, a rebuild supplies them. A rebuild derives its
`Build_info` from `make_import_build_info(context)`, whose default
`Mesh_memory_queue::interactive` is the right queue for a synchronous redo -
it must not use the async path's loader queue, which gates publication on the
loader watermark.

`Content_library_attach_operation` needs no change of its own. Releasing
`m_usership` on undo would be actively wrong: with the lossless gate failing,
the payload is kept, so an unload would succeed while the history still owns
every asset, and `unload_record` would merely log "undeclared asset user" and
erase the record anyway. Releasing the payload destroys the compound and its
children, and `~Asset_reference` unregisters the usership.

## `free_undone_loads`

The editor command `Edit.Free undone loads` and the MCP tool of the same name
free what the lossless gate declined, by discarding the redo entries that
block it:

1. find the highest index in `m_undone` whose operation `has_payload()`;
2. release that payload;
3. erase `m_undone[0, i)` - the entries recorded after it - destroying them
   and releasing their payloads too;
4. report the released and discarded counts.

`undo()` pushes the most recently recorded entry first, so in `m_undone` index
0 is newest and `back()` is the next to redo; entries recorded after index `i`
are at indices below `i`. Everything above `i` was recorded before the load,
cannot reference its content, and stays redoable. Nothing having a payload is
a no-op, not an error.

The index rule lives in its own dependency-free
`operations/operation_stack_selection.{hpp,cpp}` so it is unit-testable
without building the editor.

## Evictable BLAS caches

`Scene_tlas::m_blas_cache` and `Lightmap_baker::m_blas_cache` each hold a
`shared_ptr<Primitive>` per traced mesh expressly to pin the GPU ranges. With
ray query enabled - the default - nothing would be reclaimed for traced
geometry without eviction, and once a lightmap has been baked, every baked
mesh would stay pinned: `Lightmap_baker::set_baking_enabled` deliberately
keeps the working set on disable so re-enabling continues where it paused, so
`release_working_set()` has to be called from a definite end of the bake
(scene close) rather than implied by a disable.

Both sweep on `entry.primitive->render_shape.use_count() == 1`, not on
`entry.primitive.use_count()`: `Primitive`'s copy constructor is defaulted and
`render_shape` is a `shared_ptr`, so two distinct `Primitive` objects can
yield the same `Buffer_mesh*` key and a live mesh may hold an aliasing
`Primitive`.

A plain erase is enough - no frame-indexed retirement. `Tlas_slot` holds an
`Acceleration_structure` and a capacity but no BLAS pointer; the
`.bottom_level = blas` binding lives in `m_instances`, per-frame scratch
cleared at the top of `update()`; each slot's TLAS is built and consumed
inside one `update()`; and `~Acceleration_structure_impl` defers
`vkDestroyAccelerationStructureKHR` to the completion of the frame current at
destruction, which implies every earlier frame has completed. The cache key
cannot dangle under an erase sweep either: it is
`&render_shape->m_renderable_mesh`, a by-value member of the shape the cached
`shared_ptr` keeps alive.

Eviction does not fix the recycled-range staleness of the cache key: a
`Primitive_render_shape::commit_geometry_buffer_mesh()` move-assigns in place,
so the key stays valid and the refcount stays at two or more while the pool
ranges underneath are freed and recycled. That is a separate defect, recorded
in doc/editor/raytrace.md.

## Failure semantics for a rebuild

`Operation_stack::redo()` pushes onto `m_executed` unconditionally and never
consults `has_error()`, and `Compound_operation::execute` ignores child
failures, so `set_error()` alone is not enough. The rule: when a rebuild fails
(file gone, target scene expired, parse error), `execute()` leaves the
compound null and records the error; `undo()` on a null compound is then a
no-op, so the entry is inert but harmless. The operation-error surface is
MCP-only, and the Operation Stack window prints only `describe()`, so a failed
rebuild also logs.

## Measuring

Reclaims are frame-deferred - mesh ranges are double-gated on frame completion
and the loader watermark, texture destruction on frame completion - so every
measurement advances several frames before sampling.

The MCP `get_memory_usage` tool reports per-pool mesh bytes (used, capacity,
blocks), texture bytes and count, `Device::get_memory_budget()` (Vulkan only;
the other backends return `{}`), container records and which retain a parse,
undo/redo entry counts, and the BLAS cache entry count. It is backed by
`Buffer_pool::get_statistics()` / `Mesh_memory::get_pool_statistics()`, a
process-wide texture byte counter in `erhe::graphics::Texture` (estimated from
the create info, excluding views and wrapped images) and
`Scene_tlas::get_blas_count()`.

## Verification

- Unit (`editor_asset_tests`): the `free_undone_loads` index rule - release
  the highest-index entry with a payload, discard everything before it -
  against a stub stack, including that selecting index 0 discards nothing.
- Smoke (`scripts/reloadable_asset_loads_smoke_test.py`): import, baseline,
  undo - mesh pool used bytes, texture bytes and CPU counts all drop,
  `get_editor_references` is clear, the draw-list object count returns to the
  baseline; redo brings the content back with the same names and different
  uids, and a second cycle is stable; an import followed by a move, undoing
  the move and then the import keeps the payload (the lossless gate); an MCP
  `batch` containing an import plus another operation keeps it too (and the
  test branches on which load path ran, so it says something real either way);
  `free_undone_loads` then drops it and discards the later redo entry;
  `unload_asset` after an undo refuses while the payload is kept and succeeds
  once it is dropped; BLAS release with ray tracing on throughout, sampling
  `get_memory_usage` before the import, after it and after undo plus N frames,
  and repeated with a lightmap bake in between (the bake variant asserts on
  mesh-pool used bytes and BLAS cache counts only, because
  `release_working_set()` deliberately keeps the display atlas resident);
  `materials_as_references` true and false measured separately; and a file
  deleted between undo and redo leaving the editor consistent.

`use_draw_lists` is on by default and `Draw_list_object` owns the mesh for as
long as it is registered, so a test that does not assert that undo's detach
unregisters every draw-list object would pass while nothing is freed on the
default configuration.

## Known limits

- Mesh VRAM returns to the pool free list, not to the driver. `Buffer_pool`
  never destroys a block, so the process footprint stays at its high-water
  mark; what the release buys is that the next scene reuses the space instead
  of growing new blocks (doc/erhe/mesh_memory.md).
- `materials_as_references = true` leaves a second container parse in the
  asset manager, untouched by the drop (doc/editor/asset_manager.md, "Current
  restrictions").
- `Prefab_library` templates are never released, so an import that pulled in
  prefabs keeps those regardless.
- The BLAS cache key can go stale over recycled buffer ranges
  (doc/editor/raytrace.md).

## Future work

- [plans/asset_loading.md](../plans/asset_loading.md) - making `Scene_open` reloadable, releasing prefab templates, the second `materials_as_references` parse.
