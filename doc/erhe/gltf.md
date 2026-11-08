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
- `Gltf_image_residency` (`Gltf_data::image_residency`) -- The decode and GPU half of image loading, split out of the parse. `parse_gltf` decodes no image: it records where each image's encoded stream is (`Gltf_decoded_image::source`, the same `Gltf_image_source` export reads; see "Image source streams"), the `Sampler_create_info`s, the material texture/sampler bindings and the decode settings (transcode preference, executor). Residency decodes on executor workers ahead of the upload under an `erhe::utility::Memory_budget`: each image holds a lease of its decoded size from the start of its decode until its upload, `schedule_decodes()` starts decodes while their leases fit (a single image larger than the budget decodes while nothing else holds it), and uploading an image releases its lease. `set_memory_budget()` shares a budget with other users (the editor's load memory budget, shared with the mesh builds); without one the residency makes its own of `max_decoded_bytes_in_flight` bytes (the parameter of `schedule_decodes()`, `process_next_image()` and `drain()`). `process_next_image()` (blocking; waits for the decode workers) / `process_next_image_into_frame()` (never waits; uploads the next decoded image) create the textures and record the uploads; `bind_material_textures()` fills the material slots; `drain()` does all of it synchronously, decoding in parallel under the same budget, and flushes. `get_pending_byte_count()` / `get_pending_byte_high_water()` report the decoded bytes in flight; `is_decode_idle()` tells whether a decode task runs. After `parse_gltf` returns, `Gltf_data::images` and `::samplers` are EMPTY until residency runs. A stream that fails to decode leaves a null texture and drops its retained source.
- `Image_transfer` -- GPU texture uploads, in one of two modes, with a staging capacity given at construction (default 64 MiB). `blocking_drain` (the original): a private staging ring of that size and its own transfer command buffer; when the ring fills, `flush()` submits + fence-waits + reclaims, so loads make progress with bounded staging memory even when no frames are rendered. Required by callers with no frame loop (`src/example` and the OpenXR controller model loader). `frame_recording`: `upload_into_frame()` stages from the device's capped upload staging (`Device::allocate_upload_staging`, doc/erhe/ring_buffer_memory.md "Upload staging cap") and records copies into the caller's frame command buffer, returning `budget_exhausted` instead of blocking when the per-frame budget or the staging has no room; the private ring is not allocated at all. Destructor flushes.

## Public API
- `parse_gltf(arguments)` -- Load a glTF file and return populated `Gltf_data`.
- `scan_gltf(path)` -- Quick scan returning asset names without full parse.
- `export_gltf(Gltf_export_arguments)` -- Export a scene subtree to glTF/GLB string. The optional `erhe::scene::Physics_description` (built by the editor's `build_gltf_physics_data()`) adds the physics extension content and extensionsUsed entries. `external_assets` maps nodes to glTF 2.1 externalAsset references (deduplicated `files` entries; such nodes are written without children, and the asset version becomes 2.1 + minVersion 2.1). A `(root_node, binary, physics_data)` convenience overload exports plain glTF 2.0. A text export that holds buffer data fails here (logged, empty string): it needs `export_gltf_document()`.
- `export_gltf_document(arguments, path)` / `write_gltf_document(path, document)` / `get_gltf_buffer_file_path(path)` -- The export as a `Gltf_export_document` (the .glb or .gltf bytes plus, for a text .gltf, its buffer files) and its write next to `path` (see "Text glTF files").
- `Image_transfer(device)` / `Image_transfer(device, mode, staging_capacity_byte_count)` -- Create image upload manager.
- `Image_transfer::upload(image_info, pixels, texture, gen_mipmap)` -- Stage pixel data (full tightly packed mip chain) and record the per-level copies. `blocking_drain` mode only.
- `Image_transfer::upload_into_frame(command_buffer, ..., remaining_budget_bytes)` -- Same, into the frame's command buffer, decrementing the caller's per-frame byte budget; nothing is recorded (`budget_exhausted`) when the budget is spent or the capped upload staging has no room. `frame_recording` mode only.
- `Image_transfer::flush()` -- Submit pending copies, wait for the GPU, reclaim staging. No-op in `frame_recording` mode.
- `query_gltf_device_options(device)` -- Main-thread query filling `Gltf_device_options`.
- `write_mesh_item(prepared, gpu_vertex_formats)` -- The mesh item of one prepared primitive, without its BVH (see "Mesh items").
- `write_raytrace_bvh_item(bvh)` -- The Raytrace BVH item of one prepared primitive's BVH (see "Raytrace BVH items").
- `write_pvs_item(database)` -- The PVS item of a ready potentially visible set database (see "PVS items").
- `load_mesh_item_primitive(arguments, out_error)` -- The primitive of one mesh item (and its BVH item), as a parse of its mesh item document makes it (see "Mesh item documents").

## Dependencies
- **erhe libraries:** `erhe::scene` and `erhe::math` (public: `Gltf_data`, `Physics_description` and `Gltf_scan::bounding_box` use their types), `erhe::graphics` (private), `erhe::primitive` (private), `erhe::geometry` (private), `erhe::file` (private), `erhe::log` (private), `erhe::profile` (private), `erhe::task` (private)
- **External:** fastgltf (conditionally, via `ERHE_GLTF_LIBRARY`), Taskflow (for parallel loading), fmt

## Notes
- Backend is selected at CMake time: `ERHE_GLTF_LIBRARY_FASTGLTF` or `ERHE_GLTF_LIBRARY_NONE`.
- `parse_gltf` creates NO GPU objects at all -- not textures, not samplers. Keep it that way: it is what makes the editor's asynchronous loading safe (doc/editor/async_asset_loading.md), and nothing will catch a regression automatically.
- `parse_gltf` and `scan_gltf` read a file's JSON and never load its buffers whole: see "Reading buffers by range".
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
  the fastgltf fork): every erhe item has a uid from its creation
  (`erhe::make_random_gltf_uid()`, doc/erhe/item.md). The parse gives each item it
  creates for a top-level object of the file (node, mesh, material, image,
  skin, animation) the object's uid, or, when the file states none, one derived
  by `make_derived_gltf_uid()` (`gltf_uid.hpp`) from the file name, the kind and
  the index (`"<file name>|node|3"`); a derived uid equal to a uid or name of the
  file, or to one derived before it, is derived again from the seed with
  `#1`, `#2`, ... appended. The prims the parse adds for a node (a camera or
  light child of a mesh node: `node_camera` / `node_light`; an
  `EXT_mesh_gpu_instancing` instance: `node_instance|<index>|<instance>`) take
  derived uids by the same rule. `scan_gltf` reports the file's uids in the
  `Gltf_scan::*_uids` vectors (parallel to the name vectors). Export writes the
  item's uid on every item-backed top-level object (nodes, materials, images,
  skins, animations) and assigns none; a uid that collides with another
  object's uid or name in the file (one identifier namespace; the same
  source imported twice) is written as one derived from it with a warning,
  the item keeping its own. Objects without a backing item (accessors,
  buffers, buffer views, samplers, texture entries, extra meshes, the scene,
  the glTF mesh and camera entries) carry none. Lights are extension-hosted
  and cannot carry uids. All uid read/write stays behind erhe::gltf so the
  pre-ratification surface can drift without touching clients.
- Node transforms round-trip bit for bit. The parse stores a node's TRS
  exactly as the file holds it (each node is parented once, before its
  transform is set, with `erhe::scene::set_prim_parent()`: the
  world-preserving re-parent would leave the skew of inverse(parent world)
  in the node, which `set_trs()` keeps). The export writes every node's
  local transform as the node holds it (its TRS components) and composes
  no transform into another node's: an import root
  (`Item_flags::import_root`) on the identity path from the scene root
  (`Item_flags::world_space`; `is_transparent_import_root()` in
  `gltf_item_flags.hpp`) is written as no node, its children in its place,
  and a transformed one is written as a node with its transform. The
  editor's open of an erhe-authored scene moves the top-level nodes under
  the scene root keeping their local transforms. Saving a loaded scene
  again therefore writes identical node transforms, and every world
  transform after a reopen is bit-identical to the one before the save.
  The JSON writer prints each float in its shortest round-trip form, so the
  text carries the exact bits. A node the file gives as a `matrix` is
  decomposed once (at the latest at its first save) and written as TRS from
  then on.
  `Gltf_roundtrip.node_trs_bit_exact` and the reload-diff leg of
  `scripts/scene_roundtrip_verify.py` (save, load, save again, compare)
  check it.
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
- Prepared mesh data (`doc/plans/gltf_fast_load.md` "Exporter"; a Scene item
  carries it, no file does, "Authoring data"):
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
  default `none` writes a document of authoring data, raw soups
  ("Authoring data"); geometry-normative primitives are unaffected either
  way. The editor's Scene item passes `write`; its scene files and other
  glTF exports pass `none`.
- Texture image form: `Gltf_export_arguments::texture_export` is
  `Gltf_texture_export::source` (the source stream: PNG / JPEG through
  the core `source`, Basis Universal KTX2 through `KHR_texture_basisu`; a
  raw block KTX2 through `ERHE_texture_ktx2` only with `Prepared_mesh_data::write`,
  "Authoring data") or `ktx2_astc_4x4` / `ktx2_astc_4x4_uncompressed` (the
  quest Scene item, with `write` only): every Basis Universal KTX2 image is transcoded to a raw ASTC 4x4
  KTX2 file (all levels, SRGB / UNORM from the texture's `transfer_function`
  and the two component layout from its `normal_encoding` ("Texture
  properties"), Zstandard supercompressed unless `_uncompressed`;
  `erhe::graphics::transcode_ktx2_basis_to_raw_blocks()`), one
  task per image on `executor` after the material passes, and written
  through `ERHE_texture_ktx2` (`doc/erhe/gltf.md` "Texture items");
  PNG / JPEG are written as they are, a failed transcode writes the Basis
  source. `texture_statistics` receives the counts, byte totals and the
  transcode time.
- Raw block KTX2 on parse (`Gltf_document::item`: a quest Scene item):
  `ERHE_texture_ktx2` entries set the texture's image; `Gltf_image_residency::astc_support` (from
  `Gltf_device_options::astc_support`, `native` when the device samples
  ASTC 4x4 in both color spaces) makes `Image_loader_ktx2` upload the stored
  blocks or decode them to RGBA8 on the CPU.
  `Gltf_image_residency::get_decode_path_counts()` counts the images by
  `erhe::graphics::Image_decode_path` for the per-load summary.
- What a parse reads: `Gltf_parse_arguments::document` (`Gltf_document`).
  `file` (the default; every glTF file) reads authoring data alone: the
  derived-data extensions - `ERHE_mesh_optimized`, `ERHE_mesh_topology`,
  `ERHE_raytrace_bvh`, `ERHE_gpu_vertex_streams`, the chain of
  `ERHE_mesh_lod`, `ERHE_texture_ktx2`, `ERHE_pvs` - are not read, so the
  primitives build from their core accessors as plain soups (a texture named
  only by `ERHE_texture_ktx2` has no image, no PVS is read); a file that
  carries any (one written by an earlier build) logs one warning naming them
  ("derived-data extensions not read") and counts its marked primitives as
  `Gltf_prepared_mesh_statistics::ignored_count`. `item` (a Scene item, a
  mesh item document, `load_mesh_item_primitive()`) reads them as below.
- Prepared mesh data on parse of an item document (`doc/plans/gltf_fast_load.md`
  "Loader"): a soup primitive carrying `ERHE_mesh_optimized` is validated
  when its soup is read - in its mesh parse task, or in the build job of a
  deferred load (`Mesh_payload_load::deferred`, "Reading buffers by range") -
  all-or-nothing per extension ("Mesh items", including the full
  `erhe::raytrace::validate_prebuilt_bvh()` check of a BVH): a rejected
  marker drops the others, a rejected topology, BVH or chain leaves the
  rest. The accepted payloads go on the render shape as an
  `erhe::primitive::Prepared_primitive_data` (`doc/erhe/primitive.md`
  "Prepared mesh data on load"); a rejected marker loads the primitive as a
  plain soup. `Gltf_parse_arguments::stored_bvh_use = Stored_bvh_use::ignore`
  reads no `ERHE_raytrace_bvh` (logged once per parse, "stored BVHs are not
  used"): the primitives load as without one and their raytrace builds the
  BVH (a prepared run whose BVH items carry another stamp).
  `Gltf_data::prepared_mesh_statistics` counts the unique soup
  primitives that used every extension they carry and those with a
  rejection, by `"<extension>: <reason>"`, and one info line per document reads
  `prepared mesh data: N used, M rejected (<reasons>); topology T, BVH B`
  (`finish_prepared_mesh_statistics()`, which a deferred load's caller runs
  after the last job). The checks the JSON and the accessors decide run at
  the parse (`read_mesh_optimized_metadata()`); the payload checks run in
  `Gltf_prepared_payload::validate()`.
  Primitives sharing accessors share one validation only when their payloads
  are identical; geometry-normative (`ERHE_geometry`) primitives are not
  checked.
- A primitive with an index at or past its vertex count (invalid glTF) is skipped with an
  error, like a primitive with an unsupported attribute format.
- Tests: `erhe_gltf_tests` (`src/erhe/gltf/test/`, fastgltf backend, no graphics device):
  parse of the fixtures under `test/data/` (`variants.gltf`: hierarchy, primitives,
  materials, `KHR_materials_variants` bindings, Geometry build; `out_of_range_indices.gltf`:
  the invalid primitive is skipped) and GLB round trips (node hierarchy and transforms,
  `ERHE_node` / `ERHE_light` / `ERHE_camera` local properties and their unauthored values,
  the fixture's meshes, the prepared-mesh-data extensions and payload sizes of
  a `Prepared_mesh_data::write` document, the prepared data read back as used
  by an item parse, and tampered copies rejected with their
  reason while the primitive still loads as a soup). `erhe_gltf_gpu_tests` (same
  directory, device-backed because a material texture slot needs a `Texture`): a
  KTX2 texture exports through `KHR_texture_basisu` and a PNG through the core
  `source`, and the parse binds both material slots to their images.
- Image export: the encoded stream (`Gltf_image_source::read()`, re-read from
  its file when file-backed) is embedded verbatim as a buffer view. A source
  that cannot be read - its file changed since the load - fails the export:
  `export_gltf()` returns an empty string and the editor's save and export
  paths write nothing. A PNG / JPEG image is the texture's core `source`; a
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

## Reading buffers by range

A parse from a file never holds a GLB BIN chunk or an external `.bin` whole;
it reads the byte ranges it needs. A GLB read through a
`Gltf_buffer_reader` (`Gltf_parse_arguments::glb_reader` and `glb_size`: a
GLB held in a container item, the Scene item of a prepared run) is parsed
the same way: `Gltf_reader_stream` reads the JSON chunk through the reader
and skips the BIN chunk as `Gltf_file_stream` does, and the BIN chunk is a
named byte source over the reader (`resolve_gltf_buffer_origins_from_reader()`).
A parse from memory (`glb_data`) and buffers in data URIs are resident and
read through the asset as usual.

- `Gltf_file_stream` feeds fastgltf the file without the BIN chunk: fastgltf
  asks the buffer map callback for the chunk's memory, the callback answers
  with a marker address, and the stream turns the read into a seek. The
  file's buffer 0 is then a `fastgltf::sources::CustomBuffer` with no bytes.
  External buffers are not loaded (no `Options::LoadExternalBuffers`); they
  stay `sources::URI`. `scan_gltf` uses the same stream.
- `resolve_gltf_buffer_origins()` finds each buffer's byte source - the
  BIN chunk of a .glb (offset from the chunk headers), a named buffer
  (a URI `Gltf_parse_arguments::buffer_resolver` gives a reader for, see
  "Mesh items") or an external .bin (the buffer URI) - and stamps each
  file's size and modification time (`Gltf_buffer_origins`). A buffer in a
  data URI has no origin. A parse from memory resolves the named buffers
  only (its own buffers are resident).
- `load_eager_buffer_views()` decides which bufferViews are read whole and
  which per primitive. A soup primitive (no `ERHE_geometry`) with indices,
  whose index and attribute accessors all lie in a buffer that is in a file
  and are neither sparse nor meshopt compressed, is read per primitive. A
  view on which every accessor is such a per-primitive accessor, and which
  no sparse accessor or other user (morph targets, skins, animations,
  instancing, geometry-normative primitives) reads, is left unread, and so
  are a per-primitive primitive's `ERHE_mesh_topology` / `ERHE_raytrace_bvh`
  payload views (read with its payload) and every image view in a file or
  a named buffer (Image source streams, below). Every other view in a non-resident buffer - animation
  and skin data, anything shared with them - is read whole into a buffer of
  its own and the view is repointed at it; the file places of all views
  stay in `Gltf_buffer_origins::view_places`. One info line per file gives
  the bytes read whole and the count of views left unread.
- Every soup primitive with a file origin has a `Gltf_triangle_soup_source`
  (below) carrying its prepared mesh data payload (`Gltf_prepared_payload`:
  the marker fields, the extension JSON and the payload views' file places).
  `Gltf_parse_arguments::mesh_payload` decides who reads it:
  `Mesh_payload_load::in_parse` (default) reads the soup in the mesh parse
  task and validates the prepared data there; `deferred` gives the
  primitive the source and its counts only
  (`erhe::primitive::Primitive(source, counts)`), and the caller's jobs read
  each payload with `Primitive::load_payload()`
  (doc/editor/async_asset_loading.md "Mesh build jobs"). Primitives
  without an origin are built from the asset in the parse either way.
- All these reads, the views read whole included, go through
  `Gltf_source_files` (`Gltf_data::source_files`), by byte source: one
  `erhe::file::Positional_file_reader` per buffer file, opened at the
  first read and shared by every task, which read at their own offsets in
  parallel without a lock, and the reader of each named buffer.
  `close_source_files()` closes the file handles once
  every payload is read - parse_gltf itself unless the payloads are
  deferred, the caller after its last job otherwise - and later reads (a
  released soup read back) open the file for that read only, so no handle
  outlives the load (an open handle makes a save's rename onto the file fail
  on Windows). It also calls `Gltf_buffer_reader::close()` of each named
  buffer, which releases what the reader holds open for the load (the
  prepared run's readers: `doc/erhe/asset_store.md` "Reading payloads"). The size and modification time checks apply to files only:
  a named buffer does not change.

## Content hashes

The content hash a mesh primitive and an image carry as loaded state (R3.5 of
`doc/plans/prepared_assets.md`; `doc/erhe/primitive.md` "Content hash"):

- `Gltf_parse_arguments::content_hashes` (`Gltf_content_hash_load`, default
  `none`): with `compute` the parse sets the content hash of each soup
  primitive it reads while the soup is in memory (in its mesh parse task, or
  through `Triangle_soup_source::load_content_hash()` in the build job of a
  deferred load) and of each image it decodes
  (`Gltf_image_source::set_content_hash()`, XXH3-128 over every byte of the
  encoded stream, in the decode task).
- No document stores a soup's hash: a scene file holds raw soups
  ("Authoring data"), whose hash a reopen computes from the soup, equal to
  the import's by construction, and a Scene item and a mesh item
  (`write_mesh_item()`) carry none, so their bytes do not depend on it. A
  `compute` parse of a file whose cores were prepared by an earlier build
  hashes those cores (the file is read as plain soups), so such a file keys
  differently from its source until it is saved again.
- A primitive loaded from stored GPU vertex streams (`make_stored_meshes()`)
  never reads its soup and has no hash (a prepared run computes none).

## Mesh items

A mesh item (`doc/plans/prepared_assets_design.md` D4) is what a prepared
export writes for one soup primitive, kept outside the glTF document:
`write_mesh_item(prepared, gpu_vertex_formats)` runs the exporter's
per-primitive writer (the code a `Prepared_mesh_data::write` export runs for
a primitive it writes inline, so the bytes are the same) on one `erhe::primitive::Prepared_primitive_export` - with
`gpu_vertex_formats` non-null it packs and writes the GPU vertex streams
too - and returns a `Gltf_mesh_item`:

- `payload`: the primitive's buffer views back to back, each 4-byte
  aligned, in the writer's order: the indices, the interleaved core stream
  (or the base and optimized GPU streams), `vertex_points`, the LOD level
  indices; the BVH is not in it (a Raytrace BVH item of its own, below);
- `meta`: a JSON object that describes the payload by itself, with indices
  local to the meta:

```
{"version":1,"byteLength":<payload bytes>,
 "bufferViews":[{"byteOffset":o,"byteLength":l,"byteStride":s,"target":t}, ...],
 "accessors":[{"bufferView":v,"byteOffset":o,"componentType":c,"count":n,
               "type":"VEC3","normalized":true,"min":[...],"max":[...],"name":"..."}, ...],
 "primitive":{"attributes":{"POSITION":a, ...},"indices":i,
              "extensions":{"ERHE_mesh_optimized":{...},"ERHE_mesh_topology":{...}, ...}}}
```

`byteStride`, `target`, `normalized`, `min` / `max` and `name` are present
when the view or accessor has them; bounds are integers for integer bounds
and decimals otherwise (doubles, exact). `extensions` holds the primitive's
`ERHE_mesh_optimized`, `ERHE_mesh_topology`, `ERHE_gpu_vertex_streams` and
`ERHE_mesh_lod` members as a `write` export writes them inline, their
bufferView and accessor indices into the meta's arrays. A primitive a
Scene item writes inline (no item for it) carries its BVH inside the document.

Export with items: `Gltf_export_arguments::mesh_item_provider` is asked once
per primitive shape, a soup shape or a geometry-normative one (no soup).
The `Gltf_mesh_item_reference` it returns names the mesh item (`uri`,
`meta`) and optionally the primitive's BVH item
(`raytrace_bvh`, a `Gltf_item_reference`).
A geometry-normative shape's item holds the core prepared from its derived
triangles (`erhe::primitive::prepare_derived_primitive_for_export()`,
`doc/erhe/primitive.md` "Derived triangles"; its core carries the
anisotropy control stream as the attribute `_ANISO_CONTROL`, which the
loader maps back to `custom_attribute_aniso_control`), and the shape is written
against it with no `ERHE_geometry`; a shape the provider has no item for
is written as `ERHE_geometry`. For a shape it returns a
`Gltf_mesh_item_reference` (`uri`, `meta`) for, the exporter prepares
nothing and writes no bytes: it adds a buffer with that `uri` and the
meta's `byteLength`, appends the meta's views (over that buffer) and
accessors, and writes the primitive's attributes, indices and extension
members with the indices moved by the file's view and accessor counts. With
a BVH item it then adds a second buffer with the BVH item's `uri` and
`byteLength`, appends the BVH meta's two views over it and writes the meta's
`extension` as the primitive's `ERHE_raytrace_bvh` with `nodes` and
`primitive_ids` moved to those views; without one the primitive has no
`ERHE_raytrace_bvh`. A mesh item meta that carries `ERHE_raytrace_bvh`
beside a BVH item, or a BVH meta that does not parse, fails the export.
`combine_buffers()` folds only the in-memory buffers into buffer 0 (the
GLB BIN chunk); the item buffers follow it, so a file whose every payload
is an item has no BIN chunk. A meta that does not parse fails the export
(empty string, logged). `Gltf_export_prepared_statistics::mesh_item_count`
counts the primitives written against items. A world-baked mesh
(`Gltf_export_arguments::world_bake`) is written against World-space mesh
items when `world_mesh_item_provider` returns one for each of its
primitives (`world_mesh_item_count` counts them): its primitives are written
from the items like a provider's, and nothing is prepared or baked for it -
a shape that only such meshes draw is not prepared at all. A baked mesh
without an item for every primitive writes its own baked core as before (its
shape is still prepared for the bake). With no provider the export is
byte-identical to one before mesh items.
erhe::gltf treats the uri as an opaque string; the editor forms it
(`erhe-item:<key>`, D1) and passes the provider, so erhe::gltf does not
depend on `erhe::asset_store`.

Load with items: `Gltf_parse_arguments::buffer_resolver` maps a buffer uri
to a `Gltf_buffer_reader` (`read_at(offset, span)`, called from any thread
in parallel), or to null for a uri it does not know, which is then read as
a file path. A resolved buffer is a byte source of `Gltf_source_files`, so
`Gltf_triangle_soup_source`, the prepared payload, the stored LOD indices,
the stored vertex streams, deferred loads and the read-back of a released
soup read item payloads through the code that reads file ranges. This
works for a parse from a file and from memory (`glb_data`). Without a
resolver an item buffer has no byte source and the parse fails ("cannot
read the buffers").

Tests: `test_gltf_mesh_item.cpp` with an in-memory store
(`prepared_primitive_round_trips_bit_for_bit`: core, topology, LOD level
indices and stored GPU streams read back through the mesh item, and the BVH
through the BVH item, equal the `Prepared_primitive_export` they were
written from;
`export_with_provider_writes_no_primitive_bytes`;
`load_through_resolver_equals_file_form`: from a file and from memory;
`released_soup_reads_back_from_the_item`;
`raytrace_bvh_item_is_its_own_buffer`: the BVH meta's views and the
primitive's `ERHE_raytrace_bvh` reading the BVH item's buffer;
`primitive_without_bvh_item_loads_without_stored_bvh`: no BVH reference, and
`Stored_bvh_use::ignore`;
`geometry_normative_primitive_round_trips_through_its_item`: a box of quads
written against its item carries no `ERHE_geometry` and loads the item's
core).

## Authoring data

A raw export (`Prepared_mesh_data::none`: the editor's Save Scene, every glTF
file the editor writes) writes a document of authoring data alone (R1.1 of
`doc/plans/prepared_assets.md`); no file carries derived data (R12.2):

- each soup as it is - for a shape with an authoring soup
  (`doc/erhe/primitive.md` "Authoring soup", a core loaded in place of its
  source content) the authoring soup, read from its source, in place of the
  core; `ERHE_mesh_optimized`, `ERHE_mesh_topology`, `ERHE_raytrace_bvh` and
  `ERHE_gpu_vertex_streams` are not written (nor declared);
- the primitive's LOD request (`Primitive::lod_settings`) as `ERHE_mesh_lod`
  with `settings` alone, no chain (`doc/gltf_extensions/ERHE_mesh_lod.md`
  "Request", R4.5);
- `Gltf_export_written_layout::retained_primitives` names every raw soup with
  its `Gltf_written_soup` (`soup` or `authoring`), so
  `rebind_sources_to_written_file()` after a save onto the source file points
  the soup source, or the authoring soup source, at the written accessors;
- each image as its source stream when core glTF or `KHR_texture_basisu`
  carries it. No image source of an authoring document is a raw block KTX2:
  raw GPU blocks are derived data, which a file parse refuses as a source
  (below) and the editor's texture imports refuse
  (doc/editor/content_library.md "Importing texture files"); the export
  asserts it (`ERHE_VERIFY`), and that a raw block `texture_export` comes
  with `Prepared_mesh_data::write`;
- no `ERHE_pvs`: `Gltf_export_arguments::pvs_item` is a Scene item's ("PVS
  items").

The parse reads the `settings` of every `ERHE_mesh_lod` as the primitive's
request, with or without a chain and in every document, before the
primitive is shared. A file parse (`Gltf_document::file`) reads none of the
derived-data extensions (see "Notes") and accepts no raw block KTX2 image
(`erhe::graphics::is_ktx2_raw_blocks()` of the image's header, read with the
mime sniff): it logs "Image '<name>' of '<file>': a raw block KTX2 image is
raw GPU block data, derived data that is not accepted as a source", and the
image has no source, settled as an image that does not decode (its texture
slots stay empty). An item parse accepts it (a quest Scene item, a Texture
item). Test: `test_gltf_raw_block_images.cpp`. A file saved by an earlier build that carries the derived-data
extensions opens as plain soups, and a save of it writes authoring data.
Tests: `test_gltf_authoring.cpp` (`a_file_parse_reads_no_derived_data`: the
same `write` document parsed as an item reads its prepared data, parsed as a
file reads none), `test_gltf_content_hash.cpp`, `test_gltf_pvs.cpp`
`a_file_parse_reads_no_pvs` (a document carrying `ERHE_pvs` with its bytes).

## Mesh item documents

`Gltf_export_arguments::mesh_item_document` (not empty) makes the export
write one node, one mesh and one primitive per `Gltf_mesh_item_reference`,
in order, named `mesh_item_<i>`, each primitive written against its item as
above (with its BVH item when the reference names one), in place of the node
tree of `root_node` (an empty node will do); nothing else is written. Parsed
with a `buffer_resolver` that knows the items, node `i` of the result is a
mesh whose primitive loads item `i`: the editor loads the world-space form of
an open scene this way (`doc/editor/world_space_bake.md` "Two forms").
`load_mesh_item_primitive()` does it for one item (`Gltf_mesh_item_load_arguments`:
the reference, the resolver, `triangle_soup_reload`, `mesh_payload`; the
parse runs serially on the calling thread) and returns the primitive, a
prepared soup primitive whose soup is the item's core; the editor's own
loading builds it in place of a source soup (`doc/editor/prepared_assets.md`
"The editor's own loading").

## Raytrace BVH items

A Raytrace BVH item is one prepared primitive's BVH
(`erhe::raytrace::Prebuilt_bvh`, `doc/erhe/gltf.md` "Raytrace BVH items")
kept apart from its mesh item, so mesh items do not change with the BVH
library: `write_raytrace_bvh_item(bvh)` returns a `Gltf_raytrace_bvh_item`
whose `payload` is the node records then the primitive ids (each 4-byte
aligned), and whose `meta` is

```
{"version":1,"byteLength":<payload bytes>,
 "bufferViews":[{"byteOffset":0,"byteLength":<record size * node_count>},{"byteOffset":o,"byteLength":<4 * triangle_count>}],
 "extension":{"version":1,"triangle_hash":"...","triangle_count":n,
              "builds":[{"backend":"bvh_v2"|"tinybvh","library":"<tag>","quality":"high","node_count":n,"nodes":0,"primitive_ids":1}]}}
```

The record size is the backend's (`erhe::raytrace::get_prebuilt_bvh_node_record_size()`:
28 for `bvh_v2`, 32 for `tinybvh`; `doc/erhe/raytrace.md` "Prebuilt BVH").
`extension` is the `ERHE_raytrace_bvh` value a `write` export writes inline, its
`nodes` and `primitive_ids` indexing the meta's views. The loader reads it
like any `ERHE_raytrace_bvh`: its views lie in the BVH item's buffer, which
the resolver gives a reader for, and its validation (triangle hash against
the mesh item's core, library tag) is the extension's.

## PVS items

A PVS item is a scene's potentially visible set database
(`erhe::scene::Pvs_database`, `doc/editor/pvs.md`) kept outside the glTF
document: `write_pvs_item(database)` returns a `Gltf_pvs_item` (nullopt for
a database that is not ready or whose bits cannot be read) whose `payload`
is the three `ERHE_pvs` payloads - the column records, the page entries and
the bits (`doc/erhe/gltf.md` "PVS items") - one after the
other, and whose `meta` is

```
{"version":1,"byteLength":<payload bytes>,
 "extension":{<the ERHE_pvs members before column_identities>},
 "column_uids":[["<node uid>",<primitive index>], ...],
 "columns":{"byteOffset":0,"byteLength":<80 * column_count>},
 "pages":{"byteOffset":o,"byteLength":<16 * page_count>},
 "bits":{"byteOffset":o,"byteLength":<4 * bit_word_count>}}
```

A range is absent for an empty payload. The columns are named by node uid,
since an item belongs to no document. Export with an item:
`Gltf_export_arguments::pvs_item` (a `Gltf_item_reference`), the only way an
export writes `ERHE_pvs`: the exporter resolves each column uid to the node
index of the export, adds a buffer with the item's `uri` and `byteLength`
and a view per range, and writes `ERHE_pvs` naming them; no PVS byte is
written. A column uid that names no exported node, or a meta that does not
parse, leaves the extension out (logged). Load: the views lie in a named
buffer, which the parse reads eagerly through the resolver's reader (an
extension reads them), so the database holds its bits in memory and has no
a file parse reads no `ERHE_pvs`. Tests: `test_gltf_pvs.cpp`
`item_round_trip_is_exact`, `item_column_outside_the_export_skips_the_extension`,
`stale_database_makes_no_item`.

## Image source streams

`Gltf_image_source` is where an image's compressed stream is. A parse from a
file keeps no image bytes in memory:

- an image in a bufferView of a .glb BIN chunk or an external .bin is the
  view's byte range of that file (`load_eager_buffer_views()` leaves image
  views unread); an image with a file URI is that whole file;
- the source records the file's size and modification time; `read()` returns
  the stream, reading the range just in time, and refuses (error logged) a
  file whose size or modification time changed;
- only the first 16 bytes are read at the parse, to sniff the media type;
- an image in a named buffer (a Texture item, below) is the view's byte
  range of that buffer, read through its `Gltf_buffer_reader`
  (`Gltf_image_source::reader`, `reader_offset`); the parse reads its first
  16 bytes only, and the source keeps no bytes;
- an image in a data URI, or an image in the own buffer of a parse from
  memory, keeps its bytes (`encoded_bytes`), as does a source a caller
  builds from a standalone image file.

The decode task reads the stream when it starts and drops it when the
decode ends; its memory-budget lease covers the stream plus the decoded
pixels until the upload. The exporter reads the stream when it writes the
image (and for a raw block texture export holds it until the transcode).
`get_memory_usage` reports the resident streams as
`cpu_images.retained_source_bytes`, 0 after a load from a file and for the
Texture items of a prepared run.

Tests: `test_gltf_texture_gpu.cpp` (`raw_ktx2_texture_round_trips_through_erhe_texture_ktx2`:
the parsed source is file-backed and reads the written bytes;
`file_backed_image_source_re_exports_and_refuses_a_changed_file`).

## Texture properties

Each image the parse prepares carries the texture properties of
`doc/erhe/graphics.md` "Texture properties" (`Gltf_decoded_image::texture_properties`),
which residency sets onto the `Texture` it creates:

- from `ERHE_texture_properties` (`doc/gltf_extensions/ERHE_texture_properties.md`)
  when the file has an entry for the image (`texture_properties_stored`);
- otherwise derived, once, from the uses: `parse_images()` lists every
  material slot that uses the image in material order (normal, occlusion,
  emissive, base color, metallic roughness, diffuse) with the wrap modes of
  the slot's sampler, and `derive_texture_properties()` takes the first; an
  image used in conflicting ways is logged ("Image '<name>': used as <slot>
  first and in <n> other way(s) ..."). A derived normal map takes two
  component when the opened image is an X+Y map (`Image_info::two_component_normal`),
  else three component.

The transfer function decides the color space the image is decoded in (the
`linear` of the decode), and `generate_mipmaps` off creates the texture with
the levels the image carries. The exporter writes `ERHE_texture_properties`
with an entry for every image it writes, from the texture's properties.
Tests: `test_gltf_texture_properties.cpp` (`derived_from_the_first_use`,
`stored_properties_replace_the_derivation`).

## Texture items

A Texture item (`doc/plans/prepared_assets_design.md` D8 step 6b) is one
image kept outside the glTF file as a raw block KTX2 file
(`doc/erhe/gltf.md` "Texture items"). The editor makes it
(`erhe::graphics::transcode_ktx2_basis_to_raw_blocks()`) and describes it
with `make_texture_item_meta(ktx2)`:

```
{"version":1,"byteLength":<payload bytes>,"mimeType":"image/ktx2","vkFormat":v,"format":"<erhe format>",
 "width":w,"height":h,"levelCount":l,"supercompression":<KTX2 scheme>,"twoComponentNormal":b}
```

A Texture item may instead hold a PNG / JPEG image as it is (the editor's
`texture_encoding` `source`), described by `make_source_image_item_meta(image)`:
`{"version":1,"byteLength":<bytes>,"mimeType":"image/png" | "image/jpeg"}`.

Export with items: `Gltf_export_arguments::texture_item_provider` is asked
once per texture the export writes; for a texture it returns a
`Gltf_item_reference` (`uri`, `meta`) for, the exporter reads no source and
writes no bytes: it adds a buffer with that `uri` and the meta's
`byteLength` (`parse_texture_item_meta()`), a view over the whole buffer and
an image of it of the meta's media type: a KTX2 image the texture names
through `ERHE_texture_ktx2`, a PNG / JPEG image as its plain `source`. A meta
that does not parse, or names another media type, fails the export.
`Gltf_export_texture_statistics::texture_item_count` counts them. Load: the
image's view lies in a named buffer, so its source reads the item through
the resolver's reader when the image is decoded (Image source streams,
above), and the KTX2 file uploads as a raw block image does. Test:
`test_gltf_texture_properties.cpp` `named_buffer_image_is_read_by_range`.

## Export progress

`Gltf_export_arguments::progress`, when not null, receives the export's
stage (`Gltf_export_stage`: `mesh_data` - released soups read back in
parallel, `meshes` - per-soup preparation, `scene`, `textures` - raw block
transcodes, `encode`) and the per-stage done / total counts through a
mutex; `Gltf_export_progress::get()` reads them from any thread.

## GPU vertex streams on export

`Gltf_export_arguments::gpu_vertex_formats` (a `Gltf_gpu_vertex_formats`:
the base and optimized vertex formats, skinned or not, the index format and
the encoder version) makes a prepared export write each prepared core as
GPU-ready vertex streams (`doc/erhe/gltf.md` "GPU vertex streams on export").
The editor's Export (doc/editor/prepared_assets.md) fills it from the running
`Mesh_memory` for its mesh items and Scene item; no file carries the streams.

`erhe::primitive::pack_triangle_soup_streams()` runs the loader's soup build
(`build_buffer_mesh_from_triangle_soup()`) into a capturing sink, so the
stored bytes are the bytes a loader's build uploads, plus the position and
texcoord affines it encoded them with. The parallel preparation of the
prepared cores packs them (one task per unique primitive). Per prepared
primitive the exporter writes:

- the indices as one `UNSIGNED_INT` accessor;
- each non-zero base stream as its own buffer view with `byteStride`, and the
  core attributes as accessors over those views (min / max on POSITION
  only), so a reader that ignores the extension reads the same soup as from
  a `write` export without `gpu_vertex_formats`;
- each non-zero optimized stream as a view, with accessors named only by the
  extension (`POSITION` and `TEXCOORD_0/1` carry `scale` / `offset` as
  hexadecimal floats, exact to the bit); the stream accessors are unnamed,
  which keeps the JSON the loader parses whole small;
- an all-zero stream as `{"fill":"zero","byteStride":s}`.

A primitive the layouts cannot express (a core attribute missing from the
base layout or packed to all zeros, a non-32-bit index format) is written as
the plain core and counted in
`Gltf_export_prepared_statistics::gpu_vertex_stream_skipped`;
`gpu_vertex_stream_count` and `gpu_vertex_stream_ms` count the written ones
and the packing time summed over the tasks. Test: `test_gltf_roundtrip.cpp`
(`gpu_vertex_streams_hold_the_loader_pack`: stored optimized streams equal
the pack of the parsed core byte for byte; the soup equals a plain prepared
save's).

## Stored vertex streams on load

A deferred parse (`Mesh_payload_load::deferred`) records each primitive's
`ERHE_gpu_vertex_streams` JSON in its prepared payload; the stream views and
the extension-only accessors are read with the payload, never at the parse
(`load_eager_buffer_views()` treats them as payload views).
`Gltf_triangle_soup_source::load_stored_vertex_streams()` checks the
variants against an `erhe::primitive::Stored_vertex_stream_request` (running
formats, index format, encoding version), reads the UNSIGNED_INT indices and
each stored stream into its own vector (a zero-fill stream becomes zeros),
parses the affines, copies the float positions out of the base stream and
validates the prepared data against them
(`Gltf_prepared_payload::validate()` with
`Validation_counting::accepted_only`: a rejection is counted by the soup
load that follows). Any mismatch returns null with a fixed reason
(`signature differs`, `encoding version differs`, `stream view mismatch`,
...). `Primitive::make_stored_meshes()` turns the result into the base and
optimized buffer meshes and the raytrace (doc/editor/async_asset_loading.md
"Stored vertex streams"). Test: `test_gltf_roundtrip.cpp`
(`stored_vertex_streams_build_like_the_soup`: the stored build uploads the
same bytes as the soup build of the same file, with the same affines and
bounds bit for bit; another optimized layout is declined and the soup path
loads).

## LOD chains (`ERHE_mesh_lod`)

A prepared export (`Prepared_mesh_data::write`: a mesh item, a Scene item)
writes `ERHE_mesh_lod` (`doc/gltf_extensions/ERHE_mesh_lod.md`) for a
prepared soup primitive whose `Primitive::lod_settings` is set (a chain
requested in the editor; `doc/plans/lod.md` D1) and stores no chain for any
other primitive; a raw export writes the request alone ("Authoring data"). The chain comes from `erhe::primitive::make_export_lod_chain()`
in the primitive's preparation task: a retained core whose stored chain was
made with the same settings writes that chain back; any other core gets the
chain `generate_soup_lod_chain()` makes over the export core with the
requested settings, which is the chain the on-request generation of the
loaded primitive makes over the same core (for a primitive whose live
variant came from the geometry path - an import before its first prepared
save, an edited primitive - the live chain was generated over the
optimizer's output in the GPU vertex format instead, so its chain before the
Export and in the prepared run can differ). That generation is the one
simplification on the export path: an Export on the Quest simplifies there for a
request without a current stored chain; a load never does. The chain is
`ERHE_mesh_lod` with two members beside the request's `version` and
`settings` (`doc/gltf_extensions/ERHE_mesh_lod.md`): `indices`, a raw
bufferView (no `byteStride`, no `target`) holding the levels after level 0
back to back as uint32 indices into the core vertices, and `levels`, one
`{index_count, error}` per level after level 0 (at most 7; `index_count` a
positive multiple of 3, `error` the object-space error as a hexadecimal
float string, strictly increasing and greater than 0). `Gltf_export_prepared_statistics::lod_stored_count`,
`lod_generated_count`, `lod_none_count` (a request whose core does not
simplify) and `lod_ms` count them.

On parse of an item document (`Gltf_document::item`; a file parse reads the
request alone) the chain is validated with the rest of the prepared mesh data
(`Gltf_prepared_payload::validate()`, rejected alone: the level counts and
errors as above, the bufferView holding exactly the levels' indices, every
index below the core vertex count): the settings and the
level table go into `Prepared_primitive_data::lod`; the level indices are
read with the soup, into `Triangle_soup::lod_index_data`, by the parse and by
every read back of a released soup (`Gltf_triangle_soup_source::load()`), and
by `load_stored_vertex_streams()` into `Stored_vertex_streams::lod_indices`.
`Gltf_prepared_mesh_statistics::lod_used_count` counts the accepted chains
(`LOD chains N` in the `prepared mesh data` log line). Test:
`test_gltf_roundtrip.cpp` (`mesh_lod_round_trips`: only the requested
primitive carries the chain, the parse reads it back bit for bit, a re-export
writes the stored chain back, a tampered level table is rejected alone).

## Text glTF files

A text export (`Gltf_export_arguments::binary` false) is a .gltf and one
buffer file, `<stem of path>.bin`, next to it. The export holds at most one
in-memory buffer: buffer 0, into which `combine_memory_buffers()` folds every
other. `export_gltf_document(arguments, path)` moves it out of the asset into
a `Gltf_export_file` (`file_name` the UTF-8 file name, `uri` the same name
percent-encoded: every byte but RFC 3986's unreserved ones, so a space, `%`,
`#` or `?` in the stem survives) and writes `uri` as the buffer's `uri`,
relative to the .gltf. Buffers that already name a URI (mesh items, external
files) stay as they are; images live in buffer views, so the .bin holds them.
An in-memory buffer past buffer 0, or a JSON that would name any other file
with no bytes behind it, refuses the export (logged, empty).
`write_gltf_document(path, document)` writes the .bin and then the .gltf
through `erhe::file::write_files_via_temp()`, all or none (doc/erhe/file.md
"Writing several files"). The parse resolves a buffer `uri` by splitting it
into its components first and percent-decoding the path after, so `%23` in a
file name is not read as a fragment.
`get_gltf_buffer_file_path(path)` is the `<stem>.bin` a text save to `path`
writes (empty for a .glb). Tests: `Gltf_roundtrip.text_gltf_writes_its_buffer_file`
(the string API refuses, a stem with a space, `%`, `#` and `?` gets its
encoded `uri`, the files reopen with the source soups) and `Gltf_roundtrip.text_gltf_save_onto_source_rebinds_released_soups`.

## Rebinding after a save

`Gltf_export_arguments::written_layout` receives where an export wrote the
primitives it wrote from their retained prepared core
(`Gltf_export_written_primitive`: the primitive, glTF mesh and primitive
index) and the images it wrote from their source stream unchanged
(`Gltf_export_written_image`). After the file is written,
`rebind_sources_to_written_file(path, layout)` reads the written file's JSON
(the BIN chunk skipped) and its buffer origins, gives each retained
primitive whose soup source references `path` or a file the written document
reads its buffers from (a text .gltf's `<stem>.bin`) a new
`Gltf_triangle_soup_source` over its accessors in the written file (the same
`make_gltf_triangle_soup_source()` the parse uses, the old source's tangent
mode) through `Primitive::rebind_triangle_soup_source()`, and returns a
file-backed `Gltf_image_source` per image for the caller to install. Its
reads open the file per read: no handle stays open. One info line reports
the counts. Tests: `Gltf_roundtrip.save_onto_source_rebinds_released_soups`
(a released soup refused after the save and read back bit-identical after
the rebind; a detached primitive keeps its resident soup).

## Triangle soup reload

`Gltf_parse_arguments::triangle_soup_reload` (`Triangle_soup_reload`, default
`none`) set to `record` makes `parse_gltf` give every soup primitive its
`erhe::primitive::Triangle_soup_source` (doc/erhe/primitive.md "Releasing the
triangle soup"), the source the parse built the soup through, so the
primitive may release its CPU soup after upload and read it back on demand:

- Per primitive, the source holds, for each bufferView its index and
  attribute accessors read, the byte range those accessors cover (file
  offset, length, stride; not the whole view: a file may pack every mesh
  into a few large views), copies of the accessors rebased onto the ranges,
  the attribute names, the primitive type and whether tangents are
  generated. A primitive with a sparse accessor, an EXT_meshopt_compression
  view or a buffer without an origin gets no source and keeps its soup.
- `load()` refuses, with an error logged, when a file's size or modification
  time differs from the recorded one; otherwise it reads the ranges into a
  minimal `fastgltf::Asset` and builds the soup with `build_triangle_soup()`,
  the function every soup is built with (index widening, the interleaved
  copy of the accessors, mikktspace tangents, the injected white COLOR_0),
  so the reload is bit-identical to the parse.

Tests: `src/erhe/gltf/test/test_gltf_soup_reload.cpp` (.gltf + .bin and .glb,
primitives sharing interleaved views, uint8 / uint16 indices, a changed file;
a parse of a .glb file equals the parse of the same bytes from memory and
through a reader (whose released soups read back through it), and a
.gltf + .bin parse equals the data URI parse of the same content, soups and
an animation read from a view that is read whole).

## Future work

- [plans/gltf.md](../plans/gltf.md) - open items of the glTF persistence design.
- [plans/gltf_prefabs.md](../plans/gltf_prefabs.md) - the remaining prefab phases.
- [plans/gltf_fast_load.md](../plans/gltf_fast_load.md) - prepared mesh data (optimized core, stored colocation, BVH) so an erhe-saved scene loads with no deferred work.
