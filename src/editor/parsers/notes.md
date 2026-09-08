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

- **`import_usd()`** (`usd.{hpp,cpp}`) -- Imports a USD file (`.usd` / `.usda` / `.usdc` / `.usdz`, see `is_usd_file_extension()`) into a `Scene_root` as an undoable compound operation, through `erhe::usd` (`src/erhe/usd/notes.md`). Synchronous, unlike `import_gltf()`: there is no asynchronous asset-load path for USD yet, so `make_import_usd_operation()` loads, builds and returns the compound in one call and `import_usd()` queues it. The compound holds the content-library attaches for the textures, materials, skins and animations, the `Item_insert_remove_operation` that inserts the import_root node, and the raytrace kickoff - the same undo behavior glTF import has, including the removal announcements (doc/import-undo-reference-clearing.md). It also owns the GPU side of USD textures: `erhe::usd` reports image FILES, and this loads them with `erhe::graphics::Image_loader` (PNG / JPEG / KTX2 / DDS) through a blocking-drain `Image_transfer` and fills the material slots the loader recorded. The whole file compiles to "USD support not built" stubs when `ERHE_USD_LIBRARY=none`, so no call site needs a conditional.

- **USD composition arcs** (`usd.cpp`) -- `erhe::usd` reports the `references` / `payload` arcs a prim authors instead of flattening what they name (`src/erhe/usd/notes.md`, "Composition arcs"). Both the import and the open-scene path turn each arc into a `Prefab_instance` attachment on the carrier prim, in the arcs' order, with the arc's target cloned below it through `Prefab_library` (doc/usd-compatibility-plan.md X1). A carrier is transformable - the import makes a typeless or `Scope` prim that authors arcs an `Xform` (`src/erhe/usd/notes.md`, "Composition arcs") - so what reaches the warning here is a carrier of a type that carries no transform, whose arcs are dropped. An internal reference names the stage's own file; a relative asset path resolves against the referencing layer's directory. A glTF-backed instance is sealed; a USD-backed one is not (doc/usd-compatibility-plan.md X2), its structure alone being protected. The resolution runs before the insert operation is built, so an undo of the import removes the instances with the rest of the tree.

- **USD class prims** (`usd.cpp`) -- `resolve_usd_classes()` turns every `class` prim `erhe::usd` recorded into a `Style` item at the place the class prim has (a class under a `Scope "Styles"` becomes a style under that scope, a class inside a class a style under a style), applies the class's opinions with `erhe::scene::apply_property_values`, and makes each prim's first resolving `inherits` target its style (doc/usd-compatibility-plan.md X3). Both the import and the open-scene path run it before the insert operation is built, so an undo of the import removes the styles with the tree.

- **USD brush prims** (`usd.cpp`) -- `resolve_usd_brushes()` turns every `Brush` prim `erhe::usd` recorded into a `Brush` item at the place the prim has, with the geometry the reader took from the prim's `Mesh` child (reprocessed for edges when it has none), the density and normal-style token it authored, the material its `material:binding` names, and its other authored opinions applied with `erhe::scene::apply_property_values` (doc/usd-compatibility-plan.md E4a). A brush whose holding prim is in the loaded tree is parented there and rides that tree's insert, the way a material the file placed does; one the file gave no place gets a `make_library_attach_operation` of its own. `collect_usd_brushes()` is the save half: `erhe::usd` names no editor type, so each brush's geometry, density, normal-style token and material become a `Usd_save_brush` record for the writer.

- **USD skins** (`usd.cpp`) -- A skinned USD mesh reaches the scene the way a skinned glTF mesh does (doc/usd-compatibility-plan.md K1). `erhe::usd` gives every skinned `Mesh` prim its `erhe::scene::Skin` and lists the skins in `Usd_data::skins` (`src/erhe/usd/notes.md`, "Skinning"); `append_usd_content_library_operations()` attaches each of them to the content library with a `make_library_attach_operation` of its own, tagged `"skin"` the way the glTF import tags its skins, so a skin is a library resource under the `Skins` scope and an undo of the import takes it back out with the tree. A skin has no place of its own in the prim tree, unlike a material the file placed, so it always gets an attach. Registration and the GPU build are the format-neutral paths: `Scene_root::register_mesh()` registers a mesh's skin with the scene (`mark_skin_joints` flags the joint prims, `Bone_visualization` builds its proxies on the `Skin_registered_message`) when the tree enters it, and `finalize_imported_meshes()` builds a mesh whose `skin` is set with the skinned `Build_info` so `Shader_key::derive` turns `USE_SKINNING` on. The joint prims are plain `Xform` prims of the loaded tree, so they ride the import's insert like any other prim.

- **USD variant sets** (`usd.cpp`) -- `fill_variant_table()` turns every material-binding `variantSet` `erhe::usd` recorded into an entry of the target scene's `Variant_table` (`scene/variant_table.hpp`, doc/usd-compatibility-plan.md X4): the carrying prim and each binding's Material prim are held weakly as the items the load made, so a later switch assigns them without re-reading the file. The reader has already bound the selected variant, so an import needs no switch; the open-scene path then calls `Scene_root::apply_variant_selections()`, which applies a `Scene_settings::variant_selections` entry that names a different variant than the file's `variants` metadata did. `collect_usd_variant_sets()` is the save half: the table with its current selection becomes `Usd_save_arguments::variant_sets`, and a set whose file variants authored opinions beyond material bindings is named in one warning per save, because those opinions are not written.

- **glTF material variants** (`gltf.cpp`, `gltf_extensions_export.cpp`) -- `fill_gltf_variant_table()` turns the asset's `KHR_materials_variants` list into ONE entry of the target scene's `Variant_table`, named `c_gltf_variant_set_name` (`materials`, `parsers/gltf.hpp`), because a glTF asset holds one asset-wide variant list (doc/usd-compatibility-plan.md X4). The carrying prim is the import root on the import path - the prim an undo of the import removes, which takes the set out of the table with it - and the scene's own root prim on the open-scene path, where the file's content sits in the root's place. Bindings name a primitive by index (`<mesh path>#<primitive index>`, `scene/variant_table.hpp`), since glTF gives a primitive no name of its own. glTF authors no selection, so the set starts with none selected and the primitives keep the materials the file gave them until the user picks; the open-scene path then calls `Scene_root::apply_variant_selections()` for the `ERHE_scene` entry. `find_exported_variant_set()` + `collect_gltf_material_variants()` are the save half (doc/scene_serialization.md step 5b), and the `ERHE_scene` settings are serialized from a copy whose `variant_selections` holds the written set's entry under the empty (root) prim path - the path the reloaded scene carries that set on.

- **`load_usd_prefab_template()`** (`usd.{hpp,cpp}`) -- The USD branch of `Prefab_library::load_template`: loads the file, creates its textures, finalizes its meshes, instantiates the arcs authored inside the template subtree (recursively, through the same library, so a cycle is caught there) and returns the prim the arc named wrapped in an unhosted template root. The
target is any prim (doc/usd-compatibility-plan.md S1): an `Xformable`, a
`Scope`, the `Typed` prim a typeless `def` or an unrecognized `typeName`
becomes, or a prototype a `class` prim holds (X3). The wrapper keeps the
target's authored local transform when the target is an `Xformable` and simply
holds it otherwise - a prim without a transform composes what reaches it
through to its children. A prototype target is content-less where it sits, so
the template it becomes gets `Item_flags::content` back on every prim of the
file it holds.

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
