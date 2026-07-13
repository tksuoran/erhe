# Content Library Ownership Plan

Status: IMPLEMENTED (2026-07-13), with one refinement found during
implementation - see "Implementation refinement: owning vs. reference
entries" below. The rest of this document is the reviewed proposal, kept
as the design rationale.

## Implementation refinement: owning vs. reference entries

The plan's strict invariant ("every library item is a member of exactly one
Content_library") holds for everything EXCEPT prefab template resources:
`erhe::graphics::Texture` owns its GPU storage via a non-copyable impl, so
prefab textures (and the materials sampling them) genuinely cannot be
duplicated per instancing scene. Instead of weakening hosting everywhere,
membership is split into two entry kinds
(`Content_library_node::is_reference`):

- OWNING entry (the default): the library owns the item; the item's
  `Item_host` is the library's owner (`Content_library::set_owner`, wired by
  `Scene_root`). Adding an item already owned elsewhere trips `ERHE_VERIFY`.
- REFERENCE entry (`is_reference == true`): a listing of an item owned
  elsewhere; never claims or releases the item's host. Used only by prefab
  instantiation / refresh (`Content_library_attach_operation` with
  `is_reference`, `replace_content_library_entries`), whose texture/material
  objects are deliberately shared by every instancing scene.

Consequences: scene-owned items (created, imported, copied) resolve their
scene via `get_item_host()` / `get_hosting_scene_root()`; prefab-shared items
stay non-hosted (active-scene fallback, non-hosted selection bucket), which
matches their shared, read-only-by-convention nature.

Other notable implementation facts:
- `erhe::Item_base` carries the host pointer (`set_item_host`; not copied on
  copy/clone); the default `get_item_host()` returns it. Node / attachment /
  Scene overrides are unaffected.
- Seeding uses `copy_content_library_folder` (+
  `Brush::make_shared_payload_copy`): the default scene and every new scene
  get their own Brush items sharing geometry / GPU primitive / collision
  payloads. `Scene_builder`'s template library is never owned by a scene,
  and its scene-population paths (floor material, torus chain / mesh-node /
  cube materials) use the target scene's library.
- Copy-to-scene: `copy_library_item_to_library` (collision suffix " (N)"),
  exposed as the `copy_library_item` MCP tool and the library-item
  "Copy to Scene" context menu.
- `Tool::get_default_material` ignores a selected material hosted by a
  different scene.
- Graph output nodes resolve their scene via the owning graph asset's host
  (fixes the ">1 scene open disables graph material output" issue).
- Known bounded cost: a brush whose geometry is still a lazy generator at
  copy time (not yet materialized in the template) copies the generator, so
  each scene's copy materializes its own geometry / primitive on first use
  instead of sharing the template's later materialization. One-time per
  brush per scene, never per-frame; brushes materialized before the copy
  share their payload as designed.

## 1. Problem statement

Content library items are potentially shared between scene-owned content
libraries, and a selected material does not resolve its scene host. Concretely:

- `Scene_commands::get_scene_root(Material*)` (`src/editor/scene/scene_commands.cpp:458`)
  relies on `material->get_item_host()`, but library items (materials,
  textures, brushes, graph assets, physics items) never have an `Item_host` --
  `erhe::Item_base::get_item_host()` returns `nullptr`
  (`src/erhe/item/erhe_item/item.hpp:329`) and nothing overrides it for these
  types. Every caller silently falls back to the *active* scene, which may not
  be the scene whose library actually contains the item. The Properties
  window's material texture combo (`src/editor/windows/properties.cpp:1719`)
  can therefore offer textures from the wrong scene's library.
- Cross-scene item aliasing exists but is inconsistent: brushes are shared by
  reference into new scenes' libraries
  (`share_content_library_folder`, `src/editor/scene/scene_commands.cpp:478`),
  the default scene's library *is* the `Scene_builder`'s library
  (`src/editor/editor.cpp:2469`), and prefab refresh pushes entries into every
  open scene's library (`src/editor/prefabs/prefab_library.cpp:606`). Yet
  save/load round-trips always produce independent per-scene copies -- the
  aliasing never survives a reload, so in-session identity and on-disk
  identity disagree.
- Per-scene selection has to treat library items as a "non-hosted" bucket
  (`src/editor/tools/selection_tool.cpp:388`): they persist across
  scene-scoped clears, and MCP `get_selection` cannot report a scene for them
  (`src/editor/mcp/mcp_server_scene_query.cpp:668`).
- Graph nodes resolve their target library via `get_single_scene_root()`
  (`src/editor/texture_graph/nodes/texture_material_output_node.cpp:240`,
  `src/editor/geometry_graph/nodes/geometry_output_node.cpp:219`), which
  returns null whenever more than one scene is open, silently disabling the
  feature (known issue, `memory-bank/progress.md`).

We want a model that works well, is neat and clear, avoids these pitfalls,
and "just works".

## 2. Current state (facts)

### Ownership topology

- Each `Scene_root` owns one `std::shared_ptr<Content_library>`
  (`src/editor/scene/scene_root.hpp:235`). No two *registered* scenes ever
  share the same `Content_library` object; sharing happens only at the item
  level.
- `Content_library` construction sites and their seeding strategy:

  | Site | Library | Item sharing |
  |------|---------|--------------|
  | Editor init (`editor.cpp:1593`) | default library; also handed to `Scene_builder` and reused as the default scene's library | n/a (this is the origin) |
  | `Scene_commands::create_new_scene` (`scene_commands.cpp:527`) | fresh | brushes shared BY REFERENCE from builder library; materials/physics materials created fresh |
  | glTF open scene (`parsers/gltf.cpp:770`) | fresh, empty | populated from file content only |
  | `Scene_open_operation` (`scene_open_operation.cpp:35`) | fresh | populated from file content only |
  | Tool scene (`tools/tools.cpp:251`) | fresh | none |
  | Material preview scene (`preview/scene_preview.cpp:55`) | fresh | none |

- Prefab refresh (`prefab_library.cpp:693`) iterates ALL open scenes and
  replaces/adds texture and material entries in each scene's library.

### Host resolution

- `erhe::Item_host` (`src/erhe/item/erhe_item/item_host.hpp`) provides
  `get_host_name()`, the `item_host_mutex`, and the per-host
  `hosted_selection` bucket used by per-scene selection. `Scene_root`
  implements it (via `erhe::scene::Scene_host`); `erhe::graph::Graph` does too.
- Scene items (Node, Mesh, Camera, Light, attachments) override
  `get_item_host()`; content library items do not, so they are non-hosted.
- `Content_library_node` wraps each item (`content_library.hpp:45`); the node
  hierarchy knows which library it is in, but neither the node nor the library
  records which `Scene_root` owns it, and the wrapped item knows nothing about
  its wrapper.

### Serialization

- The scene `.glb` is self-contained: plain materials/textures/skins/
  animations go through standard glTF tables; brushes via `ERHE_brushes`;
  graph textures/meshes via `ERHE_node_graphs`; physics library items via
  their own extensions (`parsers/gltf_extensions_export.cpp:156`). Export
  iterates only the exported scene's own library.
- Import always rebuilds a fresh per-scene library from file content.
  Consequence: items aliased across libraries in-session become independent
  objects after a save/load round-trip. Aliasing is an ephemeral, in-session
  optimization only.
- Only mesh-referenced materials are exported (unused library materials are
  dropped with a warning).

### Why the brush aliasing exists

Brushes are expensive to build (procedural geometry + GPU buffers,
`Scene_builder::make_brushes`). Sharing them by reference into each new
scene's library avoids rebuild cost. Note however that `Brush` already
supports cheap shallow copies: `Brush::make_with_material`
(`src/editor/brushes/brush.hpp:113`) creates a new `Brush` whose
`Brush_data` shares the `shared_ptr<Geometry>`, primitive, and collision
shape payloads. A per-scene brush *wrapper* costs almost nothing; only the
immutable payload is expensive.

## 3. Requirements

R1. Any library item must resolve its owning scene (or "editor-global")
    deterministically -- no active-scene guessing.
R2. In-session identity must match on-disk identity: if two scenes each have
    "their own" copy after reload, they must have their own copy in-session
    too (or sharing must genuinely persist).
R3. Scene `.glb` files stay self-contained (a saved scene opens correctly on
    another machine with no side files). This is an existing, deliberate
    property of the serialization design (`doc/scene_serialization.md`).
R4. Material edits stay scene-local (already deliberate:
    `create_new_scene` gives each scene fresh default materials precisely so
    edits do not leak across scenes).
R5. No per-frame cost regressions; brush payload duplication must stay off
    the table (GPU buffers shared).
R6. Per-scene selection, MCP tools, Properties, and the graph systems all
    resolve the same answer to "which scene does this item belong to".
R7. Tool scene and material preview scene keep working (they own private
    libraries and are not registered scenes).

## 4. Options

### Option (a): content libraries and their items clearly owned by scene

Every `Content_library` belongs to exactly one `Scene_root`; every library
item lives in exactly one library; items are hosted (their `Item_host` is the
owning `Scene_root`). All cross-scene aliasing is removed: new scenes get
shallow *clones* of the default brushes (shared immutable payload, per-scene
item identity), prefab refresh creates per-scene entries, and the default
scene gets its own library instead of aliasing the builder's.

- Pros:
  - R1 solved structurally: `material->get_item_host()` just works; the
    active-scene fallback in `get_scene_root(Material*)` becomes a genuine
    fallback for the null case (new material not yet in a library) instead of
    the common path.
  - R2 solved: in-session model becomes identical to the post-round-trip
    model. The aliasing that today silently degrades into duplication on
    reload is replaced by intentional duplication up front.
  - R4 preserved by construction. R3 untouched (serialization already assumes
    this model -- import/export code barely changes).
  - Selection: library items join the per-scene hosted buckets; scoped
    clear/Ctrl-A semantics become uniform; MCP `get_selection` reports a
    scene for every hosted item.
  - Smallest conceptual surface: one rule ("a scene owns its stuff"), no
    special cases per item type.
- Cons:
  - Brush wrappers are duplicated per scene (payload shared, so cost is a few
    hundred bytes per brush per scene -- acceptable).
  - Editing a shared default brush no longer propagates to other scenes. This
    is not a real loss: brushes are documented read-only library primitives,
    and propagation already broke on reload.
  - "I want this material in every scene" has no direct answer; it becomes an
    explicit copy (see Option (c) as a future extension).
- Verdict: strong candidate; matches what serialization already believes.

### Option (b): editor-global shared content library

One `Content_library` owned by the editor; all scenes reference it.

- Pros:
  - No duplication at all; no ambiguity about which library a combo shows.
  - Simplest possible mental model for a single-scene session.
- Cons:
  - Violates R3/R2 semantics: scene files carry their own materials/textures.
    Opening two saved scenes would merge their libraries into the global one
    -- name collisions, "which Red belongs to which scene", and closing a
    scene raises "what do we delete" with no good answer.
  - Violates R4: editing a material would affect every scene using it, which
    the current design explicitly avoids.
  - Export needs a referenced-items scan per scene for every category (today
    only materials are filtered that way).
  - Selection/host reporting gets *less* informative, not more (everything
    non-scene).
- Verdict: rejected. It fights the self-contained-scene-file design at every
  step.

### Option (c): split -- editor-global library for brushes, scene-owned for the rest, with "make shared" promotion

Brushes (read-only, procedural, expensive) live in one editor-global library;
materials, textures, graph assets, physics items stay scene-owned. Any
scene-owned item can be promoted to the global library to become shared.

- Pros:
  - Matches actual current semantics closely: brushes are already de facto
    global read-only; materials are already per-scene.
  - Scene-owned categories get proper hosting (same benefit as (a)).
  - The promotion mechanism gives users an explicit sharing story.
- Cons:
  - Brushes are persisted per scene (`ERHE_brushes`) and must remain so (R3).
    A global brush library then needs merge/dedup on every scene load (load
    the same scene twice -> duplicate brushes? match by name? by content
    hash?). This is real complexity with user-visible edge cases.
  - Loaded-scene brushes leaking into every other scene's palette is a
    behavior change of debatable value.
  - Promotion of mutable items (materials) to a global library reintroduces
    every Option (b) problem for exactly those items: whose edit wins, what
    exports where, what happens on scene close. "Shared" mutable items and
    self-contained scene files are fundamentally in tension -- the only
    consistent interpretation of promotion is "copy into other scenes",
    which does not need a global library at all.
  - Two ownership regimes to document, test, and keep consistent in every
    consumer (combos, selection, MCP, export).
- Verdict: partially right instinct (brushes vs. mutable assets ARE
  different), but the global half buys little and costs dedup + two regimes.
  The useful residue of (c) -- an explicit "copy this item to scene X /
  to all scenes" action -- can be built on top of (a) later without any
  global library.

### Option (d): content libraries as external glTF assets

Libraries become standalone `.glb` asset files; scenes reference library
items through the external-asset mechanism (as prefabs do).

- Pros:
  - Real persistent sharing: two scenes referencing `common.glb`'s "Steel"
    material genuinely share it, across sessions.
  - Aligns with the existing prefab/external-asset machinery.
- Cons:
  - Breaks R3 unless every referenced item is also embedded (at which point
    the reference is just provenance metadata, not sharing).
  - Foreign-tool interop degrades: stock importers already reject the
    externalAssets scene wiring (known limitation, `memory-bank/progress.md`).
  - Large scope: reference resolution, missing-file handling, edit-the-asset
    vs. edit-the-instance semantics, versioning. This is an asset-pipeline
    feature, not an ownership fix.
  - Does not by itself solve R1 -- items still need a host answer in-session.
- Verdict: rejected as the ownership model. Worth keeping in mind as a
  *future, additive* feature ("import from asset library" / "save selection
  as asset library"), which layers cleanly on top of (a) because (a) makes
  every imported item unambiguously owned by the importing scene.

### Option (e): something else -- hosted wrappers, aliased payloads

Keep `Content_library_node` per scene (as today) but make the *node* hosted
rather than the item, leaving item aliasing in place; resolve "which scene"
through the wrapper when the item itself is ambiguous.

- Pros: no duplication anywhere; minimal churn to seeding code.
- Cons: an aliased item is in N wrappers, so item -> host is *still*
  ambiguous -- exactly the original bug, one level removed. Selection selects
  items, not wrapper nodes, so per-scene selection stays broken for library
  items. R2 still violated.
- Verdict: rejected. It relabels the problem instead of removing it. The
  load-bearing defect is aliased item identity, and any option that keeps
  aliasing keeps the defect.

## 5. Recommendation

**Option (a): strict per-scene ownership, with structural sharing of
immutable payloads.** One invariant drives everything:

> Every `Content_library` has exactly one owner (`Scene_root`). Every library
> item is a member of exactly one `Content_library`, and reports that
> library's owner as its `Item_host`.

Cheap where it matters: brush duplication is wrapper-only (`Brush` instances
per scene, `Geometry`/`Primitive`/collision-shape payloads shared via the
existing `shared_ptr` members -- the `make_with_material` precedent). GPU
buffers are never duplicated.

The user-facing sharing want ("make this material available elsewhere") is
served by an explicit **copy-to-scene** action (drag-drop between library
trees, context menu, MCP tool) -- a copy, not an alias, so it round-trips
identically to how it behaves in-session. If a true shared-asset-file
workflow is ever needed, Option (d) layers on later without conflicting.

## 6. Implementation plan

### Phase 1: ownership plumbing

- `Content_library` gains an owner: `erhe::Item_host* m_owner` (in practice
  the owning `Scene_root`), set at `Scene_root` construction. Tool scene and
  preview scene set it too (they are `Scene_root`s already). Assert non-null
  once wired.
- `Content_library_node::add/remove/make` set/clear a host back-reference on
  the wrapped item (see Phase 2 for the storage mechanism) and `ERHE_VERIFY`
  that an item being added is not already a member of another library.
- `Scene_root::get_content_library()` unchanged; add
  `Content_library::get_owner()`.

### Phase 2: item -> host resolution

Design decision (pick one at review):

1. **Storage in `Item_base`**: add a nullable `Item_host* m_item_host` with a
   protected setter, and make the default `get_item_host()` return it.
   + Uniform, one mechanism for every current and future library item type;
     scene items keep their overrides (Node already stores its host).
   + 8 bytes per item; `erhe::item` already depends on `Item_host`.
   - Widens a core library class for an editor need (but hosting is already
     an `erhe::item` concept, so this is arguably where it belongs).
2. **Per-type mixin**: a small `Hosted_item` CRTP/base used by Material,
   Texture wrapper types, Brush, graph assets, physics items.
   + No cost for never-hosted types.
   - N types to touch, and foreign types (`erhe::primitive::Material`,
     `erhe::graphics::Texture`) each need the mixin injected into their
     inheritance -- more invasive than it looks.

Recommendation: (1). It matches how `Item_host` is already positioned in
`erhe::item`, and one pointer per item is negligible.

Then:

- `Scene_commands::get_scene_root(Material*)`: host first (now the common
  path); keep the active-scene fallback ONLY for items not yet in any
  library, and log when it triggers.
- `Properties` material texture combo resolves the library via the material's
  host -- wrong-scene texture lists disappear.
- Per-scene selection: library items now land in their host's
  `hosted_selection` bucket; the non-hosted bucket shrinks to genuinely
  homeless items. Verify scoped clear / Ctrl-A / command-target semantics
  against `doc/selection-improvements-plan.md`.
- MCP `get_selection` reports `scene_name` for library items.

### Phase 3: kill aliasing at the seeding sites

- `share_content_library_folder` -> `copy_content_library_folder`: for
  brushes, create per-scene `Brush` instances sharing payload (extend
  `Brush` with a payload-sharing copy, precedent `make_with_material`; note
  `Brush` is `Item_kind::not_clonable` -- either flip it to a custom clone or
  add an explicit `make_shared_payload_copy()`).
- Default scene stops aliasing the builder's library: `create_default_scene`
  builds a fresh library and copies the default brushes/materials into it the
  same way `create_new_scene` does. `Scene_builder`'s library becomes a pure
  template/palette source that no registered scene points at.
- Prefab refresh (`replace_content_library_entries`): verify it creates
  per-scene texture/material objects (not one object added to N libraries);
  fix to copy-per-scene where it aliases today.
- Add a debug sanity check (under `Scene_root::sanity_check()`): walk all
  registered scenes' libraries and `ERHE_VERIFY` no item appears in two.

### Phase 4: retire wrong-scene resolution in consumers

- Texture/geometry graph output nodes: replace `get_single_scene_root()`
  resolution with the owning graph's host (graphs are `Item_host`s and the
  graph asset itself is now a hosted library item -> its owning scene is
  known even with N scenes open). This closes the known ">1 scene disables
  material output" issue.
- Audit remaining `get_single_scene_root()` callers (`tool.cpp`, `create.cpp`,
  `physics_window.cpp`, `operations_window.cpp`, `asset_browser.cpp`,
  `selection_tool.cpp`) -- each either has a hosted item in hand (use its
  host) or is a genuine "which scene does the user mean" case (use the active
  scene, consistent with the per-scene-selection model).

### Phase 5: explicit sharing UX (the useful residue of option (c))

- "Copy to scene..." context-menu action + cross-library drag-drop = copy
  (this also resolves the `item_tree_window.cpp:1700` cross-scene DnD TODO).
- MCP tool `copy_library_item` (item id/name, source scene, target scene).
- Copies get fresh ids and their new scene's host; name-collision policy:
  suffix " (2)" like scene naming does.

### Phase 6: docs and notes

- Update `src/editor/content_library/notes.md`, `doc/scene_serialization.md`
  (ownership section), and the memory bank once implemented.

## 7. Open questions (for design review)

- Q1: `Item_base` host pointer (Phase 2 option 1) -- acceptable to widen
  `erhe::item` for this? (Recommended yes.)
- Q2: Should the `Scene_builder` template library remain a `Content_library`,
  or become a plain palette structure that cannot be confused with a scene
  library? (Recommended: keep type, mark owner as null/template, assert it is
  never registered or shown as a scene library.)
- Q3: Brush copy mechanism: flip `Brush` to clonable-with-custom-ctor, or add
  an explicit payload-sharing copy factory? (Recommended: explicit factory --
  `clone()` has generic semantics elsewhere and a payload-*sharing* copy is
  not a deep clone.)
- Q4: Does any current workflow rely on live cross-scene brush aliasing
  (e.g. editing a brush and seeing it change in another scene)? Believed no
  (brushes documented read-only), but confirm before Phase 3.
- Q5: Selection semantics for newly hosted library items: should Ctrl-A in a
  viewport now include the scene's library items? (Recommended no -- keep
  Ctrl-A scene-content only; library items become hosted for *resolution*
  purposes, selection entry points for them remain the library trees.)

## 8. Verification

- gtest: none of this is pure `erhe::*` logic except the `Item_base` host
  pointer (add a small case to `erhe_item_tests`).
- Headless MCP smoke (fresh session, two scenes open):
  - create material in scene B while scene A is active via hierarchy focus ->
    `get_selection` reports the material under scene B; Properties texture
    combo (via `get_scene_root(Material*)` path) resolves scene B's library.
  - texture graph material output works with two scenes open.
  - save/load round-trip of both scenes -> library diff clean, no shared-item
    verify trips.
  - copy-to-scene MCP tool: copy material A->B, edit copy in B, verify A
    unchanged, round-trip both.
- Interactive: cross-library drag-drop copy; default scene + new scene brush
  palettes both populated; brush placement in both scenes.
