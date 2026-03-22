# parsers/

## Purpose

File format importers for loading 3D content into the editor.

## Key Types / Functions

- **`import_gltf()`** -- Imports a glTF file into a `Scene_root`. Uses `erhe::gltf` for parsing and creates meshes, materials, animations, and skins. Supports async loading via `tf::Executor`.

- **`scan_gltf()`** -- Scans a glTF file and returns a list of mesh/node names without fully loading it.

- **`import_geogram()`** -- Imports Geogram mesh files.

- **`import_wavefront_obj()`** -- Imports Wavefront OBJ files.

- **`Json_library`** / **`json_polyhedron`** -- Loads polyhedra definitions from JSON (used by `Scene_builder` for Johnson solids and other named polyhedra).

## Public API / Integration Points

- `import_gltf()` is called from scene loading and asset browser
- `scan_gltf()` is used by the asset browser to preview file contents
- Imported content is added to the target `Scene_root` and its `Content_library`

## Dependencies

- erhe::gltf, erhe::geometry, erhe::primitive, erhe::scene
- editor: App_context, Scene_root, Content_library
