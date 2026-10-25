# erhe_gltf

Stability: mostly stable

## Purpose
glTF file import and export using the fastgltf library. Loads .gltf/.glb files into
erhe scene objects (nodes, meshes, cameras, lights, materials, textures, animations, skins)
and exports erhe scene graphs back to glTF format. Carries KHR_implicit_shapes +
KHR_physics_rigid_bodies content in and out as plain data
(`erhe::scene::Physics_description`, `erhe_scene/physics_description.hpp`); the editor
performs all mapping to/from erhe::physics (see `doc/erhe/khr_physics_rigid_bodies_support.md`).

## Key Types
- `Gltf_data` -- Container for all imported scene data: vectors of shared_ptr to animations, cameras, lights, meshes, skins, nodes, materials, textures, samplers; plus `erhe::scene::Physics_description physics` and `unresolved_object_properties` (object-reference local values of the ERHE_* `properties` maps whose name did not resolve during the parse; the editor resolves them by name once its import operations ran).
- `erhe::scene::Physics_description` (`erhe_scene/physics_description.hpp`, see `doc/erhe/scene.md`) -- Plain-data carrier for the physics extensions, filled 1:1 from KHR_implicit_shapes + KHR_physics_rigid_bodies on import and written back from it on export. Collider geometry is mesh-keyed (current spec); node-keyed geometry is still read/written for older files.
- `Gltf_scan` -- Lightweight scan result listing names of all assets in a glTF file without fully loading them.
- `Gltf_parse_arguments` -- Parameters for `parse_gltf()`: executor, `Gltf_device_options`, root node, mesh layer, file path. It deliberately holds NO `erhe::graphics::Device` and no `Image_transfer`: `parse_gltf` is structurally device-free so it can run on a worker thread (doc/editor/async_asset_loading.md). Anything device-derived the parse needs is queried by the caller on the main thread via `query_gltf_device_options()` and passed by value.
- `Gltf_device_options` -- The two device-derived values the parse needs: the transcode format preference for KTX2/Basis images, and max sampler anisotropy.
- `Gltf_image_residency` (`Gltf_data::image_residency`) -- The decode and GPU half of image loading, split out of the parse. `parse_gltf` decodes no image: it records each image's encoded stream (`Gltf_decoded_image::source`, the same bytes retained for export), the `Sampler_create_info`s, the material texture/sampler bindings and the decode settings (transcode preference, executor). Residency decodes on executor workers ahead of the upload under a decoded-bytes budget: `schedule_decodes(max_decoded_bytes_in_flight)` starts decodes while the decoded pixels waiting for upload plus those being decoded fit (a single larger image decodes alone), and uploading an image releases its bytes. `process_next_image()` (blocking; waits for the decode workers) / `process_next_image_into_frame()` (never waits; uploads the next decoded image) create the textures and record the uploads; `bind_material_textures()` fills the material slots; `drain()` does all of it synchronously, decoding in parallel under the same budget, and flushes. `get_pending_byte_count()` / `get_pending_byte_high_water()` report the decoded bytes in flight; `is_decode_idle()` tells whether a decode task runs. After `parse_gltf` returns, `Gltf_data::images` and `::samplers` are EMPTY until residency runs. A stream that fails to decode leaves a null texture and drops its retained source.
- `Image_transfer` -- GPU texture uploads, in one of two modes, with a staging capacity given at construction (default 64 MiB). `blocking_drain` (the original): a private staging ring of that size and its own transfer command buffer; when the ring fills, `flush()` submits + fence-waits + reclaims, so loads make progress with bounded staging memory even when no frames are rendered. Required by callers with no frame loop (`src/example` and the OpenXR controller model loader). `frame_recording`: `upload_into_frame()` stages from the device's capped upload staging (`Device::allocate_upload_staging`, doc/erhe/ring_buffer_memory.md "Upload staging cap") and records copies into the caller's frame command buffer, returning `budget_exhausted` instead of blocking when the per-frame budget or the staging has no room; the private ring is not allocated at all. Destructor flushes.

## Public API
- `parse_gltf(arguments)` -- Load a glTF file and return populated `Gltf_data`.
- `scan_gltf(path)` -- Quick scan returning asset names without full parse.
- `export_gltf(Gltf_export_arguments)` -- Export a scene subtree to glTF/GLB string. The optional `erhe::scene::Physics_description` (built by the editor's `build_gltf_physics_data()`) adds the physics extension content and extensionsUsed entries. `external_assets` maps nodes to glTF 2.1 externalAsset references (deduplicated `files` entries; such nodes are written without children, and the asset version becomes 2.1 + minVersion 2.1). A `(root_node, binary, physics_data)` convenience overload exports plain glTF 2.0.
- `Image_transfer(device)` / `Image_transfer(device, mode, staging_capacity_byte_count)` -- Create image upload manager.
- `Image_transfer::upload(image_info, pixels, texture, gen_mipmap)` -- Stage pixel data (full tightly packed mip chain) and record the per-level copies. `blocking_drain` mode only.
- `Image_transfer::upload_into_frame(command_buffer, ..., remaining_budget_bytes)` -- Same, into the frame's command buffer, decrementing the caller's per-frame byte budget; nothing is recorded (`budget_exhausted`) when the budget is spent or the capped upload staging has no room. `frame_recording` mode only.
- `Image_transfer::flush()` -- Submit pending copies, wait for the GPU, reclaim staging. No-op in `frame_recording` mode.
- `query_gltf_device_options(device)` -- Main-thread query filling `Gltf_device_options`.

## Dependencies
- **erhe libraries:** `erhe::scene` and `erhe::math` (public: `Gltf_data`, `Physics_description` and `Gltf_scan::bounding_box` use their types), `erhe::graphics` (private), `erhe::primitive` (private), `erhe::geometry` (private), `erhe::file` (private), `erhe::log` (private), `erhe::profile` (private), `erhe::task` (private)
- **External:** fastgltf (conditionally, via `ERHE_GLTF_LIBRARY`), Taskflow (for parallel loading), fmt

## Notes
- Backend is selected at CMake time: `ERHE_GLTF_LIBRARY_FASTGLTF` or `ERHE_GLTF_LIBRARY_NONE`.
- `parse_gltf` creates NO GPU objects at all -- not textures, not samplers. Keep it that way: it is what makes the editor's asynchronous loading safe (doc/editor/async_asset_loading.md), and nothing will catch a regression automatically.
- `parse_gltf` and `scan_gltf` stream a file (`fastgltf::GltfFileStream`) rather than reading it whole: fastgltf copies the GLB BIN chunk into a buffer of its own, so a whole-file read first would hold the binary payload twice.
- `gltf.hpp` is a dispatch header that includes the appropriate backend.
- In `blocking_drain` mode `Image_transfer` records into its own transfer command buffer, submitted and fence-waited whenever the staging ring fills and at destruction. An image larger than the staging ring uses a dedicated one-shot staging buffer.
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
  (see `doc/plans/gltf_prefabs.md`).
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
- Import restores the authored / default distinction: after the core glTF
  fields and the legacy extras have been replayed - which makes every field
  a local value - `parse_gltf` runs
  `erhe::property::clear_default_valued_local_properties` over every parsed
  node, mesh, light, camera and material, so a field the file left at its
  default reports `Value_source::default_value`
  (`doc/erhe/property_system.md` D32). The pass runs BEFORE the `ERHE_*`
  extension pass, whose `properties` maps are the item's authored local
  set and must not be elided. `Property_flags::native_gltf` marks the
  registrations this file format carries natively, for the property
  serializer of `doc/plans/gltf_properties_extension.md`.
- `KHR_materials_variants` is read and written (doc/erhe/usd_compatibility_design.md
  X4). `parse_material_variants()` resolves the asset's flat list into
  `Gltf_data::material_variants`: per variant name, one binding per (Mesh
  prim, erhe primitive index, material) a mapping names. The bindings are on
  the instantiated Mesh prims, not on the template mesh, so a glTF mesh two
  nodes instantiate yields one binding each. A primitive that maps no
  material for a variant contributes no binding, which is the extension's
  "keep the primitive's own material" rule. `process_material_variants()`
  writes `Gltf_export_arguments::material_variants` back: the asset's
  `variants` list plus each exported primitive's `mappings`, leaving the
  primitive's `material` alone; the erhe primitive index is mapped through the
  exporter's own primitive index map, so a primitive that did not export is
  skipped with a warning. Two erhe meshes of identical content share one glTF
  mesh, and the extension is per glTF primitive, so the first binding written
  to a primitive wins (the second is warned about).
- A mesh primitive whose geometry has no facets (an empty primitive, see
  `doc/erhe/primitive.md` "Empty primitives") is not exported: glTF has no
  empty primitive (every accessor needs a count of at least one). A mesh left
  with no exportable primitive is not exported either (a glTF mesh needs at
  least one primitive); its node is written without `mesh`, and a physics
  collider whose geometry is such a mesh is skipped with a warning.
- Prepared mesh data (`doc/plans/gltf_fast_load.md` "Exporter"):
  `Gltf_export_arguments::prepared_mesh_data = Prepared_mesh_data::write`
  writes each triangle-soup primitive as the core
  `erhe::primitive::prepare_primitive_for_export()` produces, with
  `ERHE_mesh_optimized`, `ERHE_mesh_topology` (`vertex_points` raw
  bufferView) and, when the raytrace backend exports BVHs,
  `ERHE_raytrace_bvh` (`nodes` and `primitive_ids` raw bufferViews), named
  in `extensionsUsed` only. The unique soups of the exported subtree are
  prepared up front, one task each on `Gltf_export_arguments::executor`
  (serial when null); a soup the walk misses (a physics collider mesh) is
  prepared when written. A soup the preparation refuses is written raw.
  `prepared_statistics` receives the counts (prepared, skipped with
  reasons, BVH reused / rebuilt / none) and the preparation time. The
  default `none` writes raw soups; geometry-normative primitives are
  unaffected either way. The editor's `save_scene` passes `write`.
- Texture image form: `Gltf_export_arguments::texture_export` is
  `Gltf_texture_export::source` (the retained stream: PNG / JPEG through
  the core `source`, Basis Universal KTX2 through `KHR_texture_basisu`, raw
  block KTX2 through `ERHE_texture_ktx2`) or `ktx2_astc_4x4` ("Save for
  Quest"): every Basis Universal KTX2 image is transcoded to a raw ASTC 4x4
  KTX2 file (all levels, SRGB / UNORM from the texture's format, Zstandard
  supercompressed; `erhe::graphics::transcode_ktx2_basis_to_astc_4x4()`), one
  task per image on `executor` after the material passes, and written
  through `ERHE_texture_ktx2` (`doc/gltf_extensions/ERHE_texture_ktx2.md`);
  PNG / JPEG are written as they are, a failed transcode writes the Basis
  source. `texture_statistics` receives the counts, byte totals and the
  transcode time.
- Raw block KTX2 on parse: `ERHE_texture_ktx2` entries set the texture's
  image; `Gltf_image_residency::astc_support` (from
  `Gltf_device_options::astc_support`, `native` when the device samples
  ASTC 4x4 in both color spaces) makes `Image_loader_ktx2` upload the stored
  blocks or decode them to RGBA8 on the CPU.
  `Gltf_image_residency::get_decode_path_counts()` counts the images by
  `erhe::graphics::Image_decode_path` for the per-load summary.
- Prepared mesh data on parse (`doc/plans/gltf_fast_load.md` "Loader"): with
  `Gltf_parse_arguments::prepared_mesh_data_use = Prepared_mesh_data_use::use`
  (the default) a soup primitive carrying `ERHE_mesh_optimized` is validated
  in its mesh parse task against
  `doc/gltf_extensions/ERHE_mesh_optimized.md`, `ERHE_mesh_topology.md` and
  `ERHE_raytrace_bvh.md` ("Validation / fallback", including the full
  `erhe::raytrace::validate_prebuilt_bvh()` check), all-or-nothing per
  extension: a rejected marker drops the other two, a rejected topology or
  BVH leaves the rest. The accepted payloads go on the render shape as an
  `erhe::primitive::Prepared_primitive_data` (`doc/erhe/primitive.md`
  "Prepared mesh data on load"); a rejected marker loads the primitive as a
  plain soup. `Gltf_data::prepared_mesh_statistics` counts the unique soup
  primitives that used every extension they carry and those with a
  rejection, by `"<extension>: <reason>"`, and one info line per file reads
  `prepared mesh data: N used, M rejected (<reasons>); topology T, BVH B`.
  Primitives sharing accessors share one validation only when their payloads
  are identical. `ignore` skips the extensions entirely (counted as
  `ignored_count`, glTF primitives); geometry-normative (`ERHE_geometry`)
  primitives are not checked.
- The text (.gltf) export variant writes no buffer URI and cannot be re-imported; use .glb
  for round-trips and .gltf for JSON inspection.
- A primitive with an index at or past its vertex count (invalid glTF) is skipped with an
  error, like a primitive with an unsupported attribute format.
- Tests: `erhe_gltf_tests` (`src/erhe/gltf/test/`, fastgltf backend, no graphics device):
  parse of the fixtures under `test/data/` (`variants.gltf`: hierarchy, primitives,
  materials, `KHR_materials_variants` bindings, Geometry build; `out_of_range_indices.gltf`:
  the invalid primitive is skipped) and GLB round trips (node hierarchy and transforms,
  `ERHE_node` / `ERHE_light` / `ERHE_camera` local properties and their unauthored values,
  the fixture's meshes, the prepared-mesh-data extensions and payload sizes, the
  prepared data read back as used, and tampered copies rejected with their
  reason while the primitive still loads as a soup). `erhe_gltf_gpu_tests` (same
  directory, device-backed because a material texture slot needs a `Texture`): a
  KTX2 texture exports through `KHR_texture_basisu` and a PNG through the core
  `source`, and the parse binds both material slots to their images.
- Image export: the retained encoded stream (`Gltf_image_source`) is embedded
  verbatim as a buffer view. A PNG / JPEG image is the texture's core `source`; a
  KTX2 (Basis Universal) image is named only by `KHR_texture_basisu` (no fallback
  image), which the export lists in `extensionsUsed` and `extensionsRequired`.
  The KHR material extensions fastgltf writes for the exported materials
  (`KHR_materials_ior`, `_transmission`, `_unlit`, ...) are declared in
  `extensionsUsed` from the same conditions fastgltf writes them under.
- Library-domain `ERHE_*` extensions (`ERHE_node`, `ERHE_camera`, `ERHE_light`,
  `ERHE_material`, `ERHE_geometry`) round-trip erhe-specific state that has no standard
  glTF representation; editor-domain extensions (`ERHE_scene`, collections, brushes,
  physics payloads, ...) are injected by the editor through the generic extension
  passthrough (captured/emitted as raw JSON per object). This is the single scene
  persistence format (`doc/editor/gltf_scene_roundtrip.md`). Legacy reads remain for files
  written before the extensions existed: node `extras.erhe_flags` and the material
  extras carrier (`roughness_y`, `bxdf_model`, `blending_mode`, ...) are parsed but no
  longer written.

## Future work

- [plans/gltf.md](../plans/gltf.md) - open items of the glTF persistence design.
- [plans/gltf_prefabs.md](../plans/gltf_prefabs.md) - the remaining prefab phases.
- [plans/gltf_fast_load.md](../plans/gltf_fast_load.md) - prepared mesh data (optimized core, stored colocation, BVH) so an erhe-saved scene loads with no deferred work.
