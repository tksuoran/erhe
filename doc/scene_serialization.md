# erhe scene serialization

Reference for how the editor persists scenes: the file format, the save and
open pipelines, every part that participates, and what is (and is not)
persisted. Design history and rationale live in
[`gltf-scene-roundtrip-plan.md`](gltf-scene-roundtrip-plan.md); the wire
format of each vendor extension is specified in
[`gltf_extensions/`](gltf_extensions/README.md).

## Overview

A scene saves as a **single erhe-authored glTF file** (`.glb` binary by
default; a `.gltf` path selects the JSON form, which is inspection-only - it
writes no buffer URI and cannot be re-imported). One `export_gltf()` call
writes everything: render content as plain glTF 2.x, physics as
`KHR_physics_rigid_bodies` / `KHR_implicit_shapes`, prefab instances as glTF
2.1 `externalAssets` references, embedded texture source images, animations,
and the editor-domain state as `ERHE_*` vendor extensions.

`ERHE_scene` in `extensionsUsed` is the **erhe-authored marker**: a glTF file
carrying it opens as a full scene (fresh `Scene_root`, saved editor state
applied); any other glTF opens as a foreign asset import. All `ERHE_*`
extensions are optional (`extensionsUsed` only, never `extensionsRequired`),
so any stock glTF viewer can open a saved scene and see the render content.

There is no sidecar: no scene JSON, no per-scene imgui ini, no companion
geometry files. The legacy `.erhescene` directory-bundle format was removed
in phase 5 of the roundtrip plan (existing bundles were migrated by loading
and re-saving as `.glb`).

## File anatomy

Standard GLB container: 12-byte header, `JSON` chunk, `BIN` chunk. Inside the
JSON, erhe state attaches at three levels:

| Level | Extensions |
|---|---|
| per object (node / camera / material / mesh primitive) | `ERHE_node`, `ERHE_camera`, `ERHE_light`, `ERHE_material`, `ERHE_geometry`, `ERHE_physics`, `ERHE_layout` |
| the glTF `scene` object | `ERHE_scene` (per-scene setting overrides, ambient light, enable_physics) |
| asset root (`extensions`) | `ERHE_brushes`, `ERHE_node_graphs`, `ERHE_collections`, plus the Khronos physics extensions' shape/material/filter tables |

Cross-references between payloads use glTF indices within the same asset
(node index, material index, mesh index). Item flags serialize as name lists
(see [`gltf_extensions/flags.md`](gltf_extensions/flags.md)). Geometry-normative
meshes carry a bit-exact geogram dump in `ERHE_geometry` on the mesh
primitive, dual-listed with plain TRIANGLES render data so foreign viewers
still render them (flat-shaded where no fully-present per-vertex NORMAL
could be dual-listed; corner normals stay `ERHE_geometry`-only). A primitive
whose source of truth is an imported triangle soup exports the soup instead
(full vertex attributes: TEXCOORD_n, JOINTS_n / WEIGHTS_n, COLOR_n) with no
`ERHE_geometry` - its geometry is a derived artifact, re-derived on load.

## Save pipeline

Entry point: `editor::save_scene_gltf(Scene_root&, path)` in
`src/editor/parsers/gltf.cpp`. It assembles one
`erhe::gltf::Gltf_export_arguments` and calls `erhe::gltf::export_gltf()`:

1. **Root node** - the scene's root; export walks the node tree. Nodes
   flagged `import_root` are transparent (their children are written in
   their place), so open/save cycles do not nest wrappers.
2. **Physics data** - `build_gltf_physics_data()`
   (`parsers/gltf_physics_export.cpp`) converts `Node_physics` /
   `Node_joint` attachments and the content library's physics materials,
   collision filters and joint settings into the plain-data
   `erhe::gltf::Gltf_physics_data` carrier (`KHR_physics_rigid_bodies` +
   `KHR_implicit_shapes`; spec support notes in
   [`khr_physics_rigid_bodies_support.md`](khr_physics_rigid_bodies_support.md)).
   Compound / off-axis shapes export via synthesized child collider nodes;
   the importer folds them back (and removes the carrier nodes when they
   have no other content, keeping save/open/re-save node-identical).
   A settings-less joint (free six-dof) exports as a joint description with
   no limits / drives; reload materializes a `Physics_joint_settings` item
   from it. World-attached joints (no connected node) are skipped with a
   warning - the Khronos extension cannot express them.
3. **Prefab external assets** - `collect_prefab_external_assets()`
   (`prefabs/prefab_library.cpp`) walks the tree for `Prefab_instance`
   attachments and maps those nodes to glTF 2.1 `externalAssets` references
   (URIs relativized against the save directory); the instanced subtree is
   NOT flattened into the file. See
   [`gltf-prefabs-plan.md`](gltf-prefabs-plan.md).
4. **Image sources** - `make_gltf_image_source_provider()` snapshots the
   content library's retained encoded source images (PNG/JPEG bytes kept
   from import time) so textures re-embed byte-identical; a fallback re-reads
   standalone source image files for textures imported before retention
   existed. Textures with no exportable source (e.g. graph-texture bakes)
   skip the slot - graph-baked slots are reconstructed by re-baking on load.
5. **Animations** - `collect_gltf_export_animations()` exports every
   animation in the content library (samplers + channels, glTF-native).
6. **Editor state** - `add_gltf_editor_state()`
   (`parsers/gltf_extensions_export.cpp`) appends the editor-domain
   extension payloads and `extensionsUsed` entries:
   - per node: `ERHE_physics` (erhe rigid-body state the Khronos extension
     cannot express), `ERHE_layout` (Layout / Layout_item attachments),
     node bindings for graph meshes;
   - scene level: `ERHE_scene` - ambient light, `enable_physics`, and the
     per-scene `Scene_settings` overrides (issue #239), serialized through
     the codegen struct (`scene/definitions/scene_settings.py`);
   - asset root: `ERHE_brushes` (brush library; brush geometry rides as
     extra unreferenced glTF meshes), `ERHE_node_graphs` (graph texture and
     graph mesh assets as embedded node-graph JSON, plus
     `material_bindings` / `node_bindings`), `ERHE_collections` (item
     tags);
   - the **exclusion hook**: meshes and physics controlled by a
     `Geometry_graph_mesh` attachment are excluded from plain export - they
     are baked products, re-derived from the graph on load, and must not be
     double-persisted.
   The library-domain extensions (`ERHE_node`, `ERHE_camera`, `ERHE_light`,
   `ERHE_material`, `ERHE_geometry`) are written by the exporter itself in
   `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`.
7. `erhe::gltf::export_gltf()` produces the GLB/JSON string;
   `erhe::file::write_file()` writes it.

Save entry points (there is ONE save shape - full editor state; the former
Save Prefab command / MCP `save_prefab` tool were merged into Save Scene):

- **File > Save Scene** (`operations_window.cpp` `Operations::save_scene`):
  a scene opened/loaded from a glTF file (`Scene_root::get_source_path`)
  saves back to that file without confirmation; a scene with no source file
  writes `<scene name>.glb` under `res/editor/scenes` (created on demand),
  with an Overwrite/Cancel modal when the file exists, and is then
  associated with that file (further saves write back silently).
- **MCP `save_scene`** (`mcp/mcp_server_file_io.cpp`): optional path
  (default: same resolution as File > Save Scene); an explicit path with a
  missing/other extension is normalized to `.glb`.
- Both run `editor::save_scene_gltf(App_context&, ...)`: a save triggers
  `Scene_saved_message` (Asset Browser rescan), and when the written file is
  a loaded prefab source the prefab is reloaded so every instance in every
  scene reflects the edit - the prefab editing round-trip is open the
  source as a scene, edit, Save Scene.
- **MCP `export_gltf`** exports plain interchange (same call minus
  `add_gltf_editor_state` unless `editor_state: true` is passed) - use it
  for handing content to other tools, `save_scene` for full state.

## Open pipeline

Single load entry: the `Load_scene_file_message` handler (subscribed in the
`Operations` constructor, `operations_window.cpp`). Every load source funnels
through it:

- File > Load Scene (`.glb`/`.gltf` file picker, defaults to
  `res/editor/scenes`)
- Asset Browser context menu ("Load scene" on an erhe-authored file)
- MCP `load_scene` (queued; poll `list_scenes` to observe completion)
- `--scene <file>` command-line option (startup, instead of the procedural
  startup script)
- `scene.load_scene` in `config/editor/commands.json`

The handler scans the file (`editor::scan_gltf` -> `Gltf_scan_summary`,
JSON-only, no buffer decode) and branches on
`is_erhe_scene(extensions_used)`:

- **Foreign glTF** -> `Scene_open_operation` (undoable operation: new scene
  root + content library + browser + viewport, then a normal glTF *import*
  with default camera/lights and an `import_root` wrapper).
- **erhe-authored scene** -> `editor::open_scene_gltf()`
  (`parsers/gltf.cpp`), which is NOT undoable and does not appear on the
  operation stack:
  1. Parse into a temporary container (`erhe::gltf::parse_gltf`), because
     the `ERHE_scene` payload must be read before the `Scene_root` can be
     constructed (`enable_physics` is a construction-time property).
  2. Construct the `Scene_root` with a **fresh, empty `Content_library`** -
     the file carries the scene's own brushes / materials / textures /
     animations / graphs; nothing leaks in from other scenes.
  3. Apply the rest of the `ERHE_scene` payload: ambient light and the
     per-scene `Scene_settings` overrides (codegen deserialize).
  4. `finalize_imported_meshes()` - build GPU vertex/index buffers
     (skinned variant when needed), build edges for wireframe rendering
     when the geometry arrived without them (geometry restored from
     `ERHE_geometry` already has them, byte-exact), and register raytrace
     primitives.
  5. Resolve glTF 2.1 external assets: each referenced prefab is parsed
     (once, cached by `Prefab_library`) and instantiated under its carrier
     node.
  6. Execute inline (built as operations, executed and dropped - same
     ordering as the import compound): content-library attaches (textures /
     materials / skins / animations), `import_gltf_physics()` (Khronos
     payload -> `Node_physics` / `Node_joint`, compound folding, carrier
     node removal) and `import_gltf_editor_state()`
     (`parsers/gltf_extensions_import.cpp`: flags, layouts, tags, brushes,
     node graphs + their bindings; graph meshes re-bake).
  7. Reparent the parsed top-level nodes directly under the new scene's
     root - no `import_root` wrapper, no injected default camera / lights
     (an erhe-authored scene has exactly what it was saved with).
  8. Kick off `Async_raytrace_kickoff_operation` (BVH builds on worker
     threads, synchronized via the scene's `Item_host` mutex).
- The handler then wires UI for the returned scene root: a browser window,
  a viewport (an existing empty viewport is repurposed when present, else a
  new one is created), and `Scene_created_message` homes the global tools
  (Hud / Hotbar / Headset_view) when no scene owned them yet.

Legacy pre-extension files: node `extras.erhe_flags` and the material extras
carrier (`roughness_y`, `bxdf_model`, `blending_mode`, ...) are still parsed
(never written) so files exported before the `ERHE_*` extensions existed
keep their state on import.

## Parts map

| Part | Location | Role |
|---|---|---|
| `save_scene_gltf` / `open_scene_gltf` | `src/editor/parsers/gltf.{hpp,cpp}` | scene save / open entry points |
| `scan_gltf`, `is_erhe_scene` | `src/editor/parsers/gltf.cpp` | cheap scan; erhe-authored detection |
| `add_gltf_editor_state` | `src/editor/parsers/gltf_extensions_export.cpp` | editor-domain `ERHE_*` payloads + exclusion hook |
| `import_gltf_editor_state` | `src/editor/parsers/gltf_extensions_import.cpp` | editor-domain `ERHE_*` apply on open/import |
| physics export / import | `src/editor/parsers/gltf_physics_export.cpp` / `gltf_physics_import.cpp` | Khronos physics payload <-> erhe::physics |
| `export_gltf`, `parse_gltf` + library `ERHE_*` writers/readers | `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` | core glTF I/O (fastgltf fork: physics extensions, glTF 2.1 externalAssets, generic extension passthrough) |
| prefab externalAssets | `src/editor/prefabs/prefab_library.cpp` | collect on save, resolve + instantiate on open |
| `Scene_settings` / `Gltf_source_reference` codegen | `src/editor/scene/definitions/*.py` | per-scene overrides payload; content-library item -> source-file back-references |
| `Load_scene_file_message` handler | `src/editor/operations/operations_window.cpp` | single load entry, erhe-vs-foreign branch, UI wiring |
| `Scene_open_operation` | `src/editor/operations/scene_open_operation.cpp` | undoable "open foreign glTF as new scene" |
| MCP tools (`save_scene`, `load_scene`, `open_scene`, `export_gltf`, `import_gltf`) | `src/editor/mcp/mcp_server_file_io.cpp` | scripted access to the same paths |
| extension specs + JSON schemas | `doc/gltf_extensions/` | wire-format reference (schemas double as test fixtures) |

## What is not persisted (known limitations)

- **Content-library materials referenced by no mesh are not exported**
  (glTF materials exist only where meshes reference them). Consequently a
  graph-texture binding on an unused material is dropped at save with a
  warning (`"binding dropped"`). Bind materials that meshes use.
- Textures whose source cannot be reconstructed (embedded in a source
  .glb/.gltf imported before source retention, or graph bakes) do not embed
  an image; graph bakes are re-derived on load, others lose the slot with a
  warning.
- **`Brush_placement` attachments are not persisted** (the brush *library*
  is, via `ERHE_brushes`): a placed-brush node reloads as a plain mesh node
  without the link back to its source brush. The legacy scene.json format
  did not persist them either.
- A static rigid body's mass is not persisted (KHR_physics_rigid_bodies has
  no `motion` object for static bodies); the value is meaningless for
  statics and is shape-derived on reload.
- Window layout / imgui state is not part of the scene (per-scene imgui ini
  support was removed in phase 4).
- Undo/redo history, selection, and other transient session state are not
  saved.
- Corner normals of geometry-normative meshes live only in `ERHE_geometry`;
  foreign viewers render such meshes flat-shaded.
- **Prefab templates ignore editor-domain payloads.** Since the Save Scene /
  Save Prefab merge, saving over a prefab source writes the full editor
  state (the file becomes erhe-authored: `ERHE_scene` in `extensionsUsed`);
  instantiating it as a prefab parses only the render/physics content -
  `ERHE_*` payloads (layouts, tags, brushes, node graphs) do not transfer
  into instances. In particular, a mesh controlled by a
  `Geometry_graph_mesh` attachment is excluded from the save (re-derived
  from the graph on scene load) and prefab instances do not rebuild it, so
  graph-baked products are missing from instances of such a prefab.

## Verifying round-trips

**`scripts/scene_roundtrip_verify.py`** is the standing verification
harness (phase 6 of the roundtrip plan): against a fresh headless editor
session it builds a scene exercising every `ERHE_*` extension (shapes,
imported textured + skinned/animated assets, physics bodies + joint, brush
placement, graph mesh + graph texture bindings, layouts, tags, authored
animation keys, an external-asset prefab instance), saves it, validates
every `ERHE_*` payload against the JSON schemas in
`doc/gltf_extensions/schema/`, reloads it and diffs the MCP-visible state,
round-trips `res/editor/scenes/Prefab test.glb` when present, and runs the
optional foreign-tool checks (Khronos glTF validator via
`--gltf-validator` / `ERHE_GLTF_VALIDATOR`; Blender headless import+render
via `--blender` / `ERHE_BLENDER`). Exit 0 = all executed checks passed.

The graph asset round-trips are additionally covered by
`scripts/geometry_nodes_smoke_test.py` and
`scripts/texture_graph_smoke_test.py` (each parses the saved GLB JSON chunk
directly; run each suite in its own fresh editor session). Ad-hoc checks:
save -> load -> compare via the in-editor MCP (`get_scene_nodes`,
`get_scene_materials`, `get_physics_items`, `get_scene_brushes`, ...) and
`capture_screenshot`. Design history: `gltf-scene-roundtrip-plan.md`.
