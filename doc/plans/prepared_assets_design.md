# Source, editor and prepared assets: design and build plan

Status: proposed

Design and incremental build plan for the requirements of
`doc/plans/prepared_assets.md` (cited here as R-numbers). The plan builds a
minimum viable product first: Export of a simple scene into an editor asset
and a prepared asset, and a prepared run of it, with every feature that is
not needed for that left inside the Scene item the way today's prepared
glTF file carries it. Each later step moves one thing out of the Scene item
into items of its own, or adds one behaviour, and leaves a working product.

## D1 The carrying idea: the Scene item starts as today's prepared file

Today's prepared save writes one glTF document whose binary part holds
everything: mesh payloads, images, the PVS. The Scene item (R4.9) is that
same document with payloads taken out one kind at a time:

- A payload that has become an item is a glTF `buffer` of the Scene item
  whose `uri` is `erhe-item:<key as 32 hex digits>`; the accessors and
  buffer views that read it stay in the Scene item's JSON, with offsets
  into the item's payload.
- A payload that has not become an item yet stays in the Scene item's own
  binary part, written and read by the code that writes and reads it today.

So at every step the exporter and the loader differ from today's by one
thing: where the bytes of one kind of payload live. The MVP moves mesh
payloads; images, `ERHE_geometry` primitives and the PVS follow in later
steps (D8).

## D2 Library `erhe::asset_store`

New library `src/erhe/asset_store/` (target `erhe_asset_store`, with its
document under `doc/erhe/`, named after the target). It knows items, keys and the container; it
knows nothing of scenes or glTF. Dependencies: SQLite, xxHash,
`erhe::file`, `erhe::log`, `erhe::verify`.

- `Key`: 16 bytes, comparable and hashable, with hex text form.
- `Key_builder`: feeds XXH3-128 with typed, length-prefixed fields (a kind
  id, a code version, a format stamp, setting values, other keys, content
  hashes), so two different field sequences cannot produce the same byte
  stream. `Content_hasher`: XXH3-128 over byte spans (R3.4).
- `Item_kind`: `scene`, `local_mesh`, `world_mesh`, `raytrace_bvh`,
  `texture`, `basis_texture`, `pvs`, `editor_data` (R4.1). The MVP uses
  `scene` and `local_mesh`.
- `Asset_store`: one open container.
  - `open(path, mode)` with modes `editor_read_write` (creates when
    absent; recreates on an unread format version, R5.2) and
    `prepared_read_only` (fails on an unread format version).
  - `contains(key)`, `get_info(key)` (kind, target, stamp, payload size,
    meta), `read_meta(key)`, `read_payload(key, offset, span)` (a byte
    range, R5.4; thread-safe), `put(item)`, `remove(key)`,
    `for_each_item(...)`.
  - `Write_transaction` (RAII; commit or roll back, R5.3).
  - asset records: source path and scene identity (R2.1), per-target
    prepared path (R2.3), for a prepared asset its target and the key of
    its Scene item.
  - `copy_item_to(other_store, key)` for the Copy step.
- SQLite use:
  - schema: table `asset` (name, value) for the format version and the
    records above; table `item` (key BLOB unique, kind, target, stamp,
    meta TEXT, payload BLOB, info TEXT);
  - payload ranges are read with the incremental blob interface;
  - an editor asset runs in WAL mode with a busy timeout (several readers
    and one writer, R5.6; the `-wal` / `-shm` files exist while it is
    open), and a prepared asset is written in rollback-journal mode and
    opened read-only on a device, so it is one file at rest (R5.1);
  - a small pool of read connections serves parallel payload reads;
  - `VACUUM` after a drop gives the space back (R8.13).
- An item's `meta` is a JSON text that makes the item usable on its own
  (D4); `info` is the node record of R4.8.
- CMake: SQLite amalgamation and xxHash through CPM
  (`doc/cmake_conventions.md`), built on every platform, Android included.

## D3 Keys

A key is built by one function per kind, in one place per kind, from
(R3.2): the kind id, the kind's code version constant, the kind's format
stamp, the export settings fields that kind reads, and the input hashes.

- Local-space mesh: the primitive's content hash; the LOD request in force
  for it; the mesh optimizer options; stamp = vertex layout signature of
  the stored GPU streams plus the BVH library tag while the BVH rides in
  the mesh item (D8 step 5 splits it off).
- Scene: the content hash of the Scene item's own bytes (JSON and binary
  part). The Scene item is made on every Export and stored only when its
  key is absent; that is what makes "a second Export makes no item" hold
  without tracking every scene input.
- Content hash of a primitive: XXH3-128 over its indices and every vertex
  attribute, in the soup's layout order with attribute descriptors, carried
  as loaded state (R3.5, step 12).

## D4 Mesh items

A Local-space mesh item's payload is the bytes today's prepared save
writes for one primitive, contiguous: index data, the interleaved core
vertex stream, the topology (`vertex_points`), the LOD level indices, the
BVH, the GPU vertex streams. Its `meta` is the primitive's glTF JSON
fragment: attributes, accessors and buffer views with offsets into the
payload, and the `ERHE_mesh_optimized`, `ERHE_mesh_topology`,
`ERHE_mesh_lod`, `ERHE_raytrace_bvh` and `ERHE_gpu_vertex_streams`
payloads, which stay the internal description of a mesh item (R12.2
removes them from source assets, not from this role).

`erhe::gltf` gains, by refactoring the per-primitive part of the exporter
and of the loader (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`):

- `write_mesh_item(prepared export, gpu formats) -> {payload, meta}`: what
  the prepared save writes for one `Prepared_primitive_export`, as one
  payload with a self-contained meta.
- Export with items: `Gltf_export_arguments` takes a provider that, per
  primitive shape, returns the item's `uri` and `meta`; the exporter then
  writes the primitive's JSON from the meta against a buffer with that
  `uri` and writes no bytes for it.
- Load with items: the loader's buffer origin (`Gltf_buffer_origin`,
  `Gltf_source_files::read`) becomes a byte source with two forms, a file
  range and a named external buffer, and `Gltf_parse_arguments` takes a
  resolver from a buffer `uri` to a reader with `read_at(offset, span)`.
  `Gltf_triangle_soup_source`, the prepared payload, the stored LOD
  indices and the stored vertex streams then read item payloads through
  the same code that reads file ranges, including deferred loads and the
  read-back of a released soup.

`erhe::gltf` stays independent of `erhe::asset_store`: the editor passes
the provider and the resolver.

## D5 Editor side

New directory `src/editor/assets/` (with a feature document under `doc/editor/`
that grows as steps land):

- `Editor_asset`: the `Asset_store` of one source file, found by R2.2 /
  R2.4, owned through a registry keyed by the normalized source path
  (`Editor_asset_registry` in `App_context`), so two scenes that share a
  file share one store (R10.1).
- `Scene_export`: the Export job (R9). It extends `Scene_save_service`'s
  staging (`src/editor/scene/scene_save_service.hpp`): quiescence, the
  read-only hold, worker execution, status line progress, cancel, MCP
  `get_save_status`.
  - Ensure: per primitive of the scene, compute the key, and for a missing
    one run `prepare_primitive_for_export()`, `make_export_lod_chain()`
    and `write_mesh_item()` on the executor and `put` the result; then
    export the Scene item with the provider of D4, hash it, `put` it when
    absent.
  - Copy: open or create the target's prepared asset, collect the keys the
    Scene item refers to (its `erhe-item:` buffers), `copy_item_to` the
    absent ones, remove the others, record the Scene key and the target.
  - It reports made, copied, removed and present counts per kind.
- Prepared run (R11): `Load_scene_file_message` accepts an
  `.erhe_prepared` path. It reads the asset's source record; when that
  source file is readable it opens the source instead; otherwise it parses
  the Scene item with the resolver of D4 over the prepared asset and marks
  the `Scene_root` as a prepared run, which Save and Export check.

## D6 Transition switch

Until R12.1 the prepared glTF save stays. File > Prepare and MCP
`prepare_scene` take the container from an editor setting
`export.container` (`gltf` | `store`, default `gltf`), and MCP
`prepare_scene` also from an argument `container`. D8 step 13 makes `store`
the only one and removes the setting.

## D7 Minimum viable product

Scope: a saved, erhe-authored or foreign glTF scene of one file with
soup-backed meshes; target `pc`; desktop only. Deliberately inside the
Scene item for now: images (as the source bytes, as a scene save embeds
them), `ERHE_geometry` primitives (written and rebuilt as today), no PVS,
no world-space bake (`bake_static_to_world_space` and `pvs_mode` are read
as off, with a log line when the scene's settings ask otherwise). Export
settings are read as they are today (R7.1 - R7.3 land in step 4).

- M1 `erhe::asset_store` (D2) with unit tests `erhe_asset_store_tests`:
  key builder separation, put / get / range read, transaction rollback on
  a simulated crash (a process killed mid-write leaves no findable partial
  item), two writers, format version handling, `copy_item_to`, remove and
  size after `VACUUM`. Done when the tests pass on Linux and the library
  builds for Android. Built: `doc/erhe/asset_store.md`; 15 tests pass on
  Linux and the target compiles for Android arm64.
- M2 Mesh items in `erhe::gltf` (D4): `write_mesh_item`, export with a
  provider, load with a resolver. Tests in `erhe_gltf_tests` with an
  in-memory resolver: a prepared primitive round-trips through an item
  bit for bit (core, topology, LOD indices, BVH, GPU streams), a released
  soup reads back from the item, and the existing prepared glTF round-trip
  tests still pass (the refactor keeps the file path byte-identical). Built:
  `doc/erhe/gltf.md` "Mesh items".
- M3 Export (D5 `Editor_asset`, `Scene_export`, D6): File > Prepare (PC)
  and MCP `prepare_scene` with `container` `store`; MCP `get_save_status`
  reports the counts. Built: `doc/editor/prepared_assets.md`; the setting is
  `export_container`. Open: cancel, the R2.2 record, the R2.5 fallback.
- M4 Prepared run (D5): open by File > Load Scene, `--scene` and MCP
  `load_scene`; Save and Export refused with a message. Built:
  `doc/editor/prepared_assets.md` "Prepared run".
- M5 Verification script `scripts/prepared_asset_verify.py` (headless
  editor, `doc/agents/editor_runs.md`) over small hand-written glTF scenes
  (a few soup meshes, shared primitives, a mirrored node, untextured and
  textured materials, a skinned mesh, an animation):
  - Export, open the prepared asset with the source moved away, and
    compare `render_scene_image` with the source scene's (identical);
  - with the source in place, opening the prepared asset opens the source;
  - a second Export in a new editor process makes and copies zero items;
  - after an MCP edit of one mesh's vertices, Export makes one mesh item
    and one Scene item, and after `undo` and Export, none;
  - an Export killed during Ensure and during Copy, then run again, ends
    with the same keys and bytes as an uninterrupted one;
  - the prepared asset holds exactly the items its Scene item names.

  Built: `doc/editor/prepared_assets.md` "Verification". The edit and undo
  checks pass once D8 step 3 lands and the undo of a vertex move restores
  the original primitive; every other check passes.

The MVP proves R1.1 - R1.4, R3.1 - R3.4, R3.6, R5.1 - R5.5, R9.1 - R9.5
(without the world-space and PVS parts), R11.1 - R11.3 on the desktop.

## D8 Steps after the MVP

Each step states what it adds and what verifies it; `prepared_asset_verify`
grows a case per step. Order follows dependencies: 1 - 3 can come in any
order; 6 needs 5; 8 needs 7; 9 needs 8; 10 needs 8.

1. Status (R13.1, R8.11): MCP `get_prepared_status` and the scene's
   Properties "Export" group: present and missing items per kind, whether
   the prepared asset matches, paths, sizes, unreferenced items; drop
   controls (R8.13). Keys are computed on request in this step. Built:
   `doc/editor/prepared_assets.md` "Status".
2. Quest: `quest` target, the prepared asset pushed and opened as a
   prepared run on the headset (`doc/agents/quest.md` recipe updated),
   format stamp check and its failure modes (R11.4, R11.6). Verified by a
   simple scene on the device, then by Bistro without textures-as-items.
   Built: `doc/editor/prepared_assets.md` "Targets" and "Format stamps",
   `doc/agents/quest.md` "Prepared assets on the device". Bistro runs at the
   same fps as the prepared glTF but loads slower and peaks at 2.4x its
   memory (`doc/reference/quest_bench_runs.md`): the Scene item's images are
   parsed from memory and retained; step 6 closes this.
3. Every mesh is a mesh item (R4.4): geometry-normative and graph meshes
   are prepared from their derived triangles; the Scene item stops
   carrying `ERHE_geometry`, brushes and graphs (R4.9). Verified by the
   editor's default scene (brush-made meshes). Built: `doc/erhe/primitive.md`
   "Derived triangles"; the default scene exports 7 mesh items and no
   inline primitive, and its prepared run renders bit-identical to the
   source (`prepared_asset_verify.py`).
4. Export settings as R7 states: present-or-absent with the copy of the
   editor defaults, the scene as the only settings input, MCP per-call
   overrides removed (R12.4), `texture_supercompression` and
   `raytrace_bvh` as settings, the refusals of R7.3 and R2.7, USD refused
   (R1.7). Built: `doc/editor/prepared_assets.md` "Export settings". By
   the user's decision an erhe scene opened without export settings gets
   the copy of the editor defaults at the open, as adding them does; an
   imported (foreign) scene still has none until they are added.
5. Raytrace BVH as its own kind (R4.1): split out of the mesh item, keyed
   by its mesh item and the BVH library stamp; `raytrace_bvh` off leaves
   it out of the prepared asset; a prepared run builds in memory on a
   stamp mismatch or when a tool needs it (R11.4, R11.5). Built: a prepared run
   builds missing BVHs with the whole-scene deferred raytrace build at
   load; the per-tool on-demand build of R11.5 is open.
6. Textures:
   a. texture properties on the source texture, saved in the source asset
      and taken from use at import (R4.2, R4.3);
   b. Texture items for Basis Universal sources, transcoded to the
      target's block format (the transcode of today's prepared save), as
      `erhe-item:` image buffers of the Scene item;
   c. `texture_encoding`: the Basis Universal encoder as a dependency,
      Basis texture items for PNG / JPEG sources (R7.5).
   Built (a, b): `doc/editor/prepared_assets.md`; Bistro on Quest loads
   within 1.5 s and 40 MB of the prepared glTF (`doc/reference/quest_bench_runs.md`);
   after `doc/erhe/asset_store.md` "Reading payloads" its load time equals
   the prepared glTF's and its peak memory is within 40 MB. Built (c):
   `doc/erhe/graphics.md` "Basis Universal encoder" (UASTC LDR 4x4, Zstandard,
   CMake option `ERHE_BASIS_ENCODER`, off on Android) and
   `doc/editor/prepared_assets.md` "Keys", "Export" and "Export settings"; a
   `source` Texture item has no target and is shared by both targets; the
   prepared run of either value retains no PNG / JPEG bytes.
7. Stable identities and transforms (R6.1, R6.2), each with unit tests in
   `erhe_gltf_tests` / `erhe_scene_tests` and an editor round-trip check
   (save a foreign glTF with and without a transformed import root, reopen,
   compare world transforms bit for bit and `uid`s across sessions).
   Built: `doc/editor/scene_serialization.md` "Unique IDs".
8. World-space meshes (R4.6 - R4.8, R8.5 - R8.8): World-space mesh items
   keyed by mesh key and transform bits, Bake World-Space Meshes, the two
   forms in an open scene and the viewport toolbar setting, the Scene item
   written with the "World Space" node, the default of `instancing`. The
   Bake Static To World Space operation is removed. Verified by the
   hand-written scenes of `doc/editor/world_space_bake.md` "Verification"
   and by Bistro. Built: `doc/editor/world_space_bake.md` "Two forms" and
   "Validity"; the toolbar combo is not driven by the script. (A content mesh
   added or removed after a bake, and the change undone, leaves the form
   available since step 12.)
9. PVS (R4.10, R7.6, R8.9, R8.10, R9.6): the PVS item, the bake on the
   world-space form, `pvs_mode`, the read-only editor with a movable
   camera during Export. Verified by the Bistro recipe: the prepared run's
   PVS is `ready`. Built: `doc/editor/prepared_assets.md` "PVS",
   `doc/editor/pvs.md` "Bake" and D1, D5, `doc/erhe/gltf.md` "PVS items".
   The PVS key is the content hash of the bake's result (no key inputs are
   tracked), so the Scene item is written after the PVS bake to name it
   (R9.2 lists the Scene item first). Bistro (Linux headless Release,
   NVIDIA): `rebake` bakes 2068 cells, 3791 columns in 203 s, a 4.3 MB PVS
   item; the Export takes 209 s; a second Export bakes the same item and
   makes and copies nothing; the prepared run's PVS is `ready` with the same
   `bits_hash`. Open: an open scene does not load its editor asset's PVS
   item; the camera navigation during an Export is unverified
   interactively; `scripts/pvs_verify.py` `streaming` is not converted.
10. Several files (R10, R2.5): per-file editor assets through the
    registry, prefab instances flattened in the Scene item and baked,
    host settings with the logged difference, `reload_prefab` as a change
    site, the fallback location, the failures of R10.8. Built:
    `doc/editor/prepared_assets.md` "Several files" and "Locations" (the
    R2.2 record in `ERHE_scene` `editor_asset`, the R2.5 fallback under
    `cache/editor_assets`); the Scene item writes prefab instances and
    referenced materials in full; Ensure runs per file deepest prefab first and
    then makes the scene's World-space mesh items; the drops of the status
    change the scene's own editor asset only. The single-file Export is byte
    identical (the small scene's pc, quest and baked Scene keys, its editor
    and prepared asset keys and its prepared glTF). Open: a prefab template's
    stub material of a missing container is not detected; Bake World-Space
    Meshes does not read the dependency files first.
11. Bistro on Quest, complete (R13.2, R13.4): numbers recorded in
    `doc/reference/quest_bench_runs.md`. Built: the host recipe is
    `scripts/prepared_asset_bistro_verify.py` (`doc/editor/prepared_assets.md`
    "Verification"), 28 of 29 checks passing; the quest prepared asset and the
    prepared glTF file of the same scene measured on the Quest 3
    (2026-11-07 06:30 and 06:31 entries). Open:
    - R13.2, R6.1: closed by step 12 (the save carries the source content
      hashes).
    - R13.4 (2026-11-07 07:47 entries, four alternated runs each): the
      prepared run is listed 0.14 s earlier than the prepared glTF file and
      goes idle 0.16 s later; its peak is 14 MB higher (was 36 MB) and its
      steady heap 8 MB larger. Fixed: container format version 2 (payloads
      in their own table, a covering index for payload lookups), one stamp
      check per open from one read of the item table (MCP `load_scene`
      hands its resolved open to the load), the Scene item parsed through a
      reader instead of copied whole, bounded page caches freed after the
      load (doc/erhe/asset_store.md "Container", "Reading payloads"). Open:
      the mesh build reads 0.22 s longer from the store than from the file
      (2037 against 1817 ms), and the 8 MB are the open store (eight read
      connections, a byte source per item buffer); R13.4's "no higher" does
      not hold by those 14 MB. Re-measured after steps 11b - 15 (2026-11-07
      22:15 entries): listed 4.66 s, VmHWM 800 MB against 5.20 s / 783 MB,
      but the prepared glTF file now opens untextured and without a PVS
      (R12.2), so it no longer measures R13.4; the baseline is a user
      decision (`doc/plans/open_items.md` "Prepared assets").
    - The device image comparison: `render_scene_image` crashes on the Quest
      (pre-existing, `Render_pass::start_render_pass` from
      `Shadow_renderer::draw`).
    - One file-level stamp check (2026-11-07, user decision; R11.4 reworded):
      Copy writes the exporting build's stamps as records in the transaction
      of `prepared.scene_key` (`prepared.mesh_stamp`, `prepared.bvh_stamp` -
      `none` with `raytrace_bvh` off -, `prepared.scene_stamp`,
      `prepared.texture_stamps`), and the open checks those instead of
      reading the item table and the Scene item's JSON; a prepared asset
      without them is refused naming the record (`doc/editor/prepared_assets.md`
      "Format stamps"). Host, headless Release, `resolve_prepared_open()`
      cold (file dropped from the page cache) / warm: small 1.6 - 10 / 0.6 ms
      before and after (the store open dominates); Bistro (3272 mesh, 3272
      BVH, 266 Texture items) 77 - 84 / 36 - 42 ms before, 1.8 - 9.8 / 0.5 -
      0.7 ms after (1.2 - 2.4 / 0.5 - 0.7 ms with the full-rewrite Copy
      below). Not re-measured on the Quest; the per-item check took 150 -
      250 ms there after step 11, which bounds the gain.
    - Copy writes the prepared asset anew (2026-11-07, user decision; R9.3
      reworded, R11.3 states that a prepared run validates nothing): a new
      container (`Open_mode::prepared_create`, replacing
      `prepared_read_write`) at `<file name>.tmp-<16 hex>` beside the
      target, renamed over it once committed and closed; nothing of an
      earlier prepared asset is read, the counts are `made` / `present`
      (editor assets) and `written` (prepared asset), and a key an editor
      asset lacks fails the Export naming the key and its kind. A
      verification of the written content was considered and dropped: a
      fresh write from the editor assets leaves nothing to reconcile, and
      the kill cases of `scripts/prepared_asset_verify.py` show that the
      rename keeps the replacement all or nothing (a Copy killed before any
      prepared asset leaves only its temporary file, which the next Export
      removes; one killed over a prepared asset leaves it byte-identical).
      Bistro without the bake, 6811 items / 946 MB: Copy 628 ms on the first
      Export, 648 - 663 ms on an Export that makes nothing (host, warm page
      cache). On Windows the rename over a target that another scene's
      prepared run holds open fails the Export (documented, not run here).
12. Change-driven keys (R3.5, R3.7, R8.12): content hashes carried as
    loaded state and recomputed at the edit, keys recomputed at change
    sites, status current without a request. Built: `doc/erhe/primitive.md`
    "Content hash", `doc/erhe/gltf.md` "Content hashes" (Save Scene stored
    each prepared core's source content hash in `ERHE_mesh_optimized`
    `source_content_hash`, a reopen takes it; since step 14 Save Scene writes
    the source soups and no `ERHE_mesh_optimized`, the reopen computes the
    hash from the soup, and the field is read for files of earlier builds), `doc/editor/prepared_assets.md`
    "Keys" and "Status" (`Prepared_keys` per scene: change sites mark the
    status stale, a key check on its own thread computes the keys from the
    stored hashes against in-memory item catalogs of the stores; the Scene key
    comes from the last Export of the unchanged scene, else from a status job
    with the scene read-only; Refresh lists the stores again for changes
    another process made). The foreign `small` and the erhe scene saved from
    it make the same mesh, BVH and Texture items; Bistro's foreign bake and
    its reopened erhe scene make the same World-space mesh items (R6.1).
    Undoing a mesh add or removal, or setting an export setting back, makes
    the world-space form available again. Open: the status does not follow
    edits that change the Scene item alone (a light, a material value, a
    material's textures); a geometry-normative primitive's hash and the Scene
    key of a scene changed since its Export need a status job, which holds the
    scene read-only briefly; an erhe file saved before this step keys its
    cores until it is saved again.
13. The store becomes the only prepared format (R12.1): `export.container`
    removed, the prepared glTF save kind and its tests, scripts and
    recipes converted.
    Built: the editor setting `export_container` (editor settings v17; an
    older file's key is skipped and the upgrade rewrite omits it), its
    Settings row and `Gltf_scene_save_kind::prepared` with the paths only it
    served (`save_scene_gltf_prepared()`, `default_prepared_scene_path()`,
    the bake of soups only, the texture counts of the save report) are gone;
    `Scene_save_request` names its job (`Scene_save_job`: `save_scene` |
    `store`). File > Export (PC) / Export (Quest) and MCP `prepare_scene` /
    `save_scene_for_quest` always Export; MCP refuses a `container`
    argument. A `_pc.glb` / `_quest.glb` of earlier builds opens as a source
    (doc/editor/scene_serialization.md "Save pipeline"); erhe::gltf keeps
    writing the raw block texture forms and GPU streams (the quest Scene
    item uses them). `scripts/lod_roundtrip_verify.py` checks its quest
    round trip through an Export and a prepared run (69 / 69);
    `scripts/prepared_asset_bistro_verify.py --keep-quest` keeps a copy of
    the source asset as the Quest baseline in place of a prepared glTF file.
    Verified: `scripts/prepared_asset_verify.py` 302 / 302 (one new check:
    the `container` refusal), `scripts/prepared_asset_bistro_verify.py`
    29 / 29, `editor_asset_tests` 73, `erhe_gltf_tests` 55,
    `erhe_asset_store_tests` 20, `erhe_scene_tests` 193. Not run:
    `mcp_server_tests` (windowed editor), the Quest.
14. The editor's own loading (R8.1 - R8.4, R14.4): an open of a source
    asset loads Local-space mesh, Raytrace BVH and Editor data items in
    place of deriving them, and stores what it derives; Save Scene writes
    authoring data only. Verified by R13.3.
    Built: `doc/editor/prepared_assets.md` "The editor's own loading"
    (`Editor_asset_load`; the build jobs of a scene open key each soup with
    the key computation of Export, load its mesh item and BVH item, or make
    them with `make_local_mesh_items()` - Ensure's one implementation - store
    them in batches of 512 items / 64 MiB and load from them), `doc/erhe/primitive.md`
    "Authoring soup" (the loaded core keeps the source soup, which Save Scene
    writes and from which every item of the shape is made, so the GPU-format
    rounding of a core never reaches an item), `doc/erhe/gltf.md` "Authoring
    data" (raw soups, the LOD request as `ERHE_mesh_lod` `settings` alone,
    `load_mesh_item_primitive()`), `cache/mesh_optimizer` and `cache/bvh` read
    only (`Mesh_optimize_cache_access`; `erhe::raytrace` writes no entry).
    The key's LOD is the primitive's request, else the scene's export
    settings' default LOD, as Export asks. The Topology of a mesh item is the
    mesh topology R4.1 lists under Editor data; no Editor data item kind is
    made. R13.3 on Bistro (Linux headless Release, NVIDIA): first open listed
    4.3 - 6.4 s / idle 4.6 - 6.7 s, 3852 shapes derived and stored, 3852 BVHs
    built or read from old `cache/bvh` entries and stored (7704 items,
    669 MB, 17 - 18 transactions; editor asset 639 MB); second open listed
    1.22 s / idle 1.46 s, 3852 from items, 0 BVH builds, 0 mesh optimizer
    runs; an Export (bake off) then makes 0 of 3272 mesh and 0 of 3272 BVH
    items, and the 6544 items an Export makes after `drop_prepared_items`
    `all` are byte-identical to the stored ones; the saved scene file is
    515 MB. R6.1 holds (the Bistro recipe's World-space mesh items of the
    foreign bake equal the reopened scene's). Verified:
    `scripts/prepared_asset_verify.py` 306 / 306, `scripts/prepared_asset_bistro_verify.py`
    29 / 29, `editor_asset_tests` 76, `erhe_gltf_tests` 58,
    `erhe_asset_store_tests` 20, `erhe_scene_tests` 193, `erhe_primitive_tests` 93,
    `erhe_raytrace_tests` 62, `scripts/lod_roundtrip_verify.py` 69 / 69 (a
    scene file carries the LOD requests alone; the chains are items),
    `scripts/pvs_verify.py --sections sampled,wall` PASS. Not run:
    `mcp_server_tests` (windowed editor), the Quest. A default LOD of the
    export settings makes the editor draw the chain Export makes for a
    primitive without a request (as a prepared run does); the request itself
    is not changed.
    Open: imports, prefab templates and loads without worker contexts take
    no items; geometry-normative shapes are built from their Geometry (no
    item read at the open); Texture items are not read by the open; shapes
    of equal content are prepared once per shape, not per key; a file of an
    earlier build is saved once with its cores as soups and then keys by the
    cores' hashes (its items are made once more); the open writes no
    source record into a new editor asset (the next Export does).
15. Removals (R12.2, R12.3): the derived-data glTF extensions leave the
    source format and the `cache/bvh` and `cache/mesh_optimizer`
    directories go.
    Built: no glTF file is written or read with `ERHE_mesh_optimized`,
    `ERHE_mesh_topology`, `ERHE_gpu_vertex_streams`, `ERHE_raytrace_bvh`,
    `ERHE_texture_ktx2`, `ERHE_pvs` or the chain of `ERHE_mesh_lod`
    (`ERHE_mesh_lod` carries the request alone); their specifications and
    schemas are gone from `doc/gltf_extensions/`. The item formats that use
    the same JSON stay as they were (`doc/erhe/gltf.md` "Mesh items" and the
    sections after it): `parse_gltf()` reads the derived data only of an
    item document (`Gltf_document::item`: a Scene item, a mesh item
    document), a file parse (`Gltf_document::file`, the default) reads none
    and logs the ones a file carries once ("derived-data extensions not
    read") - a file of an earlier build opens as plain soups, its items are
    made and stored by the open, and its next Save writes authoring data.
    The export of a document of authoring data writes no `ERHE_pvs`
    (`Gltf_export_arguments::pvs_database` removed; a Scene item names the
    PVS item), and no authoring document holds a raw block KTX2 image (a
    file parse refuses one as a source, as the editor's texture imports and
    USD images do; the export asserts it); the BC raw block
    export forms and `Gltf_content_hash_export` (with the `source_content_hash`
    field and its reader) are gone. The editor setting `load.use_prepared_mesh_data`
    (Load_config v11) and the `file_prepared` job of `Editor_asset_load` are
    gone; `load.use_gpu_vertex_streams` (stored streams of mesh items) and
    `Stored_bvh_use` (a prepared run's BVH stamp) stay. The mesh optimizer's
    filesystem cache (`optimize_*_cached()`, `Mesh_optimize_cache_access`,
    the `Buffer_info` fields, `Mesh_memory_config::mesh_optimize_cache`,
    mesh memory config v4) and the `bvh` backend's `cache/bvh` read
    (`Bvh_source::cache`) are gone: the editor asset's items are the only
    persistent derived data. Verified: `scripts/prepared_asset_verify.py`
    308 / 308 (the cache-file check replaced by three: no cache directory
    created, no derived-data extension in any `.gltf` / `.glb` of the run,
    no file parse reading prepared mesh data), `scripts/prepared_asset_bistro_verify.py`
    32 / 32 (the same three checks), `scripts/lod_roundtrip_verify.py` 69 / 69,
    `scripts/pvs_verify.py --sections sampled,wall` PASS (bits_hash
    `eb0aa3903eb78865` / `23b23844bb191325` as before), `editor_asset_tests`
    76, `erhe_gltf_tests` 54, `erhe_asset_store_tests` 20, `erhe_scene_tests`
    193, `erhe_primitive_tests` 91, `erhe_raytrace_tests` 62,
    `erhe_dataformat_tests` 19, `erhe_gltf_gpu_tests` 5. A `_pc.glb` of an
    earlier build (ERHE_mesh_optimized, _topology, _raytrace_bvh,
    _gpu_vertex_streams, LOD chains) opened, logged the five as not read,
    stored 6 mesh and 6 BVH items, and its Save carried none of them. Not
    run: `mcp_server_tests` (windowed editor), the Quest, the OpenGL, Metal,
    Windows and Android builds.
    Open: the procedural helper geometry of an open (light and camera
    gizmos) builds its BVHs at every open; a texture whose only
    image a file named through `ERHE_texture_ktx2` opens without an image.

## D9 Risks and how the plan meets them

- The exporter and loader refactor of M2 touches a 12 000 line file whose
  prepared path is covered by round-trip tests: M2 keeps the file path
  byte-identical and adds the item path beside it, so the existing tests
  are the regression gate.
- SQLite read throughput for streaming loads on Quest (R13.4): step 2
  measures a simple scene early, and Bistro before textures become items,
  so a container problem shows before the later steps build on it.
- Scene item size: it is rewritten whenever anything in the scene
  changes. It holds JSON, skins and animations after step 6; if animation
  data makes it large, an animation kind is added (R3.11).
- The read connection pool and deferred loads: a prepared run keeps the
  prepared asset open for the life of the scene (released soups read
  back from it); closing the scene closes the store.
