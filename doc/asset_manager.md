# Asset manager

This document describes the asset system as implemented: the design, the
runtime model, the wire format, and the implementation choices behind them.
Code lives in `src/editor/assets/` (`asset_key`, `asset_reference`,
`asset_manager`, `asset_workflow`, `asset_paths`); the wire format is
specified in
[`doc/gltf_extensions/ERHE_asset_reference.md`](gltf_extensions/ERHE_asset_reference.md).

## Requirement

- An **asset reference** that holds (1) the identity of an asset - the
  container file plus the specific asset within it - and (2) a pointer to
  the asset once it is loaded.
- An **asset manager** that guarantees every asset is loaded **at most
  once**: two references to the same asset always resolve to the same
  object. The manager loads assets on request and unloads them on request;
  an unload request **fails while the asset has users**.

## Core model

### Concepts

- **Asset container file**: any glTF file. Not a special file type - a
  scene `.glb`, a single-material library file written by "make external",
  a prefab: all are containers. Identity is the normalized file path (plus
  a session identity before a scene's first save, see Container records).
- **Asset**: a thing *defined* in exactly one container. The managed asset
  types are **brush, material, animation** (`Asset_type`; `mesh` exists as
  a scene-local resolution convenience for geometry-graph source nodes and
  is not manager-owned). The container where an asset is defined is its
  **definition site** - its serialization home.
- **Entry kinds**: a glTF file contains two kinds of asset entries:
  - **definition** - full data; this file is the asset's home;
  - **reference** - a stub plus a key; the asset's home is another
    container.
  "External vs internal" is a property of the *entry in a file*, not of the
  asset: every asset is internal to exactly one container and external to
  every file that references it.

### The single-loader axiom

**No code path materializes an asset-typed object except the
Asset_manager.** Scene open, glTF import, prefab loading, cross-file
reference resolution, and in-editor creation ("Create Material") are all
clients of the same manager entry points. The manager is not a cache bolted
onto some loads - it is the only loader and the registry of every live
asset.

Consequence: **exactly one copy.** At most one live runtime object exists
per asset identity. Ownership splits into two orthogonal notions:

- **Runtime ownership** (lifetime): always the Asset_manager, through its
  container records.
- **Definition site** (serialization): the container file whose save
  writes the asset's full data.

A content-library entry does not mean "I own this object"; it records the
relationship instead: a *definition entry* ("defined in my file - serialize
fully on my save") or a *reference entry* ("defined elsewhere - serialize
as stub + key"). Lifetime belongs to the manager in both cases.

### Why the axiom (the two-loader failure mode)

If scene open created its own asset objects while the manager loaded
cross-file references independently, the same definition could exist twice:
open `warehouse.glb` as a scene (object M1, live-edited) while scene B's
reference to `(warehouse.glb, material, "RustySteel")` parses the file
again (object M2). Edits to M1 are then invisible to B; disk state after
saving disagrees with M2; use counts split across two objects - the asset
abstraction silently lies. Routing *all* materialization through the
manager makes this impossible by construction, in both orderings:

- **Scene first**: opening `warehouse.glb` registers each contained
  definition with the manager (the content-library attach hooks mirror
  asset-typed entries into the scene's container record); scene B's later
  reference resolves to the same object.
- **Reference first**: the manager already owns the object from B's
  reference; opening `warehouse.glb` as a scene *adopts the loaded
  container record* - the scene open reuses the record's parse instead of
  parsing again, and its library entries are built around the existing
  objects. The scene then live-edits exactly what B renders.

## Identity

### Asset_key

```
Asset_key { scope, type, path, uid, name }
```

- `scope` (`Asset_scope`): `builtin` (procedural item registered with the
  manager, e.g. the Scene_builder palette brushes - palette names are a
  persistence contract), `scene_local` (by-name lookup against the manager
  registry and open scene libraries; the legacy name-only form used by
  inventory slots and graph nodes), or `file` (asset defined in a container
  file).
- `path`: the identity of the CONTAINER FILE the asset is defined in -
  which glTF file to look inside; `uid` / `name` then identify the asset
  within it. File scope only (empty for builtin and scene_local keys); a
  portable forward-slash string. All path conversions go through
  `asset_paths.hpp` (`normalize_asset_path()` for the runtime registry's
  weakly-canonical form, `asset_path_to_string()` for serialization), so
  normalization cannot drift between subsystems.
- `uid`: the glTF 2.1 unique ID - the primary identity within a container.
  Every item-backed object in an erhe-authored file carries a stable uid,
  generated once at export and stored back on the item, so re-saving never
  changes uids. A clone or copy deliberately does NOT inherit the uid: a
  copy is a new asset.
- `name`: the fallback identity; per the glTF 2.1 rules a name serves as an
  identifier only when it is unique within (container, type).

There is deliberately **no array-index field**: positional addressing
silently re-binds after the target file is reordered; it is not identity.

### Resolution precedence and the ambiguity policy

Within a container: a `uid` match wins outright; otherwise a unique
conforming name matches (with a logged warning when a stale uid fell back
to the name); otherwise **resolution fails with a clear error** naming the
container and the ambiguity (e.g. "name 'X' is ambiguous in container 'Y'
(2 matches); addressing requires a uid"). There is no tie-breaking and no
guessing. The remedies are stamping uids into the file (any erhe export
does) or renaming in the authoring tool.

Keys **self-heal upward**: a key resolved via the name fallback learns the
asset's uid and re-persists with it, so stored keys migrate to uid
addressing organically on each save.

## The reference type: Asset_reference

`Asset_reference` is the non-template usership handle (typed access via
`get_as<T>()`). **Holding a resolved Asset_reference IS being a registered
user** of the asset: the special members maintain the bookkeeping exactly
(copy re-registers, move re-points the registration, destruction
releases), so unload refusals can name every holder.

- **Lazy**: constructing never loads; `resolve()` acquires on first use;
  `get()` returns null while unresolved. Callers keep their existing
  fallbacks (default material, empty slot).
- File-scope load failure **latches** `failed` so a broken container is
  not re-hit every frame; `reset_resolution()` allows retry. Scene-local
  misses do not latch (callers drive retry, e.g. the per-frame inventory
  slot resolve).
- `adopt(manager, item)` records exactly a given live object (make_key +
  usership) instead of re-resolving by name - drag-and-drop and similar
  "I already have the object" paths use it, because a name re-resolution
  could bind a *different* same-named object.
- Every reference carries a **user label** ("inventory grid slot 3",
  "scene 'B' library material 'Steel' (reference)", "undo stack: material
  change", ...) so refusals are actionable.

## The manager

### Container records

The registry maps a session-unique container id to an
`Asset_container_record`; a path index maps bound paths to records. One
record type covers three flavors:

- **Scene records**: every scene registered with the editor gets a record
  at registration. A never-saved scene's record has a session identity and
  no path; the first save binds it to the path, save-as re-homes it. The
  record's **scene entries** hold one strong reference per owning
  asset-typed library entry - the manager's runtime ownership of the
  scene's assets. Resolution against a scene record reads names/uids LIVE
  from the items (scene content is mutable), with the same
  uid-then-unique-name precedence.
- **Parsed containers**: `get_or_load_container()` parses a glTF once
  (free root node, no holding scene; container node trees are never
  rendered) and builds per-type resolution maps, validating
  identifiability up front - assets without a uid whose names are missing
  or duplicated are recorded as errors and cannot be acquired. File-scope
  types served from a parsed container are **material and animation**
  (brushes materialize through the scene editor-state import and keep
  scene-local keys). A path currently open as a scene returns that scene's
  record - no second parse, by construction.
- **Authored material containers**: created by "make external" around THE
  live object - no parse, no glTF data. Only these (and reopened parsed
  containers whose file holds nothing but material-domain content) may be
  rewritten by `save_container`; a container parsed from an arbitrary glTF
  may hold content the manager does not manage (nodes, meshes, cameras),
  which a materials-only rewrite would silently destroy, so it stays
  read-only with a clear reason.

Nested references are deliberately **not resolved when loading a
container**: a contained reference proxy is served with its stub fallback
appearance and a warning. This also makes cross-file reference cycles
impossible by construction - only scene open/import resolves references,
and container loads never recurse.

### Acquire and create

- `acquire(key)` is the only load path: builtin keys hit the builtin
  registry; file keys load the container on demand and resolve inside it;
  scene-local keys resolve by name through the registry and open scene
  libraries. Repeated acquires of the same identity return THE copy.
- `create<T>(defining_scene, args...)` is the in-editor creation funnel:
  every site that brings a new managed asset into existence constructs it
  through the manager, naming the scene whose container record is the
  asset's defining container. The caller adds the object to the scene's
  content library; the library attach hooks give the scene record its
  strong entry and the library entry its usership.
- `make_key(item)` derives the authoritative key for a live object:
  builtin scope for palette items; file scope for assets of parsed and
  authored containers; **file scope with `{path, uid, name}` for materials
  and animations defined in a path-bound scene record** (exact, durable
  identity - a persisted key auto-loads its container at startup); scene
  records without a path (never saved) produce scene-local keys, since no
  durable path exists to serialize.

### Users and unload

- `get_users(key)` / `request_unload(key)`: unload is **refused while any
  asset of the container has declared users**, and the refusal names each
  one ("in use by: scene 'B' library material 'Steel' (reference), 
  inventory grid slot 3"). Unload granularity is the container: a parsed
  container's data pins every contained object, so unloading a single
  asset of a live container could never actually release anything.
- The **undo/redo history** blocks unload two ways: operations that edit
  assets hold adopted `Asset_reference` userships with per-operation
  labels ("undo stack: material change"), and
  `Operation::collect_item_references()` walks retained node subtrees'
  mesh-primitive materials for indirect pins - refused with the collective
  label "undo/redo history (clear history to release)".
- A **dirty** container (live asset edits not yet written back) refuses
  unload with "unsaved changes"; an explicit `discard` flag drops the
  edits.
- A successful unload **verifies exclusivity**: after the record drops, a
  managed asset that is still alive means a raw `shared_ptr` bypassed
  `Asset_reference` - logged loudly as an "undeclared asset user". The
  one-copy invariant is runtime-checkable.
- There is **no automatic eviction**: zero-user assets stay loaded until
  an explicit unload or shutdown. Observability over eviction policy.

### Dirty tracking and saving

Asset-edit paths (material change, animation edit/keying operations) mark
the edited asset's **defining** container dirty. A successful scene save
clears the scene's own record. `save_container(path)`:

- a record open as a scene delegates to the scene save;
- an authored material container rewrites from its live objects;
- a reopened pure-material container rewrites with texture sources served
  from the parse's retained encoded image streams;
- anything else is refused with the reason.

### Threading

The manager is **main-thread only** (asserted): container parsing uploads
textures through the frame command buffer, and asset resolution walks
live scene state. The lazy null-until-resolved reference shape means async
loading could be added behind the same API without changing call sites,
but no worker path may mutate manager state or resolve references today.

## The ownership flip: content library integration

Asset-typed items (brush, material, animation) **never claim an
`Item_host`**: the content-library claim/release walks skip manager-owned
asset types, so "asset host is always null" replaced a family of
host-comparison checks. Consequences and rules:

- **Classification is recorded, never derived from hosting.** A library
  entry is a definition when the manager records this scene's container
  record as the asset's defining container
  (`Scene_root::is_asset_definition`, backed by the manager); otherwise it
  is a reference entry, and `Content_library_node::asset_key` records the
  defining container when known. Deriving definition-vs-reference from
  `get_item_host()` would misclassify everything after the flip - scene
  saves would write stubs instead of data. A regression test (the
  "definitions-full-data" tripwire in `scripts/scene_roundtrip_verify.py`)
  guards exactly that cliff.
- **Ownership never comes from mesh registration.** When a mesh enters a
  scene, materials the scene defines resolve to their existing owning
  entries; any other material (another scene's definition, a container's
  asset, a prefab template resource) is listed as a reference entry with
  its file-scope key when one exists. An unhosted material that is managed
  nowhere and listed nowhere is a missing explicit registration at its
  creation site: a loud warning plus a non-owning reference listing -
  rendering keeps working, but a definition must never appear as a side
  effect.
- **"The scene that owns this asset" is a manager lookup**
  (`get_defining_scene_root` / `is_defined_by` / `is_hosted_or_defined_by`)
  instead of a host comparison. Cross-scene use of an asset is accepted
  when its defining container is path-bound
  (`is_cross_scene_referenceable`) - exactly the condition under which a
  durable file key exists, so the reference can always be serialized and
  re-resolved. Session-only scene assets are rejected for cross-scene use.
- **Scene close issues a courtesy unload** of the scene's own container
  record after every close subscriber has run: closing a scene frees its
  assets exactly when nothing else uses them, while a refusal (slots,
  other scenes' reference entries, debug holds, undo history) is normal
  and leaves the container loaded - the record survives the scene as a
  plain loaded container and keeps resolving.
- **The scene-close leak watchdog splits by kind**: nodes and other
  scene-lifetime items must die with the scene (a survivor is a leak
  warning); asset-typed survivors that the manager intentionally keeps
  (container records, declared userships, inventory pins) report as info
  ("intentionally pinned by the asset manager"). Surviving animations
  drop channel targets that point at the closing scene's nodes, so an
  animation kept alive by a slot cannot pin dead nodes.
- **Textures stay scene-hosted** in the current model: a surviving
  manager-owned material keeps its texture references alive as a
  transitive pin (blessed by the watchdog); rendering stays correct, GPU
  memory stays resident until the container unloads.

## Wire format

Cross-container references serialize per
[`ERHE_asset_reference`](gltf_extensions/ERHE_asset_reference.md):

- **Export**: a library material reference entry with a file-scope key
  exports as a **name-only stub material** carrying
  `ERHE_asset_reference {file, uid}`, where `file` indexes the glTF 2.1
  `files` array naming the container (URI relativized against the
  exported file's directory). The stub degrades to a legal default
  material in loaders without the extension. Proxies carry no
  `ERHE_material` payload and are excluded from uid stamping - stamping
  would write a generated uid back onto the *shared* item, corrupting the
  identity every file key depends on. Definition entries keep exporting
  full data; a reference entry without a durable file key falls back to
  full data with a warning (no data loss, but the re-import holds an
  independent definition). Self-references are prohibited and fall back
  the same way. Emitting any proxy (or prefab externalAsset) declares
  glTF 2.1 via the `files` array.
- **Import** (scene open and glTF import): materials carrying the
  extension resolve through `acquire` - uid first, unique-name fallback,
  ambiguity is a clear error. On success THE managed object substitutes
  the stub everywhere in the parse, including index-based consumers
  (brush and node-graph payloads reference materials by glTF index). On
  failure (missing container, ambiguity, type mismatch) the stub is
  KEPT with a warning, and either way the library lists the material as a
  reference entry carrying the key - **the key is re-emitted on every
  save, never silently dropped**, so a broken reference survives
  round-trips and resolves again once the container returns.
- The exporter is otherwise **lazy**: only materials referenced by
  exported meshes (plus brush materials and explicitly requested extras)
  are written; unused library materials are not persisted.

## Workflow verbs

Implemented in `assets/asset_workflow.{hpp,cpp}` (current scope:
materials), surfaced through content-library / asset-browser context menus
and MCP tools:

- **Make external** (`make_material_external`): moves a definition out of
  the scene into a fresh asset container file. The file is written first
  (the export stamps the uid the key needs), the manager re-homes THE live
  object into a path-bound authored record - meshes, slots and references
  keep pointing at the same object - and the scene's library entry flips
  to a reference carrying the file key. The next scene save writes a
  proxy.
- **Make internal** (`make_material_internal`): the escape hatch when one
  scene wants a local tweak of a shared asset. Copies the referenced
  material's data into a manager-created scene-owned definition (no uid -
  a new asset), swaps the scene's mesh primitives to the copy, and
  replaces the reference entry with an owning one. Edits stop reaching
  other users of the shared object; other holders (slots, tools, other
  scenes) deliberately keep the shared object. Brushes pointing at the
  shared object warn and keep sharing.
- **Reference into scene** (`reference_material_into_scene`): acquires a
  keyed material and lists it as a reference entry via an undoable library
  attach. Gated by `is_cross_scene_referenceable`.
- **Import-as-reference**: `import_gltf(..., materials_as_references)`
  acquires every parsed material from the source container instead of
  keeping imported copies; the library lists reference entries.
  Import-as-copy stays the default.

**Undo boundary**: make external and make internal are NOT undoable - they
alter container files and cross-container manager state. The
reference-into-scene attach is undoable (it is a plain library attach).

## Clients

Everything that holds an asset across frames holds an `Asset_reference`
(and is therefore a declared, named user):

- **Inventory / hotbar slots**: per-slot brush and material references;
  persisted as asset keys in the autosaved settings (file-scope keys
  auto-load their container at startup, so slots fill without opening the
  defining scene). Slots intentionally pin assets across scene close -
  a persistent inventory by design.
- **Tool state**: the brush tool's active brush and the material paint
  tool's material (set via `adopt`).
- **Geometry/texture graph nodes**: source and output asset references;
  keys are written on save even while unresolved, so an unresolvable
  reference survives save/load cycles.
- **Undo/redo operations**: adopted userships plus the indirect-pin walk
  (see Users and unload).
- **Content-library entries**: every asset-typed entry (definition and
  reference) declares a usership, so scene libraries appear by name in
  unload refusals.

## Observability and verification

The in-editor MCP server exposes the manager for headless verification:
`query_asset_manager` (assets with user labels, container records with
per-type counts / dirty / open-as-scene state, debug holds),
`acquire_asset` / `release_asset` (named debug holds - real declared
users), `unload_asset` (with the refusal payload), `save_container`,
`load_asset_file`, `set_tool_asset`, `set_inventory_slot`, the R7 verbs
(`make_asset_external`, `make_asset_internal`,
`reference_asset_into_scene`) and `import_gltf`'s
`materials_as_references`.

Standing checks:

- The **scene-close leak watchdog** logs a "scene-close leak" warning for
  every tracked item of a closed scene still alive 60 frames later
  (intentional pins report as info). Any warning is a bug; verification
  runs grep for it.
- The **undeclared-user check** after every successful unload (see Users
  and unload).
- `scripts/scene_roundtrip_verify.py` covers the wire format end to end,
  including the definitions-full-data tripwire (definitions must never
  save as stubs) and an asset-references section (proxy shape, resolve in
  both directions, missing-container fallback with key survival).

## Current restrictions

- Managed types are brush, material, animation; the workflow verbs and the
  wire format currently cover **materials**. Textures, graph assets,
  physics materials and skins remain scene-hosted.
- File-scope acquisition from parsed containers serves materials and
  animations; brushes keep scene-local keys (they materialize only through
  the scene editor-state import).
- Unload granularity is the container.
- Containers do not resolve their own references (stub fallback + warning;
  see Container records) - reference chains do not recurse and cycles
  cannot form.
- `save_container` rewrites only scene containers, authored material
  containers, and reopened pure-material containers.
- Renaming or moving a container file on disk leaves stored keys pointing
  at the old path; affected references fall back to their stubs with a
  warning until the file returns or the references are re-made.
