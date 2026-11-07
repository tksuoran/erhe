# Editor assets, prepared assets and Export

Stability: experimental

An Export writes a scene into two stores beside its source file: the
editor asset, which holds every item derived from the source, and the
prepared asset of one target, which holds a copy of the items the scene
needs to run there. The requirements are `doc/plans/prepared_assets.md`
(cited as R-numbers), the design and the remaining steps
`doc/plans/prepared_assets_design.md` (D-numbers). Both stores are
`erhe::asset_store` containers (`doc/erhe/asset_store.md`); the items they
hold are mesh items (`doc/erhe/gltf.md` "Mesh items") - Local-space mesh
items and World-space mesh items (`doc/editor/world_space_bake.md`) - their
Raytrace BVH items (`doc/erhe/gltf.md` "Raytrace BVH items"), Texture items
(`doc/erhe/gltf.md` "Texture items"), the PVS item (`doc/erhe/gltf.md` "PVS
items", "PVS" below) and the Scene item.
The code is in `src/editor/assets/` (`editor_asset`, `item_keys`,
`scene_export`, `prepared_status`).

## Locations

- The editor asset of a source file is found in this order, by
  `Editor_asset_registry::get()` (`App_context::editor_asset_registry`),
  which opens it on first use, keyed by the normalized source path, and
  keeps it open for the editor's lifetime, so scenes that share a source
  file share one store (R10.1):
  1. the path the source records (R2.2): Save Scene writes `ERHE_scene`
     `editor_asset`, the editor asset relative to the saved file
     (`make_editor_asset_record()`: the open editor asset of that file, else
     the scene's own record while it saves onto its source, else
     `<stem>.erhe_editor`); an open of the file and an Export reading a
     prefab source or material container take it from there;
  2. `<dir>/<stem>.erhe_editor` beside `<dir>/<stem>.<ext>` when the source
     records none (R2.4);
  3. when that location cannot be written (a read-only asset pack), the
     fallback `cache/editor_assets/<stem>-<16 hex digits of XXH3-128 of the
     normalized source path>.erhe_editor` under the editor's working
     directory (R2.5), logged once per file per session ("Editor asset of
     '<source>': '<path>' cannot be written - using the fallback
     '<fallback>'"). A fallback editor asset is never recorded: a Save writes
     the default name, so files moved together to a writable place find or
     make their editor asset beside them, and the fallback stays behind
     (R2.6). When the fallback cannot be written either, the job fails with
     "<action> of '<scene>' failed: the editor asset of the scene '<source>'
     cannot be written at '<path>' nor at the fallback '<fallback>'" (a
     prefab source or material container: "no editor asset of the <role>
     '<path>' can be written") and stores nothing (R10.8).
  Each Export's report names where each editor asset was found
  (`location`: `record`, `default` or `fallback`). An editor asset runs in
  WAL mode: `-wal` / `-shm` files exist beside it while the editor runs.
- The prepared asset of target `<target>` (`pc` or `quest`, "Targets"
  below) is `<dir>/<stem>.<target>.erhe_prepared` (R2.4), or the `path` MCP
  `prepare_scene` names. Only Copy writes it, anew every Export, as a new
  container in rollback-journal mode (`Open_mode::prepared_create`), so it
  is one file at rest (R5.1); everything else, a prepared run included,
  opens it `prepared_read_only`, and nothing opens it for writing again.
- A scene without a source file (never saved) has no editor asset: Export
  refuses it with a message to save the scene first (R2.7). A USD-backed
  scene is refused (R1.7). Both, and a scene without export settings, are
  the refusals of "Export settings" below.

Records (`doc/erhe/asset_store.md` "Container"):

- editor asset: `source.relative_path` (the source file relative to the
  editor asset's directory), `source.scene_id` (the scene's `ERHE_scene`
  `scene_id`) and `prepared_path.<target>` per target (the prepared asset
  relative to the editor asset's directory, written by each Export);
- prepared asset: `source.relative_path` (relative to the prepared asset's
  directory), `source.scene_id`, `prepared.target`, `prepared.scene_key`
  and the stamp records of the exporting build ("Format stamps"):
  `prepared.mesh_stamp` (the Local-space and World-space mesh items),
  `prepared.bvh_stamp` (the Raytrace BVH items, `none` when the asset holds
  none), `prepared.scene_stamp` (the Scene item) and
  `prepared.texture_stamps` (the distinct stamps of its Texture items,
  sorted, joined by ` | `; empty without Texture items). Copy writes all of
  them with the items in its one write transaction
  (`write_prepared_stamp_records()` in `src/editor/assets/prepared_asset`).

## Keys

`src/editor/assets/item_keys.hpp` holds one function per kind (D3):

- Content hash of a primitive (`compute_soup_content_hash`, the key-field form
  of `erhe::primitive::compute_triangle_soup_content_hash()`): XXH3-128 over
  the soup's primitive type, its indices, and per vertex attribute in
  layout order the attribute's usage, usage index and format followed by
  every vertex's bytes of that attribute (padding between attributes is
  not read). The soup is the primitive's source triangle soup, or, for a
  geometry-normative primitive (a brush-made or graph-made mesh, an edited
  one), the triangles derived from its `Geometry`
  (`erhe::primitive::derive_triangle_soup()`, doc/erhe/primitive.md
  "Derived triangles"; R4.4). It is loaded state of the shape (R3.5,
  doc/erhe/primitive.md "Content hash"): the editor's parses of source files
  compute it on the load worker (`Gltf_content_hash_load::compute`; a prepared
  run computes none), a core loaded from its item in place of the soup keeps
  the soup's ("The editor's own loading"), and Save Scene writes the source
  soups (doc/erhe/gltf.md "Authoring data"), so a reopen computes the same
  hash and the erhe scene saved from a foreign glTF keys its primitives as
  the import did (R6.1). A
  geometry-normative primitive's hash is derived once per shape by the first
  job that holds the scene read-only (an Export, a bake, a status job); an
  edit that replaces content makes a new shape, and its undo restores the old
  shape with its hash. Key computation reads these hashes; a soup shape loaded
  without one is hashed once by the key computation (from its soup in memory,
  or read back), and counts as `computed_hash_count`.
- Content hash of an image: XXH3-128 over every byte of its encoded stream,
  set by the decode of a load that computes hashes
  (`erhe::gltf::Gltf_image_source::get_content_hash()`), carried to a
  replacement source of the same stream (`Content_library::set_image_source()`)
  and computed once by the key computation when missing.
- Local-space mesh key (`make_local_mesh_key`): kind, code version
  `c_local_mesh_code_version`, the format stamp, the source
  (`Local_mesh_source`: a `source.derived_triangles` field for derived
  triangles, none for a soup, since the two prepare differently), the content hash, the
  mesh optimizer options (the defaults `prepare_primitive_for_export()`
  runs) and the LOD request in force (the primitive's `lod_settings`, else
  the export's default LOD of `get_export_lod_settings()`; absent when
  neither is set).
- Format stamp of a mesh item (`make_local_mesh_stamp`):
  `layout=<XXH3-128 of the vertex layout signature>`. The signature names
  the four GPU vertex formats of the running `Mesh_memory`
  (`get_vertex_format_signature()`), the index format and the vertex
  encoding version. A mesh item holds no BVH, so mesh items are shared
  across BVH library versions.
- World-space mesh key (`make_world_mesh_key`, R4.7): kind, code version
  `c_world_mesh_code_version`, the mesh item stamp, the Local-space mesh key
  of the primitive and the node's transform relative to the scene root as
  `%a` text of its 16 floats (`doc/editor/world_space_bake.md` "World-space
  mesh items").
- Raytrace BVH key (`make_raytrace_bvh_key`): kind, code version
  `c_raytrace_bvh_code_version`, the format stamp and the key of its mesh
  item of either kind (R4.1); each mesh item has at most one BVH item per BVH library.
- Format stamp of a Raytrace BVH item (`make_raytrace_bvh_stamp`):
  `bvh=<backend>/<BVH library tag>` (`erhe::raytrace::get_prebuilt_bvh_backend()`
  and `get_prebuilt_bvh_library()`, the pinned `GIT_TAG` of the backend's
  library): a BVH item belongs to one backend at one library version.
- Texture key (`make_texture_key`, `Texture_key_inputs`): kind, code
  version `c_texture_code_version`, the format stamp, and per
  `Texture_item_source`:
  - `basis_source` (a Basis Universal source image): the content hash of the
    source image (above), the texture properties the transcode reads (`transfer_function`,
    `normal_encoding`; "Texture properties" below) and the export setting
    `texture_supercompression`. `generate_mipmaps` and the wrap modes are no
    key input: a Basis Universal source carries its own mip levels and the
    transcode reads neither.
  - `basis_texture_item` (a PNG / JPEG image under `texture_encoding`
    `target`): the key of its Basis texture item in place of the content
    hash, a `texture_encoding` = `target` field, and the same transcode
    inputs.
  - `source_bytes` (a PNG / JPEG image under `texture_encoding` `source`):
    the content hash and a `texture_encoding` = `source` field alone; the
    item holds the bytes, which no property changes.
- Basis texture key (`make_basis_texture_key`, `Basis_texture_key_inputs`):
  kind `basis_texture`, code version `c_basis_texture_code_version`, the
  format stamp, the content hash of the source image, all five texture
  properties (the encode reads each) and the encoder settings: the fixed
  `format` (`uastc_ldr_4x4`), `alpha` (`premultiplied`) and `mip_filter`
  (`kaiser`) and the `Basis_encode_settings` `uastc_level` and `zstd_level`
  (`doc/erhe/graphics.md` "Basis Universal encoder"). `texture_encoding` is
  no input, so switching it keeps the Basis texture items.
- Format stamp of a Basis texture item (`make_basis_texture_stamp`):
  `ktx2;basis=uastc_ldr_4x4;basisu=<basisu version>`; another encoder version
  asks for other keys.
- Format stamp of a Texture item (`make_texture_stamp`):
  `ktx2;blocks=<astc_4x4 | bc>`, the block family of the target (R3.9); the
  stamp makes the keys of the two targets differ, so each target has its own
  Texture items (R3.10). A Texture item holding a PNG / JPEG image as it is
  has the stamp `source` (`make_source_texture_stamp`) and no target: both
  targets share it.
- Format stamp of a Scene item (`make_scene_item_stamp`):
  `glb;textures=<source | astc_4x4 | bc>`, how it holds the images written
  inside it.
- Scene key (`make_scene_item_key`): kind, code version
  `c_scene_item_code_version`, the Scene item's format stamp, its target and
  the content hash of its bytes; a Scene item is made per target (R4.1), so
  two targets never share one even when their bytes agree.

A Scene item names a mesh item, a BVH item and a Texture item by a glTF
buffer whose `uri` is `erhe-item:<key as 32 lowercase hex digits>`
(`make_item_uri`, `parse_item_uri`): a primitive's accessors read its mesh
item's buffer, its `ERHE_raytrace_bvh` views read its BVH item's buffer and
an image's view is the whole buffer of its Texture item;
`collect_scene_item_keys` reads those uris from a GLB or glTF JSON
document.

## Several files

A scene's content can come from more than one file (R10): the scene file,
the prefab sources it instantiates as glTF 2.1 `externalAssets`
(`doc/plans/gltf_prefabs.md`), nested to any depth, and the material
containers its referenced materials are defined in (`ERHE_asset_reference`,
`doc/editor/asset_manager.md`). Each of them has its own editor asset,
through the registry ("Locations"), so a prefab used by ten scenes has its
items made once.

- Which file an item belongs to (R10.2), decided at `begin_scene_export()`:
  a primitive shape belongs to the file whose content the mesh node is - the
  source of the nearest instance arc above it (`get_first_instance_arc()`),
  else the scene; the depth is the number of arcs above it. A material
  belongs to the file whose container record defines it
  (`Asset_manager::get_defining_container_path()`: a loaded container, or a
  scene's own record), else to its mesh's file (a prefab template's material
  is its prefab's); a texture belongs to its material's file. A shape or
  texture that two files reach belongs to the first in the exporter's walk.
- Items whose inputs come from one file - the Local-space mesh item and its
  Raytrace BVH item, the Texture item and the Basis texture item - are stored
  in that file's editor asset; the World-space mesh items with their BVH
  items, the Scene item and the PVS item, whose inputs include how the
  content is used, in the scene's. Each editor asset gets its source record.
- Ensure runs per file in the order of R10.7: the files the scene depends on
  first, deepest prefab first (a stable sort by depth, a material container
  at depth 1), the scene last; per file its mesh items and BVH items, then
  its texture items, logged "Export of '<scene>': Ensure of the <role>
  '<path>' (depth <n>) in '<editor asset>': mesh items made .. present ..,
  ...". Then the scene's World-space mesh items.
- The Scene item is flat (R10.3, R10.9): `Gltf_scene_save_kind::scene_item`
  writes no `externalAssets` and no `ERHE_asset_reference` - the instances'
  nodes, meshes and materials are written as the open scene holds them
  (a glTF instance's nodes keep their seal and their derived uids), and a
  referenced material is written in full. The images of prefab templates and
  of loaded material containers are read from those parses
  (`Asset_manager::collect_container_image_sources()`, the prefabs'
  `Gltf_data`). Copy takes each item from the editor asset that holds it, so
  the prepared asset is one flat store and a prepared run reads no other file.
- The world-space bake admits the static meshes inside prefab instances
  (R10.4): they are no longer kept as external assets. Instances share their
  template's primitives, so `instancing` decides: `duplicate` gives each
  instance's mesh its own World-space mesh items (in the scene's editor
  asset), `skip` keeps the instances drawing the prefab's shared Local-space
  mesh items.
- Settings (R10.5): every file's content is prepared with the scene's export
  settings - the keys are computed from them. Where a prefab source has
  export settings of its own (its `ERHE_scene` `settings`, read from the
  file at the Export) and one of the fields that change its items differs
  (`lod`, `texture_encoding`, `texture_supercompression`, `raytrace_bvh`),
  Export logs "Export of '<scene>': the prefab '<path>' has export setting
  <field> = <prefab value> where the scene has <scene value> - the scene's
  value applies". Variants made for scenes with different settings coexist in
  the prefab's editor asset; the ones a scene does not ask for count as
  unreferenced in its status and are kept.
- Reload (R10.6): a reload of a prefab's instances (a Save of the prefab
  source, MCP `reload_prefab`) re-clones them with the re-parsed template's
  primitives, so the next Export or status computes their new keys; the
  reload is the change site `on_prefab_instances_reloaded()`
  (`scene/scene_save_service`), which marks the status of every scene whose
  instances it refreshed stale (logged "Prefab instances of '<scene>'
  reloaded: its status is out of date"). A closed scene computes them at its
  next Export.
- Failures (R10.8): before Ensure makes anything, every file is read
  (`ERHE_scene` of its glTF JSON) and its editor asset opened; a file that
  cannot be read fails the Export with "Export of '<scene>' failed: the
  <prefab source | material container> '<path>' cannot be read". A reference
  that did not resolve when the scene was opened (a prefab whose load failed:
  `Scene_root::get_unresolved_sources()`, `Prefab::unresolved_sources` for a
  nested one; a material whose container was missing, kept as a stub with
  its file key) fails it with a message naming the file. The job's error
  (`get_save_status` `error`) is that message; the prepared asset is not
  touched. A status job fails the same way.

`get_save_status` of an Export carries `export.files` and of a status job
`prepared_status.files` ("MCP").

## Export

File > Export (PC) / Export (Quest) and MCP `prepare_scene` run
`Scene_export` through `Scene_save_service`, with the save's staging:
quiescence before it starts, the read-only scene while it runs, the worker
thread, the status line ("Exporting '<scene>': <stage>" with the stage's
progress) and `get_save_status` (doc/editor/scene_serialization.md "Saving
in the background").

1. `begin_scene_export()` (main thread) refuses what `get_export_refusal()`
   refuses ("Export settings"), reads the scene's export settings, opens the editor asset, collects the
   export arguments of a `Gltf_scene_save_kind::scene_item` save onto the
   prepared asset path (prepared mesh data and GPU vertex streams, images
   as the target holds them, "Targets", and with `bake_static_to_world_space`
   the world-space bake planned with geometry-normative primitives
   admitted, `doc/editor/world_space_bake.md`), drops the open scene's PVS
   database from them (the Scene item names the PVS item `pvs_mode` chooses,
   "PVS" below; `pvs_mode` `rebake` is refused here without a region), collects the primitive shapes
   the exporter visits with their LOD requests - soup shapes and
   geometry-normative ones, a Geometry without facets being one the exporter
   writes nothing for -, the primitives of the baked meshes with their
   transforms and node records, and the textures of their materials
   ("Ensure, Texture items" below), and installs the mesh item, World-space
   mesh item and Texture item providers.
2. Ensure, mesh items (worker; the executor runs the per-shape tasks):
   writes the editor asset's source record; reads every other file the scene
   depends on and opens its editor asset ("Several files"); reads each soup
   shape's soup (resident or read back from its file), derives the triangles
   of each geometry-normative shape, and computes the mesh keys and the
   texture keys (step 3); then, per file in the order of R10.7, for each
   Local-space mesh key that a mesh the bake does not admit draws (R9.3) and
   that is absent from its file's editor asset, runs `prepare_primitive_for_export()` (a soup) or
   `prepare_derived_primitive_for_export()` (derived triangles),
   `make_export_lod_chain()` and `write_mesh_item()` on the first shape of
   that key and `put`s the item (kind `local_mesh`, no target, the format
   stamp). With `raytrace_bvh` on (and a raytrace backend that exports a
   BVH), a missing BVH item of the key is made in the same task from the
   prepared primitive's BVH (`write_raytrace_bvh_item()`, kind
   `raytrace_bvh`, its stamp), so a key whose mesh item and BVH item are both
   missing is prepared once and its BVH built once; a key whose mesh item is
   present and BVH item missing (another BVH library, `raytrace_bvh` turned
   on) is prepared again for its BVH alone. A primitive whose preparation
   built no BVH is written without one (logged). The file's Texture items
   follow (step 3); after every file the missing World-space mesh items
   of the baked primitives are made (`doc/editor/world_space_bake.md`
   "World-space mesh items"). The content builds of one Geometry run one at a time. The soups
   stay held until the Scene item is written, so the exporter reads none of
   them again. A shape that does not prepare (the skip reasons of the two
   preparations), and a baked primitive without a World-space mesh item, is written inside the Scene item - a soup as its soup, a
   geometry-normative shape as `ERHE_geometry` - logged with its kind and
   reason and counted as `inline_primitive_count`.
3. Ensure, Texture items (worker; the executor runs the per-texture tasks;
   per file, inside step 2): the textures of the five material slots of every
   material the exported meshes use, each once, with the image source the
   save would write and the texture properties as they are when the Export
   starts. Each source stream is read, and a Basis Universal KTX2 source and
   a PNG / JPEG source get their keys from their bytes ("Keys"). A key present in the editor
   asset is used as it is (R9.2). For the missing keys of PNG / JPEG images
   under `texture_encoding` `target`, each Basis texture item they need that
   the editor asset lacks is encoded first (stage `encoding basis texture
   items`, one task per image: `encode_image_to_basis_ktx2()`) and `put`
   (kind `basis_texture`, no target, its stamp, a meta
   `{"version":1,"byteLength":n,"mimeType":"image/ktx2","basis":"uastc_ldr_4x4"}`);
   it stays in the editor asset and is never copied (R4.1). Then for each
   missing Texture key the first texture of that key is made: a Basis
   Universal source, or the Basis texture item of a `target` image, is
   transcoded to the target's block format - BC7 (BC5 for a two component
   normal map) for `pc`, ASTC 4x4 for `quest`, in the texture's color space
   and normal layout, supercompressed as `texture_supercompression` says -
   and the raw block KTX2 file is `put` (kind
   `texture`, the target, its stamp, the meta of `make_texture_item_meta`); a
   `source` image's bytes are `put` as they are (kind `texture`, no target,
   stamp `source`, the meta of `make_source_image_item_meta`). Any other image
   (a raw block KTX2 source, DDS) and a texture whose item was not made (a
   failed encode or transcode, a build without the encoder) are written
   inside the Scene item as the source bytes (logged with the reason and
   counted as `inline_image_count`).
4. With `pvs_mode` `rebake`, the PVS bake ("PVS" below): the worker loads
   the world-space form from the World-space mesh items, the main thread
   installs it and bakes the PVS on it over frames, and the bake stores the
   PVS item; then the worker runs again for the steps below.
5. Ensure, Scene item: the export with the providers writes each baked
   mesh under the World Space node against its World-space mesh items
   (`doc/editor/world_space_bake.md` "Export (the Scene item)") and every
   other shape that has an item against its `erhe-item:` buffer, with its BVH item as
   the buffer of its `ERHE_raytrace_bvh` views (none with `raytrace_bvh`
   off), every image whose texture has a Texture item against that
   item's `erhe-item:` buffer, and `ERHE_pvs` against the PVS item `pvs_mode`
   names (none for `off`); the other images, skins and animations stay
   inside the Scene item. The Scene item is written with
   `Gltf_editor_state_scope::scene_item` (R4.9): no `ERHE_brushes`, no
   geometry graphs (`graph_meshes` and `node_bindings` of
   `ERHE_node_graphs`; texture graphs stay), and a graph's mesh is written as
   a mesh (its derived triangles) where a scene file leaves it to the graph
   to rebuild; the ghost mesh is left out of both. Its key is computed from
   its target, stamp and bytes; it is `put` (kind `scene`, the target, the
   target's Scene item stamp) when absent.
6. Copy (`write_prepared_asset()` in `src/editor/assets/prepared_asset`,
   R9.3) writes the prepared asset anew from the editor assets and reads
   nothing of an earlier one: it removes, with a warning ("removed '<path>',
   left by an Export that did not finish"), every
   `<file name>.tmp-<16 hex digits>` file an earlier, killed Export left
   beside the target, creates a new container at such a temporary path, and
   in one write transaction copies each key the Scene item names, the Scene
   item and the PVS item `pvs_mode` names from the editor asset of its file
   and writes the records ("Locations"). A key an editor asset lacks fails
   the Export, naming the key and its kind ("Export of '<scene>' failed:
   item <key> (<kind>) the Scene item names is not in the editor asset
   '<path>'; '<prepared asset>' is unchanged"). The committed, closed file
   is then renamed over the target (`std::filesystem::rename`, which
   replaces an existing file on POSIX and on Windows). On any failure the
   temporary file is removed and the target is untouched; a killed Export
   leaves the previous prepared asset as it was and at most one temporary
   file. A prepared asset is therefore complete by construction, and a
   prepared run does not validate its content.

   A prepared run of the target held open by another scene of the editor
   (Export refuses the prepared run's own scene, "Prepared run"): on POSIX
   the rename succeeds and the open run keeps reading the file it opened,
   which the file system frees when the run closes it; on Windows SQLite
   holds the file without delete sharing, so the rename fails and the
   Export fails with the rename's error, leaving the target and the run as
   they were.

Export logs to `editor.export` (the summary line "Export of '<scene>'
(<target>) done: ..." carries the counts) and the container to `erhe.asset_store`.

An Export changes neither the open scene's source binding nor its dirty
state (R9.5). Because the Scene item is made on every Export and stored
only when its key is absent, an Export of an unchanged scene makes nothing
in the editor asset (R9.4) and writes the same prepared asset again; the
Scene item's bytes must therefore be the same
from one export to the next. They are: every node has its glTF `uid` from
its creation, an imported glTF's derived from the file the same in every
session, and an Export assigns none (R6.2, `doc/editor/scene_serialization.md`
"Unique IDs"); every node is written with its own local transform (R6.1).

## PVS

The potentially visible set (`doc/editor/pvs.md`) is the one kind outside
the content-addressed rule (R4.10): its inputs are not tracked, so an
editor asset holds at most one PVS item per scene, the last bake's. Its key
(`make_pvs_key`) is the content hash of what the bake made - kind, code
version `c_pvs_code_version`, the stamp `erhe_pvs=<ERHE_pvs version>`
(`make_pvs_stamp`), the meta and the payload (`erhe::gltf::write_pvs_item()`,
`doc/erhe/gltf.md` "PVS items") - so a deterministic bake of an unchanged
scene finds its item again; the editor asset's record `pvs.key` names the
scene's current one. `store_pvs_item()` (`scene_export`, main thread) puts a
finished bake's item, removes every other PVS item and writes the record, in
one write transaction; every bake does it (the PVS window, MCP `pvs_bake`,
an Export). The bake runs on the world-space form, so its columns name the
nodes the Scene item writes under the World Space node (R8.9;
`doc/editor/pvs.md` "Terms").

The export setting `pvs_mode` (`Pvs_mode`, Scene_export_settings v3) states
what Export does about it (R7.6):

- `off` (the default): the Scene item has no `ERHE_pvs`, the prepared asset
  no PVS item.
- `reuse` (unsafe): the Scene item names the editor asset's PVS item (the
  record) and Copy writes it as it is, with no check that it matches the
  exported geometry; the log says so ("pvs_mode reuse: the PVS item <key> is
  copied as it is, with no check that it matches the exported geometry").
  Without one the Scene item names none and the log says "the editor asset
  holds no PVS item - the prepared asset holds none".
- `rebake` (safe): Export bakes a new PVS on the world-space form every time:
  after the Texture items the worker loads the world-space form from the
  World-space mesh items (as Bake World-Space Meshes does), the main thread
  installs it once its uploads are done (`start_scene_export_pvs_bake()`)
  and runs the PVS window's bake (disocclusion, 1024-pixel faces, the
  scene's region and cell size) through `Pvs_baker` over frames
  (`update_scene_export_pvs_bake()`), the bake stores the PVS item, and the
  worker then writes the Scene item naming it and runs Copy. It is refused
  before anything is made, with a message asking for a region or another
  mode, when `pvs_region_min` and `pvs_region_max` are both zero, and when
  `bake_static_to_world_space` is off (a Scene item without the World Space
  node does not hold the nodes the columns name), or while another PVS bake
  runs. The bake failing (the form unavailable, no static entry) fails the
  Export; a Scene item that left `ERHE_pvs` out (a column naming a node it
  does not hold) fails it too.

The Export reports the PVS row `counts.pvs` (`made` when the bake made an
item the editor asset did not hold, else `present`), `pvs_ms` and
`pvs_cell_count`. An Export of an unchanged scene with `reuse`, or with
`rebake` when the bake gives the same bits, makes nothing (and writes the prepared asset anew) (R9.4).
The status ("Status") names the PVS item the record names for `reuse` and
`rebake` in the Scene item it exports in memory (it counts as asked, not
unreferenced); for `rebake` without a PVS item the Scene key is unknown.

During an Export the editor is read-only from its start to its end, the PVS
bake included (R9.6, `doc/editor/scene_serialization.md` "Saving in the
background"): the bake's steps are the only scene work that runs
(`Scene_save_service::is_pvs_bake_stage()`), the status line shows
"Exporting '<scene>': baking the PVS" with the cells done, and
`get_save_status` `progress{stage: "baking the PVS", done, total}`. The
viewports keep drawing and the editor's session-only viewport camera keeps
moving (the fly camera tool moves a camera node that is `session_only`, and
the pointer reaches a viewport of the saved scene while its hover stays
blocked).

A prepared run reads the PVS with the Scene item (its `ERHE_pvs` views lie in
the PVS item; the bits are read with the parse), and its database names the
run's own nodes (form `source`), so its viewports are PVS culled. In the open
source scene a bake's database culls only in a viewport that draws the
world-space form (R8.10; `doc/editor/pvs.md` D5; MCP `set_viewport_mesh_form`).

## Texture properties

A source texture carries the properties its conversion reads (R4.2;
`doc/erhe/graphics.md` "Texture properties"): transfer function, normal
encoding, mipmap generation and the wrap modes of its edges. They are
authoring data of the texture: the Properties window shows them in the
texture's "Texture" group, MCP `get_item_properties` / `set_item_property`
read and edit them (an undoable `Property_set_operation`; `get_scene_textures`
lists the texture items), and a scene save writes them as
`ERHE_texture_properties` (`doc/gltf_extensions/ERHE_texture_properties.md`).
An open of a file that carries them takes them as they are; an import of a
file that does not (a foreign glTF, a scene saved before them) takes them
once from the material slots and samplers that use each image, the first use
deciding and the others logged (R4.3; `doc/erhe/gltf.md` "Texture
properties"). An edit takes effect at the next Export, save and load: a
Texture item is keyed by the properties its transcode reads ("Keys"), so an
edit of the transfer function or the normal encoding asks for another item,
and setting it back finds the first again (R3.3). Conversion consults no
material.

## Export settings

The scene's `Scene_export_settings` (`Scene_settings::export_settings`,
`src/editor/config/definitions/scene_export_settings.py`) are present or
absent (R7.1). A new scene and an imported one have none; adding them copies
the editor's default export settings (`Editor_settings_config::export_settings`
in `editor_settings.json`) into the scene, and from then on every field holds
a value of its own, saved with the scene in `ERHE_scene`. An erhe scene
opened without them (a file saved with every setting at its default writes no
settings object) gets the same copy at the open (`open_scene_gltf()`, logged
"has no export settings - added a copy of the editor's defaults"), so its
Export, status and bakes run without "Add Export Settings"; the scene does not
become dirty from it, and the file holds them after its next Save. They are
added and removed at two change sites:

- the scene's Properties, group "Scene Overrides" > "Export": "Add Export
  Settings" while the scene has none, else the fields and "Remove Export
  Settings";
- MCP `set_scene_settings`: an `export_settings` object in `settings`
  (`_version` optional) merges over the scene's export settings, or, when
  the scene has none, over the copy of the editor's defaults; `null` (with
  `merge`) or, without `merge`, leaving the key out removes them.

The scene's export settings are the only settings input of an Export, of a
status job, of Bake World-Space Meshes
and of the PVS bake (R7.2): they are read when the job starts, and a call
carries no settings of its own. MCP `prepare_scene` and
`save_scene_for_quest` refuse the arguments `export_settings`,
`texture_supercompression` and `container` (the choice of the prepared glTF
file, which the store replaced: R12.1), and `bake_world_space_meshes` refuses
`export_settings` (R12.4); a script changes a setting with
`set_scene_settings` before the call.

The fields beside the world-space bake's (`doc/editor/world_space_bake.md`
"Export settings"):

| Field | Type | Default | Meaning |
|---|---|---|---|
| `texture_encoding` | `Texture_encoding` | `target` | What the Texture item of a PNG / JPEG image holds (R7.5): `target` - the image encoded to the target's block format by way of its Basis texture item ("Export" step 3); `source` - the image bytes as they are, which the prepared run decodes at load. A Basis Universal image is transcoded either way |
| `texture_supercompression` | `Texture_supercompression` | `zstd` | The raw block images a target transcodes (the images the quest Scene item carries itself): Zstandard supercompressed (`zstd`) or stored as they upload (`none`) |
| `raytrace_bvh` | `Bool` | true | Each mesh item has a Raytrace BVH item, which the Scene item names and Copy puts into the prepared asset; off makes no BVH items and names none, and a prepared run builds the BVHs in memory ("Format stamps") |

`raytrace_bvh` is no key input: turning it off or on exports the same mesh
items and another Scene item; the BVH items an Export with it on made stay
in the editor asset, unreferenced while it is off ("Status"), and an Export
with it on again copies them back.

`get_export_refusal()` (`src/editor/scene/scene_root`) refuses, in this
order, a prepared run ("Prepared run"), a USD-backed scene (R1.7), a scene
without a source file (R2.7) and a scene without export settings (R7.3),
with the messages

- "<action> of '<scene>' refused: the scene is USD-backed, and Export
  covers glTF-backed scenes only (it has no editor asset)";
- "<action> of '<scene>' refused: the scene has no source file yet - save
  it first (its editor asset is made beside the file)";
- "<action> of '<scene>' refused: the scene has no export settings - add
  them (Properties > Export > Add Export Settings, or MCP set_scene_settings
  with an export_settings object)".

The action is `Export` or `Status`. File > Export logs the refusal as a
warning; MCP `prepare_scene`, `save_scene_for_quest`, `get_prepared_status`
and `drop_prepared_items` return it as the error; `Scene_save_service`
(when a queued Export or status starts) and `begin_scene_export()` refuse
the scene the same way. Bake World-Space Meshes (menu and MCP) refuses a scene as Export
does, with the action `Bake World-Space Meshes` (`get_export_action()`), and
the PVS bake (MCP `pvs_bake` and the PVS window, whose region rows then say
where to add them) refuses a scene without export settings with the third
message (`get_export_settings_refusal()`; action `PVS bake`).

Older settings: `migrate_scene_settings_json()`
(`scene/scene_settings_resolve.cpp`) rewrites a settings object before its
codegen parse (an `ERHE_scene` file, USD `customLayerData`, MCP
`set_scene_settings`). A v1 object carries the PVS grid at the top level,
which moves into `export_settings`; a v1 object without the grid has no
export settings. A v2 object has them when it carries the `export_settings`
key; a scene saved with every setting at its default wrote no settings
object and gets the editor's defaults at the open (above). A `Scene_export_settings` v1
`lod` of null (which read the editor's `lod`) reads as the `Lod_config`
defaults, the same on every machine. An `editor_settings.json` older than
v16 gets the default export settings with its own `lod` as their LOD
request.

## Targets

Export writes one prepared asset per target; the mesh items and the editor
asset are shared, the Scene item is made per target:

| Target | File | Scene item images (stamp) | Menu |
|---|---|---|---|
| `pc` | `<stem>.pc.erhe_prepared` | Basis Universal images, and PNG / JPEG images under `texture_encoding` `target`, as BC7 / BC5 Texture items (`ktx2;blocks=bc`); PNG / JPEG images under `source` as shared Texture items of their bytes (`source`); an image without a Texture item inside the Scene item as its source bytes (`glb;textures=source`) | File > Export (PC) |
| `quest` | `<stem>.quest.erhe_prepared` | The same images as ASTC 4x4 Texture items (`ktx2;blocks=astc_4x4`) and shared `source` Texture items; an image without a Texture item inside the Scene item as it is, a Basis image transcoded to raw ASTC 4x4 KTX2 (`ERHE_texture_ktx2`; `glb;textures=astc_4x4`) | File > Export (Quest) |

The GPU vertex streams of a mesh item are written in the layouts of the
running `Mesh_memory`, for either target, so
the mesh item stamp is the exporting build's. The desktop Vulkan build and
the Quest build have the same four vertex layouts (their startup lines
"Vertex format signatures" agree), so a `pc` and a `quest` Export of one
scene name the same mesh items; a build whose layouts differ asks for
different keys (R3.9), and a prepared run of an asset made by it fails
("Format stamps").

## Format stamps

A prepared run checks the stamps the prepared asset records against what
the running build reads (R11.4, R11.6; `check_prepared_formats()` in
`src/editor/assets/prepared_asset`), before the Scene item is parsed. The
stamps are the records Copy writes with the Scene key ("Locations"): Ensure
makes every missing item by its current key, the key includes the stamp
(R3.9), and nothing writes a prepared asset after Copy, so every item the
Scene item names carries the exporting build's stamps and the open reads
four records instead of the item table. A prepared asset without one of
them (made before the records) is refused like an unread format version:
"prepared asset '<path>' cannot be opened: it has no <record> record; this
build checks a prepared asset's compatibility by the format stamps an Export
records (export it again)". The stamps are a compatibility check of the
file with this build, not a validation of the asset: the open reads the
format version, `source.relative_path`, `prepared.target`,
`prepared.scene_key`, the four stamp records and the Scene item's size, and
nothing else. A damaged asset (an item gone) fails at the parse, as a glTF
file with a missing `.bin` does. The open
logs "Prepared asset '<path>': stamp records checked in <ms> ms". The
build's side is `get_prepared_build_formats()` (`scene_export`): the
platform's target (`quest` on Android, else `pc`), the mesh item stamp of
the running `Mesh_memory` and BVH library, and the Scene item stamps whose
images it reads (`source` and `astc_4x4` everywhere - a device without ASTC
decodes the blocks on the CPU - and `bc` where the device samples BC7 and
BC5), and the Texture item stamps of the same block families
(`ktx2;blocks=astc_4x4` everywhere, `ktx2;blocks=bc` where BC7 and BC5 are
sampled), and `source` everywhere.

- A recorded mesh stamp that differs fails the open: a prepared asset holds
  nothing to rebuild GPU streams from.
- A recorded BVH stamp that differs opens: the check makes
  `get_stored_bvh_use()` `ignore`, the parse then reads no primitive's
  `ERHE_raytrace_bvh` (`Gltf_parse_arguments::stored_bvh_use`, logged
  "stored BVHs are not used"), and the raytrace of every primitive is built
  in memory from its core.
- A recorded BVH stamp `none` (exported with `raytrace_bvh` off) opens with
  the notice "the local_mesh and world_mesh items of '<path>' (target
  <target>) have no raytrace_bvh items (exported with raytrace_bvh off) -
  their BVHs are built in memory" (R11.5).
- A recorded Scene item stamp or Texture item stamp the build does not read
  fails the open, naming the target ("... - this build does not sample or
  decode their block format" for Texture items): a prepared asset holds no
  source image to transcode again (R11.4, R11.6).
- An asset of another target than the platform's opens when its stamps
  pass, with a notice naming its target.

The buffer resolver logs an item the Scene item names that the store lacks
("Prepared asset '<path>' lacks item <key> its Scene item names") when the
parse resolves its buffer, and that buffer reads as missing.

A BVH built in memory is built the way an open builds the raytrace of a
primitive a file carries no BVH for (doc/editor/async_asset_loading.md):
with `load.deferred_raytrace` an AABB proxy at the load and the triangle
raytrace by the deferred raytrace task of the open, else at the load. Every
ray query (the viewport hover, MCP `raycast`, the physics and transform
tools) reads the scene's raytrace, so a prepared run builds the BVHs it
lacks for the whole scene right after the load, not on a tool's first ray
query; nothing builds a BVH per query.

Each mismatch is one line per record and stored stamp, with the kinds and
both stamps: "the <kinds> items of '<path>' (target <target>) carry format
stamp '<stored>' (recorded at Export) where this build has '<expected>' -
<effect>". A
failed open's error is "prepared asset '<path>' of target <target> is not
compatible with this build: " followed by the failing lines; the load logs it ("Open
'<path>' failed: ..."), and MCP `load_scene` / `open_scene` resolve the open
before they queue it (`resolve_prepared_open()` on the main thread) and
return the error; an open that proceeds is handed to the load
(`Load_scene_file_message::prepared_open`, `Asset_load_request::prepared_open`),
which does not resolve it again. The lines of an
open that proceeds are logged as warnings ("Prepared run: ...") and returned
in the reply's `format_notices`. Unit tests: `editor_asset_tests`
`Prepared_format_check.*` and `Prepared_asset_test.*`.

## Status

The status of a scene's stores (R13.1, R8.11) is computed on request:
`Prepared_keys` (`src/editor/assets/prepared_keys`, one per `Scene_root`,
`Scene_root::get_prepared_keys()`) keeps the last computed status and marks
it stale at the change sites of the inputs the keys are computed from (R3.7,
R8.12). Its change sites, all change-driven:

- `Scene_root::register_mesh` / `unregister_mesh` of a content mesh that is not
  a render proxy (a mesh added or removed, its undo);
- `Scene_root::on_mesh_primitives_changed` (a content edit swaps the primitive,
  its undo restores it, `doc/editor/operations.md` "Undo of an in-place edit");
- `Scene_root::on_mesh_transform_changed` of a mesh the last check found
  admitted by the world-space bake (the meshes whose World-space keys a move
  changes; other moves, such as a simulated body's, are ignored);
- `Scene_root::on_export_settings_changed()`: every row of the Properties
  "Export" group (a drag at its release) and MCP `set_scene_settings` when the
  export settings change;
- `on_prefab_instances_reloaded()` (R10.6);
- a job that wrote a store: an Export, Bake World-Space Meshes, a drop, a PVS
  bake (every scene's status is marked, since stores are shared, R10.1);
- a Refresh, or a status asked for another target or prepared asset path.

A change marks the status stale and computes nothing (2026-11-07, user
decision); the status shown stays the last computed one, flagged stale. An
update is requested by Update or Refresh in the scene's Properties or by MCP
`get_prepared_status` `update` / `refresh` (`Prepared_keys::request_update()`).
`Scene_save_service::update()` serves the scenes' `Prepared_keys` in the
prepare phase (M2): with no save queued or running and the scene quiescent
(no operation in flight, no load, no live edit), a requested update of a
stale status starts a key check, and the frame stays free of its work (R3.8):

1. On the main thread, `begin_scene_export()` in `Scene_export_mode::key_check`
   collects what the keys read, as Export collects it (the shapes, the
   world-space bake's plan, the textures, the files); the shapes are held for
   the check.
2. On a thread of its own, with the scene usable, the keys are computed from
   the stored content hashes ("Keys"; no file is read and no soup rebuilt) and
   measured against each file's editor asset as its item catalog lists it
   (`Editor_asset::get_item_catalog()`: every item's kind, target and payload
   size, listed once and listed again only after a write invalidates it -
   every job that writes a store does, and so does Refresh), so the presence
   lookups of a check are in memory; a key absent from the editor asset is
   looked up in the prepared asset.
3. The check publishes per kind `present` and `missing`, the stored and
   unreferenced counts and bytes, the files, the sizes and the prepared
   asset's presence. The Scene key is the hash of the Scene item's bytes,
   which only an export of the scene in memory gives, and that reads the live
   scene: when every item the Scene item names is present, the key is taken
   from the last Export when no change came since it, else the check leaves
   it open (`scene_key_checked` false) and queues a status job, which computes
   it with the scene read-only (below). A check that meets a
   geometry-normative primitive whose hash is not derived yet leaves it open
   the same way (`hash_pending_count`). When an item is missing the Scene item
   is missing too and no export is needed.

A status job is `Scene_export` in `Scene_export_mode::status`, queued through
`Scene_save_service` (`request_prepared_status()`) and run with an Export's
staging (quiescence, the read-only scene, the worker thread, the status line
"Checking '<scene>': <stage>"). It derives the missing content hashes, reads
each key's meta from the editor asset, else from the prepared asset, exports
the Scene item in memory with the mesh item provider exactly as Ensure exports
it and computes its key (`make_scene_item_key`). It stores no item and writes
no record. While a status job the editor queued itself runs, an MCP request
that the read-only scene would refuse waits for it instead.

The stores are measured against the keys the scene asks for (R3.3): the
Local-space mesh keys of the shapes a mesh the bake does not admit draws, the
World-space mesh keys of the baked primitives (R13.1: how many are valid),
their BVH keys while `raytrace_bvh` is on, the Texture keys, and the Scene
key; each key against the editor asset of its file ("Several files"), the
counts summed over the files, and every file listed in `files` with its
editor asset's path, `location` and size: per kind `present` and `missing`
(an unknown Scene key counts as one missing Scene item), `stored_count` /
`stored_bytes` and `unreferenced_count` / `unreferenced_bytes` (editor asset
items the scene does not ask for; payload bytes), the editor asset's size
(`get_database_size()`, the WAL excluded), and the prepared asset's presence,
size, item count and recorded Scene key; `asked_key_count` is the number of
keys over the kinds and `computed_hash_count` the content hashes the job
computed. The Basis texture keys of the PNG / JPEG images under
`texture_encoding` `target` are asked too: they count per kind and the
`unreferenced` drop keeps them, but a missing one leaves the Scene key known
(a present Texture item needs none). The prepared asset matches when it
records the target and the Scene key and holds exactly the asked keys but
the Basis texture keys - what Copy would leave in it; `mismatch` says why not.

A shape whose triangles cannot be derived is written inside the Scene item
by both Export and the status and is counted as `inline_primitive_count`. A
soup that does not prepare (the skip reasons of
`prepare_primitive_for_export()`) has a key but never an item, so it counts
as missing and the prepared asset never matches.

A scene without an editor asset has no status ("the scene has no editor
asset yet - Export makes it"): a check never makes an editor asset. An open
of a source asset makes one ("The editor's own loading"), so this is a scene
that was not opened from its file (an import into a new scene, a load
without build jobs) and was never exported. A scene without export settings, without a source file, a USD-backed
scene and a prepared run are refused, with the refusal as the status error.

The status covers the change sites above. Other edits change the Scene item
alone (a light, a material value, a name, a material's textures) and reach
the status at the next change site, at Refresh or at Export.

Drops (`Prepared_drop`), each a status job that measures after the drop:

- `unreferenced`: the editor asset items the scene does not ask for under its
  current settings;
- `target`: the editor asset items of the target (Scene items and Texture
  items carry their target; mesh, BVH, Basis texture items and `source`
  Texture items have none);
- `all`: every editor asset item (the records stay);
- `prepared_asset`: deletes the target's prepared asset file.

Drops change the scene's own editor asset alone: a prefab's or a material
container's editor asset serves other scenes too.

Item drops remove in one write transaction and then `VACUUM`
(`drop_store_items()`); the next Export makes the dropped items again. The
pure parts (`measure_store_items`, `measure_catalog_items`, `select_drop_keys`,
`drop_store_items`, `compare_prepared_items`) are tested in `editor_asset_tests`.

The scene's Properties, group "Scene Overrides" > "Export", show the status
`Prepared_keys` keeps (`Scene_root::get_prepared_status()`): "Prepared Status"
("current", "out of date" after a change, "not computed" before the first
update, "checking..." while a requested update runs, or the error), with
Update (enabled while the status is stale) and Refresh, which also lists the
stores again for a change no edit of this editor reports (another editor
writing the same editor asset, R5.6, or a file copied over it); "Prepared Asset Matches", "Editor Asset" and "Prepared
Asset" (path and size), a row per kind ("Mesh Items", "World-Space Mesh
Items", "BVH Items", "Texture Items", "Basis Texture Items", "Scene Items"),
and "Drop" with Drop Unreferenced, Drop PC Items, Drop All Items and Delete
Prepared Asset; a drop button arms the row, which then asks Confirm or Cancel.
The buttons are disabled while a check or a status job of the scene runs. A
scene without a source file, a USD-backed scene and a prepared run show that
they have no editor asset; a scene without export settings shows "Add Export
Settings" alone.

## MCP

`prepare_scene` (target `pc` or `quest`; `save_scene_for_quest` is it with
target `quest`): `path`
is the prepared asset (default `<stem>.<target>.erhe_prepared` beside the
source file;
`.erhe_prepared` is appended when missing); the scene's export settings
are the settings ("Export settings"). The job is
reported by `get_save_status` like a save, with `format: "store"`; a done
job carries `path` (the prepared asset), `file_bytes` (its size), `save_ms`
and `export`:

```
{"files":[{"source":"...","editor_asset":"...","role":"prefab|material_container|scene","depth":n,
           "location":"record|default|fallback","counts":{"<kind>":{"made":n,"present":n}}}],
 "target":"pc","editor_asset":"...","prepared_asset":"...","scene_key":"<hex>",
 "counts":{"local_mesh":{"made":n,"present":n,"written":n},
           "world_mesh":{"made":n,"present":n,"written":n},
           "raytrace_bvh":{"made":n,"present":n,"written":n},
           "texture":{"made":n,"present":n,"written":n},
           "scene":{"made":n,"present":n,"written":n},
           "pvs":{"made":n,"present":n,"written":n}},
 "inline_primitive_count":n,"inline_image_count":n,"world_mesh_node_count":n,"world_form_mesh_count":n,
 "prepared_item_count":n,"prepared_asset_bytes":n,
 "ensure_ms":t,"scene_ms":t,"copy_ms":t,"pvs_ms":t,"pvs_cell_count":n}
```

`files` lists the source files in Ensure order (the scene last) with what
Ensure did in each editor asset. `made` and `present` are the Ensure step against the editor assets (unique
keys), `written` the items Copy wrote into the new prepared asset (every
item it holds); the `raytrace_bvh` row is present when Ensure asked for BVH
items, the `texture` row when the scene has a texture, the
`basis_texture` row (made and present; never written) when a missing Texture
item of a `target` PNG / JPEG image needed its Basis texture item, the
`world_mesh` row when the bake admits a mesh node (`world_mesh_node_count`
of them; `world_form_mesh_count` is the bake job's).
While the job runs it carries `progress{stage, done, total}` (R9.7), the
stage being `computing mesh keys`, `making world-space mesh items`,
`making mesh items`, `computing texture keys`, `encoding basis texture
items`, `making texture items`,
`writing scene item`, `baking the PVS` (with `pvs_mode` `rebake`; done and
total are cells) or `copying items` (and for Bake World-Space Meshes and a
`rebake` Export `loading the world-space form`), as the status line shows it.

`get_prepared_status` (arguments `target`, `pc` by default, `path`,
`update` and `refresh`) answers at once with the status of "Status":
`{scene_name, target, pending, stale, error, prepared_status{...}}`, where
`stale` is true when a change came after `prepared_status` was computed,
`update` requests an update (the Properties Update) and `pending` is true
while a requested update has not finished (a check or a status job runs or
waits to run; a client passes `update` once and polls until it is false);
`prepared_status` is absent while pending. A target or path other than the
last one switches the status to them (stale until an update), and `refresh`
is the Properties Refresh. `drop_prepared_items`
(argument `target`) queues a status job with the drop and reports through
`get_save_status` like an Export: a done job carries `prepared_status{...}`
(doc/agents/mcp_server_usage.md "Saving scenes").

The `load_scene` / `open_scene` reply of a prepared asset names the scene
that opens: the source's stem when the source is readable, else the
prepared run `<stem>.<target>` (`get_prepared_open_scene_name()` of the
resolved open); for a
prepared run it carries the `format_notices` of "Format stamps", and a
prepared asset this build cannot run is refused with the check's error.

## The editor's own loading

The editor asset is the editor's own store of derived data (R8.1 - R8.4): a
scene open of a source asset - an erhe scene or a foreign glTF, opened by
File > Load Scene, `--scene`, MCP `load_scene` / `open_scene` - reads the
Local-space mesh items and Raytrace BVH items of its file in place of
deriving them, and stores what it derives. The code is
`src/editor/assets/editor_asset_load` (`Editor_asset_load`) and the build
jobs of `Gltf_load_task` (`run_editor_asset_job()`,
doc/editor/async_asset_loading.md "Mesh build jobs").

1. The parse task (worker) parses the file, then opens the file's editor
   asset through the registry ("Locations": the `ERHE_scene` record, the
   default location, the fallback; created empty when absent, R8.1) and lists
   its item catalog once, logged "Open '<file>': editor asset '<path>' (<n>
   items), <ms> ms". It takes the scene's export settings from the
   `ERHE_scene` payload (an erhe scene without them takes the copy of the
   editor's defaults the open gives it; a foreign glTF has none).
2. Each build job (one per unique shape, on the executor) loads its soup,
   which computes its content hash (R3.5), and computes the Local-space mesh
   key and its Raytrace BVH key with `make_shape_local_mesh_key()`, the
   computation of Export and the status: the primitive's LOD request, else
   the export settings' default LOD (`get_export_lod_settings()`), else none.
   A key present in the catalog: the job reads the item's meta and loads the
   item (`erhe::gltf::load_mesh_item_primitive()`, the item's payload read
   by range from the editor asset) with its BVH item (R8.2). A key absent:
   the job prepares the soup, makes the items with `make_local_mesh_items()`
   (`src/editor/assets/local_mesh_items`, the one implementation of Export's
   Ensure), queues them for the store and loads the primitive from the queued
   bytes (R8.3). A mesh item present without its BVH item (another BVH
   library) gets its BVH built over the item's core and stored.
3. The job builds the item's primitive as a prepared run builds one (its
   GPU streams when soups are released, else its core), with the stored BVH
   as its raytrace, and installs it in place of the source shape: the shape
   is the item's core, keyed by the source content hash, and holds the
   source soup as its authoring soup (doc/erhe/primitive.md "Authoring
   soup"), which Save Scene writes. The primitive's own LOD request is not
   changed; with a default LOD the drawn variant carries the chain Export
   makes, as in a prepared run.
4. The queued items are written in write transactions of up to 512 items or
   64 MiB, by the job that fills a batch and, for the rest, by the job that
   finishes last; each write invalidates the item catalog, and after the
   build every scene's status is marked stale (`notify_stores_changed()`),
   so `Prepared_keys` finds the stored items present with no Export (R8.12).
   A failed write is logged; the primitives keep reading their items from
   memory and the next open derives them again.
5. When the build is done the load logs "<file>: <n> primitives from items,
   <m> derived and stored, <k> BVH from items, <j> built and stored, <ms>
   ms" and a second line with the items stored (count, bytes,
   transactions) and the shapes built without items, with the reasons.

The work of the editor asset is the build jobs': the catalog is listed
before the first job, keys are computed from hashes in memory and the
presence lookups are in the catalog, so the frame is free of it and the
scene appears when its builds are done (R3.8).

What the jobs take from items and what they do not:

- A geometry-normative shape (`ERHE_geometry`, brushes, graph meshes) is
  built from its `Geometry` as before: the items' cores are the derived
  triangles (R4.4), which the polygon facets the editor edits do not map
  to. Export makes their items.
- Imports into an open scene, prefab templates, a prepared run, and a load
  without worker contexts (no build jobs) do not use editor assets; Export
  makes their items.
- Texture items: the open transcodes and decodes images as before (no Texture
  or Basis texture item is read at the open).
- An item made from a shape's core would round the attributes its GPU
  streams quantize (the core of an item with GPU streams is the base stream):
  the items of a shape that has an authoring soup are made from that soup
  (`prepare_shape_for_items()`), by the load, Export's Ensure and the world-space
  bake alike, so a key's item has the same bytes whoever makes it.

The editor asset's items are the only persistent derived data (R8.4,
R12.3): the mesh optimizer and the raytrace keep no disk cache (no
`cache/mesh_optimizer`, no `cache/bvh`; directories an earlier build left are
not read), so a shape not taken from an item is optimized and its BVH built
at each open (`doc/erhe/raytrace.md` "BVH sources"). Save Scene, and every
other glTF file the editor writes, carries authoring data only (R1.1,
R12.2; doc/editor/scene_serialization.md "File anatomy", doc/erhe/gltf.md
"Authoring data"): no derived-data extension, and no raw block KTX2 image -
a file parse and a texture import refuse one as a source
(doc/editor/content_library.md "Importing texture files"), so no scene
holds one but a prepared run, which is not saved. A file parse reads none of those
extensions: a file of an earlier build that carries them opens as a plain
glTF file (logged "derived-data extensions not read"), its primitives built
from their soups and its items made and stored as above.

Measured (R13.3; Linux headless Release, NVIDIA, warm page cache; Bistro
saved as an erhe scene by `scripts/prepared_asset_bistro_verify.py`, 2683
meshes, 3852 primitives in 3852 shapes of 3272 keys, LOD chains requested;
`<stem>.erhe_editor` deleted before the first open):

| | first open | second open |
|---|---|---|
| listed / idle | 4.3 - 6.4 s / 4.6 - 6.7 s | 1.22 s / 1.46 s |
| build phase (the log line) | 3.6 - 5.6 s: 0 from items, 3852 derived and stored, 3852 BVHs built and stored | 0.44 s: 3852 from items, 3852 BVHs from items |
| BVH builds / preparations (mesh optimizer runs) | 3317 (the other BVHs read from the `cache/bvh` entries earlier builds left, which this build no longer reads) / 3852 | 0 / 0 |
| stored | 7704 items (3272 keys of each kind; 669 MB; 17 - 18 transactions) | nothing |

The editor asset is 639 MB after the first open (WAL excluded). An Export
of the reopened scene with the bake off makes 0 of its 3272 mesh items and
0 of its 3272 BVH items; after `drop_prepared_items` `all` an Export makes
them again, and those 6544 items are byte-identical (stamp, meta and
payload) to the ones the open stored. The scene file written by Save Scene
is 515 MB (soups and images, no derived data). Shapes of equal content are
prepared once per shape, not once per key (the 580 repeated puts replace
their item with the same bytes).

## Prepared run

File > Load Scene (its dialog lists `.erhe_prepared` files), `--scene` and
MCP `load_scene` / `open_scene` accept a prepared asset; every one of them
goes through `Load_scene_file_message` and `queue_open_scene_gltf()`, and the
code is `src/editor/assets/prepared_asset`.

1. `resolve_prepared_open()` (on a load worker, in the scan phase of
   `Gltf_load_task`, or on the main thread for MCP `load_scene` /
   `open_scene`, which hand the result to the load) opens the store
   `prepared_read_only` and reads its `source.relative_path`, resolved
   against the prepared asset's directory.
2. When that source file is readable the load opens the source instead, as
   any source asset (R11.1), and logs "Open prepared asset '<path>': its
   source '<source>' is readable - opening the source instead".
3. Otherwise it reads `prepared.scene_key` and the stamp records, checks
   that the store holds the Scene item, checks the recorded format stamps
   ("Format stamps"), and logs the notice
   "Prepared run of '<path>' (target <target>): its source '<source>' cannot
   be read". The parse reads the Scene item through a `Prepared_item_reader`
   (`make_scene_item_reader()`, `Gltf_parse_arguments::glb_reader`) as it
   reads a `.glb` file: the JSON chunk for the parse and the BIN chunk by
   range, so the Scene item is never held whole in memory. Its buffers go
   through the buffer resolver of `make_prepared_buffer_resolver()`, which maps each
   `erhe-item:<key>` uri to a `Prepared_item_reader` over
   `Asset_store::read_payload`; the deferred payload loads of the build jobs
   and the read-back of a released soup (a selection's full build) read
   through it on worker threads, and so does the decode of each image whose
   view lies in a Texture item: the image's source reads the item's payload
   by range when its decode starts (`doc/erhe/gltf.md` "Image source
   streams"), so nothing of a Texture item stays in memory after its upload,
   and the raw blocks upload with no transcode. The scene opens as an erhe-authored scene
   named `<stem>.<target>`, with the prepared asset as its source path.
4. A store that does not open (a missing file, not an SQLite file, an unread
   format version), a missing stamp record, a missing Scene item, a Scene
   item that does not parse or a recorded format stamp the build cannot read
   fails the load with a logged error; nothing is opened.

On the Quest the prepared asset is pushed into the app's external files
directory and opened with `--scene <device path>` (doc/agents/quest.md
"Prepared assets on the device").

`Scene_root::set_prepared_asset()` marks the scene a prepared run. The
`Prepared_asset` (the open store and its target) is owned by the
`Scene_root` and by every buffer reader its primitives hold, so the store
stays open for the life of the scene and closes, logged as "Prepared asset
'<path>' closed", when the closed scene's content is released (D9).

The scene is edited live like any other and the edits last for the session
(R11.2). `get_prepared_run_refusal()` refuses its writers with the message
"<Save | Export | glTF export> of '<scene>' refused: the scene is a prepared
run of '<path>', which holds no source data; edits last for this session
only (open the source asset to save or export)":

- File > Save Scene, File > Export (PC) / Export (Quest), File > Save glTF (standard) log it as a warning;
- MCP `save_scene`, `prepare_scene`, `save_scene_for_quest` and `export_gltf`
  return it as the error;
- `begin_scene_save_gltf()` and `begin_scene_export()`, which every scene
  save and Export runs, refuse the scene the same way, so no other path
  writes it and no editor asset is made for it.

## Verification

`py -3 scripts/prepared_asset_verify.py --editor <headless editor>` (usage in
its docstring) launches its own headless editors
(doc/agents/editor_runs.md), stops them by the pid it launched, and works in
a temporary directory it removes afterwards (`--keep` keeps it). It writes
two hand-written glTF scenes: `small` (soup meshes, one primitive drawn by
three nodes, one of them mirrored, untextured and PNG-textured materials, a
skinned mesh, an animation and a directional light) and `many`
(`--mesh-count` distinct soup meshes, 600 by default, so the Ensure and Copy
stages last long enough to interrupt). Each is opened as a foreign glTF and
saved by the editor, and the erhe-saved file is the source asset. The checks,
one PASS / FAIL line each, with a summary count and a non-zero exit code on
any failure:

- The editor's own loading ("The editor's own loading"): Save Scene writes
  no `ERHE_mesh_optimized`, `ERHE_mesh_topology`, `ERHE_raytrace_bvh`,
  `ERHE_gpu_vertex_streams`, `ERHE_texture_ktx2`, `ERHE_pvs`,
  `source_content_hash` or LOD chain into the saved `small`; its first open logs 0 primitives from items, 4 derived and
  stored, 0 BVHs from items, 4 built and stored, and the status finds the
  mesh items present with no Export; the open in a new process logs 4 from
  items and 4 BVHs from items, 0 built, and no mesh optimizer run of its
  meshes (the procedural helper geometry of the open builds its BVHs); the whole run creates no `cache/mesh_optimizer` and no
  `cache/bvh` directory and touches no file in one an earlier build left,
  and no `.gltf` / `.glb` file of the run (Saves, inputs) carries a
  derived-data extension (R12.2, R12.3).
- Export of `small`: the four mesh items and four BVH items the open stored
  found present, one Scene item made, all written, every mesh of the Scene item a mesh item, no mesh item
  carrying `ERHE_raytrace_bvh` and every primitive's `ERHE_raytrace_bvh`
  reading one BVH item of stamp `bvh=<backend>/<library>`, the records of both stores
  (the stamp records are the stamps of the items the Scene item names), and
  the prepared asset holds exactly the items its Scene item names plus the
  Scene item (read with SQLite from the container, "Container" of
  `doc/erhe/asset_store.md`).
- The foreign `small.gltf`, opened again and exported with the same export
  settings into its own editor asset, makes the mesh, BVH and Texture items
  of the erhe scene saved from it: the same keys and the same bytes (the
  saved file holds the source soups, "Keys"; R6.1); the foreign editor asset
  also holds the items its own open stored, keyed without the export
  settings' LOD request (it had no export settings).
- In a new editor process a second Export makes nothing (and writes the prepared asset anew) and has
  the same Scene key.
- After a `set_mesh_attribute_values` move of one torus vertex an Export
  makes one mesh item, its BVH item and one Scene item; after `undo` an
  Export makes none and has the first Scene key.
- `get_prepared_status` with `update`, polled until nothing is pending and
  never asked to refresh, after the second Export: no item missing, the prepared asset
  matches, the Export's Scene key, both paths and sizes; after the vertex
  edit: one mesh item, its BVH item and the Scene item missing, no match;
  after undo and Export: matches, with the edit's mesh item, BVH item and
  Scene item unreferenced; after a `texture_encoding` edit to `target`: one
  Texture item and one Basis texture item missing, no match; set back to
  `source`: nothing missing and a match with the last Scene key. `drop_prepared_items` `unreferenced` leaves no unreferenced
  item and a smaller editor asset, and the next Export makes and copies
  nothing; `prepared_asset` deletes the file, and the next Export copies
  every item again.
- With the source in place, `load_scene` of the prepared asset opens the
  source (the reply's `scene_name` and the log notice).
- With the source moved away it opens as the prepared run `small.pc`,
  logging "stamp records checked" once and no per-item stamp line, and
  `render_scene_image` from two cameras, in the bind pose and at a sampled
  animation time, gives the source scene's images pixel for pixel (decoded
  RGBA compared), and MCP `raycast` along the torus axis through its hole
  hits the floor at the source scene's distance (a bounding box proxy would
  hit the torus); Export of the prepared run is refused.
- Target `quest`: an Export of `small` for quest makes no mesh or BVH item,
  copies the four mesh items and four BVH items the pc asset holds (one
  vertex layout serves both) and
  makes one Scene item of target `quest` with stamp `glb;textures=astc_4x4`
  and its own key, into `small.quest.erhe_prepared`; the pc prepared asset
  is unchanged and the editor asset records both paths. With the source
  away the quest asset opens on the host as a prepared run with the notice
  naming target `quest` and renders the source images pixel for pixel (the
  scene's image is a PNG, a shared `source` Texture item). Copies of it
  whose stamp records are changed with SQLite: a recorded mesh stamp of
  another layout refuses `load_scene` (the kinds, both stamps); a recorded
  BVH stamp `bvh=other library` opens with the raytrace_bvh notice, logged,
  the stored BVHs unused, and the torus-hole ray answers as in the source
  scene once the BVHs are built in memory; a recorded Scene item stamp
  `glb;textures=etc2` refuses the load naming target `quest`. A copy whose
  mesh item stamps differ from the records runs (the open reads no item
  stamp), and a copy without one of the four stamp records is refused
  naming the record.
- `raytrace_bvh` off (`set_scene_settings`) on a copy of `small`: the Export
  makes no mesh item (the mesh items of the setting on serve), no BVH item
  and a new Scene item, and Copy writes no BVH item; the prepared
  asset holds no BVH item and no primitive has `ERHE_raytrace_bvh`; a second
  Export makes nothing; the status matches with the four BVH
  items unreferenced; with the source away the prepared run carries the
  "have no raytrace_bvh item" notice, renders the source image and answers
  the torus-hole ray as the source scene; `raytrace_bvh` on again makes
  nothing, writes the four BVH items back and has the first Scene key.
- The editor's default scene, saved and reloaded (its brush-made meshes are
  geometry-normative): Export makes a mesh item per mesh from the derived
  triangles, writes no primitive inside the Scene item, and the Scene item
  carries no `ERHE_geometry`, `ERHE_brushes` or geometry graphs; a second
  Export makes nothing; with the source moved away its prepared run is
  rendered from two cameras and compared with the source scene's images.
- Texture properties and Texture items, on a scene of a plane and a cube
  with a Basis Universal base color and normal map (two images of the Bistro
  download, `res/editor/assets/bistro_lights_compressed`; the checks print
  `SKIP` without them): the import derives sRGB and not a normal map for the
  base color with the sampler's wrap modes, linear and two component for the
  normal map; an MCP `set_item_property` of `generate_mipmaps` is saved in
  `ERHE_texture_properties` with the derived properties and read back after
  a reload. The pc Export makes and copies two Texture items (target `pc`,
  stamp `ktx2;blocks=bc`, BC7 sRGB and BC5 metas), writes no image inside
  the Scene item, and the Scene item's images are views of their `erhe-item:`
  buffers; the quest Export makes two Texture items of its own (stamp
  `ktx2;blocks=astc_4x4`) and no mesh item. `transfer_function` set to linear
  makes one Texture item and one Scene item; set back, nothing, with the
  first Scene key; the status then matches with the linear item
  unreferenced. A second Export in a new process makes nothing (and writes the prepared asset anew).
  With the source away the pc prepared run transcodes nothing ("0 Basis KTX2
  transcoded, 2 raw block KTX2 uploaded as stored"), retains no source image
  bytes (`get_memory_usage` `cpu_images.retained_source_bytes` 0) and renders
  the source scene's images pixel for pixel from two cameras; the quest asset
  runs on the host with its ASTC blocks decoded; a copy whose
  `prepared.texture_stamps` reads `ktx2;blocks=etc2` refuses the load (the
  kind and the target named).
- `texture_encoding` (R7.5; `--only-encoding` runs these alone), on a scene of
  a plane and a cube with a 64 x 64 PNG base color and a 32 x 32 PNG normal
  map (three component), exported with the default `target`: the pc Export
  makes two Basis texture items in the editor asset (no target, stamp
  `ktx2;basis=uastc_ldr_4x4;basisu=<version>`, Basis Universal KTX2
  payloads) that the prepared asset does not hold, and two Texture items of
  target pc (stamp `ktx2;blocks=bc`, BC7 sRGB and BC7 linear with mips) the
  Scene item's images name, with no image inside the Scene item; the status
  counts the Basis texture items present and matches. `source` makes two
  Texture items with no target, stamp `source` and the exact PNG bytes, under
  other keys, and leaves the Basis texture items in place; `target` again
  makes nothing and has the first Scene key; the quest Export makes two
  ASTC 4x4 Texture items from the same Basis texture items. In a new process
  a second Export makes nothing (and writes the prepared asset anew). With the source away the
  `target` prepared runs (pc: BC7 uploaded as stored; quest on the host:
  ASTC decoded) render within 24 / 255 per channel and 1.5 / 255 on average
  of the source scene (UASTC and the block format are lossy, and basisu
  makes the mips where the source load makes them on the GPU; the encoder
  test bounds level 0 at 8 / 255 on a smooth gradient and the sun of
  intensity 3 scales a texel error up to about three times; measured: at
  most 9 / 255, mean 0.31 / 255), and the `source` prepared run renders
  pixel for pixel; both retain no source image bytes
  (`cpu_images.retained_source_bytes` 0).
- Identities (R6.1, R6.2): `small` opened as a foreign glTF in two
  processes has the same node uids and bit-identical world transforms
  (`get_scene_nodes` `world_from_node_hex`), and its Export in the second
  process has the first Scene key and makes nothing. Saved with the import
  root at the identity (the file holds no node for it) and after a
  `set_node_transform` of the root (the file holds it as a node), each save
  reopened in a new process has the same uids and bit-identical world
  transforms, the moved root as a plain node.
- `many` (each load sets an export LOD request of 3 levels, so Ensure has
  the mesh items to make that the open, which stored its own, did not ask
  for): an Export killed with `SIGKILL` while `get_save_status` reports
  `making mesh items` (with items of the Export already in the editor asset) and another
  killed while it reports `copying items`, before any prepared asset
  exists, leave no prepared asset and one temporary file, which the next
  Export removes (logged) while it writes every item; a third Export killed
  while copying over that prepared asset leaves it byte-identical, with at
  most one temporary file; run again, the editor asset and the prepared
  asset have the same keys, and per key the same kind, stamp, meta and
  payload bytes, as an uninterrupted Export of the same source in another
  directory, the same Scene key and stamp records, and no temporary file
  remains. Every Export check also finds no temporary file beside the
  prepared asset.

The checks above export with `bake_static_to_world_space` off and, but for
the `texture_encoding` checks, with `texture_encoding` `source`, so the PNG of
`small` is a Texture item of its bytes and its prepared runs render pixel for
pixel; the
world-space checks of `doc/editor/world_space_bake.md` "Verification" follow
them (`--only-world` runs those alone).

Bistro: `py -3 scripts/prepared_asset_bistro_verify.py --editor <headless
editor> --bistro <copy>/bistro_lights_c.gltf` (usage in its docstring; R13.2)
takes a copy of `res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf`
outside `res/`, opens it as a foreign glTF, rotates the import root -90
degrees about X, generates the LOD chains for the scene, adds export settings
(the bake on, `instancing` `duplicate`, the PVS grid of `doc/agents/quest.md`:
4 m cells over the content bounds in x and z, y 0.6 - 4.6, 2068 cells), bakes
the world-space meshes and saves an erhe scene, which is the source asset of
the checks. Its work directory is removed at the end; `--keep-quest <dir>`
keeps the quest prepared asset and a copy of the source asset beside it for
the device, the source as the baseline to compare the prepared run with
("Prepared assets on the device" in `doc/agents/quest.md`). Headless Release, Linux, NVIDIA (2026-11-07, after
D8 step 14), all 29 checks pass in 299 s:

- R6.1: the 2781 saved nodes keep their uids and bit-identical world
  transforms through the save and the reopen (the editor's default camera is
  session-only and not saved), and the World-space mesh items of the foreign
  scene's bake are the reopened scene's: 3795 keys in common, each with the
  same payload bytes (the saved file, 515 MB, holds the source soups, R3.5).
- The open of the saved scene derives and stores its Local-space mesh items
  and BVH items ("The editor's own loading"); the first Export (pc) finds the
  10 it asks for (the kept, animated nodes) and their 10 BVH items present and
  makes the rest: 3795 World-space mesh items, their 3795 BVH items, 266
  Texture items and the Scene item, in 7.7 s (ensure 6.5 s - the World-space
  mesh items are made from the authoring soups -, scene 0.2 s, copy 0.7 s);
  prepared asset 7877 items, 1014 MB, exactly what its Scene item
  names, no item of target quest, no Basis texture or Editor data item, no
  Local-space mesh of a baked node.
- In a new session a second Export makes nothing and writes the prepared
  asset anew (Copy of the 6811 items, 946 MB, of a Bistro without the bake:
  0.63 - 0.66 s on the host), the
  status matches, and Bake World-Space Meshes makes nothing with all 2670
  admitted nodes valid.
- A texture's `transfer_function` set to Linear: the Export makes one Texture
  item and the Scene item; set back, nothing, with the first Scene key. A
  baked node of one primitive moved 1 m: one World-space mesh item, one BVH
  item and the Scene item; after undo every node is valid and the Export
  makes nothing, with the first Scene key.
- An Export killed while making World-space mesh items (6812 of the 14401
  items of the editor asset there at the kill) and another killed while copying (the rollback journal beside the
  prepared asset), then run again: the rerun copies every item, and both
  assets hold the uninterrupted first Export's keys with the same kind,
  target, stamp, meta, info and payload bytes per key.
- With the source away the pc prepared run is listed in 0.8 s (idle 1.0 s,
  VmHWM 1640 MB, no retained image bytes). From the bench camera of
  `scripts/bistro_frame_bench.py` at 960 x 540 it differs from the source
  meshes in 0.98 % of the pixels, 0.030 % by more than 2 / 255 (the rounding
  of the baked vertices, max 170 / 255 at silhouette pixels; the bound is
  0.5 % of the pixels by more than 2 / 255), and from the open scene's
  world-space form in one pixel by 1 / 255.
- `pvs_mode` `rebake`, Export for quest: 211 s (the PVS bake of 2068 cells
  204.3 s), 266 Texture items (ASTC 4x4) and one PVS item made, every mesh
  and BVH item shared with pc; prepared asset 7878 items, 1044 MB, nothing of
  target pc; its prepared run on the host reports the PVS `ready` with the
  open scene's `bits_hash` (`fd5d5edadffa53f7`).

`--negative-control image` alters one pixel of a prepared-run image,
`--negative-control items` leaves one mesh item out of the prepared asset's
item set and `--negative-control drop` skips the drop of unreferenced items;
each makes its check FAIL.

The edited torus is geometry-normative after the move, so its mesh item is
made from its derived triangles; the undo restores the torus's original
primitive (doc/editor/operations.md "Undo of an in-place edit"), whose soup
gives the first Export's key.

The default scene's images depend on the anisotropy control stream: the
brushed-metal materials of its solids read it, and the circular-brushed-metal
tangent mix of the shader variant reads it for the floor too. Its mesh items
carry the stream (`_ANISO_CONTROL`, doc/erhe/primitive.md "Derived
triangles"), so the prepared run draws what the geometry build draws.

## Editor assets of earlier versions

Mesh items made before Raytrace BVH items existed (code version 1, stamp
`layout=...;bvh=...`) carry their BVH inside. Nothing converts them: the
keys an Export asks for now are other ones, so an Export of such a scene
makes new mesh items and BVH items, and the old items stay in the editor
asset as unreferenced items, which the status shows and Drop Unreferenced
removes. A prepared asset made before the stamp records ("Format stamps")
fails a prepared run naming the missing record; Export it again.

## Verification of several files

`prepared_asset_verify.py` (`--only-multi` runs these alone; `--negative-control
multi` keeps the prefab source readable, which makes the first R10.8 check
FAIL) builds, with the editor, `prefab_b.glb` (a PNG-textured torus),
`prefab_a.glb` (a box and an instance of `prefab_b`), the material container
`materials.gltf` (a PNG-textured material) and `host.glb` (a floor of the
container's material, a pillar and two instances of `prefab_a`), each
erhe-saved, and checks:

- each saved file records its editor asset in `ERHE_scene` (R2.2);
- the Export names four files, `prefab_b` (depth 2), `prefab_a`, `materials`,
  the host, and the log shows Ensure in that order (R10.7); each editor asset
  lies beside its file and holds what R10.2 assigns it: `prefab_b` a mesh item,
  its BVH item and a Texture item, `prefab_a` a mesh item and its BVH item, the
  container a Texture item, the host its two meshes, their BVH items and the
  Scene item (the host's two mesh items and BVH items stored by its open,
  the prefabs' made by the Export, since prefab templates load without
  items); the prefab's items are made once for both instances;
- the prepared asset holds exactly the items its Scene item names, from every
  editor asset; the Scene item has no `files`, `externalAssets` or
  `ERHE_asset_reference` and holds both instances' nodes; the status matches;
- an unreadable prefab source and an unreadable material container
  (`chmod 0`) fail the Export naming the file, the prepared asset unchanged;
- in a new process a second Export makes nothing (and writes the prepared asset anew); `prefab_a` with
  its own export settings (`raytrace_bvh` off) logs that one setting with
  both values, and the host's value applies (R10.5);
- a Save of an edit of `prefab_b` opened as a scene marks the host's status
  stale and starts no key check; an update then misses one mesh item and
  does not match; MCP
  `reload_prefab` after restoring the file matches again (R10.6);
- `bake_static_to_world_space` with `instancing` `duplicate` makes six
  World-space mesh items in the host's editor asset, none in the prefabs';
  `skip` keeps the two shared prefab mesh items (R10.4);
- with every source file moved away the prepared asset opens as the prepared
  run `host.pc` and renders the source images pixel for pixel (R10.3);
- the files moved together and the scene renamed, its editor asset is found
  through the record (`location` `record`) and the Export makes nothing
  (R2.2, R2.6);
- in a read-only directory every file's editor asset is a fallback under
  `cache/editor_assets`, the fallback is logged once per file and a second
  Export makes nothing; with that directory unwritable too the Export fails
  naming the scene's file and both locations (R2.5, R10.8). The checks print
  `SKIP` when a read-only directory is writable for the user (root).

## Future work

- [plans/prepared_assets_design.md](../plans/prepared_assets_design.md) -
  the steps of D8
  (change-driven keys, the store as the only prepared format).
- Several files: a material that a prefab template references from a
  container whose load failed is a stub the Export does not detect (the
  scene's library holds no key for it); a USD prefab source is exported as
  the open scene holds it but its editor asset reads no `ERHE_scene`; Bake
  World-Space Meshes does not read the dependency files first.
- The status computes only on request (Update, Refresh, MCP `update`); a
  live refresh beside Refresh in the export settings, which the user turns
  on, would compute it again after each change.
- An open scene does not load its editor asset's PVS item: after a new
  session's Bake World-Space Meshes the open scene has no PVS until a bake;
  a prepared run's PVS bits are read whole (no page streaming from an item).
