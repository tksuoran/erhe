# glTF scene prefabs in erhe -- plan

Status: phases 0-5 implemented and verified 2026-07-10 (commits 2199f0ef,
b9264190, bad5895d, 4225b7d3, b4a1de9c + the phase 5 commit). Phase 6 items
1 and 2 implemented 2026-07-11 as the "sealed instances" editing model (see
below); items 3-5 remain. Written 2026-07-10.

### Editing model decision (2026-07-11): sealed instances ("option 2")

Prefab instances are sealed: the subtree under a `Prefab_instance` carrier
node is not editable in the containing scene. Editing a prefab requires
opening its source glTF as a scene (`open_scene`); saving it back
(`save_prefab_scene` / File > Save Prefab / MCP `save_prefab`) writes the
source file and reloads the prefab, refreshing every instance in every
scene. `Prefab_library::reload` tracks prefab->prefab references recorded
during template loads and rebuilds dependent templates in dependency order,
so editing a nested prefab propagates through every prefab that references
it. Instance refresh drops all carrier children and re-clones (consistent
with save/export, which never persist instance subtrees) while preserving
the carrier's transform, name and flags; it is deliberately not undoable
(instances are projections of the source file, not scene edits). The
alternative "option 1" model (editing through an instance writes back to the
prefab) can later be layered on top as edit-forwarding into the opened
prefab scene; it shares all of this propagation machinery.

Goal: make glTF scenes usable as *prefabs* in erhe -- a glTF file can be
instantiated (multiple times) inside another scene, instances stay live
references to their source file, and scenes containing instances can be
exported as glTF that itself references the sub-scenes as external assets
(so glTF scenes nest inside other glTF scenes on disk, not just in memory).

## Background

### glTF 2.1 features involved

The glTF 2.1 proposal (KhronosGroup/glTF#2585) contains three features that
map directly onto this:

- **Unified File References** (KhronosGroup/glTF#2590): a top-level `"files"`
  array that references external/embedded resources of any type. Each entry
  has a required `mimeType` and a `uri` (or embedded data).
- **External Assets** (KhronosGroup/glTF#2586): a top-level `"externalAssets"`
  array; each entry references a glTF file through the `files` array
  (`"file": <index>`). A node instantiates an external asset via
  `"externalAsset": <index into externalAssets>`. Cyclical references between
  glTF assets are strictly prohibited by the spec.
- **Multiple Scene Deprecation** (KhronosGroup/glTF#2591): future glTF files
  should contain exactly one scene; importers stay backwards compatible.

Example (from the fastgltf fork's test asset):

```json
{
    "asset": { "version": "2.1", "minVersion": "2.1" },
    "files":          [ { "mimeType": "model/gltf+json", "uri": "sub_model.gltf" } ],
    "externalAssets": [ { "name": "sub_model", "file": 0 } ],
    "nodes": [
        { "name": "RootNode", "children": [1] },
        { "name": "SubModel", "externalAsset": 0 }
    ],
    "scene": 0,
    "scenes": [ { "nodes": [0] } ]
}
```

The explainers deliberately leave some details open (whether an
`externalAsset` node may also have children/mesh, and which scene of the
referenced asset is instantiated). erhe adopts these interpretations:

- The **default scene** (`"scene"`) of the referenced asset is instantiated;
  warn if the referenced asset has multiple scenes (deprecated in 2.1).
- The instancing node's transform applies to the instantiated content
  (ordinary node-hierarchy semantics; instantiated roots become children).
- On **export** erhe writes instance nodes with `externalAsset` and no
  children/mesh/camera. On **import** erhe is tolerant: if such a node also
  has children, both the children and the instantiated content are kept.

### What the fastgltf fork provides (tksuoran/fastgltf, commit ce078c16 / 234ffe0)

erhe's `CMakeLists.txt` already pins `GITHUB_REPOSITORY tksuoran/fastgltf`,
`GIT_TAG 234ffe08...`. The fork adds:

- `fastgltf::Asset::files` (`fastgltf::File { DataSource data; string mimeType; string name; }`)
  and `Asset::externalAssets` (`fastgltf::ExternalAsset { size_t fileIndex; string name; }`).
- `fastgltf::Node::externalAssetIndex` (`Optional<size_t>`).
- `Options::LoadExternalFiles` to eagerly load `files` bytes (mirrors
  `LoadExternalBuffers`); without it, URI entries stay as URIs.
- `MimeType::GltfJson` / `MimeType::GltfBinary`, `AssetInfo::minVersion`,
  `Error::UnsupportedVersion` for assets requiring > 2.1.
- Exporter support: `writeFiles` / `writeExternalAssets`, node
  `externalAsset` output, `Exporter::setFilePath`, `ExportResult::filePaths`.
- `Category::Files | Category::ExternalAssets` are part of `Category::All`,
  so erhe's existing `loadGltf` calls already parse them.

**fastgltf explicitly does NOT recursively parse referenced assets and does
NOT detect cross-file cycles** -- both are the application's (erhe's) job.
This division is deliberate and this plan keeps it: `erhe::gltf` surfaces the
data for one file; recursion, caching, and cycle detection live in the editor.

### Relevant erhe infrastructure today

- Import: `editor::import_gltf` (`src/editor/parsers/gltf.cpp:90`) parses via
  `erhe::gltf::parse_gltf` (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp:2256`)
  into a temp scene, finalizes meshes into the shared `Mesh_memory`
  (`primitive.make_renderable_mesh`, `gltf.cpp:212`), then inserts everything
  via undoable operations. **No caching: every import is a fresh parse.**
- Reusable-template precedent: `Brush::make_instance` + `place_brush_in_scene`
  (`src/editor/brushes/brush.cpp:320,380`) -- instances share the cached
  `Primitive` (GPU ranges), insertion goes through the operation stack.
- Deep clone: `Node::clone()` (`erhe_scene/node.cpp:65` + `Hierarchy` copy at
  `erhe_item/hierarchy.cpp:16`) deep-copies a subtree including attachments;
  `Mesh` clones keep their `shared_ptr<Primitive>`, so clones share GPU
  buffers. The clipboard (`src/editor/tools/clipboard.cpp:133`) already
  instantiates clones through `Item_insert_remove_operation`.
- Native persistence: `.erhescene` bundle
  (`src/editor/scene/scene_serialization.cpp`), `scene.json` version 5 +
  `data.glb` + `.geogram` files; has a per-load-call glTF parse cache.
- There is **no existing prefab concept** anywhere in the repo.

## Design overview

Three new pieces, layered so each phase is independently useful:

1. **`Prefab_library`** (editor part, app-wide): cache of parsed glTF
   templates keyed by canonical absolute source path. Each entry (`Prefab`)
   owns the parse result: a template root node living in a private, never
   rendered holding scene, with meshes finalized into `Mesh_memory` once.
   Handles recursive loading (a prefab source may itself reference external
   assets) with an active-load-path stack for cycle detection.
2. **`Prefab_instance`** (a `Node_attachment`): marks a node as the root of a
   prefab instance and records the source reference (canonical path + display
   name). It is what import writes, export and `.erhescene` serialization
   read, clipboard cloning preserves, and the UI styles.
3. **Instantiation = `Node::clone()`** of the template root's children under
   the instance node, inserted through the operation stack (mirroring
   `place_brush_in_scene`). No per-instance GPU upload; primitives, materials
   and textures are shared with the template.

## Phases

### Phase 0 -- groundwork (small)

- [x] Pin erhe to the fork (`CMakeLists.txt` -> `tksuoran/fastgltf` @
  `234ffe08...`). Already done.
- Update `src/erhe/gltf/notes.md`: the "fastgltf_khr_physics.patch applied by
  CPM" note is stale (fork replaces the patch); document the fork and the
  glTF 2.1 subset it carries.
- Build all configurations that CI covers; run an existing glTF import
  (headless MCP `import_gltf` + screenshot) to confirm the fork bump is a
  no-op for 2.0 content.

Verification: clean build, unchanged import behavior.

### Phase 1 -- `erhe::gltf` surfaces glTF 2.1 data (no behavior change)

Library-only changes in `src/erhe/gltf/`:

- `Gltf_data` gains:
  - `files`: vector of `{ name, mime_type, resolved_path (empty when
    embedded), bool embedded }` -- URIs resolved against the glTF directory.
    Do not pass `Options::LoadExternalFiles`; erhe wants paths as cache keys,
    not eagerly loaded bytes.
  - `external_assets`: vector of `{ name, file_index }`.
  - `node_external_asset`: per-node `optional<size_t>` parallel to
    `Gltf_data::nodes` (erhe `Node` has no glTF-specific fields; keep the
    association in the parse result).
- `parse_node` (`gltf_fastgltf.cpp:1888`): when
  `fastgltf::Node::externalAssetIndex` is set, still create the (empty)
  carrier `Node` with its transform and record the mapping. No recursion here.
- `Gltf_scan` gains `files` / `external_assets` name lists so the asset
  browser can badge 2.1 composite files.
- `gltf_none.hpp` mirrors the API additions; update `notes.md`.

Verification: parse the fork-style `external_asset.gltf` test file in a
scratch scene; `Gltf_data` contains the mapping; existing imports unchanged.

### Phase 2 -- editor `Prefab` concept + manual instantiation (works with any glTF, 2.1 not required)

The core editor feature, useful on its own ("place this .glb here as an
instance, N times, parsed once"):

- `Prefab_library` editor part (constructed like other parts, explicit ctor
  refs per the App_context rule):
  - `get_or_load(canonical_path) -> shared_ptr<Prefab>`: parse via
    `erhe::gltf::parse_gltf` into the library's holding scene, finalize
    meshes (reuse/extract the finalization block from
    `parsers/gltf.cpp:172-216` into a shared helper so import and prefab
    loading do not diverge), keep `Gltf_data` alive in the `Prefab`.
  - Active-load-path stack for cycle detection (used in Phase 3).
- `Prefab_instance : Node_attachment` with source path + name;
  `clone_attachment()` support so clipboard copy/paste of an instance keeps
  working.
- Instantiation helper `instantiate_prefab(prefab, parent, transform)`:
  create instance node + `Prefab_instance`, clone template children under it,
  register the template's materials/textures/skins/animations in the target
  scene's `Content_library` (idempotent, keyed by existing
  `Gltf_source_reference`), wrap in a `Compound_operation` on the operation
  stack (mirror `place_brush_in_scene`, `brush.cpp:380`).
- Entry points:
  - Asset browser context menu: "Instantiate as prefab" next to the existing
    "Import" (import copies content in; instantiate references it).
  - MCP tools: `instantiate_prefab` (path, parent, transform),
    `list_prefabs`. These make the headless verify loop possible.
- Content library: add a `prefabs` category node per `Content_library` that
  lists prefabs used by that scene (entries point at the shared
  `Prefab_library` cache).
- Scope limitation, explicit: prefabs containing **skins/animations** clone
  without remapping node targets (see Risks); log a warning and treat such
  prefabs as static for now. Prefab-internal cameras are skipped; lights are
  instantiated. The "add default camera/light when missing" logic in
  `import_gltf` must not run for prefab instantiation.

Verification (headless MCP): instantiate the same .glb twice + screenshot;
log shows one parse; undo/redo removes/restores an instance; clipboard
copy/paste of an instance yields a working third instance.

### Phase 3 -- import glTF 2.1 external assets (recursive instantiation)

- `editor::import_gltf` (and the `Prefab_library` loader, so nesting works):
  after parse, for each entry in `Gltf_data::node_external_asset`:
  - Resolve `external_assets[i].file_index` -> `files` entry -> canonical
    path. Embedded (data URI / GLB-packed) sub-assets: reject with a clear
    error for now (tracked as deferred work).
  - `Prefab_library::get_or_load(path)` -- recursion happens here because
    the prefab's own load walks its own `node_external_asset`. The active
    load stack turns cycles into an import error naming the cycle (spec
    prohibits cycles; do not silently break them).
  - Instantiate under the carrier node (Phase 2 helper, minus the
    operation-stack wrapping when running inside an import that already
    builds one compound operation); attach `Prefab_instance` to the carrier.
- `scan_gltf` consumers (asset browser tooltip): show referenced external
  assets.
- Test assets: add `res/`-side (or test-data) equivalents of the fork's
  `external_asset.gltf` / `sub_model.gltf`, including one nested two levels
  deep and one deliberate cycle pair.

Verification (headless MCP): `import_gltf` on the composite file -> node tree
contains carrier node with instantiated sub-scene; screenshot; cycle file
produces the error, not a hang or crash.

### Phase 4 -- export glTF 2.1 external assets

- Change `erhe::gltf::export_gltf` to take a `Gltf_export_arguments` struct
  (root node, binary, physics data, plus new: export destination directory
  for URI relativization, and a per-node external-asset callback or
  pre-collected `node -> source path` map supplied by the editor).
- Editor export (`mcp_server_file_io.cpp:122` and any menu path) collects
  `Prefab_instance` nodes before calling export: for each, emit a `files`
  entry (uri = source path relativized against the export destination;
  `mimeType` = `model/gltf+json` / `model/gltf-binary` by extension,
  deduplicated by path) and an `externalAssets` entry, and export the node
  with `externalAsset` set and **children skipped** (`process_node`,
  `gltf_fastgltf.cpp:3035`).
- Asset version: write `"version": "2.1"` + `"minVersion": "2.1"` only when
  `files`/`externalAssets` are actually emitted; plain scenes keep `"2.0"`.
- Always export a single scene (already the case) per the multiple-scene
  deprecation.

Verification: import composite -> export -> re-import round-trip headlessly;
diff exported JSON against expectations; a 2.0-only scene's export is
byte-identical to pre-phase output.

### Phase 5 -- `.erhescene` persistence of prefab instances

- `scene_serialization.cpp`: nodes carrying `Prefab_instance` serialize as a
  prefab-instance record `{ source path (relative to the bundle when
  possible), name, transform, flags }`; their subtree is **not** flattened
  into `data.glb` / `.geogram` files. Bump `Scene_file` version 5 -> 6; old
  files load unchanged (they simply contain flattened content).
- `load_scene`: re-instantiate records through `Prefab_library` (missing
  source file -> placeholder empty instance node + error in the scene load
  report, so the scene still opens).

Verification: save scene with instances, reload, screenshot matches; bundle
size shows sub-scene content is not duplicated; missing-file case degrades
gracefully.

### Phase 6 -- editing semantics and UX polish

Ordered by value; each item is independent:

1. **Instance subtree protection** [DONE 2026-07-11]: descendants of a
   `Prefab_instance` node are sealed with `lock_edit |
   lock_viewport_selection | lock_viewport_transform`
   (`seal_instance_subtree` in prefab_library.cpp); the item tree renders
   instance roots as non-expandable leaves with a distinct attachment icon;
   viewport pick redirects to the outermost instance root
   (`get_outermost_prefab_instance_node`). No drill-in modifier (sealed
   model: interiors are edited via the opened prefab scene only).
2. **Reload prefab** [DONE 2026-07-11]: `Prefab_library::reload(path)`
   re-parses the source, rebuilds (transitively) referencing prefab
   templates in dependency order, and re-clones every instance in every
   scene preserving each carrier transform; scene content-library
   texture/material entries from the previous parse are replaced. Entry
   points: MCP `reload_prefab`, and the save round-trip
   (`save_prefab_scene`: File > Save Prefab / MCP `save_prefab` exports the
   opened prefab scene back to its source path -- recorded on `Scene_root`
   by `Scene_open_operation` -- then reloads). File watching remains future
   work.
3. **Create prefab from selection**: export the selection to a `.glb` via the
   existing exporter, then replace the selection with an instance of it
   (compound operation, undoable).
4. **Skin/animation remapping**: build a source->clone node map during
   instantiation (walk template and clone trees in lockstep, or extend the
   clone framework with an optional remap context) and remap `Skin` joint
   pointers and animation channel targets; lifts the Phase 2 static-only
   limitation and makes animated prefabs (e.g. characters) work with
   independent playback.
5. **Embedded sub-assets**: support `files` entries with data URIs /
   GLB-packed payloads (parse from memory; cache key = parent canonical path
   + file index).

## Risks and open questions

- **Skins/animations reference nodes by pointer.** `Node::clone()` does not
  remap them; a cloned skinned mesh would deform against the *template's*
  joints. This is why Phase 2 declares skinned prefabs static-only and the
  remap is its own work item (6.4). Decide before Phase 2 lands whether to
  hard-block skinned sources or warn-and-continue (plan says warn).
- **Physics inside prefabs**: `import_gltf` builds physics through separate
  operations, not node attachments cloned by `Node::clone()`. First pass:
  prefab instances carry no physics; follow-up: instantiate
  `Gltf_physics_data` per instance (needs the same node remap as skins).
- **Content_library is per `Scene_root`** while `Prefab_library` is
  app-wide. Instantiating one prefab into two scenes registers the same
  shared materials/textures in both libraries -- acceptable (textures are
  device-global, materials are shared_ptr), but "edit material of an
  instance" then edits it everywhere, including the template. That is
  standard prefab semantics; per-instance overrides are explicitly out of
  scope for this plan.
- **Spec gaps**: the 2.1 explainers do not yet pin down externalAsset-node
  children or scene selection. The interpretations above (default scene,
  tolerant import, strict export) are recorded so they can be revisited when
  the normative text lands; keep an eye on KhronosGroup/glTF#2586.
- **No render-level instancing**: instances share GPU vertex/index data but
  still draw per-mesh-node. `EXT_mesh_gpu_instancing` for many-instance
  scenes is a separate future optimization, out of scope here.
