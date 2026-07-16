# erhe_gltf

## Purpose
glTF file import and export using the fastgltf library. Loads .gltf/.glb files into
erhe scene objects (nodes, meshes, cameras, lights, materials, textures, animations, skins)
and exports erhe scene graphs back to glTF format. Carries KHR_implicit_shapes +
KHR_physics_rigid_bodies content in and out as plain data (`gltf_physics.hpp`); the editor
performs all mapping to/from erhe::physics (see `doc/khr_physics_rigid_bodies_support.md`).

## Key Types
- `Gltf_data` -- Container for all imported scene data: vectors of shared_ptr to animations, cameras, lights, meshes, skins, nodes, materials, textures, samplers; plus `Gltf_physics_data physics`.
- `Gltf_physics_data` (`gltf_physics.hpp`) -- Plain-data 1:1 carrier for the physics extensions: implicit shapes, physics materials, collision filters, joints, per-node body descriptions (motion / collider / trigger / joint), and export-only `synthesized_colliders` (colliders the exporter places on synthesized glTF child nodes: compound shape children, non-Y shape axes, non-node wrapper scales). Collider geometry is mesh-keyed (current spec); node-keyed geometry is still read/written for older files.
- `Gltf_scan` -- Lightweight scan result listing names of all assets in a glTF file without fully loading them.
- `Gltf_parse_arguments` -- Parameters for `parse_gltf()`: graphics device, executor, image transfer, root node, mesh layer, file path.
- `Image_transfer` -- Manages GPU texture upload via a ring buffer for streaming image data.

## Public API
- `parse_gltf(arguments)` -- Load a glTF file and return populated `Gltf_data`.
- `scan_gltf(path)` -- Quick scan returning asset names without full parse.
- `export_gltf(Gltf_export_arguments)` -- Export a scene subtree to glTF/GLB string. The optional `Gltf_physics_data` (built by the editor's `build_gltf_physics_data()`) adds the physics extension content and extensionsUsed entries. `external_assets` maps nodes to glTF 2.1 externalAsset references (deduplicated `files` entries; such nodes are written without children/attachments, and the asset version becomes 2.1 + minVersion 2.1). A `(root_node, binary, physics_data)` convenience overload exports plain glTF 2.0.
- `Image_transfer(device)` -- Create image upload manager.
- `Image_transfer::upload_to_texture(image_info, range, texture, gen_mipmap)` -- Upload image to GPU texture.

## Dependencies
- **erhe libraries:** `erhe::graphics` (private), `erhe::scene` (private), `erhe::primitive` (private), `erhe::geometry` (private), `erhe::file` (private), `erhe::log` (private), `erhe::profile` (private)
- **External:** fastgltf (conditionally, via `ERHE_GLTF_LIBRARY`), Taskflow (for parallel loading), fmt

## Notes
- Backend is selected at CMake time: `ERHE_GLTF_LIBRARY_FASTGLTF` or `ERHE_GLTF_LIBRARY_NONE`.
- `gltf.hpp` is a dispatch header that includes the appropriate backend.
- Uses `Image_transfer` with a ring buffer for asynchronous texture uploads.
- fastgltf is pinned in the top-level CMakeLists to the `tksuoran/fastgltf` fork, which
  carries the KHR_physics_rigid_bodies spec-compliance fixes (mesh-keyed collider
  geometry, spec inertia key names, exporter JSON fixes) plus a subset of the glTF 2.1
  proposal (KhronosGroup/glTF#2585): the unified top-level `files` array, the
  `externalAssets` array with `Node::externalAssetIndex`, and `minVersion` handling.
  Drop the fork when upstream ships both.
- glTF 2.1 external assets: `Gltf_data::files` / `external_assets` /
  `node_external_assets` surface what the file references, with file URIs resolved
  against the glTF directory. Neither fastgltf nor erhe::gltf recursively parses
  referenced assets or detects cross-file cycles -- the editor's prefab layer does
  (see `doc/gltf-prefabs-plan.md`).
- glTF 2.1 unique IDs (KhronosGroup/glTF#2597, pre-ratification; parsed/written by
  the fastgltf fork): import carries each top-level object's `uid` onto the created
  erhe item (`Item_base::set_gltf_uid`); `scan_gltf` reports them in the
  `Gltf_scan::*_uids` vectors (parallel to the name vectors). Export stamps a uid
  on every item-backed top-level object (nodes, meshes, cameras, materials,
  images, skins, animations): a carried uid is preserved verbatim, an absent one
  is generated exactly once (16-char alphanumeric, unique against every uid AND
  name in the file -- one identifier namespace) and stored back on the item, so
  re-saving never changes uids. Objects without a backing item (accessors,
  buffers, buffer views, samplers, texture entries, extra meshes, the scene) are
  not stamped. Lights are extension-hosted and cannot carry uids. All uid
  read/write stays behind erhe::gltf so the pre-ratification surface can drift
  without touching clients.
- The text (.gltf) export variant writes no buffer URI and cannot be re-imported; use .glb
  for round-trips and .gltf for JSON inspection.
- Library-domain `ERHE_*` extensions (`ERHE_node`, `ERHE_camera`, `ERHE_light`,
  `ERHE_material`, `ERHE_geometry`) round-trip erhe-specific state that has no standard
  glTF representation; editor-domain extensions (`ERHE_scene`, collections, brushes,
  physics payloads, ...) are injected by the editor through the generic extension
  passthrough (captured/emitted as raw JSON per object). This is the single scene
  persistence format (`doc/gltf-scene-roundtrip-plan.md`; the editor's legacy
  `.erhescene` bundle format was removed in phase 5). Legacy reads remain for files
  written before the extensions existed: node `extras.erhe_flags` and the material
  extras carrier (`roughness_y`, `bxdf_model`, `blending_mode`, ...) are parsed but no
  longer written.
