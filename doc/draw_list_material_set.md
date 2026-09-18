# Draw list material set (material buffer + texture heap owned by Material_set)

Stability: stable

Planning context - the reported bug and its root cause, the second motivation,
and the review and citation state - lives in
`doc/draw_list_material_set_context.md`, **which also owns the status of this
work**: what has landed, what has been verified and what has not. This document
states the requirements, the design and the phase steps, and keeps them in the
imperative even where they are done; it is not the place to look for progress.

**All of it is implemented.** Section 1 here states the requirements and is the
entry point; section 2 designs against them, and every later section refers back
by label rather than restating. References to "section 1" that name the context
doc mean that document's motivation section.

## 1. Requirements

These say what must be true of the editor, not how. The design in section 2
introduces `Material_set` and everything else that follows from them.

### Correctness

**R1 - A primitive renders with the material assigned to it.** In every pass,
whatever else rendered in the same frame, and whatever order the assignments
happened in. This is the reported defect: today a material dragged onto a second
mesh leaves that mesh nearly unchanged, and reversing the order moves the
failure to the other mesh (context doc, section 1).

**R2 - A cached material reference stays valid while its record can be drawn.**
*Draw-list renderer only.* Its primitive records are written once and consumed
on later frames, so whatever they name has to keep meaning the same material
until the record is rewritten or dropped. The other paths write their records
during the pass that consumes them, so nothing of theirs outlives a frame and
this holds trivially.

**R3 - Every material a pass can draw is ready before that pass runs.** The GPU
material state the pass binds covers every material any primitive it draws can
name, including a material assigned to a mesh *after* that mesh was registered,
and including a material no content library contains.

**R4 - Assigning a material is sufficient by itself.**
`Mesh::set_primitive_material()` leaves everything correct with no bookkeeping
at the call site, and no other API can change a primitive's material. All four
assignment gestures - drag-drop, the paint tool, the Properties picker and the
material preview's per-thumbnail assignment - go through it.

**R5 - A material edit becomes visible without the editing code announcing it.**
Writing `material->data.base_color` directly, or re-baking a texture a material
references, changes what is rendered. Code that edits materials knows nothing
about how they reach the GPU: `properties.cpp` writes fields directly while a
colour picker is dragged, and the MCP `edit_material` tool does the same.

**R6 - Scenes and render paths do not share slot numbering.** What one scene or
one path renders is unaffected by what another renders in the same frame. Today
they do share it, which is the mechanism behind R1.

### Compatibility and scope

**R7 - Every existing render path keeps working, and stays independent of the
draw-list renderer.** Forward/bucket, shadow, DDGI, ray trace, ID, material
preview, brush preview, thumbnails, the BRDF slice window and the standalone
example all continue to render, and the bucket path remains a first-class
fallback rather than a deprecated one. A consumer with no scene and no draw list
can render materials.

**R8 - Rendering output is unchanged.** Visually identical on OpenGL (bindless
and sampler-array paths), Vulkan, Quest and null/headless.

**R9 - Per-pass texture-heap bind/unbind is preserved.** `Texture_heap::unbind()`
is load-bearing: on the GL bindless path it makes handles non-resident
(`gl_texture_heap.cpp:246-260`) and `bind()` re-makes them resident
(`gl_texture_heap.cpp:317-328`); on the sampler-array path unbind zero-binds the
heap's texture-unit region (`gl_texture_heap.cpp:262-269`).

### Performance

The three requirements below are what the **draw-list renderer** must meet; the
tag on each says where the obligation is, not where the mechanism stops. D9 gives
both kinds of set the same persistent storage, so the forward set gets the same
treatment without being required to (D0's table, D9), and V5 measures the result
for the frame as a whole.

**R10 - Material GPU state is written only when it has changed.** *Draw-list
renderer only.* A frame in which no material was added, removed or edited
performs no material upload, no texture-heap rebuild and no descriptor-set
allocation.

**R11 - Per-frame material cost scales with materials and with changes, not with
passes.** *Draw-list renderer only.* Today the whole material buffer and texture
heap are rebuilt once per pass (context doc, section 1.2).

**R12 - Material capacity follows scene content.** *Draw-list renderer only.* A
scene renders correctly however many distinct materials it uses, rather than
being bounded by a fixed compile-time count.

### Constraints from the existing system

**R13 - Mesh registration may run on a worker thread.**
`Scene_root::register_mesh` says so (`scene_root.cpp:1219-1221`) and does during
async glTF load, so whatever tracks materials must tolerate being reached from
one, while the effects land on the main thread.

**R14 - Materials and their slots are released when nothing references them.**
Repeated preview and thumbnail rendering, which assigns a different material to
the same mesh several times per frame, accumulates neither slots nor strong
references over a session.

**R15 - A texture handle a record names resolves correctly for as long as that
record can be drawn.** A handle is only meaningful in the heap it was allocated
from, so heap contents and record contents stay in step.

### Verification

**R16 - The membership and slot logic is testable without a window or a real
GPU**, and the reported defect has an automated regression test that inspects
what a cached record resolved to rather than what the material says.

Explicitly out of scope: material *assignment* is still not undoable (dragging a
material onto a mesh pushes no `Operation`, so undo reverts the previous
material-data edit instead). That is a separate defect with a separate fix
(`Mesh_material_assign_operation`).

## 2. Design

### D0 - `Material_set`, and two of them per root

**One object owns the material state.** A `Material_set` holds both halves: the
membership that says which materials are members and at which slots, and the GPU
objects written from it. Every `Material_buffer`, and every `Texture_heap` that
backs the texture references written into one, belongs to exactly one
`Material_set`, and nothing else creates, writes or resets either. This covers
material state only: the heaps owned by `Imgui_renderer`, `Text_renderer`,
`Texel_renderer`, `Post_processing` and hextiles' `Tile_renderer` hold no
material textures, are reached through no material slot, and are untouched by
this plan.

The relation runs the other way for materials: a `Material` may be a member of
any number of sets at once, and its slot, its buffer record and its heap entry
are per-set, not properties of the material. Two sets holding the same
`Material` object are fully independent - neither can observe or disturb the
other's GPU state - which is how R6 is met, and with it R1.

**Its dependencies are the material layer alone.** `material_set.hpp` names
`erhe::primitive::Material`, `erhe_graphics` types and `Material_interface`;
that closed list is the whole of it, and the dependency arrow runs
`Draw_list_scene` -> `Material_set`. This is what lets the `Forward_renderer`
bucket path bind materials without naming a draw-list type (D5), what lets a
consumer with no scene use materials at all (R7), and what makes the material
half testable against a bare device (V2).

**The two render paths get two independent sets.** The editor renders a scene
through two paths, and each gets its own `Material_set`:

| | **Forward set** | **Draw-list set** |
| --- | --- | --- |
| Owned by | `Scene_root` | `Draw_list_scene` |
| Exists for | every scene root | only roots that render through draw lists |
| Feeds | `Forward_renderer::render` / `draw_primitives`, `Shadow_renderer`'s bucket path, and from phase 5 `Ddgi_renderer` / `Ray_trace_renderer` | `Draw_list_renderer::render` and the shadow draw-list path |
| Records naming its slots | written by `Primitive_buffer::update` *during* the pass and consumed by that same pass | cached in `Draw_list::primitive_records`, consumed on later frames |
| Slot stability | holds trivially; records die with the pass | **required** - R2 |
| Membership sources | content library + object references counted by `Scene_root`'s mesh hooks (D1c) | content library + draw-list object references (D1a) |

`Scene_root::get_material_set()` returns the forward set;
`Draw_list_scene::get_material_set()` returns the draw-list set. The same
`Material` normally holds a *different* slot in each, and every code path
resolves a material through the set it binds.

**This separation is safe because each path consumes only its own records.** The
bucket path re-buckets mesh spans and writes fresh primitive records into the
renderer's own `Primitive_buffer` on every pass, reading them back within that
same pass; the draw-list path reads records cached in `Draw_list`. Each path
reads back what it wrote, so a pass has to agree with *itself* and the two slot
spaces stay separate.

**It is preferred over one shared set** because the two have genuinely different
constraints, visible in every row of the table above. Fusing them would impose
the draw-list set's cached-record obligations (R2, and the invalidation rule D10
that follows from persistence) on a path that has no cached records, and would
couple two lifetimes - one owned by an object that may not exist - for no
benefit either path can use.

**A draw-list set exists exactly where draw-list rendering is used.** A
`Draw_list_scene` is created for the roots that render through it, exactly as
today. Owning one *is* the eligibility test: `Composition_pass` routes to the
draw-list path whenever `scene_root->get_draw_list_scene()` is non-null and the
pass is expressible (`composition_pass.cpp:289-308`), so the set of owners is
also the set of roots on that path, and the forward set has to serve roots that
have no draw list at all. In particular the two preview roots
(`scene_preview.cpp:59`) keep exactly their current shape: they render through
the `Forward_renderer` bucket path against their forward set (R7, R8), whose
object references are what make their per-thumbnail material reassignment
correct (D1c).

*Consumers outside any scene* own a `Material_set` directly, of neither kind:
the BRDF slice window (`brdf_slice.cpp`), the standalone example
(`src/example/example.cpp`) and the shared empty set in `App_rendering` (D8).
Each is its own one-off slot space whose membership comes entirely from
`sync_library`, and each is a case R7 requires to keep working.

**The slot leaves the material.** `Material::material_buffer_index` and the
unused `Material::preview_slot` are removed (phase 6, D7); a slot is obtained by
lookup in a set. The shared mutable field is what makes "slot 7" mean different
materials in different passes today.

### D1 - `Material_slot`: refcounted membership

Membership is by reference: a material belongs to a set while anything
references it *in that set* (R3), rather than because a caller passed it in a
list. There are two kinds of owner (D0) - `Scene_root` for the forward set,
`Draw_list_scene` for the draw-list one - and they share this type entirely;
only their reference sources differ. Entries live in a free-listed vector with
the same shape as `Draw_list_object`, declared in `material_set.hpp`, in the
material layer (D0), so a renderer that binds materials includes material
headers alone:

```cpp
// Handle to a registered material; index is stable (free-list) and IS the GPU
// slot, generation detects use after release.
class Material_slot_id
{
public:
    uint32_t index     {invalid_index};
    uint32_t generation{0};

    static constexpr uint32_t invalid_index = 0xffffffffu;

    [[nodiscard]] auto is_valid() const -> bool { return index != invalid_index; }
    [[nodiscard]] auto operator==(const Material_slot_id&) const -> bool = default;
};

// One registered material. Owns the material for as long as it is registered
// (R2).
class Material_slot
{
public:
    std::shared_ptr<erhe::primitive::Material> material{};
    // The two reference sources, kept separate because they reconcile
    // differently:
    bool     in_library {false};  // diff-reconciled, regenerated whole each update
    uint32_t use_count  {0};      // counted, follows registered-object lifetimes
    // Content hash of everything the GPU record is written from (D10). A
    // change dirties the buffer. This is the material layer's own concern;
    // shader-variant identity lives in the draw-list layer - see D7.
    uint64_t content_hash{0};
    uint32_t generation  {0};
    bool     alive       {false};
};
```

and the membership half of `Material_set` itself, in the same header (its GPU
half is D2 - one class, described in two places):

```cpp
class Material_set
{
public:
    // Library membership, once per update. Materials that left the library lose
    // their library reference; their slot survives while object references
    // remain.
    void sync_library(std::span<const std::shared_ptr<erhe::primitive::Material>> materials);

    // Object membership, refcounted. Main thread only (R13).
    auto add_ref(const std::shared_ptr<erhe::primitive::Material>& material) -> Material_slot_id;
    void release(const erhe::primitive::Material* material);

    // Lookup only, const; the non-const mutators above are what assign (R3).
    [[nodiscard]] auto find     (const erhe::primitive::Material* material) const -> Material_slot_id;
    [[nodiscard]] auto get_slot (const erhe::primitive::Material* material) const -> std::optional<uint32_t>;
    [[nodiscard]] auto get_slot_count() const -> std::size_t;         // highest live slot + 1
    [[nodiscard]] auto get_materials () const -> std::span<const Material_slot>; // slot-indexed; `alive` marks live entries

    // R13. Scene_root::register_mesh may run on a worker thread during async
    // load (scene_root.cpp:1219-1221), so an off-thread caller enqueues and
    // flush_pending() applies on the main thread. Draw_list_scene already runs
    // inside its own main-thread flush and calls add_ref / release directly.
    void enqueue_object_materials(uint64_t object_key,
                                  std::span<const std::shared_ptr<erhe::primitive::Material>>);
    void enqueue_release_object  (uint64_t object_key);
    void flush_pending           ();

    // ... GPU half: update / bind / unbind / invalidate (D2)

private:
    std::vector<Material_slot>                                     m_materials;  // holes: alive == false
    std::vector<uint32_t>                                          m_free_slots;
    std::unordered_map<const erhe::primitive::Material*, uint32_t> m_index_by_material;
    bool                                                           m_membership_dirty{true};
    // ... GPU half: buffer, heap, copies (D2)
};
```

**Every mutator above is non-const and every lookup is const**, which is how R4
is enforced at the type level: a record writer is handed a `const Material_set*`
and can call `find` / `get_slot` and nothing else - no assignment, no update, no
bind (D1a, D1c).

A slot is freed only when `in_library` is false and `use_count` is zero. The
set-based source and the counted source are deliberately different mechanisms:
the library is regenerated whole every update, so it reconciles by diff, while
object references arrive and depart with object lifetimes and must be counted.
Using a counted reference for a regenerated source would leak - every material
ever previewed would hold a slot and a strong reference for the session.

Slots are sparse; the GPU write walks slot order and zero-fills holes.
`Material_slot::material` holds a strong reference while a slot is alive.

A `Material_set` is a plain member of its owner (D3), so the slot table above
lives for as long as that owner does. The const half of the interface is what
record writers get (R3), and V1 exercises the slot logic on a null device
(R16).

Freeing a slot pushes it on the free list and bumps the entry's generation, the
same way `Draw_list_scene::release_object` does. Reusing a freed slot for a
different material is the one thing that could make a *stale* cached record
name the wrong material - R2 is what keeps it harmless: a record's material
holds a reference for as long as the record exists, so the slot stays assigned
while the record lives.

### D1a - One refcount site, before any record write

References are taken in exactly one place: the membership hooks, ahead of every
record write (R3). The record writers are the wrong granularity and the wrong
moment for it - `write_slot_fields` runs once per *entry* while release runs
once per *distinct material per object*, and two of its three call paths run
inside the rendergraph, after the frame's buffer was written - so R3 removes the
problem instead of managing it.

`watch_object_materials` / `unwatch_object_materials`
(`draw_list_scene.cpp:719-764`) become `sync_object_materials` /
`release_object_materials`. `sync_object_materials` recomputes the object's
material list from its current primitives and applies the **difference** -
add_ref what is new, release what is gone - so a material shared between the old
and new lists keeps a positive count throughout. `Draw_list_object::materials`
changes from `std::vector<const Material*>` to `std::vector<Material_slot_id>`,
which is what makes `write_slot_fields` a lookup that cannot fail for a
registered object. Call sites:

| Site | Order |
| --- | --- |
| `register_object` (`draw_list_scene.cpp:703-704`) | `sync_object_materials` **before** `add_entries` |
| `rebuild_all` (`draw_list_scene.cpp:915-917`) | `sync_object_materials` **before** `add_entries` |
| `refresh_object_records` (`draw_list_scene.cpp:550-572`) | `sync_object_materials` **after** the lost-node early return at `:559-561` and before the record rewrite - it replays entries against a scene that may have mutated since (`draw_list_scene.cpp:474-477`). Syncing before the early return would release a dropped material while the un-rewritten records still name it. |
| `unregister_object` (`draw_list_scene.cpp:825`) | `release_object_materials` |

The current code has `add_entries` *first* in both `register_object` and
`rebuild_all`, and `rebuild_all` additionally does `unwatch` then `watch` -
which would momentarily drop a reference count to zero and free a slot the
records written one line earlier already name. Both orderings are corrected as
part of phase 4; the fix is the ordering, not an invariant asserted about it.

With membership established first, `write_slot_fields` (`draw_list_scene.cpp:455`,
reached from `write_entry_record` `:512`, `write_object_gpu_slots` `:545` and
`refresh_object_records` `:565`) stops consulting the set at all. It gains a
`const Draw_list_object*` parameter (it is a free function in an anonymous
namespace) and resolves the primitive's material against **the object's own
`materials` list** - the distinct-material list `sync_object_materials` builds
and `release_object_materials` walks (`draw_list_object.hpp:63-67`), now holding
`Material_slot_id`s - writing that id's `index`, which *is* the slot (D1). That
is a short linear scan over one object's materials, not a lookup in the set's
`m_index_by_material`, and it has no fallback value: an `ERHE_VERIFY` fires when
the primitive's material is absent from the list, which means R3 was violated
for a registered object. `Id_renderer` never reaches this writer - it is on the
bucket path, where the miss policy is `Primitive_buffer`'s (D1c, D5).

`ERHE_VERIFY` is unconditional in every configuration (`verify.hpp:28,41,50` -
three platform branches, identical definitions; erhe has no debug-only assert
macro). That is intended here rather than worked around: a lookup miss means R4
was violated, and silently rendering the wrong material is precisely the class
of bug this plan exists to eliminate.

The verify is the guard for the one lookup path that still runs mid-pass:
`write_object_gpu_slots` is called from the joint half of `sync_gpu_slots()`
(`draw_list_scene.cpp:635-644`) inside `draw_color()` / `draw_shadow()`. It
rewrites slot fields from the *live* primitives while references came from a
snapshot, so the verify is what makes such a case fail loudly.

### D1b - Lifetime

Releasing the last reference to an entry drops the set's strong reference.
For a material removed from the library while meshes still use it, that happens
when the last object is unregistered or repainted. `Material_preview::
on_close_scene` / `on_items_removed` (`material_preview.cpp:185-215`) clear the
preview library, `sync_library` drops the reference, and the material is
released the same frame.

`release()` can drop the last reference and destroy the `Material` while
`release_object_materials` is iterating. `Draw_list_object::materials` holds
ids rather than raw pointers, which blunts this, and the set finishes every
read of an entry's `material` pointer before it clears that entry's
`shared_ptr`.

### D1c - Where each set's object references come from

The two sets take object references from two different hooks, and each works on
its own.

**The forward set - `Scene_root`'s mesh hooks.** `Scene_root` already sees
every mesh that enters or leaves it and every material reassignment, which is
all the forward set needs:

| Hook | Effect on the forward set |
| --- | --- |
| `Scene_root::register_mesh` (`scene_root.cpp:1211-1233`) | reference the mesh's current materials |
| `Scene_root::unregister_mesh` | release them |
| `Scene_root::on_mesh_material_changed` (`scene_root.cpp:1440-1445`) | diff: reference the new material, release the old (D11) |
| `Scene_root::on_mesh_primitive_data_changed` (`scene_root.cpp:1461-1466`) | re-diff the mesh's material list |

These are the same four hooks that drive draw-list registration today, so the
existing call sites and notifications carry it: every mutation of a mesh's
materials already reaches the scene host - `set_primitive_material` at
`mesh.cpp:161`, and `add_primitive` / `set_primitives` through
`update_rt_primitives()`, which ends with `notify_primitives_changed()` at
`mesh.cpp:77`. `register_mesh` may run on a worker thread, so these enqueue and
`flush_pending()` applies them (R13, D1).

The coverage argument for the bucket path follows: a mesh renderable by a
composition pass is attached to a node and registered with its root, so its
materials are referenced in the forward set before any pass can name them
(R3). This is also what makes the **preview roots** correct without a draw list
(D0) - `Material_preview::render_preview` assigns a different material to its
already-registered sphere on every call (`material_preview.cpp:233`), which the
content library alone does not cover (context doc, section 1), but
`on_mesh_material_changed` does.

It stays bounded, too (R14): the hook applies a **diff** of the mesh's material
list, so the previous thumbnail's material is released as soon as the mesh stops
naming it. The brush preview, which clears and re-adds the primitives of a persistent
mesh per thumbnail (`brush_preview.cpp:260-276`), is the same shape through
`on_mesh_primitive_data_changed`.

**The draw-list set - draw-list object registration.** Unchanged from D1a:
`sync_object_materials` / `release_object_materials` at `register_object`,
`rebuild_all`, `refresh_object_records` and `unregister_object`. It exists only
for roots that have a `Draw_list_scene` (D0).

A material used by a mesh in a root with a draw list is therefore referenced
**twice, in two sets**. That is the intent (D0).

*Three consumers register no objects at all* and own a `Material_set` whose
membership is library-only (D0): the **BRDF slice window**, whose `sync_library({material})`
is its entire membership; the **standalone example**, whose `m_gltf_data.materials`
(`example.cpp:439`) is by construction every material its meshes use; and the
**shared empty set** (D8).

**`Primitive_buffer` looks slots up as well.** It has four `update` overloads; two
reach `write_primitive` (`primitive_buffer.cpp:359`) and are live:

- the `Render_bucket` overload (`primitive_buffer.cpp:407`, `write_primitive` at
  `:431`) - used by `forward_renderer.cpp:421`, `shadow_renderer.cpp:243` and
  `id_renderer.cpp:394`. It takes the root's **forward** set, which is the set
  the pass binds;
- the `Draw_list` overload (`primitive_buffer.cpp:439`), whose *slow path* calls
  `write_primitive` at `:516`, invoked from `draw_list_scene.cpp:1284` **inside
  the pass**. It already receives a `const Draw_list_scene&`, so it sources the
  **draw-list** set from there, which is the set that pass binds.

Each therefore looks up in the set its own pass binds -
which is the whole of what D0's separation requires of record writers. The
`meshes` overload (`:107`) has no caller and is dead. The nodes overload (`:527`)
hardcodes `material_index = 0` (`:548`), so it needs no parameter and
`Cube_renderer` (`cube_renderer.cpp:128`) is untouched.

**The two record writers differ on a lookup miss, deliberately.**
`write_slot_fields` (the draw-list record writer, D1a) `ERHE_VERIFY`s the
lookup: a miss there means R3 was violated for a *registered* object, which is
a plan bug and must be loud. `Primitive_buffer::write_primitive` (the bucket
path) writes slot 0 on a miss, as it effectively does today. The difference has
a cause: the bucket path is the one that can legitimately be handed a material
the set has yet to see: the forward
set's references are *enqueued* from `Scene_root::register_mesh` (R13) while
`Scene::register_mesh` files the mesh into its layer immediately, so a mesh
created after that frame's flush is visible to a composition pass running in the
rendergraph (`editor.cpp:1019`) one frame before its materials are referenced.
The window is narrow and pre-existing - today such a mesh renders with a stale
`material_buffer_index`, i.e. an arbitrary material - and slot 0 is no worse.
Turning that into an abort would convert a rare, long-standing cosmetic edge
into a crash, which is the opposite of the trade D1a makes for records that are
supposed to be complete.

### D1d - The one membership hole that must be closed for R3 to be total

R3 puts slot assignment ahead of every record writer. One existing behaviour
defeats that unless it is fixed as part of phase 4. It is specific to the **draw-list** set:
the forward set has no cached records and nothing rebuilds it mid-frame.

**`rebuild_all()` is reachable from inside the rendergraph.** Besides
`material_change_operation.cpp:47` (main thread, before the flush - fine),
`Draw_list_scene::set_exclude_unlit_from_shadows` (`draw_list_scene.cpp:878`)
calls `rebuild_all()` at `:889` when the setting changed, and its caller is
`shadow_render_node.cpp:587` - three lines before
`m_context.shadow_renderer->render(...)` at `:590`, i.e. inside
`execute_rendergraph_node`, after the frame's `Material_set::update()` ran. A
reference taken there could assign a slot past the written copy, and it rewrites
every cached record mid-frame after viewport passes may already have consumed
them. Fix: `set_exclude_unlit_from_shadows` sets the flag and *enqueues* the
rebuild, which `flush_pending` performs on the next frame, ahead of that frame's
queued register / unregister / refresh ops so the rebuild cannot undo them. One
consequence to accept: the frame on which the user toggles
`exclude_unlit_primitives` renders shadows with the previous caster
classification, self-correcting on the next frame. Today that frame instead
rewrites every cached record mid-pass, after some viewport passes may already
have consumed them, so the deferral is still the better ordering.

### D2 - `Material_set`

```cpp
// material_set.hpp - include list per D0, plus erhe_graphics
// multi_copy_buffer and material_buffer.hpp for Material_interface.
class Material_set    // continued from D1: membership half there, GPU half here
{
public:
    explicit Material_set(const Material_set_create_info&);  // carries the Device (D3)

    // Called once per frame (D6), after sync_library() and flush_pending()
    // have settled membership. Rehashes members (D10), then writes the GPU copy
    // and resets + repopulates the texture heap ONLY if anything changed (R10);
    // otherwise returns after the hashing. Library membership arrives through
    // sync_library() (D1) and nowhere else.
    void update(erhe::graphics::Command_buffer&);
    auto bind  (erhe::graphics::Render_command_encoder&) -> bool;
    auto bind  (erhe::graphics::Compute_command_encoder&) -> bool;
    void unbind(erhe::graphics::Command_buffer&);          // R9, per pass
    // Force the next update to rewrite, for changes the content hash cannot
    // see (device loss, heap rebuild).
    void invalidate();

private:
    // ... membership half: slot table, free list, dirty edge (D1)
    Material_buffer                               m_material_buffer;  // record writer; its storage is m_gpu
    std::unique_ptr<erhe::graphics::Texture_heap> m_texture_heap;
    erhe::graphics::Multi_copy_buffer             m_gpu;              // D9
    bool                                          m_force_dirty{true};
};
```

`Draw_list_scene` owns the **draw-list** set (D0), declared **before**
`m_objects`, because `Draw_list_object` holds `Material_slot_id`s into it:

```cpp
    Material_set m_material_set;   // before m_objects; constructed with the owner (D3)
    ...
public:
    [[nodiscard]] auto get_material_set()       -> Material_set&;
    [[nodiscard]] auto get_material_set() const -> const Material_set&;
```

`Scene_root` owns the **forward** pair with the same shape (D4). Each owner
reaches its own pair, and slots stay within the set that issued them.

A set's whole per-frame API is `update` / `bind` / `unbind`: the persistent
buffer owns its own storage (D9), so the `~Ring_buffer_range` release/cancel
assert (`ring_buffer_range.cpp:79`) has no bearing here, and a set on a frame
that skips rendering simply idles.

`Material_buffer` stops being a `Ring_buffer_client`. Its record-writing loop
survives almost verbatim, but it writes into a caller-supplied
`std::span<std::byte>` in slot order over its set's `get_materials()`,
zero-filling holes; the record is the whole of its output (D0). Its own
fallback sampler (`material_buffer.cpp:83-89`, for texture slots whose
`Material_texture_sampler` carries no sampler) is distinct from the texture
heap's fallback pair and stays where it is.

The texture heap moves here because of R15: a heap handle baked into a material
record is only meaningful in the heap it was allocated from
(`material_buffer.cpp:157-172`) - and, with persistence, only meaningful for as
long as that heap keeps the allocation. Heap and buffer are therefore reset and
rewritten together, always, in the same `Material_set::update()` call, which is
the one place either is rewritten. This is safe to own here: the only
`Texture_heap::allocate()` calls in the repo are `material_buffer.cpp:159`,
`imgui_renderer.cpp:1298`, `text_renderer.cpp:356` and
`hextiles/tile_renderer.cpp:761`, so `Material_buffer`'s record writer is the
sole populator of the scene-renderer heaps. (`Texture_heap::get_shader_handle()`
does have one external caller, `imgui_renderer.cpp:1423`, on a different heap.)

Two heaps outside this work reset/bind/unbind per pass independently of
materials and share the same bind-group slot, so both are checked in the
sweep: `Texel_renderer` (`texel_renderer.cpp:52,102,108,132`) and
`Post_processing` (`rendergraph/post_processing.cpp:719,769,837,881`).

`Light_buffer` binds fixed reserved slots of the same bind group through
`Command_encoder::set_sampled_image` (`light_buffer.cpp:652-706`,
`c_texture_heap_slot_*`) rather than through `Texture_heap::allocate()`, so
shadow / lightmap / DDGI samplers are unaffected by heap persistence and keep
their existing per-pass binding.

### D2a - `max_textures`

Meaningful on **Vulkan only**. There, `m_max_textures`
(`vulkan_texture_heap.hpp:72`) must stay `<=` `max_texture_heap_size` in the
set-1 pipeline layout (`vulkan_device_init.cpp:2249`, currently 4096); the valid
range is therefore 1..`max_texture_heap_size`, because the set uses a variable
descriptor count (`vulkan_texture_heap.cpp:176`).

**Correction, found in phase 5.** That was only half true as the code stood:
`Texture_heap` also built its own set-1 *descriptor set layout* sized
`m_max_textures`, and a set allocated from a layout that declares a different
descriptor count is not compatible with the pipeline layout - the bind is
rejected, and the editor aborts on it. The layout is now always
`max_texture_heap_size` wide and `max_textures` sizes only the variable
descriptor count and the slots the heap hands out, which is what D2a intended. On GL the sampler-array path
sizes itself from `max_per_stage_descriptor_samplers`
(`gl_texture_heap.cpp:44-50`) and the bindless path grows on demand, so the
parameter is accepted and ignored; likewise Metal and null.

It **is defaulted**, so the five unrelated `Texture_heap` construction sites
(`imgui_renderer.cpp:461`, `text_renderer.cpp:227`, `post_processing.cpp:663`,
`texel_renderer.cpp:53`, `hextiles/tile_renderer.cpp:755`) keep their current
calls.
Adding a member to a backend `Texture_heap_impl` must respect the pimpl size
assert (`texture_heap.cpp:25`, `sizeof <= 512`); the Vulkan impl already has the
member.

### D2b - Vulkan descriptor-set recycling keys on last *use*

`Texture_heap_impl::acquire_descriptor_set` (`vulkan_texture_heap.cpp:246-289`)
recycles a set whose `entry.frame` the GPU has retired, and `entry.frame` is
written only when the set is acquired, i.e. at `reset_heap()`. Today that is
sufficient because every pass resets. With a persistent heap the current set can
stay bound for hundreds of frames while its `entry.frame` stays at the frame it
was acquired; a later `reset_heap()` would then find that entry "completed" and
recycle the very set frames still in flight are reading.

Fix, in phase 3, in the Vulkan impl: `bind()` refreshes
`m_set_entries[m_current_set_index].frame = m_device_impl.get_frame_index()`, and
`acquire_descriptor_set()` additionally skips `m_current_set_index`. The same
"refresh on use" rule is what D9 applies to the buffer copies; both exist for the
same reason.

GL has the equivalent property already: the bindless path re-establishes
residency per pass in `bind()` (`gl_texture_heap.cpp:317-328`) and the
sampler-array path re-binds its units per pass (`:262-269`), so each pass starts
from its own binding.

### D3 - Construction

`Material_set_create_info` carries the device, the shared `Material_interface`
and `Bind_group_layout` from `Program_interface`, a shared dummy texture and
fallback sampler, the heap limit (`max_textures`, D2a) and the initial material
count the buffer is sized from, which it grows past on demand (D9). The shared
fallback texture / sampler pair replaces the per-renderer pairs each renderer
creates today (`forward_renderer.cpp:55-72`, `shadow_renderer.cpp:78-108`); it
and the shared empty set are owned by `App_rendering` (`editor.cpp:1988`),
created where an init command buffer exists, and reached through `App_context`.

**Every construction site hands the `Device` over**: the five `Scene_root`
sites (`editor.cpp:3018`, `scene_open_operation.cpp:44`, `gltf.cpp:1477`,
`scene_preview.cpp:59`, `scene_commands.cpp:521`), every `Draw_list_scene`, and
the three library-only owners below.

**`Draw_list_scene` is created exactly where it is today** (D0). Its
constructor takes four positional arguments
(`draw_list_scene.hpp:113-118`) and this plan adds a `Material_set_create_info`;
collapse them into a `Draw_list_scene_create_info` in the same header and let
`Draw_list_scene_dependencies` (`scene/draw_list_scene_dependencies.hpp`) fill
it.

**Direct `Material_set` owners** - the BRDF slice window, the example
application and the shared empty set construct a `Material_set` and nothing
else: with no scene and no registered objects, `sync_library` is their entire
membership mechanism, and for each that is sound - the BRDF slice window passes
exactly the one material it renders (`brdf_slice.cpp:116`), the example passes
`m_gltf_data.materials` (`example.cpp:439`), which is by construction every
material its meshes use, and the empty set passes nothing. This is the concrete
payoff of D0's dependency rule: a consumer that needs only materials takes only
materials (R7).

### D4 - `Scene_root`

`Scene_root` gains the **forward** set (D0) and the hooks that feed it (D1c):

```cpp
    Material_set m_material_set;   // constructed with the root (D3)
public:
    [[nodiscard]] auto get_material_set()       -> Material_set&;
    [[nodiscard]] auto get_material_set() const -> const Material_set&;
```

`get_material_set()` returns the **forward** set. `get_draw_list_scene()`
(`scene_root.cpp:1468-1471`) keeps its signature, and a caller that wants the
draw-list set asks that object for it. The two accessors stay distinct, each
named for its path, so a call site chooses by the path it is on and that choice
stays visible where it is made.

There are five live `make_shared<Scene_root>` sites - `editor.cpp:3018`,
`scene_open_operation.cpp:44`, `gltf.cpp:1477`, `scene_preview.cpp:59`,
`scene_commands.cpp:521` - plus one inside a `#if 0` block
(`item_tree_window.cpp:1881`, disabled at `:1876`). Each gains a `Device&`; the
sites with a dependency bundle see only its new contents, and the disabled site
keeps compiling-if-revived.

Per-root granularity is right for the forward set because every pass takes its
meshes and its materials from one and the same root: `Composition_pass`
takes its mesh layers from the same `scene_root` it resolves the material
library from (`composition_pass.cpp:143-144`, `:225`), and the draw-list path
passes that same root's `Draw_list_scene` (`composition_pass.cpp:289,332`).
That pairing is the justification; library completeness is not, since the
library omits materials a mesh uses (context doc, section 1), which is why
membership is refcounted (D1).

### D5 - Renderers bind a set they are handed

**Why ownership has to move at all.** There is exactly one `Forward_renderer`
in the editor (`editor.cpp:1724`, reached through
`App_context::forward_renderer`), and it renders every scene: each open scene's
viewport, the two previews, the BRDF slice, the shadow passes. A material
buffer owned by that renderer is therefore shared by scenes that do not share a
slot space.

That is fatal twice over. **The index space would still depend on the last
writer**: a renderer-owned buffer holds one slot mapping at a time and must be
re-filled from whatever list the current pass was handed, which is the context
doc's section 1 mechanism relocated. The bug is not that
`Material_buffer::update` is wrong; it is that "slot 7" means different
materials depending on which pass wrote last, and stable per-scene slots (R6,
R2) cannot be expressed in a buffer another scene's pass overwrites a moment
later. **Persistence would be impossible**: R10 needs the buffer to survive
from the frame that writes it to every later frame that binds it, and a shared
buffer never outlives a single pass.

So the buffer must belong to whatever defines the slot space - the draw list -
and the renderer becomes stateless with respect to materials: it is handed a set
already updated for this frame, and binds it.

The texture heap follows the buffer for the reason D2 gives, rather than as a
separate decision. The dummy texture and fallback sampler move out because they
were per-renderer duplicates of the same two objects (D3), not because ownership
demanded it.

`Scene_pass_resources` - shared by the bucket and draw-list renderers - and
`Shadow_renderer`, `Ddgi_renderer` and `Ray_trace_renderer` lose
`m_material_buffer`, `m_texture_heap`, `m_dummy_texture` and
`m_fallback_sampler`. `Base_render_parameters` replaces
`std::span<const std::shared_ptr<Material>> materials` with
`Material_set* material_source`, already updated for this frame; `nullptr`
selects the shared empty set (D8). This field names `Material_set` precisely
because `Base_render_parameters` is shared by the bucket, fullscreen and
draw-list entry points alike: a `Draw_list_scene*` here would make *binding
materials* - a thing the bucket path does on every pass - require a draw-list
type.

**No renderer owns, creates, updates or resets a set.** Each is handed one
already updated for this frame (D6) and its use of that set is to bind it and to
look slots up. Which set it gets follows the path its caller already chose:

| Renderer / entry point | Path | Which set, and who fills it |
| --- | --- | --- |
| `Draw_list_renderer::render` | draw list | the draw-list set of the `Draw_list_scene` in its parameters, passed by `Composition_pass`'s draw-list branch (`composition_pass.cpp:289,332`); an `ERHE_VERIFY` pins `material_source == &draw_list_scene.get_material_set()` so the two always agree |
| `Forward_renderer::render` / `draw_primitives` | bucket | the root's **forward** set, from `Composition_pass`'s bucket branch (`composition_pass.cpp:354,365`; `:324` is the draw-list branch) and every other bucket caller; the shared empty set when null (D8) |
| `Shadow_renderer::render` | both | whichever matches the path that light takes, filled by `Shadow_render_node` (`shadow_render_node.cpp:601`) |
| `Ddgi_renderer`, `Ray_trace_renderer` | compute | the root's forward set, **from phase 5**; until then they keep their own material buffers and library-order indices, self-consistent because each writes and reads its records within one pass |
| `Id_renderer` | bucket | none, and it binds none: its shader reads only the id, and it is not a `Base_render_parameters` caller at all - it owns its `Primitive_buffer` and drives its own encoder (`id_renderer.cpp:76,394-398`). Its only material contact is the null `const Material_set*` it hands `Primitive_buffer`, which then writes a constant slot 0 (D1c) |

A frame over one root therefore binds *both* of that root's sets, in different
passes (D0); every comparison of slots stays within one set.

**This plan reverses half of R8a, deliberately.**
`doc/draw_list_renderer.md` R8a currently reads:
"`Draw_list_scene` owns NO GPU buffers or texture heap. The per-pass Camera /
Light / Material / Joint / texture-heap update + bind sequence remains the
responsibility of the owning renderer (`Forward_renderer` for color,
`Shadow_renderer` for shadow), and `draw()` is invoked *inside* that sequence."
After this plan, `Draw_list_scene` *does* own GPU state - a `Material_set`, and
through it the material buffer and the texture heap. The camera, light and
joint buffers stay with the renderer, so R8a holds for those. This is not an
oversight to be reconciled later: R8a's own closing sentence is the bug this
plan exists to fix - "material and joint GPU slots (`material_buffer_index`,
`joint_buffer_index`) are assigned per `Material_buffer::update` /
`Joint_buffer::update` call and therefore cannot be baked into entries - an
entry stores a stable reference to the material / skin and the slot is read at
upload time". Slots that are assigned per renderer call are exactly why "slot
7" means different materials in different passes (context doc, section 1).
Phase 4 must therefore also amend R8a in `draw_list_renderer_requirements.md` -
its material and texture-heap clauses, and its naming of the colour path's
owning renderer, which the prerequisite commit changes - together with the
four comments that cite it - `primitive_buffer.hpp:161`,
`draw_list_scene.cpp:598`, `draw_list_scene.hpp:58`, and the one on the
draw-list entry point (`forward_renderer.hpp:173` today, moved into
`draw_list_renderer.hpp` by the prerequisite commit) - rather than leaving a
requirements document that contradicts the code.

**The scope of this decoupling.** It removes the *material* reason for any
renderer to name a draw-list type: after phase 4 the bucket path's parameters,
buffers and bind sequence are expressed in material types alone, and
`Forward_renderer` names no draw-list type at all. The two paths keep sharing
their per-pass prologue through `Scene_pass_resources` (`c6ab99db4`), which is
where D2's and D5's edits land, and each renderer reaches it by reference (the
prerequisite commit, section 3).

Everything that fills the old field must change with it (phase 4):
`composition_pass.cpp:201,324,365`, `shadow_render_node.cpp:601`,
`brdf_slice.cpp:116`, `depth_visualization_window.cpp:149`, `example.cpp:439`.

Inside `begin_pass` (`forward_renderer.cpp:106-190`) the sequence
`reset_heap` -> `m_material_buffer.update` -> `m_material_buffer.bind` ->
... -> `m_texture_heap->bind` collapses to a single
`material_source->bind(encoder)` placed where the material bind is
today (the heap bind must stay after the light buffer's `set_sampled_image`
calls, as it is now). `end_pass` (`forward_renderer.cpp:192-210`) drops
`state.material_range.release()` and keeps `unbind()`.
`draw_primitives` (`:458-461`, `:527`, `:562`) and `Shadow_renderer::render`
(`:393-394`, `:493`, `:678`, `:717`) get the same treatment.

Slot readers:

- `Draw_list_scene` - `write_slot_fields` reads the `Material_slot_id`s the
  object holds into the **draw-list** set, and consults no set itself (D1a).
- `Primitive_buffer` - the three live entry points take a `const Material_set*`
  and look up **in the set their own pass binds**: `forward_renderer.cpp:421`
  and `shadow_renderer.cpp:243` pass the forward set, the `Draw_list` overload
  sources the draw-list set from the `Draw_list_scene&` it already receives, and
  `id_renderer.cpp:394` passes null. Overload inventory: D1c.
- `Scene_tlas` (`scene_tlas.cpp:295`) - the forward set of the scene root it is
  given, which is what DDGI and the ray tracer bind (phase 5).
- `Light_buffer` (`light_buffer.cpp:567`) - `Light_projections::brdf_material`
  becomes a pre-resolved `brdf_material_slot`. **This must land in phase 4**, not
  later: once Forward/Shadow stop calling the legacy `Material_buffer::update`,
  nothing in the raster path writes `material_buffer_index`, and the light buffer
  would otherwise read a stale index from DDGI's or the ray tracer's index space.

### D6 - Flush and update scheduling

Every set a pass can bind is flushed and updated, including the owners that are
not in `App_scenes::m_scene_roots` - the two preview roots' forward sets, the
BRDF slice window and the shared empty set - and a root with a draw list has two
of them updated per frame. Per frame, in `Editor::tick()`, the material update
runs **after** `flush_draw_lists()` (`editor.cpp:796`) and before the first pass
that binds - i.e. before the DDGI tick (`editor.cpp:802`) and well before
`m_rendergraph->execute` (`editor.cpp:1019`), which is also where XR renders
(`headset_view.cpp:170-175`). Ordering after the flush is what satisfies R3: the
flush is where records are written and where object references are taken, so
updating afterwards guarantees the bound copy covers every slot a record can name
this frame.

Three steps, of which only the middle one is per root:

1. `App_scenes::flush_draw_lists()` (`editor.cpp:796`) - unchanged, and already
   its own loop over a copy of `m_scene_roots` taken under `m_mutex`
   (`app_scenes.cpp:152-162`). Draw-list records are written and draw-list
   object materials referenced here, for the roots that have a draw list.
2. For every root in `App_scenes::m_scene_roots`, iterated over a copy taken
   under `m_mutex` - the same pattern and for the same documented deadlock
   reason as `App_scenes::flush_draw_lists`:
   a. `sync_library()` on the forward set, from the root's own content library
      (`content_library->materials->get_all<Material>()`, the list
      `composition_pass.cpp:160` assembles per pass today), and on the
      draw-list set if the root has one - the two sets reconcile the same
      library independently (D0);
   b. `Scene_root::get_material_set().flush_pending()` - the forward set's
      enqueued references applied on the main thread (R13, D1c). This step is
      new and applies to **every** root, including those with no draw list;
   c. `Material_set::update()` on the forward set;
   d. `Material_set::update()` on the draw-list set, if the root has one.
3. `Material_set::update()` for the shared empty set (D8), so every fullscreen
   pass binds a live copy. Its membership never changes, so it has no 2a / 2b.

Steps 2c and 2d are independent - separate slot tables, separate dirty state,
and either may write while the other does not. On a typical frame neither does any
GPU work: membership is unchanged and the content hashes match, so each update
returns after the hash pass (D10). A set's obligations end there (D2), so one
that no pass binds simply keeps its copy.

**Preview roots run step 2 on their own.** They have one set each (D0),
so their whole schedule is that set's 2a-2c - sync, flush, update - done at each
render entry point rather than in the tick. They are re-rendered several times
per frame with a different material each time, from `Thumbnails::update()` (`editor.cpp:766`, via
`hotbar.cpp:1070` / `inventory_window.cpp:276`) and from `draw_imgui_windows()`
(`editor.cpp:664`, via `properties.cpp:1770-1812`), so each render entry point
does its own sync + flush + update after the preview library is repopulated and
after the mesh's material has been assigned:

- `Material_preview::render_preview` - both overloads,
  `material_preview.cpp:168` (texture + material, the one the hotbar and
  inventory call) and `:218`; the library is repopulated at `:224-228` and the
  sphere's material assigned at `:233`;
- `Brush_preview::render_preview` - both overloads, `brush_preview.cpp:198` and
  `:218`; its library holds two materials (`brush_preview.cpp:115-116`) and it
  clears and re-adds its persistent mesh's primitives at `:260-276`.

The flush is load-bearing for both: the assignment at `material_preview.cpp:233`
enqueues its reference (R13), and the flush before the update is what makes the
set carry *this* thumbnail's material rather than the previous one's.

These are also the sets that genuinely dirty on nearly every update - which is correct and is what R12's per-frame *capability* exists for: a
preview may write a fresh copy several times per frame, and D9 sizes the buffer
so it can.

This keeps the bug fixed: each preview root has its own set, so a slot it
assigns is invisible to every other root's - which is R1's independence
applied across roots, the same property that makes a root's own two sets
independent of each other.

Two more are outside the tick schedule entirely and follow the same pattern:

- the BRDF slice window's one-material set:
  `Brdf_slice_rendergraph_node::execute_rendergraph_node`
  (`brdf_slice.cpp:80-125`) runs inside `m_rendergraph->execute`, after every
  step above. Its membership is library-only and one material wide, so
  `sync_library({material})` followed by `update()` is its whole step - and on
  every frame but the first both are no-ops (D10);
- the example application's set: `src/example/example.cpp` has its own loop and
  its own set, established with `sync_library(m_gltf_data.materials)` -
  the same list it passes today (`example.cpp:439`). It is the clearest
  demonstration of R12: a static glTF viewer writes its material buffer once and
  never again.

### D7 - What becomes dead

- `Material_watch` (`draw_list_scene.hpp:305-311`) and `m_material_watches` are
  **deleted**, but one of the three fields survives the move. `use_count` becomes
  `Material_slot::use_count` and `slot` goes (the slot is the set's own index);
  `identity_hash` **stays in `Draw_list_scene`**, as a plain
  `std::unordered_map<const Material*, uint64_t>` beside the objects. That hash is shader-variant identity - a draw-list
  partitioning concern - and putting it on `Material_slot` would make the
  material layer carry a field it never interprets, so it stays in the
  draw-list layer, where D0's dependency rule keeps it. `check_material_changes()` (`draw_list_scene.cpp:766-810`) keeps its
  logic, iterating the set's live materials and comparing against its own side
  map.
- The material half of `sync_gpu_slots()` (`draw_list_scene.cpp:603-634`) is
  removed; the joint half (`:635-644`) stays. A cached record's `material_index`
  stays valid on its own: the slot is the draw-list set's own index, and R2
  keeps it assigned for as long as the record exists.
- `Scene_root` shares the `Material_set` *type* with `Draw_list_scene` and takes
  its references from its own mesh hooks (D1c), which already exist.
- `Material::material_buffer_index`, `Material::preview_slot`, and the
  `Ring_buffer_client` base of `Material_buffer` (phase 6).
- The three per-pass `reset_heap()` calls (`forward_renderer.cpp:121`, `:458`,
  `shadow_renderer.cpp:393`) and the per-pass material ring acquires that go
  with them.
- The four renderers' `m_dummy_texture` / `m_fallback_sampler` pairs
  (`forward_renderer.cpp:55-72`, `shadow_renderer.cpp:78-108`, and the DDGI and
  ray-trace equivalents), replaced by the one shared pair in `App_rendering`.

### D8 - The shared empty set

A single `Material_set` holding no materials, owned by `App_rendering` next to
the shared fallback texture/sampler (D3). It exists for the texture heap, not
the buffer: today an empty material list already produces an empty
`Ring_buffer_range` whose `bind()` early-outs at `byte_count == 0`
(`ring_buffer_client.cpp:39-41`), so the material buffer is genuinely not bound
for these passes and that works - but `reset_heap()` and `Texture_heap::bind()`
still run, because the renderer owns the heap. Once the heap moves into the set
(D5), a pass with no materials has no heap to bind, and giving each such pass
its own would add Vulkan descriptor-set pools for permanently empty content.

Bound by the two passes that carry no materials of their own - the grid / sky
branch of `Composition_pass` (`composition_pass.cpp:201`, taken when
`data.mesh_layers.empty()`) and the depth visualization window's full-screen
triangle (`depth_visualization_window.cpp:149`). `material_source == nullptr`
selects it, so those call sites stay as simple as they are now. It is updated
once on its first `update()` and clean forever after (D10), so under R10 every
later update just rebinds that copy.

### D9 - `Multi_copy_buffer`: the persistent storage (R10)

New in `erhe_graphics`: a ring-buffer-like construct that holds N complete
copies of one payload, where a copy is written only when the owner has something
new to say and every other frame rebinds the copy that is already current.

**Both kinds of set use it**, though only the draw-list set is required to
(R10, R11). The forward set gets the same treatment because it removes the
per-pass rewrite the context doc's section 1.2 measures, and because one
mechanism for both sets is simpler than two; the bucket path's own obligations
end at R7 and R8.

```cpp
class Multi_copy_buffer_create_info
{
public:
    std::size_t                 initial_copy_byte_count{0}; // grows on demand
    std::size_t                 copy_count     {0};   // 0 = device frames in flight + 1
    Buffer_target               buffer_target  {};
    std::optional<unsigned int> binding_point  {};
    erhe::utility::Debug_label  debug_label    {};
};

class Multi_copy_buffer
{
public:
    Multi_copy_buffer(Device&, const Multi_copy_buffer_create_info&);

    // A writable span in a copy other than the current one, chosen so that
    // no in-flight frame is reading it. Reallocates when byte_count exceeds
    // the current copy size (see Growth); the copy count itself is what the
    // sizing rule below makes sufficient.
    [[nodiscard]] auto begin_write(std::size_t byte_count) -> std::span<std::byte>;
    // The written copy becomes current; the previous current copy stays
    // readable until the frames that bound it complete.
    void commit(std::size_t byte_count);

    // Binds the current copy and stamps it as used by the current device
    // frame. True once a copy has been committed.
    auto bind(Command_encoder&) -> bool;

    [[nodiscard]] auto has_current() const -> bool;
    [[nodiscard]] auto get_current_byte_count() const -> std::size_t;

private:
    class Copy
    {
    public:
        std::size_t byte_offset    {0};
        std::size_t byte_count     {0};
        uint64_t    last_used_frame{0};   // refreshed by bind(), not by commit()
        bool        written        {false};
    };
    Device&                 m_device;
    std::unique_ptr<Buffer> m_buffer;      // copy_count * aligned copy size
    // Superseded allocations, each destroyed once every frame that bound it
    // has completed (Growth).
    std::vector<std::pair<std::unique_ptr<Buffer>, uint64_t>> m_retired;
    std::vector<Copy>       m_copies;
    std::size_t             m_current{invalid};
};
```

**Why `last_used_frame` is refreshed by `bind()`, not by `commit()`.** A copy
that is committed once and then bound for two hundred frames is *in use* on
every one of those frames. Stamping only at commit is the same mistake D2b
describes for the Vulkan descriptor set, and has the same consequence: a later
write would target a copy that in-flight frames are still reading.

**Sizing.** `copy_count = Device::get_number_of_frames_in_flight() + 1`. With F
frames in flight, the frames that have not completed are at most F, and each of
them bound exactly one copy, so at most F distinct copies can be unsafe to
write; one more copy guarantees `begin_write` always finds a free one, even when
every consecutive frame updates. Vulkan has F = 2 (`vulkan_device.hpp:461`),
Metal 3 (`metal_device.hpp:238`); GL and null report their own.
`initial_copy_byte_count` starts a set at what it is expected to hold (the
`Material_set_create_info` sizing hint, D3) times the material struct size, and
the buffer grows from there as membership does.

**Growth** (R12). `begin_write` for a payload larger than the current copy size
allocates a new `Buffer` with the same `copy_count`, writes into a copy of it,
and moves the old allocation to `m_retired` with the highest frame that bound
any of its copies. A retired allocation is destroyed once
`is_frame_completed()` passes that frame. Nothing is copied forward: every write
`Material_buffer` performs is a complete payload over the slot table in order
(D2), and growth only happens on a write, so the new allocation is fully
populated by the write that triggered it. The shader side imposes no ceiling -
`materials` is declared as an unsized array in a shader storage block
(`material_buffer.cpp:71`) - so `Material_interface::max_material_count`
(default 1000, `program_interface.hpp:30`) stays what it is today, the reported
capacity in the interface, and stops being a hard limit on a set.

Cost: a set holds `copy_count` copies of its current payload, so a 200-material
set is roughly 200 x ~208 B x 3, about 125 KB, and grows only with membership.
The bare sets (BRDF slice, example, empty) start at a small hint and mostly stay
there, the same way D2a sizes their heaps down.

**Frame completion.** `begin_write` selects a copy that is not current and whose
`last_used_frame` the device has retired. That needs
`Device::is_frame_completed(uint64_t frame)`, which exists only on the Vulkan
`Device_impl` today (`vulkan_device.hpp:441`); phase 2 promotes it to the public
`Device` API and implements it for GL (from `m_completed_frames`,
`gl_device.hpp:335`), Metal (from its completed-frame bookkeeping,
`metal_device.hpp:227`) and null (any frame below the current one is complete).
This is the same predicate the Vulkan texture heap already uses for descriptor
sets, so the three new implementations have a working reference.

**Why a dedicated buffer rather than a held `Ring_buffer_range`.** The existing ring
buffers are device-shared per `Buffer_target` (`ring_buffer_client.cpp:26-29`
routes every acquire through `Device::allocate_ring_buffer_entry`) and the
underlying algorithm is circular: a range held for hundreds of frames would sit
in the middle of a FIFO that every other client of that target allocates from,
and would stall reclamation behind it. A dedicated, non-circular, fixed-slot
buffer is simpler to reason about and is the shape that keeps one long-lived
allocation clear of unrelated per-frame traffic.

### D10 - Invalidation (R5)

`Material_set::update()` writes a new copy when **any** of the following holds:

1. `m_force_dirty` - construction, and explicit `invalidate()`.
2. The set's membership dirty edge - any slot was assigned or freed by
   `sync_library`, `add_ref` or `release` since the last update. This is an
   exact edge: the mutators set it only when they actually changed an entry's
   liveness, not on every call.
3. Any live entry's **content hash** changed.

The content hash is computed in the update, per live entry, over exactly the
inputs `Material_buffer`'s record writer reads:

- the POD fields of `Material_data` that are written to the record (scalars,
  colours, per-texture rotation / scale / offset, `bxdf_model`);
- for each of the five texture slots, the *resolved* `const Texture*` from
  `Material_texture_sampler::texture_reference->get_referenced_texture()` and
  the `const Sampler*` (or the fallback sampler when null).

Hashing the resolved texture pointer is what makes a re-baked editor
`Graph_texture` - whose reference returns a different texture object after a
bake - dirty every set the material is a member of, without the texture graph
knowing material state exists.

**What persistence removes.** Today's correctness rests on the per-frame
rewrite, and `material_change_operation.cpp:18-24` says so: only the
draw-list-identity fields (blending class, double-sided) need an explicit
re-registration, because "Everything else in `Material_data` reaches the shader
through `Material_buffer`, which is re-uploaded each frame anyway" (verbatim).
The rule above is what replaces that guarantee, which is why R5 demands a
mechanism no writer can slip past.

**Why a hash and not a version counter on `Material`.** A `data_serial` bumped
by every writer is cheaper, but it can be missed, and *is* missed the moment
someone writes `material->data.base_color = ...` directly - which is what
`properties.cpp` does while a colour picker is being dragged, and what the MCP
`edit_material` tool does. R5 exists because that failure is silent, produces
exactly the class of "the mesh did not change colour" bug this document opens
with, and would be indistinguishable from it in a report. The hash cannot be
missed: it reads the same bytes the record write reads.

The cost is a hash of a few hundred bytes per member material per frame - for a
500-material scene, roughly 100 KB hashed per frame, against the ~100 KB
written, several hundred heap `allocate()` linear searches and one descriptor
set per pass that it replaces. Use the hash function already vendored for
`Draw_list_key_hash` / `material_identity_hash` rather than adding a dependency.

Two consequences to accept:

- **A material edit is visible on the frame after the edit at the earliest**, if
  the edit lands after that frame's update. It already is: the update runs after
  `flush_draw_lists()` and imgui windows are drawn before that, so an edit made
  by a slider drag is picked up on the next tick, the same as today.
- **The hash pass is the new floor cost of a draw list.** It is O(members), not
  O(objects), and it is the only per-frame material work left on a clean frame.

`identity_hash` (draw-list variant identity) stays separate and keeps its
existing consumer, `check_material_changes()` (D7): an identity change
re-registers objects, a content change only dirties the buffer, and conflating
them would re-register the world on every colour tweak.

### D11 - How `set_primitive_material` reaches the set (R4)

The chain already exists, end to end. `Mesh::set_primitive_material`
(`mesh.cpp:153-171`) early-outs when the material is unchanged, writes
`m_primitives[i].material`, and calls `Scene_host::on_mesh_material_changed`.
`Scene_root::on_mesh_material_changed` (`scene_root.cpp:1440-1445`) is the one
place that has to fan out to both sets (R4):

- **the forward set**, directly - enqueue the diff of the mesh's material list,
  applied by `flush_pending()` on the main thread (D1c, R13);
- **the draw-list set**, when the root has a draw list, by the existing
  enqueued re-register; `flush_pending()` applies it, and `register_object` runs
  `sync_object_materials` **before** `add_entries` (D1a).

Both are diffs: the new material is `add_ref`-ed, the old one released if that
was its last reference in that set, and each set's records are written from its
own slots. A root with no draw list simply does the first. Every
assignment gesture - drag-drop (`item_tree_window.cpp:807`), the paint tool
(`material_paint_tool.cpp:184,231`), the Properties picker
(`properties.cpp:761`) and the material preview's per-thumbnail assignment
(`material_preview.cpp:233`) - goes through it without knowing material sets
exist.

Two things stand between that and "it just works", and both are part of this
plan rather than follow-ups.

**The accessor hole (R4's second half) - closed, `fbaaa33a4`.** `set_primitive_material`
and `set_primitive_lightmap_uv_scale_offset` are the only writers of a
`Mesh_primitive` field and of the vector's structure, so the comments at
`erhe_scene/mesh.hpp:111-116` are enforced rather than advisory. Every call site
reaches what it needs through the const `get_primitives()`, since `shared_ptr`
constness is shallow and a `const Mesh_primitive&` still grants a mutable
`Primitive` pointee.

The seal covers the `Mesh_primitive` fields that decide draw-list membership.
Geometry and renderable-mesh edits go through that same `Primitive&` and reach
the draw lists by the separate `enqueue_refresh` / re-register paths, out of
scope here.

**The cost.** `enqueue_reregister` is a full unregister + re-register: every
entry removed, every primitive re-classified, every record rewritten. A
material swap only changes the object's draw list *identity* when the new
material differs from the old in the three key fields it feeds -
`Draw_list_key::blending`, `::double_sided` and `::primitive_key`
(`draw_list_key.hpp:49-66`); every other key field comes from the mesh, node or
primitive and stay put across a material swap. When those three match, the
correct update is the cheaper one that already exists:
`sync_object_materials` followed by `write_object_gpu_slots`
(`draw_list_scene.cpp:535-548`), which rewrites just the slot fields of the
object's existing records.

This is an optimization, not a correctness fix - the full re-register is
already correct, which is why it is safe to land it as a separate step. It
matters because R4 puts it on the material preview's per-thumbnail path
(`material_preview.cpp:233`, several times per frame) and on every drag of the
material picker. Land it in phase 4 behind the exact predicate - compare the
three key fields old against new - because a mis-predicted "unchanged" key
leaves an entry in the wrong draw list, which is a wrong pipeline, not a wrong
slot.

## 3. Implementation

One commit per phase: edit, build the primary tree, self-review the diff,
commit. Test suites and the multi-backend sweep run once, at the end (phase 7).

**Prerequisite commit - `Draw_list_renderer`** (landed, `c497f339e`). The draw-list draw entry point
becomes its own renderer before phase 1: `Draw_list_render_parameters` and the
body of today's `Forward_renderer` draw-list entry point move to a new
`Draw_list_renderer` in `erhe_scene_renderer`, its one caller
(`composition_pass.cpp:313`) is re-pointed at it, and `forward_renderer.hpp`
drops `draw_list.hpp`. The shared per-pass prologue stays in
`Scene_pass_resources`, which `c6ab99db4` already extracted and which holds no
per-frame state of its own (`begin_pass` returns a `Pass_state`), so it is owned
once and both renderers take a reference. This commit carries no material
content, and the plan is written against `Draw_list_renderer` throughout.

**The index space must switch atomically.** A cached draw-list record's
`material_index`, a primitive record written during a pass, and the material
buffer bound when they are drawn all have to agree. The draw-list conversion,
the raster-path conversion, every direct filler of the `materials` parameter,
and the BRDF material slot are therefore one commit (phase 4).

**Phase 1 - MCP surface for the regression test.**
The MCP tool table has `get_scene_materials` (`mcp_server.cpp:491`),
`get_material_details` (`:495`), `edit_material` (`:555`) and `create_material`
(`:556`), but no way to assign a material to a mesh primitive and no way to read
back what a *cached draw-list record* resolved to - `query_draw_lists`
(`mcp/mcp_server_scene_query.cpp:184-247`) reports counts and diagnostics only,
and the record bytes sit behind `Draw_list_scene`'s private
`m_primitive_interface` / `get_record()`. Add:

- `assign_mesh_material` (mesh, primitive index, material), which also renders
  the material preview once so the test does not depend on the Properties
  window being open;
- a public accessor on `Draw_list_scene` returning an entry's record
  `material_index`, and a per-entry `material_index` field in
  `query_draw_lists`.

V3 is written and shown **red** here, before any behaviour change.

**Phase 2 - Device-agnostic foundations, no callers.**

- `Device::is_frame_completed()` promoted to the public API and implemented on
  all four backends, plus `Device::get_number_of_frames_in_flight()` (D9). GL
  and Metal keep a highest-retired watermark advanced from the completion sink
  they already had, and both also advance it in `wait_idle()`, which proves
  every earlier frame retired: GL plants a fence only when something asked for
  a sync, so without that the watermark would never move for a consumer whose
  frames request none.
- New `erhe_graphics/multi_copy_buffer.{hpp,cpp}` (D9), with its own unit tests
  (V6).
- New `erhe_scene_renderer/material_set.{hpp,cpp}` (D1): `Material_slot`,
  `Material_slot_id` and `Material_set`'s **membership half** - the slot table,
  the two reference sources, the enqueue / flush path and the dirty edge. The
  GPU half (D2) is phase 3, which adds the buffer, the heap and
  update / bind / unbind to this same type; keeping the data members out until
  then is what lets V1 run with no device at all. The header names the material
  layer alone (D0), and V1 is the standing check on that: it links against
  `erhe::scene_renderer` and `erhe::primitive` and nothing else.
- `Texture_heap` gains the defaulted `max_textures` parameter (D2a).
- Also extracts `gpu_test_fixture` / `gpu_test_environment` from
  `src/erhe/graphics/test` into a reusable `erhe_gpu_test_support` target.

Nothing in the tree uses any of it yet. `Material_buffer` is untouched and keeps
writing `material->material_buffer_index` - every existing reader still depends
on it until phase 6. Ships with the V1 and V6 tests.

Two things the implementation settled that the design did not say:

- **`Buffer::begin_write(offset, count)` is not usable for a multi-copy write.**
  On the persistently mapped path it ignores its offset and returns the
  whole-buffer map, so every copy would land at offset zero.
  `Multi_copy_buffer` therefore writes through `Buffer::get_map().subspan()`
  where there is a map and through a CPU staging block plus
  `upload_sub_data()` where there is not - the same two paths `Ring_buffer`
  takes, for the same reason.
- **`begin_write` grows rather than stalls when every other copy is in use.**
  The backends answer "is this frame retired?" conservatively, so a consumer
  sized exactly to the frames in flight can find no free copy through no fault
  of its own. Taking a fresh allocation (and retiring the old one) keeps that
  from wedging the caller or, worse, overwriting a copy a frame is reading.

**Phase 3 - Sets get owners.**

- `Material_set::update()` / `bind()` / `unbind()` / `invalidate()` (D2), with
  the D10 invalidation rule, over the slot table of D1.
- `Material_set::enqueue_* / flush_pending()` (D1, R13).
- **`Scene_root` gains the forward set** (D4): a plain member from the
  constructor (D3), and the four mesh hooks that reference and release
  materials (D1c). This half of the plan lives entirely in `Scene_root`, and it
  is what makes the preview roots correct through their forward set alone.
- **`Draw_list_scene` gains the draw-list set**, declared before `m_objects`;
  `Draw_list_scene_create_info` (D3) carries the `Material_set_create_info`. The
  set of `Draw_list_scene` owners stays exactly as it is (D0).
- Library-only owners: the BRDF slice window, `example.cpp`, and the shared
  empty set in `App_rendering` (D3, D8).
- `Material_buffer` gains the span-writing, slot-table-driven record writer
  alongside its existing ring-based `update()`.
- The Vulkan texture heap's use-stamping fix (D2b).
- Shared fallback texture / sampler and the shared empty set in `App_rendering`
  (D3, D8); the new fields on `Draw_list_scene_dependencies`.
- The D6 schedule, all three steps, plus the preview roots' own sync + flush +
  update in their render entry points.

No consumer reads either set's slots yet, so the frame is unchanged. Ships with
V2.

Three things the implementation settled that the design did not say:

- **The content hash and the record writer are generated from one list**, which
  is what D10's open question asked for. `Material_record_inputs`
  (`material_buffer.hpp`) is every value one record is written from, resolved:
  the POD fields, and per texture slot the resolved `const Texture*`, the
  `const Sampler*` and the packed rotation / scale / offset.
  `gather_material_record_inputs()` builds it, `write_record()` writes from it
  and nothing else, and the hash is the hash of the whole struct. The two
  cannot drift, because a field that is not in the struct reaches neither.
- **The shared fallback pair and the empty set are owned by
  `Material_set_factory` (`editor/renderers/material_set_factory.{hpp,cpp}`),
  not by `App_rendering`** as D3 says. `App_rendering` is built inside the
  construction taskflow, and so are the scene roots and both previews, each of
  which needs a GPU-backed set from its own constructor; an owner built inside
  that graph is visible to some of them and not others depending on
  scheduling. The factory is constructed next to `Program_interface`, before
  the taskflow, and published into `App_context` on the spot. D3's stated
  reason - "created where an init command buffer exists" - holds there too.
- **`Material_set` keeps a membership-only default constructor.** V1 runs
  against no device at all, and phase 3 does not take that away: the GPU half
  is held behind a `unique_ptr` that a create info with no device leaves null,
  and `has_gpu()` reports it. The deviceless editor configurations take that
  path as well.

V2 as shipped covers V2.1-4, 7-10 and 13 plus an `invalidate()` case, reading
records back through a compute shader that binds the set exactly as a pass
does. V2.5, 6, 11, 12 and 14-16 are deferred to phase 4: they need a record
writer that actually consults a set, or a raster pass sampling through the
heap, neither of which exists until the index space switches.

This phase also sets `max_textures` (D2a) and the initial material count (D9)
per set: 4096 textures for the sets of roots that render content; 256 textures
and a small initial count for the two preview roots, the shared empty set and
the BRDF slice. The heap limit is a hard one, so it is the sizing that matters;
the buffer only starts where the hint puts it. Sizing matters more than it did
under a single-set design, because a root that renders through draw lists now
carries **two** sets - two copy buffers and two heaps (D0). The heap baseline is eight 4096-descriptor heaps today, not
four - every `Texture_heap` defaults to `m_max_textures{4096}`
(`vulkan_texture_heap.hpp:72`), so `imgui_renderer`, `text_renderer`,
`post_processing` and `texel_renderer` count too - so the delta is smaller than a
naive count suggests, but it is now a doubling on the main roots and must be
measured here rather than at the end (V5, section 5).

**Phase 4 - Atomic index-space switch.**

Phase 3's shape to build on: `Scene_root::get_material_set()` is the forward
set and `Draw_list_scene::get_material_set()` the draw-list one, both live and
updated per frame by `App_scenes::update_material_sets()`;
`App_context::material_set_factory` hands out create infos and owns the shared
empty set; `Material_buffer::write_records()` is the span writer the sets use.


- `Scene_pass_resources` and `Shadow_renderer` take `Material_set*
  material_source` through `Base_render_parameters`; delete their material
  buffers, heaps and fallback pairs, and the three per-pass `reset_heap()` calls
  that go with them (`forward_renderer.cpp:121`, `:458`,
  `shadow_renderer.cpp:393`) - R9 keeps bind / unbind per pass (D5).
- D1d's membership fix, the last piece R3 needs to be total:
  `set_exclude_unlit_from_shadows` (`draw_list_scene.cpp:878-890`) enqueues its
  rebuild, replacing the inline `rebuild_all()` call from the rendergraph.
- Every filler of the old `materials` field changes with them, **each choosing
  the set that matches its path** (D5): `composition_pass.cpp:201,324,365`
  (draw-list branch -> the draw-list set, bucket branch -> the forward set),
  `shadow_render_node.cpp:601`,
  `brdf_slice.cpp:116`, `depth_visualization_window.cpp:149` (shared empty
  set), `example.cpp:439` - the last of which does not build otherwise, which is
  why its `Material_set` lands in phase 3.
- `Light_projections::brdf_material` becomes `brdf_material_slot` - the read at
  `light_buffer.cpp:567` and its only assignment, `brdf_slice.cpp:95`. D5
  explains why this cannot wait.
- `Primitive_buffer`'s `Render_bucket` overload (`primitive_buffer.cpp:407`),
  `Draw_list` overload (`:439`) and `write_primitive` (`:359`) take a
  `const Material_set*` - the bucket overload the **forward** set, the draw-list
  overload the **draw-list** one, each the set its own pass binds (D1c, D0);
  `id_renderer.cpp:394` passes null.
- `Draw_list_scene`: `sync_object_materials` / `release_object_materials`,
  **including the ordering corrections** at `register_object` (`:703-704`) and
  `rebuild_all` (`:915-917`) and the new call in `refresh_object_records`;
  `Draw_list_object::materials` becomes `std::vector<Material_slot_id>`;
  `write_slot_fields` reads the object's own `Material_slot_id`s, with a verify;
  `Material_watch`,
  `m_material_watches` and the material half of `sync_gpu_slots()` deleted, with
  `check_material_changes()` re-pointed at the set (D7).
- **Amend R8a in `doc/draw_list_renderer.md`**, as D5 sets out,
  along with the four code comments citing it: `primitive_buffer.hpp:161`,
  `draw_list_scene.cpp:598`, `draw_list_scene.hpp:58`, and the draw-list entry
  point's (`forward_renderer.hpp:173` today, in `draw_list_renderer.hpp` by
  then).
- The R4 cheap path (D11), **as its own commit after the phase 4 commit**:
  the full re-register is already correct, so the optimization lands on its own
  and leaves the atomic switch standing by itself. **Landed**, comparing the
  whole key plus the entry's baked variant rather than the three named fields:
  the classification has to be recomputed either way, so the exact test costs
  no more than the predicate and cannot mis-predict.

V3 turns green here.

Two things the implementation settled that the design did not say:

- **`Scene_pass_resources` and `Shadow_renderer` take the shared empty set by
  reference at construction**, because `material_source == nullptr` has to
  resolve to *something* and neither owns one. `Editor` hands them
  `Material_set_factory::get_empty_material_set()`; the example, which has no
  pass without materials of its own, hands them its own set.
- **V3's second assertion had to change with the index space.** It compared a
  cached record's `material_index` against `Material::material_buffer_index`,
  which after this phase nothing in the raster path writes. `get_draw_lists`
  gained a per-entry `material_set_slot` - the material's slot in that scene's
  **draw-list** set - and the test compares against that. The comparison is
  the same claim, now stated in the slot space the records actually name.

**Phase 5 - Compute path.** `Ddgi_renderer`, `Ray_trace_renderer` and
`Scene_tlas` move to the root's **forward** set (D5).
Until this lands these three keep their own material buffers and library-order
indices, which is self-consistent: they write their TLAS instance records and
read them back within the same pass.

**Phase 6 - Delete the field.** Remove `material_buffer_index` and
`preview_slot` from `Material`, and the legacy ring-based
`Material_buffer::update()` overload together with its `Ring_buffer_client`
base. The compiler enumerates the last readers: the trace line at
`brush.cpp:350` (drop it) and the developer row at `properties.cpp:770` (read
the slot from the mesh's scene root's forward set). After this commit the reported
bug is unrepresentable.

**Phase 7 - Verification.** Section 4. What has and has not been run is
recorded in `doc/draw_list_material_set_context.md`; the automated half is
green and the interactive half needs a person at the keyboard.

## 4. Verification

### V1 - `Material_set` membership unit tests (null device)

New `src/erhe/scene_renderer/test/` with a googletest target
`erhe_scene_renderer_tests` running against the null device - no window, no
scene, no draw list (R16). Repo conventions: test directories are gated on
`ERHE_BUILD_TESTS` (default **OFF**, `CMakeLists.txt:70`), every test
`CMakeLists.txt` repeats its own `CPMAddPackage(googletest)`, and
`src/erhe/scene_renderer/CMakeLists.txt` has no `add_subdirectory(test)` yet -
all three need adding.

1. `library_sync_assigns_slots_in_order` - three materials get 0, 1, 2.
2. `slot_is_stable_across_sync` - re-syncing the same library changes nothing.
3. `unrelated_removal_does_not_shift` - remove the middle material; the third
   keeps slot 2. **The R2 property that makes cached records safe.**
4. `unrelated_addition_does_not_shift` - appending never renumbers.
5. `two_sets_are_independent` - the same `Material` in two sets gets two
   different slots; syncing one does not touch the other. **The unit-level
   statement of the reported bug.**
6. `get_slot_never_assigns` - **R4.** The const half of the interface cannot
   change membership: `get_slot()` on an unknown material
   returns nullopt and leaves the set unchanged (no slot count growth).
7. `add_ref_assigns_slot_for_material_not_in_library` - the
   assign-to-registered-mesh path from the context doc's section 1.
8. `referenced_material_survives_library_sync` - add_ref, then `sync_library`
   without it: slot held.
9. `slot_freed_only_when_both_sources_are_gone` - drop the library reference,
   keep the object reference: held; drop it: freed.
10. `freed_slot_is_reused` - and a new material then gets it, **with a bumped
    generation** so a stale `Material_slot_id` does not validate (D1).
11. `add_ref_is_refcounted` - two objects reference one material, one releases,
    slot held.
12. `add_ref_returns_existing_id` - referencing a library material returns its
    id.
13. `removed_material_has_no_slot` - `get_slot()` returns nullopt.
14. `slot_count_covers_holes` - a sparse slot table reports highest live slot + 1.
15. `set_releases_material_reference_when_slot_freed` - no leak (D1b).
16. `membership_dirty_is_an_exact_edge` - **R10.** Set by an assignment or a
    free; not set by a `sync_library` that changes nothing, by a repeated
    `add_ref` of an already-referenced material, or by a `release` that leaves
    the count above zero.
17. `object_churn_does_not_grow_the_slot_table` - **R14**, the leak guard for
    object-sourced membership. Simulate the material-preview pattern: repeat
    {add_ref(new material), release(previous material)} 100 times with a fresh
    material each round; the slot table stays at two and the released
    materials' strong references are dropped. Two rather than one because the
    new material is referenced *before* the old one is released - the ordering
    that keeps a shared material from ever reaching zero (D1a) - so both hold a
    slot for an instant. The property is that the table does not grow with the
    number of rounds.
18. `enqueued_reference_is_not_visible_until_flush` and
    `flush_applies_enqueued_references_in_order` - **R13.** The deferral path
    `Scene_root::register_mesh` needs from a worker thread (D1c).
19. `shared_material_survives_a_list_diff` - the property
    `sync_object_materials` rests on, stated in set terms so V1 needs no
    draw list: apply the difference between an old and a new material list
    (add_ref what is new, release what is gone) where both lists contain one
    common material, and assert that material's slot and reference are held
    throughout. V2.14 and V3 cover the hook itself.

### V2 - `Material_set` GPU tests (real device)

Uses the `erhe_gpu_test_support` target extracted in phase 2. The GPU target is
conditional (`_gpu_tests_supported`: Vulkan with any window library, or
OpenGL/Metal with a non-`none` window library), so V2 runs wherever a device
exists; V1, V5 and V6 are what cover the null/headless configuration.

1. `two_sets_same_material_distinct_slots` - two `Material_set`s over
   overlapping lists ordered differently; each one's record at *its own* slot
   carries that material's base colour.
2. `record_survives_foreign_update` - update set A, then set B (same material,
   different slot), then read A's buffer back unchanged. The direct analogue of
   "preview render clobbers the main scene".
3. `stable_slot_after_material_added` - the first material's record stays at its
   original slot.
4. `referenced_non_library_material_is_written` - a material present only by an
   object reference is written at its slot, not left as a zero-filled hole (R3).
5. `object_registration_admits_material` - register an object whose material is
   not in the library; its record is written. **The replacement for the old
   fallback-walk test, and the coverage claim D1c rests on.**
6. `texture_handles_resolve_within_own_heap` - a textured material in two draw
   lists resolves to a valid handle in each, and a one-triangle render samples
   the right texture in both.

Persistence (R10):

7. `clean_update_writes_nothing` - update twice with identical inputs; the
   second commits no new copy (a commit counter on `Material_set`) and the bound
   buffer offset is unchanged.
8. `material_data_edit_dirties_the_set` - edit `base_color` directly on
   the `Material` object, with no notification of any kind, then update: a new
   copy is committed and carries the new colour. **The R5 test**; written to
   fail against a version-counter implementation that only trusts notifications.
9. `texture_rebake_dirties_the_set` - point a slot's texture reference at
   a different texture (the `Graph_texture` re-bake shape) and update: a new
   copy is committed, and the record's handle resolves to the new texture in the
   heap.
10. `membership_change_dirties_the_set` - add and remove a material.
11. `heap_survives_clean_frames` - update once with a textured material, then
    render N frames binding without updating; every frame samples the right
    texture. Covers D2b on Vulkan and heap residency on GL.
12. `consecutive_updates_do_not_overwrite_in_flight_copy` - update and render on
    every frame for 3 * (frames in flight) frames with different content each
    time, reading each frame's rendered result back; every frame shows its own
    content. The end-to-end statement of D9.

Standalone use (R7, D0):

13. `material_set_alone_updates_and_binds` - construct a `Material_set` with no
    scene, no draw list and no objects anywhere in the test, `sync_library` one
    material, update, bind, render a fullscreen quad sampling it. This is the
    BRDF slice / example / empty-set configuration, and the test is also the
    standing check on D0's dependency rule: its translation unit includes `material_set.hpp`
    alone, so a dependency creeping back in breaks the build rather than a
    review.

Assignment (R4):

14. `set_primitive_material_updates_the_set` - register an object with
    material A, call `Mesh::set_primitive_material(0, B)`, flush: B has a slot,
    the object's records name it, and A's slot is freed (nothing else
    referenced it). The unit-level statement of R4, with no caller-side step
    between the setter and the assertion.
15. `reassignment_to_an_identity_equal_material_keeps_the_entry` - A and B
    differ only in base colour, so `Draw_list_key::blending`, `::double_sided`
    and `::primitive_key` match. The entry stays in the same draw list at the
    same location, and only its `material_index` changes. Guards the D11 cheap
    path against a mis-predicted key; written to run green on the full
    re-register too, so it can land before that optimization.
16. `reassignment_across_blending_classes_moves_the_entry` - A opaque, B
    translucent: the entry moves to the list matching B, with correct records
    there. The case that takes the full re-register.

Per the known lavapipe limitations (no D24S8, no `fillModeNonSolid`) these
render to a colour-only target with solid fill.

### V3 - End-to-end regression test for the reported bug

Against a running editor over MCP (`mcp/test/mcp_server_tests.cpp`), default
endpoint `127.0.0.1:3743` (`mcp_server.hpp:75`, `ERHE_MCP_PORT`, matching
`mcp_server_tests.cpp:154`), using the two additions from phase 1.

`material_drag_to_second_mesh_uses_same_record_slot`:

1. Create two meshes in one scene.
2. Assign the material to mesh A (the MCP action renders the material preview,
   reproducing the clobber deterministically).
3. **Wait for a rendered frame** before the second assignment. This is
   load-bearing: the asymmetry only appears because `sync_gpu_slots`
   (`draw_list_scene.cpp:593-645`) repairs mesh A during a draw *between* the
   two assignments. If both assignments land before a single
   `flush_draw_lists()` (`editor.cpp:796`), both records are written wrong
   identically and an equality assertion would pass with the bug present.
4. Assign the same material to mesh B; wait for another rendered frame; read
   both cached record `material_index` values via `query_draw_lists`.
5. Assert they are equal. From phase 4, additionally assert both equal the
   material's slot in the root's **draw-list** set - the set those cached
   records were written from. Not the forward set: the same material normally
   holds a different slot in each (D0).
6. Repeat with the assignment order reversed.

Red from phase 1, green at phase 4. Run with `ERHE_MCP_TEST_TIMEOUT_S=1` to keep
the suite near four minutes.

### V4 - Targeted manual checks

1. The original report: select Gold, edit base colour to red, drag onto the
   cube, then the icosahedron - both red; repeat in the opposite order - both
   red. Undo still reverts the colour edit (bug 2 out of scope).
2. **Live material editing (R5, the persistence risk):** drag every slider and
   colour picker in the Properties material section and confirm the viewport
   tracks them with no more lag than today - base colour, metallic, roughness,
   emissive, opacity, alpha cutoff, IOR, transmission, occlusion strength,
   normal scale, and each texture slot's rotation / scale / offset. Any field
   that does not update is a hole in D10's hash coverage.
3. **Texture re-bake:** edit a texture graph feeding a material in the scene and
   confirm the surface changes when the bake lands (D10's resolved-pointer term).
4. **Cross-scene assignment:** with two scenes open, drag a material from one
   scene's library onto a mesh in the other - the reference path where the
   target root's library never contained the material.
5. **Preview roots (D0, D3):** confirm they keep their current shape - one
   forward set each - and still render through the `Forward_renderer` bucket
   path, including the first thumbnail drawn after startup.
5a. **Both paths over one root (D0), the check this design most needs:** on a
   main scene root, render a frame containing both a draw-list-expressible
   colour pass and a draw-list-ineligible one (colour-blend override or force
   masks, `composition_pass.cpp:354`), plus its shadow pass. Every pass must
   show correct materials even though the same material holds a different slot
   in the two sets. Then edit that material and confirm **both** passes update:
   both sets dirtying on one edit is the property this split rests on.
6. **Preview churn:** open the inventory and hotbar so many material thumbnails
   render per frame; the material preview set stays at one material, the
   brush preview stays bounded by distinct brush materials, and the copy count
   in `Multi_copy_buffer` never exhausts. Check the draw lists' object counts
   stay bounded too - the brush preview re-registers its persistent mesh per
   thumbnail (D6).
7. **Brush preview:** both of its materials (`brush_preview.cpp:115-116`) render
   correctly through both `render_preview` overloads.
8. **Lightmap UV path:** `on_mesh_primitive_data_changed`
   (`scene_root.cpp:1461-1466`) drives `refresh_object_records`
   (`draw_list_scene.cpp:550-572`) repeatedly during baking/streaming -
   materials stay correct, slot count does not grow.
9. **ID rendering / picking** still selects the right object (null set).
10. **BRDF slice window** renders unchanged (R7, library-only `Material_set`).
11. **Async glTF import** of a large scene, and undo of that import - slots
    released, no leak, one buffer write per import rather than per frame.
12. **`src/example/example.cpp`** runs and renders textured glTF content (R7),
    and writes its material buffer exactly once.

### V5 - Build and behaviour sweep (once, end of plan)

- Builds: OpenGL, Vulkan, Quest, null/headless - Debug configurations.
- `erhe_scene_renderer_tests`, `erhe_graphics_tests` (including V6), the GPU
  tests where supported, `mcp_server_tests`, and the editor asset tests (async
  import).
- A/B screenshots (control pair, settled, DDGI off, viewport-crop diff) of a
  textured scene before and after, to confirm R8. Include a frame with tools
  and gizmos visible - the transform gizmo is debug-rendered and owns no
  material state, so it must be pixel-identical.
- An explicit run on the OpenGL **sampler-array** (non-bindless) path, the
  weakest heap path, and the one most changed by a persistent heap.
- A before/after measurement of the win R10 and R11 exist for: material bytes written
  and `Texture_heap::reset_heap()` calls per frame on a steady-state viewport of
  a textured scene. Expect both to fall to zero on frames with no material
  activity. Also record device memory: each new set reserves a copy buffer and a
  heap, and a root with a draw list carries two of them (D9 sizing note).
- Quest: only if the OpenXR path is touched; every launch behind a fresh
  explicit confirmation prompt.

### V6 - `Multi_copy_buffer` tests

In `erhe_graphics_gpu_tests`, not the deviceless `erhe_graphics_tests`: the copy
choice is driven by the device's frame-completion predicate and by `bind()`
stamping the current frame, so these need a `Device`, which the deviceless
target does not create. They bind the buffer as the uniform input of a small
compute shader that copies it into a storage buffer, so each assertion is about
what the GPU actually read. That target is built wherever a device comes up
(`_gpu_tests_supported`), which today excludes the null backend; the null
device answers the same `is_frame_completed()` contract, so extending the
condition to cover it is future work rather than a gap in what V6 states.

1. `first_commit_becomes_current` and `bind_before_commit_returns_false`.
2. `clean_frames_rebind_same_offset` - many binds, one commit, one offset.
3. `begin_write_never_returns_the_current_copy`.
4. `begin_write_refuses_a_copy_this_frame_bound` - bind two copies inside one
   still-open frame; a third write must go somewhere else again. Stated within
   one frame rather than across several because the test fixture retires each
   frame before returning, so "an unretired frame" can only be the open one.
   **The D9 stamping rule; fails against a commit-time stamp**, which would
   leave the first copy looking free while the frame still reads it.
5. `copies_recycle_when_updating_every_frame` - update and bind on every frame
   for 3 * copy_count frames; every `begin_write` succeeds and no copy is
   written while a non-completed frame bound it.
6. `write_larger_than_the_current_copy_grows_the_buffer` - `begin_write` past
   the current copy size; the committed copy carries the whole new payload and
   binds correctly.
7. `the_superseded_allocation_outlives_the_frames_that_bound_it` - bind a copy,
   grow before that frame completes, and assert the old `Buffer` is destroyed
   only once `is_frame_completed()` passes the frame that bound it. **The
   growth half of D9.**
8. `exhausting_the_copies_allocates_rather_than_stalling` - with every copy
   bound by the open frame, a further write reallocates instead of stalling or
   overwriting.

## 5. Risks

- **Persistence is a correctness trade, and D10 is where it is paid.** Every
  frame of every material edit now depends on the content hash covering exactly
  the bytes the record writer reads. A field added to `Material_data` and
  written to the record but not added to the hash produces a material that
  silently never updates - the exact bug in the context doc's section 1, with a
  new cause. Keep the hash function and the record writer adjacent in
  `material_buffer.cpp`, and add a comment on each saying the other changes with
  it. V4.2 exists to catch a miss; it is a manual check, which is the weakest
  link in this plan.
- **Two sets per main root is the newest and least reviewed decision.** The
  memory cost is a straight doubling on roots that render through draw lists -
  two copy buffers and two texture heaps each (phase 3 sizing, V5). The
  correctness cost is that every invalidation must reach *both*: a material edit
  that dirties one set and not the other produces a scene where the same mesh
  updates in one pass and not another, which is a worse symptom to diagnose than
  the bug being fixed because it is path-dependent. D10 runs per set and is
  driven by content hashes rather than notifications, which is what should make
  this safe - V4.5a is the check that it is.
- **A call site can take the wrong set.** `Scene_root::get_material_set()` and
  `Draw_list_scene::get_material_set()` return different objects with
  interchangeable types, so passing the forward set to
  `Draw_list_renderer::render` (or the reverse) compiles. The `ERHE_VERIFY`
  there (D5) catches
  that one direction loudly; the bucket direction has no equivalent guard and
  would render wrong materials silently. Worth considering distinct wrapper
  types before phase 4 if the verify proves insufficient.
- **Phase 4 is a large single commit.** The atomic index-space switch makes it
  unavoidable; it is the one phase where self-reviewing the diff matters most.
- **The persistent texture heap holds raw `Texture*` across frames.** Today a
  per-pass reset bounds the exposure to one pass. Now a destroyed texture stays
  referenced until the draw list is dirtied. The set's strong reference to each
  member `Material` is what bounds this: a material keeps its
  `Material_texture_sampler` alive, which keeps its texture reference alive. The
  case to watch is a texture destroyed *while* still referenced by a live
  material - which would already be a dangling-reference bug today, one pass
  wide instead of many frames wide.
- **Vulkan descriptor-set recycling (D2b) is a real, currently latent bug** that
  persistence would turn active. It lands in phase 3 - the commit that first
  creates a heap intended to outlive a pass, one phase before the renderers give
  theirs up.
- **Resource footprint.** Each Vulkan heap allocates pools of 8 sets x
  `max_textures` combined image samplers, and each set also holds `copy_count`
  copies of its material buffer. The count is now roughly *two per main scene
  root* plus one each for the two previews, the BRDF slice, the empty set and
  the example - against the four renderer heaps they replace, out of the eight
  4096-descriptor heaps the process holds today (phase 3). That is a real
  increase, not a wash. The buffer side scales with what a set actually holds (D9); the
  heap side is the fixed one, so the small sets need small `max_textures`
  (D2a). Measure before and after.
  Persistence helps strongly on the heap side: `reset_heap()` acquires one
  descriptor set per call (`vulkan_texture_heap.cpp:289-298`), so a clean frame
  acquires none at all, against one per pass today.
- **OpenGL sampler-array (non-bindless) path.** Handles are texture *units*,
  capped by `max_per_stage_descriptor_samplers`, and the record writer
  `ERHE_VERIFY`s that `allocate()` did not fail (`material_buffer.cpp:160`) - an
  overflow aborts. Note a per-set membership list is a slight *superset* of
  today's per-pass content, not a subset: today the forward pass's heap holds
  exactly the content-library materials, while a set holds library + object
  references, and the context doc's section 1 establishes the library does not
  contain every material a mesh uses. The excess is small (materials assigned to
  meshes but never registered in the library), but it is in the wrong direction
  on the one path that aborts on overflow, so V5 runs it explicitly.
- **The D10 hash pass runs every frame for every set.** O(members), on every
  set that exists - both sets of a root with a draw list, and the forward set of
  every root that has none - including the ones that never change. It is the price of R5
  and it replaces strictly more work than it adds, but it is a new per-frame
  cost that scales with material count rather than with anything the user did.
- **Slot growth from object references.** A material referenced by an object but
  absent from the library keeps its slot until the object is unregistered.
  Bounded by the distinct materials meshes actually use, which the buffer must
  hold anyway. Since the buffer grows on demand (D9) this costs device memory
  rather than correctness, so what to watch is the footprint of a set that
  accumulates materials no mesh renders.
- **`Device::is_frame_completed()` on three new backends.** The GL and Metal
  implementations are derived from existing completed-frame bookkeeping that was
  written for a different consumer; an implementation that is merely
  *conservative* (reports "not completed" too long) costs nothing but a deferred
  update, while one that is *optimistic* corrupts an in-flight frame. Write them
  conservative, and V6.4 pins the behaviour.
