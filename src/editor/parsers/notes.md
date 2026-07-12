# parsers/

## Purpose

File format importers for loading 3D content into the editor, plus the
erhe-authored glTF scene persistence entry points
(doc/gltf-scene-roundtrip-plan.md phase 4).

## Key Types / Functions

- **`import_gltf()`** -- Imports a glTF file into a `Scene_root` as an undoable compound operation. Uses `erhe::gltf` for parsing and creates meshes, materials, animations, and skins. Supports async loading via `tf::Executor`.

- **`save_scene_gltf()`** -- Scene save: one `export_gltf()` call writing the whole scene (render content + physics + prefab external assets + texture sources + animations + editor-domain `ERHE_*` extensions via `add_gltf_editor_state`) into a single `.glb`/`.gltf`. `ERHE_scene` in `extensionsUsed` marks the file erhe-authored.

- **`open_scene_gltf()`** -- Scene open: opens an erhe-authored glTF file as a full `Scene_root` (not undoable; fresh empty `Content_library`; `ERHE_scene` payload applied: `enable_physics` at construction, ambient light, per-scene `Scene_settings`). Reuses the import machinery; no import_root wrapper, no default camera/lights.

- **`scan_gltf()`** -- Scans a glTF file and returns tooltip content lines, the structured `extensions_used` list, and the accessor-bounds AABB without loading buffer data. `is_erhe_scene()` checks `extensions_used` for `ERHE_scene`.

- **`gltf_extensions_export/import`** -- Editor-domain `ERHE_*` extension payload builders / appliers (phase 3).

- **`import_geogram()`** -- Imports Geogram mesh files.

- **`import_wavefront_obj()`** -- Imports Wavefront OBJ files.

- **`Json_library`** / **`json_polyhedron`** -- Loads polyhedra definitions from JSON (used by `Scene_builder` for Johnson solids and other named polyhedra).

## Public API / Integration Points

- `import_gltf()` is called from scene loading and asset browser
- `save_scene_gltf()` / `open_scene_gltf()` back File > Save Scene / Load Scene (via the `load_scene_file` message handler in operations_window.cpp) and the MCP `save_scene` / `load_scene` tools
- `scan_gltf()` is used by the asset browser to preview file contents and by the load path to branch erhe-authored vs foreign glTF
- Imported content is added to the target `Scene_root` and its `Content_library`

## Dependencies

- erhe::gltf, erhe::geometry, erhe::primitive, erhe::scene
- editor: App_context, Scene_root, Content_library
