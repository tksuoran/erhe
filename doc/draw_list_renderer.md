# Draw_list renderer plan (replacement for Forward_renderer)

## Status: first draft -- iterate before implementing.

## Context

`erhe::scene_renderer::Forward_renderer` (src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.{hpp,cpp})
walks scene meshes every frame:

- Caller (mainly `editor::Composition_pass::render`, src/editor/renderers/composition_pass.cpp:152-218)
  builds a `vector<span<shared_ptr<Mesh>>>` from `Mesh_layer::meshes` and hands it in as
  `mesh_spans`.
- For every span, every frame, the renderer peeks at each `Mesh -> Mesh_primitive ->
  Primitive -> Buffer_mesh` to discover the GPU vertex/index buffer set
  (`peek_mesh_buffers`, forward_renderer.cpp:114), buckets meshes by buffer set
  (`bucket_meshes_by_buffers`, forward_renderer.cpp:159), then for each bucket calls
  `Primitive_buffer::update` (primitive_buffer.cpp:86) which re-reads
  `world_from_node`, recomputes the normal transform, looks up the material index
  again, and writes a Primitive UBO/SSBO entry for every primitive that passes
  `Item_filter`.
- `Draw_indirect_buffer::update` does an analogous walk to emit MDI commands.
- `Item_filter` is used to pick subsets at draw time (translucent/opaque,
  selected/hovered, etc. -- see app_rendering.cpp:88-135).

This is correct but pays a per-frame O(meshes * primitives) "discover everything"
cost regardless of whether any of it changed. There is no place to attach
"this material assignment is fixed forever" or "this transform never moves" so we
can hoist work out of the hot path. Filters are encoded after the fact rather
than as how the data was bucketed up-front.

## Goal

Add a parallel renderer that maintains *its own* GPU-friendly draw representation,
fed by scene change notifications, and renders by walking that representation
instead of the scene. Keep `Forward_renderer` in place and route to either path
behind a config flag so the two outputs can be diffed visually.

Non-goals for the first cut:

- No frustum culling at any level. v1 selects lists by flags only; every
  entry of every selected list is uploaded and drawn. See "Deferred
  optimizations".
- No persistent (multi-frame) buffer allocator. Static-mode bits are
  partition keys only; v1 rewrites all selected lists every frame.
- No new shader variants.
- No migration of id_renderer / shadow_renderer / BRDF slice / depth viz off
  Forward_renderer.
- No fullscreen-pass migration. `Forward_renderer::draw_primitives` (sky /
  grid / post) keeps working unchanged.

## Vocabulary

```
class Draw_entry {                       // one Mesh_primitive worth of draw state
    // Source. Raw pointer; valid while the entry exists, because removal
    // from the scene -> register_mesh/unregister_mesh -> remove from lists.
    erhe::scene::Mesh*                source_mesh;
    std::size_t                       source_primitive_index;

    // GPU geometry. Buffer-set is (index_buffer, vertex_buffers...); see
    // "Buffer-set sorting" below. Pointers are shared with the source
    // Buffer_mesh and remain valid for the entry's lifetime.
    erhe::primitive::Buffer_range     index_range;
    std::vector<erhe::primitive::Buffer_range> vertex_ranges; // one per stream
    erhe::primitive::Index_range      index_sub_range;      // tri-fill / edge / etc.

    // World-space placement (world AABB + TRS) is read on demand from
    // source_mesh->get_node() during the per-frame upload walk; nothing is
    // cached between frames in v1. AABB caching + frustum cull are deferred
    // (see "Deferred optimizations" below).

    // Shading
    std::shared_ptr<erhe::primitive::Material> material;
};

// Bucketing axes are encoded as a single uint64_t flag mask per Draw_list.
// Each list contains entries whose computed flags equal the list's flag mask
// exactly -- i.e. the flag mask is a *partition key*, not a "any of these".
class Draw_list_flags {
public:
    // Opacity (exactly one bit set)
    static constexpr uint64_t opaque                       = (1ull <<  0);
    static constexpr uint64_t translucent                  = (1ull <<  1);

    // Material assignment lifetime (exactly one bit set)
    static constexpr uint64_t static_material_assignment   = (1ull <<  2);
    static constexpr uint64_t dynamic_material_assignment  = (1ull <<  3);

    // Transform lifetime (exactly one bit set)
    static constexpr uint64_t static_transform             = (1ull <<  4);
    static constexpr uint64_t dynamic_sleeping_transform   = (1ull <<  5);
    static constexpr uint64_t dynamic_alive_transform      = (1ull <<  6);

    // Geometry-orientation flag (mirrors Item_flags::negative_determinant)
    static constexpr uint64_t negative_determinant         = (1ull <<  7);

    // Category flags (mirror the Item_flags categories used as filter axes by
    // app_rendering.cpp:88-138). Each entry sets at most one of these.
    static constexpr uint64_t content                      = (1ull <<  8);
    static constexpr uint64_t tool                         = (1ull <<  9);
    static constexpr uint64_t brush                        = (1ull << 10);
    static constexpr uint64_t controller                   = (1ull << 11);
    static constexpr uint64_t rendertarget                 = (1ull << 12);
    static constexpr uint64_t id                           = (1ull << 13);

    // Auto-classified from skin != nullptr at registration time. Always
    // co-occurs with dynamic_alive_transform, never with the static variants.
    static constexpr uint64_t skinned                      = (1ull << 14);

    // Bits 15..63 reserved for future axes.
};

class Draw_list {
    uint64_t                               flags;            // partition key

    // Entries kept sorted by buffer-set: lexicographic order over
    // (index_buffer*, vertex_ranges[0].buffer*, vertex_ranges[1].buffer*, ...).
    // A contiguous run sharing the same buffer-set is a single MDI span,
    // detected inline during the per-frame upload walk. See "Buffer-set
    // sorting".
    std::vector<Draw_entry>                entries;
};

class Draw_list_set {
    // Flat vector. Lists are sparse (only flag combinations with at least
    // one entry exist), and the count is small in practice (~10-20). Lookup
    // by flags is a linear scan, which is faster than a hash for this size.
    std::vector<Draw_list> lists;
};

class Draw_list_filter {
    uint64_t            require_all_set    {0}; // list passes if all bits set
    uint64_t            require_all_clear  {0}; // list passes if none of these set
};
```

A `Draw_list` is selected at draw time iff:
1. `(list.flags & filter.require_all_set) == filter.require_all_set`, AND
2. `(list.flags & filter.require_all_clear) == 0`.

Within a selected list, the renderer walks `entries` in sorted (buffer-set)
order, detects MDI-span boundaries inline, and emits one MDI per span.
During that walk it reads each entry's
`source_mesh->get_node()->world_from_node()` to write the GPU Primitive
entry. Frustum culling (per-list or per-entry) is deferred -- see
"Deferred optimizations".

### Static-mode bits in v1

The `Ring_buffer_range`s the existing `Primitive_buffer` and `Draw_indirect_buffer`
hand out are single-frame: they are released back to the ring at the end of the
frame and the next frame's draws need a fresh range. So in v1, every selected
list rewrites its Primitive and Draw-indirect ranges every frame regardless of
the `static_*_transform` and `static_material_assignment` bits. The savings the
static modes promise (upload-once, keep resident) require a new persistent /
multi-frame dynamic buffer allocator that does *not* exist today; that is a
separate piece of work.

What the static bits still buy us in v1:

- A stable partition key. When we add the persistent buffer later, the lists
  are already segregated by lifetime; the renderer's outer loop already knows
  which lists *would* skip the rewrite.
- Semantic intent. A node author can declare "static transform" before the
  renderer cares, so we never have to migrate the data.

Lists are independent: the same node can have one mesh primitive contributing
to `opaque | static_material_assignment | dynamic_alive_transform | content`
and another (rare) translucent primitive contributing to a different list.

## Buffer-set sorting (intra-list MDI coalescing)

`Forward_renderer::bucket_meshes_by_buffers` (forward_renderer.cpp:159) exists
because `multi_draw_indexed_primitives_indirect` requires identical vertex- and
index-buffer bindings for every draw it covers. Today that bucketing happens
fresh per frame on the caller-supplied mesh span.

The new renderer does this once-per-edit instead. Each `Draw_list` keeps its
`entries` sorted by buffer-set: lexicographic order over
`(index_buffer*, vertex_ranges[0].buffer*, vertex_ranges[1].buffer*, ...)`.
At draw time the renderer walks `entries` once; whenever the buffer-set key
of `entries[i]` differs from `entries[i-1]`, that's a span boundary:

```
for each selected Draw_list L:
    span_begin = 0
    for i in 1..L.entries.size():
        if buffer_set_key(L.entries[i]) != buffer_set_key(L.entries[span_begin]):
            emit_mdi_for_span(L, span_begin, i);
            span_begin = i
    emit_mdi_for_span(L, span_begin, L.entries.size())

emit_mdi_for_span(L, lo, hi):
    bind index_buffer + vertex_buffers from L.entries[lo];
    write Primitive entries for [lo, hi);
    write Draw_indirect commands for [lo, hi);
    emit one multi_draw_indexed_primitives_indirect.
```

Insertions are placed in sorted position (binary search on key). Removals
shift in-place. No span-boundary cache to maintain.

Two consequences worth noting:

- The single-buffer-set common case collapses to one MDI per list, which is
  what `Forward_renderer` does today. We are not regressing the scalar case.
- A list that mixes buffer-sets (e.g. content with both glTF-imported and
  procedurally-generated meshes) now emits N MDIs instead of fragmenting at
  scene-load time. Today that scenario is bucketed every frame; we move the
  cost to scene-mutation time.

## Deferred optimizations

These are intentionally out of scope for v1, listed here so it is clear what
the v1 design is *not* trying to do. Each of these has a natural shape but
adding them now would obscure the first delta against `Forward_renderer`.

**Per-entry / per-list AABB + frustum culling.** Add `aabb_local` (copy of
`Buffer_mesh::bounding_box`) to `Draw_entry`, recompute world AABB during
the per-frame upload walk, accumulate a per-list `union_aabb_world` for the
next frame's cull, and extend `Draw_list_filter` with an optional frustum.
Lists whose union AABB does not intersect the frustum skip their upload
walk entirely. v1 uploads everything in every selected list.

**MDI-span boundary cache.** Add `vector<size_t> span_starts` to `Draw_list`,
rebuilt on insert/remove, so the per-frame walk does not have to compare
buffer-set keys. Worthwhile only if profiling shows the inline comparisons
dominate.

**Persistent GPU-side caching for static lists.** A
`Persistent_primitive_buffer` / `Persistent_draw_indirect_buffer` whose
allocations survive across frames so lists with `static_transform |
static_material_assignment` genuinely upload once. Requires:

- A buffer allocator that hands out lifetime-typed ranges (per-frame vs.
  persistent vs. multi-frame-pooled), distinct from today's `Ring_buffer`.
- A descriptor binding strategy so a draw can pull from a persistent SSBO
  range for static state and a per-frame range for the dynamic delta.
- An invalidation protocol so an edit to a "static" entry rewrites just that
  entry, not the whole list. Most likely a `world_from_node_serial`-based
  comparison done at draw time -- still no callback, still no map.

v1 is "scene-mirror with per-frame uploads, no culling". Phase 3 is the
landing point for the persistent-buffer work; the AABB / frustum and
span_starts optimizations are independent and can land before, after, or
alongside.

## Where this plugs in

Two integration points:

1. **Construction-time** (per `Scene_root`): the `Draw_list_set` is updated
   from scene mutation hooks:
   - `Scene_root::register_mesh` / `unregister_mesh` (already overrides
     `Scene_host`, src/editor/scene/scene_root.hpp:134-135).
   - `Mesh::add_primitive` / `set_primitives` / `clear_primitives` -- mesh-level
     primitive set changes invalidate the entries sourced from that mesh.
   - `Item_flags` changes on a mesh / node (translucent, negative_determinant,
     category bits, static_transform / static_material_assignment) -- move
     the affected entries between lists.
   - Material edits: `erhe::primitive::Material` is a shared_ptr referenced by
     `Mesh_primitive::material`. List bucketing only depends on which
     material is *assigned* and on `Item_flags::translucent` (see Risks 1).
     Material *data* edits don't invalidate list membership; the existing
     `Material_buffer` already handles per-frame material UBO updates.

   *No transform-update callback.* Transforms are read on demand from
   `entry.source_mesh->get_node()` during the per-frame upload walk. v1
   does not cache anything transform-related between frames, so there is
   nothing to invalidate. Phase 3's persistent fast-path will introduce a
   `world_from_node_serial`-based comparison at draw time -- still no
   callback, no map.

   On every mutation the implementation does a linear scan over
   `Draw_list_set::lists` to find affected entries (by `source_mesh`
   pointer comparison). Mutations are rare (driven by user actions, not
   per-frame), and the total entry count is small enough that a scan is
   cheaper than maintaining a side-index.

2. **Draw-time**: a new equivalent of `Forward_renderer::render` that takes:
   - the existing camera / lights / skins / materials / multiview state,
   - a `Draw_list_filter` (frustum + flag require-set / require-clear),
   - the rest unchanged (pipeline states, debug labels, etc.).

   `Composition_pass` is where `Item_filter` is set up today. Each
   Composition_pass becomes a `Draw_list_filter`:
   - `require_all_bits_clear = Item_flags::translucent` ->
     `filter.require_all_set = Draw_list_flags::opaque`.
   - `require_all_bits_set   = Item_flags::translucent` ->
     `filter.require_all_set = Draw_list_flags::translucent`.
   - `require_all_bits_clear = Item_flags::negative_determinant` ->
     `filter.require_all_clear = Draw_list_flags::negative_determinant`.
   - Per-category passes (controller, brush, rendertarget, tool, content)
     each set the matching `Draw_list_flags::*` bit in `require_all_set`.

   Selected / hovered remain as a *separate, post-list per-entry filter* in
   v1. Today's selection-outline / hover-outline passes typically operate on
   a tiny subset, so a per-entry walk over already-frustum-and-flag-filtered
   lists is acceptable. Promoting selection/hover to bucket axes would cause
   entries to bounce between lists on every selection click; we are not doing
   that.

## Name

`erhe::scene_renderer::Draw_list_renderer`. New files:
`src/erhe/scene_renderer/erhe_scene_renderer/draw_list_renderer.{hpp,cpp}`.

## Where the parallel state lives

`Scene_root` owns a `Draw_list_set`. Lists live with the data they mirror;
subscription wiring is local to `Scene_root::register_mesh` /
`unregister_mesh` and the new `Mesh` change hooks. The renderer queries
`scene_root->get_draw_list_set()` at draw time. This pulls a
`scene_renderer`-level header into `src/editor/scene/scene_root.{hpp,cpp}`,
which is acceptable -- `Scene_root` already includes scene-renderer-adjacent
types via its rendertarget-mesh tracking.

## Phased implementation

### Phase 0 -- decisions to lock in (no code)

Locked in:

- Name: `Draw_list_renderer`.
- Ownership: `Scene_root` owns the `Draw_list_set`.
- Bucketing model: single `uint64_t` flag mask per `Draw_list`, see
  `Draw_list_flags` above. Lists are sparse and lazily created.
- Filter model: `Draw_list_filter` with frustum + flag require-set /
  require-clear, passed into `Draw_list_renderer::render` per call.
- Buffer-set sorting: entries within each list are kept sorted by
  `(index_buffer*, vertex_buffers...)` so MDI runs over contiguous spans.
- Selected / hovered are NOT bucket axes. They remain as a per-entry filter
  applied at draw time within already-selected lists.
- v1 does not persist GPU buffer ranges across frames. Static-mode bits are
  partition keys only.
- Static-transform / static-material flags: introduce two new `Item_flags`
  bits, `Item_flags::static_transform` and
  `Item_flags::static_material_assignment`. Tagged on the `Mesh` (for material
  assignment) and on the `Node` (for transform). Without the flag, the entry
  defaults to the dynamic bucket.
- Config flag: `erhe_graphics.json -> "use_draw_list_renderer"` (Bool,
  default false). Defined as a v4 codegen field on `Graphics_config` (see
  `src/erhe/graphics/definitions/graphics_config.py`). Sets the *initial*
  `Renderer_choice` per new viewport; per-viewport runtime override is the
  combo in Scene View Config (already wired up at
  `src/editor/windows/scene_view_config_window.cpp:107`,
  `Viewport_scene_view::m_renderer_choice`).
- Skinned meshes (`mesh->skin != nullptr`) are force-classified at registration
  time as `dynamic_alive_transform | skinned`, regardless of any
  `static_transform` flag on the node. Joint moves do not propagate through
  `Mesh::handle_node_transform_update`, so static-classification a skinned
  mesh would silently freeze it. Treat it as a hard invariant.

### Phase 0.5 -- Item_flags additions (separate, small commit)

Files: `src/erhe/item/erhe_item/item.hpp`.

- Add `static constexpr uint64_t static_transform            = (1u << 27);`.
- Add `static constexpr uint64_t static_material_assignment  = (1u << 28);`.
- Add corresponding strings to `c_bit_labels[]`: "Static Transform",
  "Static Material Assignment".
- Bump `count` from 27 to 29.

These flags are inert until Phase 2 reads them for list classification. Adding
them up front keeps the plan-driven scene authoring (e.g. brushes, glTF
import) able to declare intent before the renderer cares.

### Phase 1 -- types and ownership, no rendering yet

- New file: `src/erhe/scene_renderer/erhe_scene_renderer/draw_list.{hpp,cpp}`.
  Defines `Draw_entry`, `Draw_list_flags`, `Draw_list`, `Draw_list_set`,
  `Draw_list_filter`.
- `Draw_list_set` exposes:
  - `add_mesh(Mesh*)` -- computes the flag mask per primitive, linear-scans
    `lists` for a matching `flags`, creates a new `Draw_list` if none, and
    inserts each new `Draw_entry` in sorted position by buffer-set.
  - `remove_mesh(Mesh*)` -- linear-scans every list, erases entries whose
    `source_mesh == mesh`. Drops empty lists.
  - `on_mesh_primitives_changed(Mesh*)` -- treat as remove_mesh + add_mesh.
  - `on_item_flags_changed(Mesh*)` -- treat as remove_mesh + add_mesh; the
    flags-to-bucket map is recomputed on insertion.
  - `on_material_assignment_changed(Mesh*, primitive_index)` -- linear-scan
    to update the entry's cached `material` shared_ptr in place. Does not
    move the entry; opacity bucketing keys off `Item_flags::translucent`
    (see Risks 1).
  - `for_each_list(filter, callback)` -- iterates `lists` in insertion
    order, applying frustum + flag tests.

- Compute flag mask per primitive at insertion:
  ```
  uint64_t flags = 0;
  flags |= (mesh.has_flag(Item_flags::translucent)) ? translucent : opaque;
  flags |= (mesh.has_flag(Item_flags::static_material_assignment))
            ? static_material_assignment : dynamic_material_assignment;
  flags |= (node.has_flag(Item_flags::static_transform) && (mesh.skin == nullptr))
            ? static_transform : dynamic_alive_transform;
  if (node.has_flag(Item_flags::negative_determinant)) flags |= negative_determinant;
  // category bits: pick at most one from {content, tool, brush, controller,
  // rendertarget, id} based on the corresponding Item_flags bit.
  if (mesh.skin != nullptr) flags |= skinned;
  ```

- Wire into `Scene_root::register_mesh` / `unregister_mesh` and the
  primitive-set / flag-change paths on `Mesh`. Build it; nothing renders
  differently because the new code isn't on the draw path yet.

### Phase 2 -- standalone draw path, no integration

- New file: `draw_list_renderer.{hpp,cpp}`.
- `Draw_list_renderer::render(const Render_parameters&, const Draw_list_filter&)`
  mirrors `Forward_renderer::render` but consumes a filter + the scene's
  `Draw_list_set` (reached via the source `Scene_root`). Reuses the existing
  `Camera_buffer`, `Light_buffer`, `Material_buffer`, `Joint_buffer`,
  `Primitive_buffer`, `Draw_indirect_buffer`, `Texture_heap`,
  `Program_interface` -- *no* duplication of the GPU buffer pipeline. Only the
  *source* of the per-primitive data changes.
- Render loop:
  1. For each list passing the filter (frustum + flag), in deterministic
     order:
     a. For each MDI-span (contiguous run of identical buffer-set):
        bind the buffer-set, write Primitive entries for the span, write
        Draw_indirect commands, emit one MDI.
- For Phase 2, every selected list rewrites Primitive + Draw_indirect every
  frame regardless of static-mode bits. This isolates correctness from the
  fast-path optimisation. One ring-buffer acquire per MDI-span, matching
  `Forward_renderer`'s current allocation pattern when the scene has one
  buffer-set per layer (the common case).
- A developer toggle in one Composition_pass that swaps in
  `Draw_list_renderer` based on `Viewport_scene_view::get_renderer_choice()`.
  Render side-by-side visually with the original.

### Phase 3 -- DEFERRED: persistent GPU-side caching

This phase is out of scope for the first cut. It requires a new persistent /
multi-frame buffer allocator (see "Deferred optimizations" above). When that
infrastructure lands, this is what plugs into it:

- For lists with `transforms == static_` and `materials == static_assignment`:
  populate the persistent Primitive buffer range once, keep it resident across
  frames, skip the rewrite. Track a per-list `dirty` bit and per-entry edits.
- For `dynamic_sleeping`: same as static, but flip back to "rewrite every frame"
  on wake. Wake/sleep notifications come from the physics layer
  (`Node_physics`, src/editor/scene/node_physics.{hpp,cpp}) -- needs an
  observer hook or a per-frame poll.
- Validate: the visual diff against `Forward_renderer` must remain a no-op.

Until then v1 rewrites every list every frame, and the static-mode tags ride
along as data-model intent only.

### Phase 4 -- migrate Composition_pass call sites

- `Composition_pass` builds a `Draw_list_filter` instead of `mesh_layers +
  Item_filter`. Translation rules listed under "Where this plugs in" above.
- Selection / hover filters remain as Item_filter applied *within* the lists
  the filter has selected. The filter walk over a frustum-and-flag-filtered
  list is bounded.
- Fullscreen passes (sky / grid / post) use `non_mesh_vertex_count` and don't
  consume any list. They keep calling `Forward_renderer::draw_primitives`
  unchanged in v1; that path does not depend on `mesh_spans`. We will return
  to merging it later (probably as a "list-less" mode on
  `Draw_list_renderer`).
- `Forward_renderer` stays compiled and reachable behind the config flag for
  as long as the comparison is useful. Plan to remove it only after Phase 4
  has soaked and the deferred renderers (id, shadow, depth-viz, BRDF) have
  been migrated.

## Risks identified by review (still live)

These were flagged by an independent review pass and are not all closed by
the bucketing/filtering decisions above. Listed in priority order.

1. **Material opacity vs. `Item_flags::translucent` mismatch.** v1 keys the
   `opaque` / `translucent` bucket bits off `Item_flags::translucent` on the
   *mesh*, not `Material::data.opacity`. This matches today's `Forward_renderer`
   filter behavior, but it means runtime drops of `material->data.opacity`
   below 1.0 do not move the entry to a translucent list. If we ever want
   material-opacity-driven bucketing, the material owner has to mirror
   opacity to the mesh's `Item_flags::translucent` bit explicitly. Stating
   this here so we don't accidentally take a dependency the other way.

2. **id_renderer compatibility.** `Primitive_buffer::update`'s id-range path
   (primitive_buffer.cpp:193-199) advances `m_id_offset` per primitive in
   iteration order, rounding up to the next power of two of `index_count`.
   Multiple `Primitive_buffer::update` calls per frame (one per MDI-span) will
   scramble id offsets across the frame. The id renderer either keeps using
   `Forward_renderer` (which is the v1 plan), or id-range allocation must
   move out of `Primitive_buffer` into a separate per-frame allocator that
   is fed deterministically. Out of scope for v1 but flagged as the blocker
   for migrating `id_renderer`.

3. **Visual-diff determinism.** "Looks the same" is the success criterion.
   Three ways it can silently drift:
   - List iteration order. `Draw_list_set::for_each_list(filter, ...)` must
     visit lists in a fixed order matching `Forward_renderer`'s mesh-layer
     iteration. Use mesh-layer-id as a primary key in the iteration order.
   - Within a list, entries are sorted by buffer-set, not by mesh-layer
     insertion order. For overlapping geometry with z-equal writes (e.g.
     selection-outline z-fighting), this changes outcome. Mitigation:
     secondary sort key = source mesh insertion order.
   - `material_buffer_index` is assigned by `Material_buffer::update` based
     on materials-span order. The materials span is built by
     `composition_pass.cpp:97`; it does not change between the two
     renderers, so this should be stable. Verify in Phase 2.

4. **Per-frame ring-buffer pressure.** Each MDI-span acquires its own
   `Primitive_buffer` and `Draw_indirect_buffer` ring-buffer range. A scene
   with one buffer-set across all categories produces one MDI per matching
   list, not one per layer; in the worst case (mixed buffer-sets, many
   categories alive) this is more than today. Worth measuring in Phase 2;
   if it bites, the fix is to widen each ring buffer's per-frame budget,
   not to redesign the renderer.

5. **Lifetime / unsubscribe.** `Mesh::handle_item_host_update` already routes
   through `register_mesh` / `unregister_mesh`, so meshes attached/detached
   from a scene before Scene_root death are fine. The pathological case
   (Mesh held externally past Scene_root death) is handled by the
   `Scene_host*` member in Mesh going stale -- the dispatch is null-checked
   in Mesh; no callback fires. State this in code but don't add an RAII
   handle layer.

6. **`dynamic_sleeping` bucket stays empty in v1.** No Mesh / Node code path
   classifies into it. The bit exists; population waits for Phase 3 (when
   the persistent buffer makes the distinction matter) and the physics-side
   wake/sleep observer hook. Keeping the bit in the layout costs nothing.

## Resolved (no longer open)

- *Bucketing-vs-filtering boundary*: list flag mask + draw-time filter.
- *Buffer-set bucketing*: intra-list sort + span coalescing.
- *Transform-update hook mechanism*: not needed in v1. Per-frame upload
  walk reads transforms on demand; nothing is cached between frames. Phase
  3 will use a `world_from_node_serial` comparison at draw time, still
  without a callback.
- *AABB / frustum cull*: deferred entirely. v1 has neither per-entry nor
  per-list AABB. See "Deferred optimizations".
- *Mesh -> entries lookup*: linear scan over `Draw_list_set::lists` on
  mutation. No side-index, no `unordered_map`. Mutations are user-driven
  and rare; total entry count is small.
- *Skin / dynamic_alive invariant*: hard-classified at registration.
- *Selection / hovered handling*: per-entry Item_filter at draw time, not
  bucket axes.
