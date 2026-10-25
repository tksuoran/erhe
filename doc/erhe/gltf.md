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
- Node transforms round-trip bit for bit. The parse stores a node's TRS
  exactly as the file holds it (each node is parented once, before its
  transform is set; a world-preserving re-parent would replace the TRS
  with inverse(parent world) * world). The export writes the TRS
  components the node holds; it composes through matrices, and so writes
  a `glm::decompose()` result, only when a non-identity import-root
  transform is folded into the node. The editor's open of an erhe-authored
  scene moves the top-level nodes under the scene root keeping their local
  transforms. Saving a loaded scene again therefore writes identical node
  transforms, and the re-saved file renders identically.
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
  `Gltf_texture_export::source` (the source stream: PNG / JPEG through
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
  when its soup is read - in its mesh parse task, or in the build job of a
  deferred load (`Mesh_payload_load::deferred`, "Reading buffers by range") -
  against
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
  `prepared mesh data: N used, M rejected (<reasons>); topology T, BVH B`
  (`finish_prepared_mesh_statistics()`, which a deferred load's caller runs
  after the last job). The checks the JSON and the accessors decide run at
  the parse (`read_mesh_optimized_metadata()`); the payload checks run in
  `Gltf_prepared_payload::validate()`.
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
it reads the byte ranges it needs. A parse from memory (`glb_data`) and
buffers in data URIs are resident and read through the asset as usual.

- `Gltf_file_stream` feeds fastgltf the file without the BIN chunk: fastgltf
  asks the buffer map callback for the chunk's memory, the callback answers
  with a marker address, and the stream turns the read into a seek. The
  file's buffer 0 is then a `fastgltf::sources::CustomBuffer` with no bytes.
  External buffers are not loaded (no `Options::LoadExternalBuffers`); they
  stay `sources::URI`. `scan_gltf` uses the same stream.
- `resolve_gltf_buffer_origins()` finds where each buffer is on disk - the
  BIN chunk of a .glb (offset from the chunk headers) or an external .bin
  (the buffer URI) - and stamps each file's size and modification time
  (`Gltf_buffer_origins`). A buffer in a data URI has no origin.
- `load_eager_buffer_views()` decides which bufferViews are read whole and
  which per primitive. A soup primitive (no `ERHE_geometry`) with indices,
  whose index and attribute accessors all lie in a buffer that is in a file
  and are neither sparse nor meshopt compressed, is read per primitive. A
  view on which every accessor is such a per-primitive accessor, and which
  no sparse accessor or other user (morph targets, skins, animations,
  instancing, geometry-normative primitives) reads, is left unread, and so
  are a per-primitive primitive's `ERHE_mesh_topology` / `ERHE_raytrace_bvh`
  payload views (read with its payload) and every image view (Image source
  streams, below). Every other view in a non-resident buffer - animation
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
- All these reads go through `Gltf_source_files` (`Gltf_data::source_files`):
  one `erhe::file::Positional_file_reader` per buffer file, opened at the
  first read and shared by every task, which read at their own offsets in
  parallel without a lock. `close_source_files()` closes the handles once
  every payload is read - parse_gltf itself unless the payloads are
  deferred, the caller after its last job otherwise - and later reads (a
  released soup read back) open the file for that read only, so no handle
  outlives the load (an open handle makes a save's rename onto the file fail
  on Windows).

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
- an image in a data URI, or every image of a parse from memory, keeps its
  bytes (`encoded_bytes`), as does a source a caller builds from a
  standalone image file.

The decode task reads the stream when it starts and drops it when the
decode ends; its memory-budget lease covers the stream plus the decoded
pixels until the upload. The exporter reads the stream when it writes the
image (and for "Save for Quest" holds it until the ASTC transcode).
`get_memory_usage` reports the resident streams as
`cpu_images.retained_source_bytes`, 0 after a load from a file.

Tests: `test_gltf_texture_gpu.cpp` (`raw_ktx2_texture_round_trips_through_erhe_texture_ktx2`:
the parsed source is file-backed and reads the written bytes;
`file_backed_image_source_re_exports_and_refuses_a_changed_file`).

## GPU vertex streams on export

`Gltf_export_arguments::gpu_vertex_formats` (a `Gltf_gpu_vertex_formats`:
the base and optimized vertex formats, skinned or not, the index format and
the encoder version) makes a prepared export write each prepared core as
GPU-ready vertex streams (`doc/gltf_extensions/ERHE_gpu_vertex_streams.md`).
The editor's "Save for Quest" fills it from the running `Mesh_memory`.

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
  a plain prepared save;
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

## Rebinding after a save

`Gltf_export_arguments::written_layout` receives where an export wrote the
primitives it wrote from their retained prepared core
(`Gltf_export_written_primitive`: the primitive, glTF mesh and primitive
index) and the images it wrote from their source stream unchanged
(`Gltf_export_written_image`). After the file is written,
`rebind_sources_to_written_file(path, layout)` reads the written file's JSON
(the BIN chunk skipped) and its buffer origins, gives each retained
primitive whose soup source references `path` a new
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
a parse of a .glb file equals the parse of the same bytes from memory, and a
.gltf + .bin parse equals the data URI parse of the same content, soups and
an animation read from a view that is read whole).

## Future work

- [plans/gltf.md](../plans/gltf.md) - open items of the glTF persistence design.
- [plans/gltf_prefabs.md](../plans/gltf_prefabs.md) - the remaining prefab phases.
- [plans/gltf_fast_load.md](../plans/gltf_fast_load.md) - prepared mesh data (optimized core, stored colocation, BVH) so an erhe-saved scene loads with no deferred work.
