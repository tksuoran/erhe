# glTF fast load: prepared mesh data in ERHE_* extensions

Status: in progress

Extends `doc/erhe/gltf.md`, `doc/editor/gltf_scene_roundtrip.md` (ERHE
extension conventions) and `doc/editor/async_asset_loading.md` ("Deferred
load finalize").

## Goal

Load a foreign glTF (Bistro), save it from erhe, and load the saved file with
no deferred work after the load: no mesh optimizer, no soup-to-`Geometry`
conversion, no BVH build or cache lookup, no full buffer-mesh rebuild. The
saved file has to load fast on Quest as well, where `cache/` starts cold and
there are fewer cores. The data that makes this possible is stored in
optional `ERHE_*` extensions, so the file stays a valid glTF that stock
viewers render.

## Baseline (2026-10-25, Release headless, desktop, warm caches)

Round trip: load `bistro_lights_c.gltf`, save it with MCP `save_scene`, close
it, then load the saved `.glb` twice.

| Open | listed | idle | parse (images / meshes) |
|---|---|---|---|
| foreign `.gltf` (350 MB on disk) | 3.2 s | 5.4 s | 1.49 s (0.84 / 0.50) |
| erhe-saved `.glb` (514 MB) | 3.7 s | 5.9 s | 1.71 s (0.91 / 0.11) |
| erhe-saved `.glb`, reopen | 3.8 s | 6.3 s | 1.74 s (0.91 / 0.14) |

Save took 1.4 s. All three opens replay 3852 optimizer entries from
`cache/mesh_optimizer` and load every BVH from `cache/bvh`: the saved file
is treated as foreign and gets no faster. With cold caches, which is the
first load on any Quest, the deferred phase also runs the geometry-path
optimizer (28 worker-seconds on desktop) and builds every BVH.

## What a load spends today

From file read to idle, for every soup primitive. The numbers are desktop
Bistro figures from `doc/plans/bistro_load_profiling.md`.

| Step | Where | Cost | Removed by |
|---|---|---|---|
| KTX2 Basis transcode | parse worker | 0.9 s wall, 21 worker-s | stays (decision 3: KTX2 kept in the file; transcode cache deferred) |
| mikktspace for primitives without TANGENT | parse worker | part of mesh parse | TANGENT always written (step 4) |
| fill-only soup buffer mesh | build phase, parallel | 0.16-0.25 s | stays (now the only build) |
| residency upload | main thread, budgeted | 0.45-1.4 s | stays |
| soup to `Geometry`: `colocate()` under the global geogram lock, tangents, `Geometry::process` | deferred task | 1.84 s serial, ~15 worker-s waiting, ~1.9 GB attribute stores | `ERHE_mesh_topology` + lazy `Geometry` |
| full buffer mesh from `Geometry` (edge lines, points, smooth normals) | deferred task | ~14 worker-s | lazy, built on demand |
| geometry-path mesh optimizer | deferred task | 28 worker-s cold; 1.6-1.8 s replay | `ERHE_mesh_optimized` |
| BVH: hash, build or `cache/bvh` load, precompute | deferred task | 6-7 worker-s warm | `ERHE_raytrace_bvh` |

## Design

Content-only results go in the file. The loader does only linear packing
into the running device's formats. A device-dependent result, such as a
quantized vertex weld or GPU texture blocks, is not stored, or it is
stored with a tag the loader checks.

### Saved core primitive

On save, a soup primitive's core accessors are written in the form the
optimizer would produce:

- one float vertex per unique corner, welded bitwise;
- positions snapped to their colocated values (tolerance 1e-5);
- triangles in meshoptimizer vertex-cache, overdraw and vertex-fetch order;
- degenerate triangles dropped, so triangle t is facet t;
- TANGENT always written;
- joint weights normalized.

It remains a plain TRIANGLES primitive.

### `ERHE_mesh_optimized` (primitive)

This is the marker and the contract that the core is in that form. The format
contract, including the exact `content_hash` byte sequence, is
`doc/gltf_extensions/ERHE_mesh_optimized.md`:

```json
"ERHE_mesh_optimized": {
  "version": 1,
  "passes": ["weld_bitwise", "vertex_cache", "overdraw", "vertex_fetch"],
  "overdraw_threshold": 1.05,
  "content_hash": "<16 hex digits: FNV-1a 64 over indices + POSITION>",
  "bounding_sphere": {"center": [0, 0, 0], "radius": 1}
}
```

- The optimized render variant is then one linear pack of the core into the
  device's optimized vertex format, using the existing
  `build_buffer_mesh_from_triangle_soup` with the optimized `Buffer_info`. No
  meshoptimizer call runs.
- The element mappings are identity.
- The AABB comes from the POSITION accessor's required min/max.
- A device with coarser quantization only misses weld merges that would
  have been possible, and the packed bytes are still exact.

### `ERHE_mesh_topology` (primitive)

```json
"ERHE_mesh_topology": {"version": 1, "point_count": 1234, "vertex_points": 17}
```

`vertex_points` is a bufferView of uint32 values that gives the geogram
point of each core vertex, in first-reference order (the colocate path's
order). It is a raw bufferView because glTF allows UNSIGNED_INT accessors
only for primitive indices. Format contract:
`doc/gltf_extensions/ERHE_mesh_topology.md`.

- It replaces `colocate()`. Building a `Geometry` from the soup then needs no
  kd-tree and no global lock, and the point order is deterministic.
- About 4 bytes per vertex, roughly 16 MB for Bistro.

### `ERHE_raytrace_bvh` (primitive)

```json
"ERHE_raytrace_bvh": {
  "version": 1, "triangle_hash": "<hex>", "triangle_count": 1000,
  "builds": [{"backend": "bvh_v2", "library": "<pinned GIT_TAG>", "quality": "high",
              "node_count": 1999, "nodes": 21, "primitive_ids": 22}]
}
```

- The BVH is built over the core triangles in order.
- Nodes are stored in erhe's own documented layout: six floats plus one
  packed uint32 per 28-byte record, in a raw bufferView (no valid glTF
  accessor describes the record). The bvh::v2 stream format is not used,
  because it serializes `size_t`. Format contract:
  `doc/gltf_extensions/ERHE_raytrace_bvh.md`.
- The loader compares three things: the triangle hash it computes anyway,
  the triangle count, and the backend/library tag. On any mismatch it
  rebuilds.
- The tag comes from the pinned dependency version, not `git rev-parse`.
  `git rev-parse` reports "unknown" on Android today, which makes the existing
  `cache/bvh` key meaningless there.
- tinybvh blobs are optional. Embree rebuilds.

### Edges, smooth normals and element mappings

These are computed, not stored. With the stored colocation they are linear
passes: `build_edges` sums to 0.67 s over Bistro. Storing them would add an
estimated 80 MB or more. They are only needed when a `Geometry` is built.

### Lazy `Geometry` and full build

Nothing drawn by default needs a `Geometry` after load:

- edge lines are off in `config/editor/default_viewport_config.json`;
- hover uses the non-blocking `get_geometry_const`;
- facet picking uses the raytrace's triangle-to-facet map, which is
  identity under the contract.

A prepared primitive therefore loads with:

- the fill-only soup build as its base render shape, with facet ids filled
  (triangle index);
- the packed optimized variant;
- the real raytrace from the stored BVH.

The `Geometry` and the full buffer mesh, with edge lines, points and smooth
normals, are built on first demand through `get_geometry()`. The consumers
that need them are:

- mesh-component tools, the geometry spreadsheet and geometry-graph source
  nodes;
- the lightmap baker;
- mesh colliders;
- turning edge lines on;
- export of a non-prepared primitive.

The full build is also what the deferred task does today, so it reuses
that code. The deferred kickoff skips prepared primitives. The
`Mesh_primitive` count properties fall back to the soup counts while no
`Geometry` exists.

### Loader

`parse_gltf` (worker) captures the three payloads per primitive, the way
`ERHE_geometry` is captured, and validates them:

- the accessor counts;
- every `vertex_points` id is below `point_count`;
- no triangle is degenerate;
- every BVH primitive id is below the triangle count;
- the content hash matches.

The validated payloads go into an immutable `Prepared_primitive_data` on
the `Primitive_shape`.

`build_imported_buffer_meshes` then builds the base and the optimized pack in
parallel, and installs the raytrace. `finalize_imported_meshes` and the
kickoff find nothing left to do.

Each extension is all-or-nothing per primitive, and falls back to today's
path:

- a rejected marker drops the topology and the BVH as well, because both
  depend on triangle t being facet t;
- a rejected BVH alone falls back to the proxy plus the deferred raytrace.

One summary line per file reports `prepared mesh data: N used, M rejected
(reasons)`. A `load.use_prepared_mesh_data` setting allows A/B runs.

### Exporter

`save_scene` writes the extensions. Plain `export_gltf` does not, unless
asked.

- A primitive loaded with prepared data and not edited since writes its
  retained core and payloads unchanged. The save is fast and idempotent.
- Otherwise the exporter prepares the primitive as executor tasks:
  1. builds the `Geometry` if it is missing;
  2. runs the corner build into float CPU sinks;
  3. runs the meshoptimizer passes;
  4. writes the core and `vertex_points`;
  5. reuses the session's BVH with its primitive ids remapped through the
     triangle permutation, and recomputes the hash and the sphere.
- Any edit that replaces or rewrites a primitive's data drops its prepared
  data. In-place GPU edits such as paint need a change serial for this; to
  be verified.
- Geometry-normative primitives (`ERHE_geometry`) can take `ERHE_raytrace_bvh`
  as they are, because their core fan order equals the raytrace order. Their
  optimizer marker is later work.

## Build order

One commit per step: Opus coder, reviewed by the orchestrator.
The load gate is `scripts/scene_load_profile.py` on the Release headless
editor, run on the erhe-saved Bistro, with `--render-image` compared
byte-for-byte against the foreign Bistro at idle.

1. **Specs.** Write spec pages for `ERHE_mesh_optimized`,
   `ERHE_mesh_topology` and `ERHE_raytrace_bvh` in the extension spec directory, with schemas and README
   rows. Gate: `check_doc_links.py` is clean.
2. **Stored colocation.** Add a `Triangle_soup` to `Geometry` overload that
   takes `vertex_points`. Gate: the primitive test is bit-exact against the
   colocate path for points, facets, corner attributes, edges and smooth
   normals (hexfloat compare), and takes no geogram lock.
3. **BVH export and import API** in `erhe::raytrace`, with the pinned tag.
   Gate: a round trip gives identical nodes and primitive ids and identical
   hits over a ray set, and a mismatched tag, count or hash is rejected.
4. **Exporter.** Write the prepared core and the three extensions from
   `save_scene`. Gates:
   - the Khronos validator reports no errors;
   - the extensions appear in `extensionsUsed` only;
   - a stock viewer renders the file;
   - file size and save time are recorded here.

   Result (2026-10-25, Release headless, Bistro): `save_scene` prepares all
   3852 soup primitives (0 skipped, BVH reused 3852 / rebuilt 0, 0
   degenerate triangles dropped) in 1.2 s of parallel preparation; the save
   takes 3.0 s (plain save 1.4 s) and writes 664 MB (plain 514 MB). The
   Khronos validator 2.0.0-dev.3.10 reports 1077 errors, all present in a
   plain save of the same scene as well (zero-length TANGENT vectors from
   the import, `image/ktx2` MIME types, undeclared `KHR_materials_ior`);
   the extensions add only infos (`UNUSED_OBJECT` for the 3 x 3852 raw
   bufferViews, `UNUSED_MESH_TANGENT`). The extensions are in
   `extensionsUsed` only. In place of a stock viewer (headless), the saved
   file reloaded with today's loader (extensions ignored) renders
   byte-identical to the plain-saved file reloaded; both differ from the
   foreign load in 124 of 786432 pixels (max channel delta 44), which is
   pre-existing.
5. **Parser read and validate, data not used yet.** Gates: the log reads
   `3852 used, 0 rejected`; parse time grows by less than 100 ms; the render
   is identical.

   Result (2026-10-25, Release headless, Bistro prepared save, two runs of
   two opens per setting): `prepared mesh data: 3852 used, 0 rejected ();
   topology 3852, BVH 3852`. Mesh parse 115-182 ms on vs 74-122 ms off
   (+41 to +63 ms, the full `validate_prebuilt_bvh()` check included);
   `async parse_gltf` +40 to +58 ms. All eight renders at idle are
   byte-identical (on and off, first and second open).
6. **Load fast path:** base build plus optimized pack plus stored BVH, and
   the kickoff skips prepared primitives. Gates:
   - idle within 0.2 s of listed;
   - no "mesh optimize", "BVH build" or colocate work in the log or a Tracy
     capture;
   - 0 deferred tasks;
   - the render is byte-identical to the foreign Bistro at idle, or the
     difference is recorded and goes to the user;
   - two runs give identical results.

   Result (2026-10-25, Release headless, the step 5 prepared Bistro, two
   editor runs of two opens per setting, warm caches): on, listed 3.57 /
   4.02 / 3.62 / 4.03 s and idle 0.17-0.18 s later (one MCP status round
   trip); off, listed 3.12-4.29 s and idle 1.6-2.8 s later.
   `build_imported_buffer_meshes` 325-352 ms with all 3852 shapes prepared
   (off: 131-154 ms), `finalize_imported_meshes` 6-8 ms, `0 deferred
   finalize tasks queued, 2727 meshes skipped` (off: 2727 queued). The log
   holds no mesh optimize, BVH build or colocate line for the file (one
   2-triangle primitive the editor creates outside the file is optimized in
   both settings). Every render at idle, on and off, first and second open,
   is byte-identical (md5 89832b6289571d743904759b59872f03), the reference
   being the prepared file loaded with the setting off (which differs from
   the foreign Bistro in 124 pixels, pre-existing, see step 4). Cold caches
   (`cache/bvh` and `cache/mesh_optimizer` moved aside): on, listed 3.49-
   3.62 s, idle 0.17-0.18 s later, same render; off, listed 3.68 s, idle
   7.68 s.
7. **Lazy `Geometry` and on-demand full build.** Gates:
   - no `Geometry` alive after a Bistro load (`get_memory_usage`; RSS down
     by about 1.9 GB);
   - turning edge lines on builds them and renders like the foreign load at
     idle;
   - `mcp_server_tests` mesh-component and lightmap cases pass;
   - an export of an untouched prepared scene is byte-identical to its
     source.

   Result (2026-10-25, Release headless, the step 5 prepared Bistro): at idle
   `render_shapes {count 3852, prepared 3852, with_geometry 0}`, RSS 1407 MB
   (off: 3852 with Geometry, 5048 MB). MCP `set_viewport_style edge_lines`
   queues 2683 full-build tasks, idle 3.25 s later with 3852 Geometry and
   5253 MB; the viewport capture with edge lines is byte-identical to the
   off path's (and differs from the capture before the toggle). Saving the
   untouched prepared scene retains all 3852 primitives (`3852 prepared
   (3852 retained)`, BVH stored 3852) in 1.66 s (step 4: 3.0 s); the BIN
   chunk is byte-identical to the source, the JSON differs only in node
   `scale` / `translation` / `rotation` values of 1771 nodes (a float
   round-trip drift of the node transforms on load and save, outside the
   mesh data).
8. **Textures** (decision 3): the retained KTX2 source bytes stay in the
   saved file; the exporter writes a KTX2 image's texture through
   `KHR_texture_basisu` (no core `source`, extension used and required) and
   declares the KHR material extensions it writes (`KHR_materials_ior`, ...).
   Gate: the Khronos validator reports no KTX2 MIME or undeclared-extension
   errors on the saved Bistro, and the saved file reloads with its textures
   bound. Result (2026-10-25, Release headless): the validator's errors on the
   saved Bistro go from 1077 to 478, all `ACCESSOR_VECTOR3_NON_UNIT`
   (the zero-length TANGENT vectors from the import, a separate issue);
   the 393 `TEXTURE_INVALID_IMAGE_MIME_TYPE` and 206 `UNDECLARED_EXTENSION`
   errors are gone. The saved file reloads with "prepared mesh data: 3852
   used", no texture warnings, and a `render_scene_image` byte-identical
   to the pre-change reload.
9. **Quest.** Push the erhe-saved Bistro to the headset and time the load
   against the foreign file, with logcat phase lines and memory, following
   `doc/agents/quest.md`. Gate: 0 deferred tasks, plus load time and peak
   memory recorded here.

   Result (2026-10-25, Quest 3, release APK, off-head automated runs, the
   default startup scene loaded first, MCP `load_scene` with both files in
   the app's external files directory, fresh install so `cache/bvh` and
   `cache/mesh_optimizer` start empty): listed and idle are host wall time
   from the `load_scene` reply (0.25 s poll), peak RSS is `VmHWM` of the
   process.

   | Open | listed | idle | parse (images / meshes) | build | deferred tasks | peak RSS |
   |---|---|---|---|---|---|---|
   | foreign `.gltf`, run 1 (cold caches) | 15.1 s | killed | 8.8 s (5.1 / 3.3) | 0.58 s | 2727 | > 2.9 GB sampled; lmkd 6.3 GB at the kill |
   | foreign `.gltf`, run 2 | 16.1 s | killed | 9.1 s (5.2 / 3.5) | 0.63 s | 2727 | > 2.8 GB sampled; lmkd 6.6 GB at the kill |
   | erhe-saved `.glb`, run 1 | 16.7 s | 16.7 s | 8.1 s (5.2 / 1.4) | 1.64 s | 0 | 3.7 GB |
   | erhe-saved `.glb`, run 2 | 18.9 s | 19.4 s | 7.5 s (5.0 / 1.1) | 1.51 s | 0 | 3.4 GB |
   | erhe-saved `.glb`, run 3 | 17.2 s | 17.7 s | 8.6 s (5.1 / 1.9) | 1.55 s | 0 | 3.6 GB |

   The foreign Bistro does not finish loading on the Quest: in the deferred
   phase (2727 finalize tasks, geometry-path optimizer and BVH builds) the
   process grows past 6 GB and the low memory killer's watchdog kills it
   about 25 s after the load starts, in both runs (run 2 found most BVHs in
   `cache/bvh` from run 1 and still ran out of memory). The erhe-saved file
   loads with `prepared mesh data: 3852 used, 0 rejected; topology 3852,
   BVH 3852`, `0 deferred finalize tasks queued, 2727 meshes skipped`, no
   mesh optimize or BVH build line for the file, `render_shapes {count 3864,
   prepared 3852, with_geometry 8}` (the 8 are the startup scene), and goes
   idle within one poll of being listed. `dumpsys meminfo` at idle: total
   PSS 4.6-5.6 GB; device-local GPU memory 3.25 GB, of which textures
   1.96 GB (652 textures). The load is now bound by the KTX2 Basis
   transcode (images 5.0-5.2 s of the 7.5-8.6 s parse, decision 3) and the
   residency upload between the build and the finalize (about 5 s).

## Decisions (2026-10-25, user: "proceed" on the recommendations)

1. The first save rewrites the imported vertex data into the prepared core
   (reorder, colocation snap within 1e-5, degenerate triangles dropped,
   tangents baked, weights normalized). No lossless remap form.
2. `Geometry` and the full buffer mesh are lazy (step 7).
3. Textures stay Basis Universal KTX2 in the saved file, written through
   `KHR_texture_basisu` (user 2026-10-25: "for now - use basis universal,
   store in erhe gltf"); transcode cache / per-target images deferred.
4. `save_scene` always writes the extensions; `export_gltf` only when its
   arguments ask for it.
5. The BVH is stored in the file; `cache/bvh` keeps serving foreign files.

## Follow-up work (user, 2026-10-25)

1. **`--no-scene` creates an empty scene with a default camera** and nothing
   else; today the flag aborts on Quest (`scene_view.cpp` assert
   `!m_render_views.empty()`).
2. **Memory report.** Measure where CPU and GPU memory goes after a
   prepared Bistro load on desktop and Quest (per owner: soups, raytrace
   BVH and precomputed triangles, retained glTF buffers and image bytes,
   Geometry, mesh memory, textures, staging), and rank the optimizations.
   Measured: `doc/reference/memory_usage_2026_10.md` (per-owner tables,
   transient peak, ranked candidates; MCP `get_memory_usage` reports the
   owners).
3. **Drop CPU mesh memory after GPU upload.** An option, default on under
   OpenXR / Quest, that releases a primitive's CPU-side mesh data once its
   GPU buffers are uploaded and reloads it from the source glTF on demand
   (raytrace, Geometry, export, edits). Scope decided by item 2.
   Done:
   - Setting `load.cpu_mesh_data` (`keep` / `release` / `platform_default`
     = keep on desktop, release in OpenXR; the committed desktop config
     says `keep`, the OpenXR config `release`; a device config that predates
     the field gets `platform_default`, so Quest releases without a config
     change). With `release` the parse records a triangle soup reload source
     per primitive (the .glb BIN / .bin byte ranges of its bufferViews,
     accessor copies, file size + modification time; doc/erhe/gltf.md
     "Triangle soup reload") and `finalize_imported_meshes` releases the soup
     of every final prepared primitive (buffers, stored-BVH raytrace and
     optimized variant built) and of its optimized variant
     (doc/editor/async_asset_loading.md "Releasing CPU mesh data").
   - One read path: `Primitive_shape::get_triangle_soup()` reloads a
     released soup, shared while held, freed with its last holder; a changed
     source file is refused with an error. Count properties use recorded
     counts; the display-color rebuild reads the soup in its task; the glTF
     exporter holds the reloaded soups for the export (doc/erhe/primitive.md
     "Releasing the triangle soup").
   - Released: the soups only. The prepared topology (16 MB) stays: it is
     the colocation `get_geometry()` builds from without `colocate()`, and
     the raytrace BVH and precomputed triangles serve every ray cast.
   - Always (memory report candidate 2): `Primitive_raytrace` frees its
     vertex / index copies after the commit when the backend does not read
     them (`IGeometry::release_buffers()`: bvh, tinybvh; not embree) and maps
     a soup raytrace's facets by identity through a flag.

   Result (2026-10-25, prepared Bistro `logs/step9/bistro_prepared.glb`,
   Release headless desktop, AMD iGPU, warm caches, `load_scene`; Quest 3
   release APK, off-head automated, `--no-scene`, `load_scene` of
   `files/item4/bistro_quest.glb`, `close_scene "Scene 1"`, three runs):

   | | before | after |
   |---|---|---|
   | desktop idle heap (`keep` / `release`) | 1068 MB | 775 MB (-293) |
   | desktop `cpu_mesh.render_shapes` total | 773 MB (report) | 660 MB `keep` (raytrace copies + facet map 0), 361 MB `release` (soups 0, 3852 released) |
   | desktop listed / idle | 3.1-3.6 / 3.5-3.6 s (`keep`) | 3.16-3.19 / 3.56-3.59 s (`release`) |
   | desktop render md5 | 89832b62... | 89832b62... (both settings) |
   | desktop save after load (`release`) | 1.8 s (`keep`) | 2.3 s: 3852 soups (305 MB) read back, BIN chunk byte-identical to the input, heap back to idle + selection after the save |
   | Quest heap (malloc in use, idle) | 1440 MB (item 4) | 994 MB (-446) |
   | Quest RSS + swap (idle) | 1598-1641 MB | 1199-1357 MB |
   | Quest VmHWM | 1988-2012 MB | 1981-2018 MB (the load transient; soups exist until finalize) |
   | Quest listed / idle | 10.1-10.3 s | 9.7-10.1 / 10.1-10.6 s |

   Selecting three meshes reloaded their 5 soups (full build and
   `get_geometry()`); no reload happens during or after the load itself.
4. **Quest-ready glTF with ASTC textures.** A save variant that writes the
   textures as KTX2 with a raw ASTC 4x4 vkFormat (transcoded from the
   Basis source), so Quest uploads them without transcoding; desktop
   loaders without ASTC support decode them on the CPU.
   Done: File > Save glTF for Quest / MCP `save_scene_for_quest` write the
   `save_scene` file with every Basis texture transcoded on executor
   workers to an ASTC 4x4 KTX2 (all levels, SRGB / UNORM from the loaded
   texture's format, two component normal maps kept as X in RGB / Y in A
   with the `--normal-mode` marker, Zstandard supercompressed through the
   zstd already bundled with basis_universal), carried by the asset-root
   `ERHE_texture_ktx2` extension (`doc/gltf_extensions/ERHE_texture_ktx2.md`;
   `extensionsUsed` only, because fastgltf rejects unknown required
   extensions and has no texture extension passthrough). PNG / JPEG images
   are written as they are. The loader uploads the stored blocks when the
   device samples ASTC and otherwise decodes level 0 to RGBA8 with the ARM
   astc-encoder decoder (5.3.0, decoder sources only).

   Result (2026-10-25, Release headless desktop, AMD iGPU without ASTC;
   Quest 3 release APK, off-head automated, `--no-scene`, `load_scene`, then
   `close_scene "Scene 1"`):

   | | value |
   |---|---|
   | save (foreign Bistro loaded and idle) | 8.0 s (export 6.7 s: preparation 1.7 s, transcode + zstd 3.7 s); 393 of 393 images transcoded, 0 failed |
   | file | 857 MB (normal prepared save 664 MB); ASTC textures 402 MB (1807 MB before zstd; the Basis sources were 208 MB) |
   | desktop load | listed 8.1 s, idle 8.5 s (prepared normal 3.1-3.4 / 3.5-3.8 s): parse 1.0 s, 393 images decoded to RGBA8, 3852 prepared used, 0 deferred tasks; scene textures 7.2 GB RGBA8 (4x the ASTC size), peak working set 4.8 GB |
   | desktop render vs prepared reference (89832b62...) | 40411 of 786432 pixels differ, max channel delta 183, PSNR 48.2 dB; no missing textures, normal or sRGB errors seen; identical with and without zstd |
   | Quest load, 3 runs | listed = idle 10.1-10.3 s (item 6: 11.2-11.5 s); parse 1.55-1.80 s; 393 raw ASTC uploaded as stored, 0 transcodes; 3852 prepared used; VmHWM 1988-2012 MB (item 6: 1660-1728 MB) |
   | Quest GPU textures | 1806 MB (ASTC SRGB 884 MB / 203, ASTC UNORM 923 MB / 190) |

   The Quest residency now takes about 6.5 s and is bound by
   `load.gpu_upload_bytes_per_frame` (4 MiB per frame at 72 Hz, 1.8 GB of
   blocks), not by decoding. The VmHWM growth is the retained encoded
   streams (402 MB instead of 208 MB, memory report candidate 5). The
   device's writable config predates `load.max_upload_staging_bytes`, so the
   runs used the 64 MiB staging default (high-water 29-41 MB); it does not
   change the upload rate.
5. **Headset scene selection.** Done: closing the scene the global tools
   are homed in moves them to the most recently opened remaining scene, and
   MCP `set_headset_scene` (or the headset window's scene combo) moves them
   explicitly (`doc/editor/tools.md` "Headset scene"). On Quest, `--no-scene`
   + `load_scene` of the prepared Bistro + `close_scene "Scene 1"` renders
   Bistro in the headset.
6. **Limit the CPU-visible memory used for GPU uploads.** Done:
   - `parse_gltf` decodes no image; `Gltf_image_residency` decodes /
     transcodes on workers ahead of the upload under
     `load.max_decoded_bytes_in_flight` (128 MiB), starting when the parse
     is done and overlapping the build (doc/editor/async_asset_loading.md
     "Image residency").
   - Texture and loader-queue buffer uploads stage through one capped ring,
     `load.max_upload_staging_bytes` (64 MiB desktop, 32 MiB OpenXR); a
     full ring makes the upload wait a frame, an upload larger than the cap
     goes alone through a one-shot buffer (doc/erhe/ring_buffer_memory.md
     "Upload staging cap").
   - `parse_gltf` / `scan_gltf` stream the file, which removes the .glb
     double copy (memory report candidate 6).

   Result (2026-10-25, Release headless desktop, AMD iGPU, warm caches, two
   opens per file; listed / idle from `scene_load_profile.py --tool
   load_scene`, peaks from MCP `get_memory_usage` process sampling):

   | Open | listed before / after | idle before / after | peak working set before / after | peak heap before / after |
   |---|---|---|---|---|
   | prepared `.glb` | 4.35-4.50 / 3.08-3.40 s | 4.35-4.50 / 3.48-3.81 s | 3916 / 1675 MB | 3140 / 1225 MB |
   | foreign `.gltf` | 3.01-4.01 / 2.26-3.71 s | 6.18-6.90 / 5.50-6.90 s | load phase 2746 / about 1100 MB | 4403 / 4404 MB (deferred phase) |

   `parse_gltf` 1.86-2.31 s before, 0.82-0.93 s after (prepared). The
   foreign file's overall peak is its deferred `Geometry` phase, unchanged
   (4.9 GB working set). Renders byte-identical before / after (prepared
   md5 89832b62..., foreign d488fefc...). Logged per load: decoded pixels
   high-water 128.6 MB of the 128 MiB budget, upload staging high-water
   33-45 MB of the 64 MiB cap, 0 waits; no main-role ring buffer spill, no
   outpacing warning. Stress run with a 4 MiB cap and a 16 MiB decode
   budget: both files load in 11-13 s, the 325 images larger than the cap
   (5.6 MB BC7 chains) take the one-shot path, staging high-water equals
   the largest image, renders identical.

   Quest 3 (release APK, off-head automated, `--no-scene` then
   `load_scene` of the prepared Bistro): listed 16.7 s before, 11.2-11.5 s
   after (3 runs); VmHWM 3879 MB before, 1660-1728 MB after. Upload staging
   high-water 20-29 MB (the device's writable config predates the setting,
   so these runs used the 64 MiB default; a fresh install gets 32 MiB),
   decoded pixels high-water 128.6 MB.

Decisions on the follow-up items (user, 2026-10-25):

- Item 4: Quest uses ASTC. A dedicated "Save for Quest" operation (File
  menu and MCP) transcodes every Basis texture to raw ASTC 4x4 KTX2 at
  save time, so loading on Quest uploads the blocks with no transcode;
  desktop loaders decode the ASTC images on the CPU.
- Item 6 (new): **limit the CPU-visible memory used for GPU uploads.** A
  hard cap on upload staging memory, and images are decoded / transcoded
  only as fast as residency uploads them (an in-flight byte budget), which
  removes the all-images-decoded peak the memory report measured (about
  1.5 GB).
- Order: item 6, then item 4, then item 3.

