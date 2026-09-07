# parsers/

## Purpose

File format importers for loading 3D content into the editor, plus the
erhe-authored glTF scene persistence entry points
(doc/gltf-scene-roundtrip-plan.md phase 4).

## Key Types / Functions

- **`import_gltf()`** -- Imports a glTF file into a `Scene_root` as an undoable compound operation. Uses `erhe::gltf` for parsing and creates meshes, materials, animations, and skins. **Asynchronous** (doc/async-asset-loading.md): it queues an `Asset_load_task`, and the undoable operation is built from the finished parse once the load lands. `make_import_gltf_operation()` takes an optional `Prepared_gltf_parse*` for that; with `async_gltf_load` off it parses inline as before.

- **`finish_open_scene_gltf()`** / **`build_imported_buffer_meshes()`** -- The two halves the asynchronous path needs. `finish_open_scene_gltf` is the main-thread tail of `open_scene_gltf` (Scene_root construction, reference resolution, buffer-mesh finalize, content-library / physics / editor-state operations, node reparenting, raytrace kickoff); `build_imported_buffer_meshes` is the worker-side `Buffer_mesh` build, deliberately serial because mesh clones share `Primitive` objects and `make_buffer_mesh` has no per-shape serialization. `make_renderable_mesh` is idempotent, so the main-thread `finalize_imported_meshes` pass fast-paths over what the worker built.

- **Deferred load finalize** (doc/gltf-load-speedup-plan.md, `Load_config` in editor settings): with `deferred_edge_lines` / `deferred_raytrace` on (default), `finalize_imported_meshes()` builds only a fill-only buffer mesh straight from the triangle soup plus an AABB proxy raytrace (picking works immediately, on approximate bounds); the `Async_raytrace_kickoff_operation` then runs one background task per mesh that builds the Geometry (edges, smooth normals), the full buffer mesh and the real triangle raytrace, and swaps them in under the scene lock. `parallel_gltf_parse` gates parallel image decode / mesh parse / animation parse inside `erhe::gltf::parse_gltf` (`Gltf_parse_arguments::parallel`). Disabling the options restores fully eager, serial loading. Per-stage timings log under `editor.parsers` and `erhe.gltf.log`.

- **`save_scene_gltf()`** -- Scene save: one `export_gltf()` call writing the whole scene (render content + physics + prefab external assets + texture sources + animations + editor-domain `ERHE_*` extensions via `add_gltf_editor_state`) into a single `.glb`/`.gltf`. `ERHE_scene` in `extensionsUsed` marks the file erhe-authored. The `App_context&` overload is THE save entry point: it also sends `Scene_saved_message` and reloads the prefab when the written path is a loaded prefab source (this replaced the separate Save Prefab command / `save_prefab_scene`). `resolve_scene_save_path()` picks the destination: the scene's own source file when set, else `default_scene_dir()/<scene name>.glb`.

- **`open_scene_gltf()`** -- Scene open: opens an erhe-authored glTF file as a full `Scene_root` (not undoable; fresh empty `Content_library`; `ERHE_scene` payload applied: `enable_physics` at construction, ambient light, per-scene `Scene_settings`). Reuses the import machinery; no import_root wrapper, no default camera/lights.

- **`scan_gltf()`** -- Scans a glTF file and returns tooltip content lines, the structured `extensions_used` list, and the accessor-bounds AABB without loading buffer data. `is_erhe_scene()` checks `extensions_used` for `ERHE_scene`. It is still a whole-file read plus a full JSON parse, so callers on the main thread run it on a worker: the load path scans inside `Gltf_load_task`, and the asset browser through `ensure_scanned()` (`ensure_scanned_blocking()` remains for the drag-and-drop drop target, which needs the AABB in the same frame).

- **`gltf_extensions_export/import`** -- Editor-domain `ERHE_*` extension payload builders / appliers (phase 3).

- **`import_usd()`** (`usd.{hpp,cpp}`) -- Imports a USD file (`.usd` / `.usda` / `.usdc` / `.usdz`, see `is_usd_file_extension()`) into a `Scene_root` as an undoable compound operation, through `erhe::usd` (`src/erhe/usd/notes.md`). Synchronous, unlike `import_gltf()`: there is no asynchronous asset-load path for USD yet, so `make_import_usd_operation()` loads, builds and returns the compound in one call and `import_usd()` queues it. The compound holds the content-library attaches for the textures and materials, the `Item_insert_remove_operation` that inserts the import_root node, and the raytrace kickoff - the same undo behavior glTF import has, including the removal announcements (doc/import-undo-reference-clearing.md). It also owns the GPU side of USD textures: `erhe::usd` reports image FILES, and this loads them with `erhe::graphics::Image_loader` (PNG / JPEG / KTX2 / DDS) through a blocking-drain `Image_transfer` and fills the material slots the loader recorded. The whole file compiles to "USD support not built" stubs when `ERHE_USD_LIBRARY=none`, so no call site needs a conditional.

- **USD composition arcs** (`usd.cpp`) -- `erhe::usd` reports the `references` / `payload` arcs a prim authors instead of flattening what they name (`src/erhe/usd/notes.md`, "Composition arcs"). Both the import and the open-scene path turn each arc into a `Prefab_instance` attachment on the carrier prim, in the arcs' order, with the arc's target cloned below it through `Prefab_library` (doc/usd-compatibility-plan.md X1). An internal reference names the stage's own file; a relative asset path resolves against the referencing layer's directory. A glTF-backed instance is sealed; a USD-backed one is not (doc/usd-compatibility-plan.md X2), its structure alone being protected. The resolution runs before the insert operation is built, so an undo of the import removes the instances with the rest of the tree.

- **USD class prims** (`usd.cpp`) -- `resolve_usd_classes()` turns every `class` prim `erhe::usd` recorded into a `Style` item at the place the class prim has (a class under a `Scope "Styles"` becomes a style under that scope, a class inside a class a style under a style), applies the class's opinions with `erhe::scene::apply_property_values`, and makes each prim's first resolving `inherits` target its style (doc/usd-compatibility-plan.md X3). Both the import and the open-scene path run it before the insert operation is built, so an undo of the import removes the styles with the tree.

- **USD variant sets** (`usd.cpp`) -- `fill_variant_table()` turns every material-binding `variantSet` `erhe::usd` recorded into an entry of the target scene's `Variant_table` (`scene/variant_table.hpp`, doc/usd-compatibility-plan.md X4): the carrying prim and each binding's Material prim are held weakly as the items the load made, so a later switch assigns them without re-reading the file. The reader has already bound the selected variant, so an import needs no switch; the open-scene path then calls `Scene_root::apply_variant_selections()`, which applies a `Scene_settings::variant_selections` entry that names a different variant than the file's `variants` metadata did. `collect_usd_variant_sets()` is the save half: the table with its current selection becomes `Usd_save_arguments::variant_sets`, and a set whose file variants authored opinions beyond material bindings is named in one warning per save, because those opinions are not written.

- **`load_usd_prefab_template()`** (`usd.{hpp,cpp}`) -- The USD branch of `Prefab_library::load_template`: loads the file, creates its textures, finalizes its meshes, instantiates the arcs authored inside the template subtree (recursively, through the same library, so a cycle is caught there) and returns the prim the arc named wrapped in an unhosted template root.

- **`import_geogram()`** -- Imports Geogram mesh files.

- **`import_wavefront_obj()`** -- Imports Wavefront OBJ files.

- **`Json_library`** / **`json_polyhedron`** -- Loads polyhedra definitions from JSON (used by `Scene_builder` for Johnson solids and other named polyhedra).

## Public API / Integration Points

- `import_gltf()` is called from scene loading and asset browser
- `save_scene_gltf()` / `open_scene_gltf()` back File > Save Scene / Load Scene (via the `load_scene_file` message handler in operations_window.cpp) and the MCP `save_scene` / `load_scene` tools
- `scan_gltf()` is used by the asset browser to preview file contents and by the load path to branch erhe-authored vs foreign glTF; both run it off the main thread
- Imported content is added to the target `Scene_root` and its `Content_library`

## Dependencies

- erhe::gltf, erhe::geometry, erhe::primitive, erhe::scene
- editor: App_context, Scene_root, Content_library, Asset_manager (asynchronous loads)
