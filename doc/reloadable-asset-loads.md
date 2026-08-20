# Reloadable asset loads: drop on undo, re-read on redo

## Context

Loading a large glTF, undoing it, then loading something else keeps the first scene's
memory alive for as long as the undo entry exists. The undone import is simultaneously the
**owner** of every imported object (`Content_library_attach_operation::m_item`,
`Item_insert_remove_operation::m_item` holding the whole detached subtree,
`Async_raytrace_kickoff_operation::m_scene_root` — a *strong* `shared_ptr` to the entire
target scene, `async_raytrace_kickoff_operation.hpp:31`), a **declared user**
(`m_usership`), and a **history pin** that `Asset_manager::unload_record` refuses on with
`"undo/redo history (clear history to release)"` (`asset_manager.cpp:1036-1052`).

Goal: an undone load keeps only what it needs to *redo the load*, and re-reads the file on
redo.

**Feasibility: yes.** `make_import_gltf_operation` already supports being handed no
prepared parse and parsing inline (`gltf.cpp:769-806`); `Prefab_library::reload` is
precedent for re-parsing from disk and re-instantiating rather than rebinding
(`prefab_library.cpp:398-443`); and the `Items_removed_message` work that just landed is
what makes it *safe*, because every window and tool now drops cached references when
content is removed.

**What is actually reclaimed:**

| memory | reclaimed on drop? |
|---|---|
| CPU: triangle soups, `GEO::Mesh`, element mappings, CPU BVH (~110-140 B/triangle) | **yes** |
| CPU: `Gltf_image_source::encoded_bytes` (original PNG/JPEG kept for byte-exact re-export) | **yes** — held once on the attach operations for a plain import (`gltf.cpp:322`; the parse-local `gltf_data` dies with `make_import_gltf_operation`). Held twice only in the Asset_manager container-record and prefab cases |
| Texture VRAM (`vmaDestroyImage` via a frame-completion handler, `vulkan_texture.cpp:68-96`) | **yes**, deferred to frame completion |
| Mesh vertex/index VRAM | **into the pool free list only**, and frame-deferred (`buffer_pool.cpp:26-37,154-159`; `mesh_memory.cpp:641-718`). `Buffer_pool` never destroys a block (`buffer_pool.cpp:92,208`), so the committed footprint stays at its high-water mark |

The mesh-VRAM caveat still matters here: pools are capped at `max_buffers_per_pool: 64`
and exceeding it makes the mesh build **fail gracefully** (`buffer_pool.cpp:184-194`
returns `{}`; callers set `build_failed`, `primitive_builder.cpp:145-149`), so reclaiming
pool space is what stops the second scene from silently losing meshes. Redo is much
cheaper than the first load: the BVH disk cache is keyed by a content hash of the triangle
positions (`bvh_geometry.cpp:265,289-291`), so a re-import hits it and skips the build.

## Decisions

- **Automatic drops are lossless-only** (your choice).
- **Plus an explicit `free_undone_loads`** that frees the rest by discarding the redo
  entries that block it.
- **Redo re-parses synchronously.** Verified safe: `current_command_buffer` is live across
  `Operation_stack::update()` (`editor.cpp:549,690,695,735,1106`), satisfying
  `get_or_load_container`'s `ERHE_VERIFY` (`asset_manager.cpp:1143`) and the inline parse's
  `image_residency.drain` (`gltf.cpp:794`). Re-entrancy is fine — nothing in the import
  build path re-enters the stack except `queue()`, which is legal while `m_executing`.
  Redoing a Bistro-sized import will stall for seconds.
- **Scope is `Import glTF`.** `Scene_open` is phase 6.

## Why the gate is what it is

Undo is LIFO (`operation_stack.cpp:208-222`), so by the time a load is undone, everything
recorded after it is already in `m_undone`, holding raw `shared_ptr`s to the loaded
objects. `Item_parent_change_operation` asserts
`ERHE_VERIFY(m_child->get_parent().lock() == m_parent_before)`
(`item_parent_change_operation.cpp:43,68`), so redoing one against re-created content
**aborts**. Nothing remaps identity: `erhe::Unique_id` is a process-global counter, never
persisted, not copied by clone (`unique_id.hpp:47`, `item.cpp:112-119`).

Index order, for the explicit command: `undo()` pushes the **most recently recorded** entry
first, so in `m_undone` **index 0 is newest and `back()` is the next to redo**; entries
recorded after index `i` are at indices `< i`.

## Design

### 1. The drop decision belongs to the stack, not to the operation

A self-query (`m_undone.empty()` from inside `undo()`) is **unsound under nesting**:
`Compound_operation::undo()` iterates children (`compound_operation.cpp:41-51`), and a
child sees the stack's redo state, not its siblings'. This is reachable — the MCP `batch`
tool wraps arbitrary sub-calls in `begin_group`/`end_group` (`mcp_server.cpp:692,743`),
`queue()` executes and collects into the group (`operation_stack.cpp:94-100`), and
`end_group` with `count > 1` wraps them in a `Compound_operation`
(`operation_stack.cpp:159-165`). An import batched with later siblings would drop its
payload and then abort on redo.

So `Operation_stack::undo()` drives it, only for the top-level entry it popped:

```cpp
// operation_stack.cpp, in undo(), after m_undone.push_back(operation):
if (m_undone.size() == 1) {
    // Nothing recorded after this entry survives in the redo stack, so an
    // operation that can rebuild itself may release what it is holding.
    operation->on_lossless_undo(m_context);
}
```

`Operation::on_lossless_undo()` is a no-op by default. `Import_gltf_operation` overrides it
to release its payload. **`Compound_operation` deliberately does not forward it** — a
nested import keeps its payload, which is conservative and correct.

### 2. `Import_gltf_operation` — the payload/recipe split

New `src/editor/operations/import_gltf_operation.{hpp,cpp}`:

```cpp
class Gltf_import_recipe
{
public:
    std::filesystem::path     path;
    std::weak_ptr<Scene_root> scene_root;
    bool materials_as_references{false};
    bool fit_view_to_content   {false};
    // Decisions the first import derived from the target scene's live state
    // (gltf.cpp:892-923: an existing camera / non-empty light layer suppresses
    // the defaults). Recorded, not re-derived - otherwise a redo after the user
    // added a camera produces a different node set than the original import.
    bool add_default_camera{false};
    bool add_default_light {false};
};

class Import_gltf_operation : public Operation
{
public:
    void execute          (App_context& context) override; // rebuilds when the payload is gone
    void undo             (App_context& context) override;
    void on_lossless_undo (App_context& context) override; // releases the payload
    void collect_item_references(std::unordered_set<const erhe::Item_base*>&) const override;

    [[nodiscard]] auto has_payload() const -> bool;
    void release_payload();

private:
    Gltf_import_recipe                   m_recipe;
    std::shared_ptr<Prepared_gltf_parse> m_prepared_parse; // consumed by the first execute
    std::shared_ptr<Compound_operation>  m_compound;       // null == payload dropped
};
```

`make_import_gltf_operation` **gains a parameter**: a non-const `Gltf_import_recipe*`,
used in/out — on the first build it *records* the derived default-camera / default-light
decisions into the recipe; on a rebuild it *supplies* them. (A `const` pointer cannot do
both.) Both flags are locals derived inside that function today — initialised `true` and
falsified from the parse and from live target-scene state (`gltf.cpp:832-833,889-919`).

The rebuild derives its `Build_info` from `make_import_build_info(context)`
(`gltf.hpp:92`), whose default `Mesh_memory_queue::interactive` is the right one for a
synchronous redo — it must **not** copy the async path's loader queue, which gates
publication on the loader watermark.

`collect_item_references()` forwards to the compound when present and reports nothing when
dropped — which is what stops `unload_record` refusing.

**No `Content_library_attach_operation` change is needed.** Releasing `m_usership` on
*undo* would be actively wrong: with the lossless gate failing, the payload is kept, so an
unload would succeed while the history still owns every asset — `unload_record` would only
log `"undeclared asset user"` and erase the record anyway
(`asset_manager.cpp:1069-1088`). (Materials reachable from the retained mesh subtree would
still be refused via `Item_insert_remove_operation::collect_item_references`,
`item_insert_remove_operation.cpp:200-229`; textures, skins, animations and physics
materials would slip through.) Releasing the payload already destroys the compound and
its children, and `~Asset_reference` unregisters the usership. Verify this by test rather
than adding code.

### 3. `free_undone_loads` — the explicit command

Editor command (`Edit.Free undone loads`) plus an MCP tool:

1. find the **highest index** in `m_undone` whose operation `has_payload()`;
2. release that payload;
3. erase `m_undone[0, i)` — the entries recorded after it — destroying them and releasing
   their payloads too;
4. report bytes freed, entries dropped, entries discarded.

Everything at indices `> i` was recorded *before* the load and cannot reference its
content, so it stays redoable. No-op, not an error, when nothing has a payload.

### 4. Make the BLAS caches evictable

`Scene_tlas::m_blas_cache` holds a `shared_ptr<Primitive>` per traced mesh expressly to pin
the GPU ranges, and is documented as never evicted (`scene_tlas.hpp:120-126,141-144`). With
ray query enabled — the default here — nothing is reclaimed for traced geometry.

- **Refcount on the right object.** `Primitive`'s copy constructor is `= default`
  (`primitive.cpp:966`) and `render_shape` is a `shared_ptr`, so two distinct `Primitive`
  objects can yield the same `Buffer_mesh*` key; `entry.primitive.use_count() == 1` can be
  true while a live mesh holds an aliasing `Primitive`. Test
  `entry.primitive->render_shape.use_count()` instead.
- **A plain erase is enough; no frame-indexed retirement.** `Tlas_slot` holds only an
  `Acceleration_structure` and a capacity — no BLAS pointer (`scene_tlas.hpp:149-156`) —
  and `.bottom_level = blas` lives in `m_instances`, per-frame scratch cleared at the top
  of `update()` (`scene_tlas.cpp:166,235`). Each slot's TLAS is built *and* consumed inside
  one `update()` (`ray_trace_renderer.cpp:463,515`; `ddgi_renderer.cpp:998,1012`), and
  `~Acceleration_structure_impl` defers `vkDestroyAccelerationStructureKHR` to the
  completion of the frame current at destruction
  (`vulkan_acceleration_structure.cpp:242-252`), which implies every earlier frame has
  completed. (An earlier draft of this plan demanded retirement on evidence that turned out
  to be wrong.)
- **The key cannot dangle** under an erase-sweep: it is `&render_shape->m_renderable_mesh`,
  a by-value member of the shape the cached `shared_ptr` keeps alive
  (`primitive.hpp:211,255`).
- **This does not fix the recycled-range staleness bug**, contrary to an earlier draft:
  `Primitive_render_shape::commit_geometry_buffer_mesh()` move-assigns in place
  (`primitive.cpp:752`), so the key stays valid and the refcount stays ≥ 2 while the pool
  ranges underneath are freed and recycled — a stale BLAS no refcount sweep triggers on.
  Pre-existing; note it, do not fix it here.

**`Lightmap_baker::m_blas_cache` needs the same treatment — it is a live, unbounded pin.**
An earlier draft claimed it is cleared between bakes. It is not:
`release_working_set()`, which holds the `m_blas_cache.clear()`
(`lightmap_baker.cpp:4585,4615`), has **no callers anywhere in `src/`** — it is a private
member reachable from nothing. And `set_baking_enabled` states the opposite of that draft
(`lightmap_baker.cpp:4448-4452`): *"the working set (accumulation, sweeps, G-buffer,
BLAS/TLAS) is deliberately KEPT on disable so re-enabling continues where it paused"*. Its
`Blas_entry::primitive` is a `shared_ptr<Primitive>` commented "keeps the Buffer_mesh
alive" (`lightmap_baker.hpp:637`). So once a lightmap has been baked on an imported scene,
dropping the payload frees nothing for every baked mesh. Give it the same
`render_shape.use_count()` sweep, **and** give `release_working_set()` a call site (bake
finished / baking disabled with no resume intent) so the rest of the working set is
reachable too. The smoke test must include a bake, or it would assert an invariant that
does not exist.

### 5. Prefab reference entries — deliberately NOT made operation-consistent

This was investigated and rejected. Two reasons, both discovered in review:

- **A redo does not duplicate them.** `add_prefab_reference_entries` adds the *template's*
  objects (`prefab_library.cpp:759-805`), `Prefab_library::get_or_load` returns the cached
  template on the second call (`prefab_library.cpp:112-116`), and templates are never
  released. So the redo passes the **same pointers**, and
  `Content_library_node::add`'s pointer-equality dedupe (`content_library.hpp:414-444`)
  does catch them. Nothing accumulates, and the drop/redo cycle works with these entries
  left exactly as they are.
- **Converting them would introduce a shared-entry removal regression.** Because the
  entries hold shared template objects, two imports referencing the same prefab produce one
  deduped entry. With per-import `Content_library_attach_operation`s, undoing the second
  import would call `m_sublibrary->remove(m_item)` and delete the entry the first import's
  live instances still need. For a *plain* import that failure mode does not exist today,
  because a second import re-parses into different material pointers — but with
  `materials_as_references` it already does, since
  `acquire_import_materials_as_references` substitutes manager-owned materials
  (`gltf.cpp:273-283`), so two imports of one path already share pointers behind two
  per-import attach operations. That strengthens the rejection rather than weakening it.
  Avoiding the hazard would require refcounting the reference entries. (A secondary wrinkle: one prefab instantiated by N
  carriers calls `add_prefab_reference_entries` N times, `prefab_library.cpp:627-679`, so a
  collector would have to dedupe as well.)

What remains real is only the other half: **undoing an import leaves the prefab's
reference entries in the content library permanently**, because they were never part of
the operation. That is a pre-existing bug, orthogonal to this work, and is left out of
scope — fixing it properly means refcounted reference entries, not per-import operations.

## Pins that bound the win — state, and assert in tests

- **Draw-list objects own the mesh.** `Draw_list_object_create_info::mesh` is a
  `shared_ptr<erhe::scene::Mesh>` and `Draw_list_object` is documented "Owns the mesh (and
  through it the primitives) for as long as it is registered"
  (`draw_list_object.hpp:18,47-53`). `use_draw_lists` is ON by default, so the test must
  assert that undo's detach actually unregisters every draw-list object
  (`Draw_list_scene::enqueue_unregister`, `draw_list_scene.hpp:160`) — otherwise nothing is
  freed on the default configuration.
- **`materials_as_references = true` roughly halves the win.**
  `acquire_import_materials_as_references` (`gltf.cpp:816`) → `acquire` →
  `get_or_load_container` (`asset_manager.cpp:508,1119`) parses the same file *again* into
  `record->gltf_data` and drains its textures to the GPU (`:1162,1165`). Dropping the
  operation payload does not touch that record. Measure both flag values separately.
- **`Prefab_library` templates are never released** (`prefab_library.hpp:41-42,128`;
  clones share the template's primitives, `prefab_library.cpp:684`), so an import that
  pulled in prefabs keeps those templates regardless.
- **`take_adopted_parse` is not a redo hazard here**: it fires only when the target *is*
  the scene record for that path (`asset_manager.cpp:1474-1478`) — the Scene_open flow —
  and it is destructive (`:1489-1498`), so a redo finds no parse and falls to the disk
  parse. Naming and flags are identical on both branches (`gltf.cpp:752-753` vs `772-775`).

## Failure semantics for a rebuild

`Operation_stack::redo()` pushes onto `m_executed` unconditionally and never consults
`has_error()` (`operation_stack.cpp:255-260`); `Compound_operation::execute` ignores child
failures (`compound_operation.cpp:30-39`). So `set_error()` alone is not enough. Rule: if
the rebuild fails (file gone, target scene expired, parse error), `execute()` leaves
`m_compound` null and records the error; `undo()` on a null compound is then a no-op, so
the entry is inert but harmless. Note the existing operation-error surface is MCP-only
(`mcp_server_scene_query.cpp:1517-1518`); the Operation Stack window prints only
`describe()` (`operation_stack.cpp:302`), so a failed rebuild needs a log line and ideally
a description suffix to be visible in the UI. Test the "file deleted between undo and redo" case explicitly.

Related pre-existing quirk, which does interact with the new hook: `undo()` does not check
`!m_grouping`, and `queue()` inside a group neither clears nor consults `m_undone`
(`operation_stack.cpp:94-100`). So an MCP `batch` of `[X, undo]` can reach
`m_undone.size() == 1` and drop the payload of an entry that the already-executed sibling
`X` was recorded after. There is no redo hazard — `end_group` discards the entry entirely
(`operation_stack.cpp:155,164`) — but "the stack drives the hook only for the top-level
entry it popped" is not the whole story, and the implementation should assert or document
it. Undoing `X` afterwards still faces content the undone import detached; `X` holds its
own `shared_ptr`s, so that hazard is pre-existing and unchanged by the payload drop — this
note is not an all-clear for the grouping quirk itself.

## Verification

### Measurement first — prerequisite, nothing below is assertable without it

The editor has no memory accounting; the only byte figure in the UI is the DDGI window's
probe textures (`ddgi_window.cpp:66`). Add:

- `Buffer_pool` statistics. `Free_list_allocator` already has the accessors
  (`erhe_buffer/free_list_allocator.hpp:33-36`), but `Buffer_pool` exposes no statistics
  API and `m_blocks` is private (`buffer_pool.hpp:121-186`), as are
  `Mesh_memory::m_vertex_pools` / `m_index_pools` (`mesh_memory.hpp:244-245`) — so this is
  two new accessor layers, not an aggregation of existing ones.
- A process-wide texture byte counter in the graphics `Texture` implementation.
- MCP `get_memory_usage`: per-pool mesh bytes (used / capacity / blocks), texture bytes and
  count, `Device::get_memory_budget()` (`device.hpp:564`; Vulkan only — GL/Metal/null
  return `{}`), container records and which retain a parse, undo/redo entry counts, BLAS
  cache entry count.

Reclaims are **frame-deferred** (mesh ranges double-gated on frame completion and the
loader watermark; texture destroy on frame completion), so every measurement must advance
several frames before sampling.

### Tests

- **Unit** (`editor_asset_tests`): the recipe/payload state machine, and the
  `free_undone_loads` index rule — "release the highest-index entry with a payload, discard
  everything before it" — against a stub stack. Include the nesting case: an import inside
  a compound must **not** receive `on_lossless_undo`.
- **Smoke** (new script, sibling of `undo_reference_clearing_smoke_test.py`):
  1. import → baseline → undo → mesh pool used bytes, texture bytes and CPU counts all
     drop; `get_editor_references` clear; draw-list object count returns to baseline.
  2. redo → content back (same names, *different* uids), usable; a second cycle is stable.
  3. import → move a node → undo the move → undo the import → payload **kept** (memory
     unchanged), both redos still work — the lossless gate.
  4. MCP `batch` containing an import plus another operation → undo → payload **kept** —
     the nesting case that a self-query gate would have got wrong.
  5. after (3), `free_undone_loads` → payload dropped, later redo entry discarded, memory
     drops; redoing the import re-loads it.
  6. `unload_asset` after an undo: must still **refuse** while the payload is kept, and
     succeed once it is dropped.
  7. ray-query build, with ray tracing **on** throughout: sample `get_memory_usage` before
     the import, after it, and after undo + N frames, and assert the post-undo figure
     returns to the pre-import baseline. (Comparing RT on against RT off would only show
     that ray tracing costs memory — true whether or not eviction works, since with RT off
     the BLAS cache is never populated — so it could not fail.) Repeat with a lightmap
     bake in between, for `Lightmap_baker::m_blas_cache`. The bake variant asserts on
     **mesh-pool used bytes and BLAS cache entry counts only**: `release_working_set()`
     deliberately keeps the display atlas resident (`lightmap_baker.cpp:4586-4590,4619`),
     so process-wide texture bytes cannot return to the pre-import baseline after a bake.
  8. `materials_as_references` true vs false, measured separately.
  9. file deleted between undo and redo → redo reports an error and leaves the editor
     consistent.
- **Cost**: report the redo re-parse duration, and confirm the BVH disk cache is hit by the
  absence of `log_geometry`'s `"BVH build ... in ... ms"` line.

### Builds

`build_vs2026_vulkan` (ray query on) and `build_vs2026_opengl` Debug, plus
`ctest --test-dir build_tests_asan -C Debug`.

---

# Implementation record

Implemented and verified for the `Import glTF` scope. `Scene_open` remains phase 6.

## Measured result

The premise, measured with the new `get_memory_usage` MCP tool on
`RiggedFigure.glb` (mesh pool bytes, sampled after advancing frames):

| | before this work | after |
|---|---|---|
| import | +296,480 | +296,480 |
| **undo** | **0 released** | **293,888 released** |
| clear history | 293,888 released | 0 (already freed) |

Redo re-reads the file in 18.5 ms, then 7.7 ms on the next cycle, with **zero**
`BVH build` log lines — the content-hashed disk cache is hit, as predicted.

## What was built

- **Measurement first**, since none of the above was observable before:
  `Buffer_pool::get_statistics()` + `Mesh_memory::get_pool_statistics()` (two new
  accessor layers, as the plan anticipated), a process-wide texture byte counter in
  `erhe::graphics::Texture` (estimated from create info, excluding views and wrapped
  images), `Scene_tlas::get_blas_count()`, and the `get_memory_usage` MCP tool.
- **`Operation::on_lossless_undo()`**, driven by `Operation_stack::undo()` for the
  top-level entry only, guarded by `m_undone.size() == 1`. `Compound_operation` does not
  forward it.
- **`Import_gltf_operation`** holding a `Gltf_import_recipe` and a droppable compound.
  `make_import_gltf_operation` gained an in/out `Gltf_import_recipe*` that records the
  default-camera / default-light decisions on the first build and replays them on a
  rebuild.
- **`free_undone_loads`**: an editor command (`Edit.Free undone loads`) and an MCP tool
  reporting `released_count` / `discarded_count`. The index rule lives in its own
  dependency-free `operation_stack_selection.{hpp,cpp}` so it is unit-testable.
- **BLAS eviction** in both `Scene_tlas` and `Lightmap_baker`, on
  `render_shape.use_count() == 1`, plus the missing `release_working_set()` call site
  (scene close).

## Departures from the plan

- **The `Content_library_attach_operation` usership needed no change**, as the plan
  predicted: releasing the payload destroys the compound and `~Asset_reference`
  unregisters. Confirmed by the smoke test — `unload_asset` after an undo now succeeds
  with `undeclared_survivors == 0`, without `clear_undo_history`.
- **The batch-nesting hazard is unreachable with async loading on.** `import_gltf` queues
  its operation from a completion callback frames after `end_group` has closed, so a
  batched import lands as its own top-level entry and is gated normally. The nesting case
  only exists on the synchronous path (`load.async_gltf_load: false`), where `end_group`
  really does wrap the import in a `Compound_operation`. Verified by hand on that path:
  the batched import kept its payload (0 bytes freed) and the compound entry was not
  marked unloaded — the case a self-query gate would have got wrong. The smoke test
  asserts both outcomes and branches on which path ran, so it says something real either
  way.
- **Prefab reference entries were left alone**, per the plan's own rejection.

## Coverage

- Unit (`editor_asset_tests`): 7 cases over the `free_undone_loads` index rule, including
  that selecting index 0 discards nothing.
- Smoke (`scripts/reloadable_asset_loads_smoke_test.py`): 29 checks — undo frees, redo
  re-reads with fresh ids and a stable second cycle, the lossless gate declining when a
  later entry survives, `free_undone_loads` releasing and discarding, the no-op case,
  batch nesting (both paths), `unload_asset` succeeding after a drop, BLAS release, and a
  missing-file rebuild leaving the editor responsive.
- Full suite: 588/588 `ctest`. Vulkan and OpenGL Debug builds clean.

## Known limits

- **Mesh VRAM returns to the pool free list, not to the driver** — `Buffer_pool` still
  never destroys a block, so the process footprint stays at its high-water mark. What this
  buys is that the next scene reuses the space instead of growing new blocks, which
  matters because exceeding `max_buffers_per_pool` silently skips mesh builds.
- **`materials_as_references = true` still leaves a second container parse** in the asset
  manager, untouched by the drop.
- **`Prefab_library` templates are never released**, so an import that pulled in prefabs
  keeps those regardless.
- **Undoing an import still leaves prefab reference entries in the content library** —
  pre-existing, out of scope, and documented above.
- The BLAS eviction does **not** fix the recycled-range staleness bug described in
  section 4; that needs a generation counter on the cache key.
