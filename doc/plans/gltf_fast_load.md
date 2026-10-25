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

Further items (user, 2026-10-25):

- Item 7: **raise the per-frame upload budget while a load is in
  progress** (texture upload is ~6.5 s of the 10 s Quest load at 4 MiB per
  frame); back to the normal budget once the load settles.
  Done: `load.gpu_upload_bytes_per_frame_while_loading` (Load_config v6;
  0 = platform default, 32 MiB desktop / 16 MiB OpenXR, half of each
  platform's staging cap; committed configs carry the explicit values, a
  stale device config reads 0) applies while any load task is live; the
  phase switches when the first task is queued and the last is reaped
  (`doc/editor/async_asset_loading.md` "Upload budget while loading").
  Result (2026-10-25):

  | | before (4 MiB) | after |
  |---|---|---|
  | Quest 3 release APK, off-head, `--no-scene`, `load_scene` `bistro_quest.glb`, 3 runs | listed = idle 10.1-10.3 s | listed = idle 7.1-8.0 s (16 MiB, stale device config) |
  | Quest VmHWM | 1981-2012 MB | 1987-2021 MB |
  | Quest frames during residency (60-frame averages) | upload term 1.2-3.5 ms, full tick 13.9-15.8 ms | upload term 3.8-12.2 ms, full tick 14.3-20.4 ms; publish frame window 38-40 ms (before 44 ms); 0 XR frame discards |
  | desktop Release prepared Bistro, 3 interleaved runs | listed 3.07-3.14 s, idle 3.48-3.56 s | listed 2.94-3.12 s, idle 3.03-3.35 s (32 MiB) |
  | desktop render | 89832b62... | 89832b62... (all 6 runs) |

  Desktop is bound by the Basis transcode, not the upload; staging
  high-water at 32 MiB per frame reached the 64 MiB cap (5 waits).
- Item 8: **MCP save commands are asynchronous** (save_scene,
  save_scene_for_quest): they return at once and the save's completion and
  result are polled, so a long save is not cut off by the MCP reply time
  limit. Saving goes through a temporary file (user, 2026-10-25): a save
  onto the scene's own source never reads a partially written file, and a
  failed save keeps the old file.
  Done: the tools queue a job that runs on the main thread at the end of
  the MCP request pass; `get_save_status` (answered on the server thread)
  reports pending / running / done / failed with path, file_bytes,
  save_ms, export_ms, prepared and texture statistics
  (`doc/agents/mcp_server_usage.md` "Saving scenes"). Every glTF write uses
  `erhe::file::write_file_via_temp`.
  Result (2026-10-25, Release headless desktop): foreign Bistro
  `save_scene_for_quest` replied in 0.16 s (incl. the client process),
  status answered while running, done after 8.5 s, 857 249 836 bytes
  (same as item 4), 393 / 393 transcoded; `save_scene` done after 3.9 s,
  663 773 028 bytes. Prepared Bistro loaded with `cpu_mesh_data` release
  and saved onto its own path: done in 2.7 s, the file is byte-identical
  to a keep-mode save to another path and to the same save by the
  pre-change editor; it renders 001cdd5e... (356 pixels, max delta 55,
  from 89832b62... of `logs/step9/bistro_prepared.glb`; a re-save of the
  prepared file renders so at HEAD too). A save onto a file another
  handle holds open fails the job (rename refused) and leaves the file
  byte-identical, no temporary left.

## Item 9: streaming load (design, 2026-10-25, awaiting review)

User requirements: texture file bytes are read just in time for their
upload and never retained; mesh data streams the same way; one in-flight
budget bounds the CPU memory of both; a save onto the scene's own source
rebinds the reload sources to the new file; "Save for Quest" stores GPU
ready vertex / index streams that load with at most a memcpy, with explicit
per-attribute scale and bias in an ERHE extension.

### Where the memory goes today (the targets)

- `parse_gltf` streams the file, but fastgltf still copies the whole GLB
  BIN chunk into one buffer (664 MB prepared, 857 MB Quest file) and keeps
  it until the parse ends.
- The parse builds every soup at once (about 300 MB) and retains every
  encoded image stream (208 MB, Quest file 402 MB) for export.
- Every build task converts its soup into base and optimized sink vectors
  and enqueues them on the `Mesh_memory` loader queue. The queue holds them
  until `flush_budgeted` stages them at 16-32 MiB per frame, so the backlog
  can hold most of the mesh pool content (Bistro: about 650 MB) at once.
- Quest VmHWM 1.98-2.02 GB against about 1.2-1.36 GB RSS at idle.

Targets: Quest VmHWM at most 1.2 GB with `bistro_quest.glb`; desktop
(`keep`) peak working set at most idle + 0.35 GB; listed / idle times no
worse than item 7 (Quest 7.1-8.0 s, desktop 2.94-3.12 / 3.03-3.35 s);
renders byte-identical.

### Load data flow

```
scan (worker) -> parse (worker: JSON + structure, no payload bytes)
  -> per unique primitive: mesh job   \  scheduled from Asset_manager::tick
  -> per image:            image job  /  under one Memory_budget
       job: positional read of the byte ranges -> validate -> convert
            (portable) or nothing (GPU ready) -> enqueue on the loader
            queue / Image_transfer -> budget lease released when the bytes
            are copied into upload staging
  -> publish (main, watermark-gated, unchanged)
```

- **Parse.** `parse_gltf` reads the JSON and the structure only. The GLB
  BIN chunk and external `.bin` files are not loaded, with no fastgltf fork
  change: external buffers stay URIs (no `Options::LoadExternalBuffers`),
  and erhe's `Gltf_file_stream` skips the BIN chunk (the buffer map
  callback answers the chunk with a marker address the stream turns into a
  seek; doc/erhe/gltf.md "Reading buffers by range").
  `resolve_gltf_buffer_origins()` becomes the parse's buffer model. Small payloads that structure needs
  are read eagerly by range during the parse: animation samplers, skin
  inverse bind matrices, `ERHE_geometry` payloads, physics collider
  meshes, `ERHE_brushes` and node-graph meshes. Buffers without a file
  origin (data URIs, `glb_data` parses from memory) stay on today's eager
  path. The parse stays device-free; it records a source per primitive and
  per image and copies the accessor / view descriptors the jobs need, so
  the fastgltf asset dies with the parse.
- **Source model** (one for meshes and images). `Source_file` (shared:
  path, size, modification time) plus `Source_range` (file offset,
  length, stride). `Triangle_soup_source` (doc/erhe/gltf.md "Triangle soup
  reload") keeps its accessor copies and holds `Source_range`s;
  `Gltf_image_source` gets a `Source_range` in place of the retained
  bytes. Every read opens the file, checks the stamp, reads, closes: no
  handle stays open after a load or a reload, because an open handle makes
  the save's rename fail on Windows (item 8 result).
- **Mesh job** (worker, one per unique render shape; replaces the build
  phase's per-shape task). `erhe::gltf::load_primitive_payload(source,
  options)` (device-free) reads the primitive's ranges and validates the
  prepared extensions (the step 5 checks move here from the parse). Then
  `erhe::primitive` builds:
  - portable path: soup from the ranges (`build_triangle_soup()`, the
    reload function, so first load and reload are one code path), then
    `Primitive::make_prepared_meshes()` or `make_renderable_mesh()` as
    today;
  - GPU ready path (below): no soup, no conversion.

  The raytrace with a stored BVH is committed in the job. With
  `load.cpu_mesh_data` `release` the soup is dropped at the end of the job
  (it was never retained); with `keep` it stays on the shape. A primitive
  whose prepared data is rejected, or a foreign primitive, builds its soup
  in the job and is picked up by the deferred finalize exactly as today.
- **Image job** (worker): reads the encoded range just in time, decodes /
  transcodes as `Gltf_image_residency` does today, drops the encoded bytes
  after the decode. A raw ASTC KTX2 without supercompression is read level
  by level straight into the buffer that is staged (no decode). Nothing is
  retained for export.
- **One budget.** `erhe::utility::Memory_budget`, owned by `Asset_manager`, shared by
  every live load: `load.max_in_flight_bytes` (Load_config v7, replaces
  `max_decoded_bytes_in_flight`, migrated; default 128 MiB desktop, 64 MiB
  OpenXR). A job takes a lease of its estimated peak before it reads
  (computed from the JSON: mesh = range bytes + soup bytes + base and
  optimized sink bytes, or the stream bytes on the GPU ready path; image =
  encoded + decoded bytes); a single job larger than the budget runs alone
  (today's image rule). The lease is not released when the job ends but
  when its bytes leave the CPU: `Buffer_transfer_queue` entries and
  `Image_transfer` uploads carry the lease and drop it when
  `flush_budgeted` / `upload_into_frame` copy the bytes into upload
  staging. That bounds the loader queue backlog, today's unbounded holder.
  Mesh jobs have priority (publish waits for meshes, textures may lag);
  images get the remainder, at least one in flight. `get_memory_usage` and
  the "resident" log line report the budget high-water and the lease
  counts per kind.
- **Peak** = resident data (scene, retained soups under `keep`, raytrace,
  topology) + budget + upload staging cap + parse structures.

### Retention and reload, unified

| | first load | after upload | read again by |
|---|---|---|---|
| mesh soup | job reads ranges | `keep`: stays; `release`: dropped | `get_triangle_soup()` (one read path, as today) |
| image bytes | job reads range | always dropped (no setting: the user's rule) | export, through `Gltf_image_source` |
| prepared topology, raytrace BVH | job | stay (unchanged) | - |

Sources without a file origin keep their bytes (meshes: soup; images:
encoded stream), as today's reload does.

### Export re-read and post-save rebind

- The exporter reads each image's bytes from its source at export time
  (one task per image, beside the transcode tasks); a changed source file
  fails the save with an error naming the image (no silent drop). Meshes
  already reload their soups for the export.
- `export_gltf` returns, beside the bytes, the written layout: per exported
  shape and per image, the bufferView ranges relative to the BIN chunk,
  the BIN chunk offset, and whether the written bytes are the shape's soup
  / the image's source bytes unchanged (`retained`).
- After `write_file_via_temp` succeeds and the target path is a path some
  sources reference, the editor (main thread) stats the new file and
  rebinds: every source on that path whose data was written `retained`
  gets a new source (new `Source_file`, new ranges). A released soup whose
  written core is not its soup (a non-prepared primitive saved onto its
  own file) cannot be rebound: the exporter already holds it reloaded, and
  the rebind keeps it resident (pinned, counted in the save status) so its
  data does not vanish with the old file.
- The swap is under the shape's soup mutex. A reload that captured the old
  source and is refused for the changed stamp retries once when the
  shape's current source is a different object (the source moved; this is
  the defined behavior, not a tolerance of a stale state).
- "Save for Quest" onto the scene's own source path is refused: it
  rewrites textures (ASTC) and vertex data (GPU streams), so the scene in
  memory would no longer be what its source holds (decision D4).

### Quest file: GPU ready vertex streams (`ERHE_gpu_vertex_streams`)

The Quest file writes each prepared primitive's base and optimized vertex
streams and its index buffer exactly in the running `Mesh_memory` layout.
The base streams are the core primitive's accessors, so the file stays a
valid glTF 2.0 that stock viewers render with no extension required; the
optimized streams are reachable only from the extension. Spec skeleton:
`doc/gltf_extensions/ERHE_gpu_vertex_streams.md` (draft).

Layout per primitive: one contiguous region `[indices][base s0][base
s1][optimized s0][optimized s1]`, so the loader reads a primitive with one
read and enqueues slices of it (loader queue entries hold a shared owner
plus offset / length, so the shared index bytes feed both variants without
a second copy).

Mapping (the formats `Mesh_memory` derives on Quest and desktop today; the
writer takes offsets and strides from the running `Vertex_format`, so this
table is informative):

| variant / stream | attribute | glTF accessor | normalized | stride | offset | scale / bias |
|---|---|---|---|---|---|---|
| core | indices | SCALAR UNSIGNED_INT | - | - | - | - |
| base s0 | POSITION | VEC3 FLOAT | no | 12 (skinned 20) | 0 | - |
| | JOINTS_0 (skinned) | VEC4 UNSIGNED_BYTE | no | 20 | 12 | - |
| | WEIGHTS_0 (skinned) | VEC4 UNSIGNED_BYTE | yes | 20 | 16 | - |
| base s1 | NORMAL | VEC3 FLOAT | no | 68 | 0 | - |
| | TANGENT | VEC4 FLOAT | no | 68 | 12 | - |
| | TEXCOORD_0 / 1 / 2 | VEC2 FLOAT | no | 68 | 28 / 36 / 44 | - |
| | COLOR_0 | VEC4 FLOAT | no | 68 | 52 | - |
| base s2 | smooth normal, aniso, valency, facet id | none: zero-filled | - | 24 | - | - |
| optimized s0 | POSITION | VEC3 SHORT (a KHR_mesh_quantization type) | yes | 8 (skinned 16) | 0 | scale = AABB half extent (min 1e-6), bias = AABB center |
| | JOINTS_0 (skinned, permuted smallest weight last) | VEC4 UNSIGNED_BYTE | no | 16 | 6 | - |
| | `_WEIGHTS_IMPLICIT` (skinned) | VEC3 UNSIGNED_SHORT | yes | 16 | 10 | fourth weight = 1 - sum |
| optimized s1 | `_TBN_QUATERNION` | VEC4 SHORT | no | 24 | 0 | erhe encoding (`erhe_vertex_tbn.glsl`) |
| | TEXCOORD_0 / 1 | VEC2 UNSIGNED_SHORT | yes | 24 | 8 / 12 | scale = range max - min (min 1e-6), bias = range min |
| | TEXCOORD_2 (lightmap) | VEC2 UNSIGNED_SHORT | yes | 24 | 16 | - |
| | COLOR_0 | VEC4 UNSIGNED_BYTE | yes | 24 | 20 | - |
| optimized s2 | aniso control | none: zero-filled | - | 4 | - | - |

- Attributes the soup path writes as zeros (base s2, optimized s2) are not
  stored: the stream is declared zero-filled and the loader fills the
  staging range with zeros.
- The optimized skinned offsets 6 and 10 break glTF's 4-byte element
  alignment for mesh attributes, one more reason the optimized accessors
  are referenced only from the extension. The validator reports them as
  unused objects (info), as it does for the step 4 raw views.
- The core indices are written UNSIGNED_INT (the `Mesh_memory` index
  format) and are shared by both variants (identity mappings).
- Base WEIGHTS_0 is unorm8, so the Quest file's core carries the
  quantized weights; `ERHE_mesh_optimized` contract item 2 is amended for
  primitives that carry `ERHE_gpu_vertex_streams`.

Extension JSON (primitive):

```json
"ERHE_gpu_vertex_streams": {
  "version": 1,
  "encoding_version": 1,
  "variants": [
    {"role": "base",
     "signature": "s0=12:position.0=32_vec3_float@0|s1=68:normal.0=32_vec3_float@0,...|s2=24:...",
     "streams": [{"bufferView": 40}, {"bufferView": 41}, {"fill": "zero", "byteStride": 24}]},
    {"role": "optimized",
     "signature": "s0=8:position.0=16_vec3_snorm@0|s1=24:tangent.0=16_vec4_sint@0,...|s2=4:...",
     "streams": [{"bufferView": 42}, {"bufferView": 43}, {"fill": "zero", "byteStride": 4}],
     "attributes": {
       "POSITION":        {"accessor": 90, "scale": [4.1, 2.0, 7.3], "offset": [0.5, 2.0, -1.0]},
       "_TBN_QUATERNION": {"accessor": 91},
       "TEXCOORD_0":      {"accessor": 92, "scale": [3.0, 3.0], "offset": [-1.0, 0.0]},
       "TEXCOORD_2":      {"accessor": 93},
       "COLOR_0":         {"accessor": 94}
     }}
  ]
}
```

- `signature`: canonical string of the variant's `Vertex_format` after
  repack (per stream: stride, then per attribute `usage.index=format@offset`
  with `erhe::dataformat` names), produced by one function,
  `Mesh_memory::get_vertex_format_signature()`. It captures the
  device-dependent choices (position quantization on / off, unorm16x3
  weights, attribute alignment) as layout.
- `encoding_version`: bumped whenever an encoder changes bits for the same
  layout (TBN rounding, implicit-sum rounding, affine epsilon).
- `scale` / `offset`: decoded = stored (normalized) * scale + offset per
  component, written with float round-trip precision (9 significant
  digits) and parsed back to the same float. They are the values the
  encoder used, stored, never re-derived.
- **How scale / bias reach the shader.** `Buffer_mesh` gains explicit
  `position_quantization` and `texcoord_quantization` fields. Both build
  paths fill them (computed from the AABB / UV ranges as today, or read
  from the extension); `Draw_list_scene::write_entry_record()`, the other
  `Primitive_buffer` writers and the BLAS build transform read the fields
  instead of recomputing them from `bounding_box` / `texcoord_ranges`. The
  per-draw `Primitive_struct` already carries `position_scale` /
  `position_offset` and `texcoord_scale` / `texcoord_offset`, so shaders do
  not change. Further per-attribute scale / bias for other shader uses add
  fields there the same way.
- **Future work:** baking the position scale / bias into the model matrix
  (saves a shader `fma`; not possible for skinned meshes, see
  doc/erhe/vertex_position_quantization.md section 3) as a renderer option.
- **Loader.** Per variant: `signature` equal to the running format's,
  `encoding_version` equal to the compiled one, every stream's
  `bufferView.byteLength` = count * stride, zero-fill strides equal to the
  running strides. Always: indices below the vertex count (one read pass,
  needed for GPU safety) and the `ERHE_mesh_optimized` content hash
  (indices + base POSITION, from the read buffer). A matching variant is
  enqueued as stored; a mismatching one is built from the soup (portable),
  which the job then builds. Stream bytes are otherwise trusted. One line
  per file: `gpu vertex streams: N stored, M portable (reasons)`.
  `load.use_gpu_vertex_streams` allows A/B runs.
- **Build phase with GPU ready streams.** `Primitive::make_stored_meshes()`
  allocates both variants' ranges (lockstep, as the soup build does), sets
  the bounding box (POSITION min / max), the sphere
  (`ERHE_mesh_optimized`), the quantization fields and `vertex_input_key`,
  and enqueues the read slices. The raytrace commits from base s0
  positions + indices with the stored BVH (a `Primitive_raytrace` overload
  over a strided position view; the precomputed triangles are its only
  copy). Under `release` no soup is ever built; under `keep` the soup is
  built from the core accessors.
- **Edge lines, full build, Geometry:** lazy as today (step 7), built from
  the soup (read back from the core accessors) with the stored topology.
- **Raytrace BVH:** `ERHE_raytrace_bvh` unchanged, read in the same
  per-primitive region.
- **Textures in the Quest file:** `ERHE_texture_ktx2` as today. Zstandard
  supercompression is processing; a `supercompression: none` option makes
  the levels a direct read (about 1.8 GB of textures instead of 402 MB).
  Decision D3.
- **What a stock viewer sees:** a valid glTF 2.0 whose core primitives are
  the float base streams in interleaved bufferViews (all core-legal types;
  no extension required). Opaque to it: the optimized streams (quantized
  POSITION, TBN quaternion, implicit-sum weights), zero-filled streams,
  topology and BVH.

### Pre-existing finding: re-saving the prepared Bistro changes 356 pixels

Likely cause (read-only investigation): node transforms are not
idempotent through a save. `process_node()` always writes
`from_erhe(pre_transform * node.authored_parent_from_node_transform())`
(`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`). `operator*` on
`Trs_transform` builds a matrix-only transform, so the written translation
/ rotation / scale come from `glm::decompose()` of the composed matrix,
not from the TRS the node was loaded with; TRS -> matrix -> decompose is
not bit-exact, so every save moves node transforms by a few ulps.
Evidence: step 7 found the re-saved file's BIN chunk byte-identical and
its JSON different only in the `scale` / `translation` / `rotation` of
1771 nodes, so mesh and texture bytes are not involved. Ulp shifts of node
transforms move silhouettes and shadow edges, consistent with 356 changed
pixels (max delta 55). The first save of the foreign file (124 pixels,
step 4) has the same cause plus the core rewrite of decision 1.

Item 9's mesh / texture export changes do not fix it. Fix (commit 0
below): with an identity `pre_transform`, write the node's stored TRS
components (its `Trs_transform` keeps them valid after `set_trs`); compose
and decompose only when a non-identity import-root transform is folded in.
A node loaded from a `matrix` is decomposed once, at its first save, and
is stable afterwards.

### Build order

One commit per step (Opus coder, diff reviewed by the orchestrator). Load
gate: `scripts/scene_load_profile.py --render-image` on the Release
headless editor, peaks from `get_memory_usage` process sampling. Since
step 0 the reference is a prepared Bistro saved fresh from the foreign
file, which renders byte-identical to the foreign load: md5 d488fefc...
for both (the step 4-8 prepared file `logs/step9/bistro_prepared.glb`
carries the transform drift step 0 removed and renders 89832b62... at
HEAD before step 0, 434633d8... after). Quest: release APK, off-head
automated, `--no-scene` + `load_scene bistro_quest.glb`, 3 runs, VmHWM.

0. **Idempotent node transforms on export.** Gates: load prepared -> save A
   -> load A -> save B gives A == B byte for byte; A renders 89832b62...;
   an `erhe_gltf_tests` round trip checks TRS bit-exact.

   Result (2026-10-25, Release headless): three causes, all fixed. The
   exporter wrote `glm::decompose()` of `pre_transform * local` even for an
   identity `pre_transform`; the parse re-parented every child through the
   world-preserving `Xformable::set_parent` (local := inverse(parent world)
   * world); the open of an erhe-authored scene did the same for the
   top-level nodes. Old prepared file -> save A -> load A -> save B: A == B
   byte for byte (663773028 bytes), A's JSON equals the source's, every
   render 434633d8... (13 pixels from 89832b62..., which carried the
   drift). Foreign Bistro -> fresh save -> load -> save: the two saves are
   byte-identical and every render is d488fefc..., the foreign render: the
   124 pixels of step 4 were the same drift. `erhe_gltf_tests`
   `node_trs_bit_exact` (fails with the export fix disabled).
1. **JSON-only parse and the source model.** Buffer origins with the file
   places of every view, eager range reads for the views the parse reads
   through the asset, soups built through their sources; soups and images
   still built / retained as today. Gates: `erhe_gltf_tests` (GLB, .gltf +
   .bin, data URI cases), render identical, desktop and Quest peak recorded
   (expected drop: about the BIN size).

   Result (2026-10-25): no fastgltf fork change (`Gltf_file_stream`,
   doc/erhe/gltf.md "Reading buffers by range"). The source views cover only
   the bytes their accessors read (the foreign Bistro packs all meshes into
   7 views of up to 47 MB), which also makes the reload of a foreign file's
   soup read kilobytes instead of whole views. `erhe_gltf_tests` 17/17 (new:
   .glb file parse == memory parse, .gltf + .bin parse == data URI parse,
   soups and an animation read from a view read whole), `erhe_gltf_gpu_tests`
   2/2. Renders: fresh prepared and foreign Bistro d488fefc... (unchanged).

   | | before | after |
   |---|---|---|
   | desktop prepared Bistro (Release headless, one editor per open) peak working set | 1635 MB | 1351-1360 MB (3 runs) |
   | desktop listed / idle | 3.13 / 3.55 s | 2.51-2.94 / 2.51-2.94 s |
   | desktop `parse_gltf` | 818 ms | 933-1120 ms |
   | desktop bytes read whole / views read per primitive | whole BIN (664 MB) | 355 MB (images, topology, BVH) / 7704 |
   | foreign Bistro listed / idle | 2.06-3.76 / 5.7-7.0 s | 2.25 / 5.47 s (parse 861 ms; 0.6 MB read whole) |
   | Quest `bistro_quest.glb` VmHWM | 1987-2024 MB | 1747-1782 MB |
   | Quest listed = idle | 7.43-7.99 s | 6.91-7.08 s |
   | Quest `parse_gltf` | 1769-1856 ms | 2145-2217 ms (549 MB read whole: the ASTC images, topology, BVH) |

   The drop is the mesh part of the BIN; the views still read whole are the
   images (step 3) and the prepared topology / BVH payloads (step 4 moves
   them into the per-primitive job). The parse is 0.1-0.4 s slower: 3852
   small range reads through one shared handle instead of one sequential
   read.
2. **Shared load budget.** `Memory_budget`, `max_in_flight_bytes`
   (v7 migration), leases carried by loader queue entries and image
   uploads; images move onto it. Gates: budget high-water at most the
   budget (a single oversize job excepted), render identical, load time
   recorded.

   Result (2026-10-25): `erhe::utility::Memory_budget` owned by
   `Asset_manager` (doc/editor/async_asset_loading.md "Load memory budget");
   the build phase dispatches one task per unique shape from the tick while
   its loader-queue bytes fit, and hands the lease to the queue
   (`hold_until_drained`); image decodes hold leases through the shared
   budget. Load_config v7 `max_in_flight_bytes` (0: 128 MiB desktop, 64 MiB
   OpenXR); `max_decoded_bytes_in_flight` deleted from the definition rather
   than marked `removed_in` (the `[[deprecated]]` member codegen leaves for a
   removed field breaks clang `-Werror` wherever the struct is copied - a
   codegen defect to fix separately). Tests: `erhe_utility_tests` 3/3 (new
   target), `erhe_gltf_tests` 17/17, `erhe_gltf_gpu_tests` 2/2,
   `erhe_scene_renderer_gpu_tests` 34/34, `ctest -R Mcp_` 92/95 (the 3
   ImGui hierarchy-row cases fail at HEAD too). `example`, `hextiles`,
   `hello_swap` build.

   | | step 1 | step 2 |
   |---|---|---|
   | desktop prepared Bistro, budget high-water (meshes / images) | - | 134.21 of 134.22 MB (134.21 / 128.6), 0 oversize |
   | desktop listed / idle | 2.51-2.94 s | 2.41-2.54 / 2.49-2.95 s (4 runs) |
   | desktop build phase | - | 368-381 ms |
   | desktop peak working set | 1351-1360 MB | 1349-1358 MB |
   | foreign Bistro listed / idle, render | 2.25 / 5.47 s, d488fefc | 2.18 / 5.35 s, d488fefc |
   | Quest budget high-water (meshes / images) | - | 67.11 of 67.11 MB (67.11 / 61.5) |
   | Quest VmHWM | 1747-1782 MB | 1724-1729 MB |
   | Quest listed (idle) | 6.91-7.08 s | 6.83-7.09 (7.30) s |
   | Quest build phase | 1.61 s | 1.58-1.66 s |

   Renders d488fefc... for both files. The peak barely moves: on this file
   the build backlog was not the peak; the parse-time views still read whole
   (images 402 MB on Quest, topology, BVH) are, until steps 3 and 4. The
   per-primitive reads also move into the step 4 jobs, which then own their
   ranges instead of the parse's shared reader (the step 1 parse slowdown).
3. **Image streaming.** Just-in-time read in the image job, no retained
   streams, export re-reads, changed-file error. Gates: `cpu_images` 0 at
   idle; a save of the untouched prepared Bistro has a byte-identical BIN;
   `erhe_gltf_gpu_tests` export re-read and changed-file cases; Quest
   VmHWM and idle heap (about -400 MB) recorded.

   Result (2026-10-25): `Gltf_image_source` is a file range for every image
   loaded from a file (doc/erhe/gltf.md "Image source streams"); the parse
   reads 16 bytes per image to sniff the media type and leaves image views
   unread; the decode task reads the stream and its lease covers stream +
   pixels until the upload; the exporter re-reads (Save for Quest holds the
   stream until its transcode); an unreadable source fails the export
   (empty result; the save / export paths write nothing). Tests:
   `erhe_gltf_gpu_tests` 3/3 (new: re-export from a file-backed source and a
   changed file refused), `erhe_gltf_tests` 17/17, `ctest -R Mcp_` 92/95
   (same 3 pre-existing failures).

   | | step 2 | step 3 |
   |---|---|---|
   | desktop prepared Bistro `cpu_images.retained_source_bytes` at idle | 208 MB | 0 |
   | desktop re-save of the untouched prepared Bistro | - | whole file byte-identical to the source (663646840 bytes), 1.8 s |
   | desktop Save for Quest of it | - | 857 MB, 393 / 393 transcoded, 5.8 s |
   | desktop bytes read whole at parse / `parse_gltf` | 355 MB / 944-957 ms | 147 MB (topology, BVH) / 751 ms |
   | desktop listed (= idle) | 2.41-2.54 s | 2.61-2.70 s (3 runs) |
   | desktop peak working set / heap at idle | 1349-1358 / 1068 MB | 1151-1154 / 860 MB |
   | foreign Bistro listed / idle, render | 2.18 / 5.35 s | 2.37 / 5.67 s, d488fefc |
   | Quest VmHWM | 1724-1729 MB | 1109-1138 MB |
   | Quest heap at idle (mallinfo in use) | 991 MB (402 MB retained ASTC streams) | 554 MB (0 retained) |
   | Quest listed (= idle) | 6.83-7.09 s | 6.20-6.42 s |

   Renders d488fefc... for both files. Quest is now below the item 9 target
   (1.2 GB) already; the desktop listed time grows by about 0.15 s (each
   image is read from the file inside its decode task).
4. **Mesh streaming.** The parse records sources only; mesh jobs run under
   the budget; validation moves into the job. Gates: prepared and foreign
   renders identical; prepared 0 deferred tasks, foreign deferred phase
   unchanged; Quest VmHWM at most 1.2 GB, desktop `keep` peak at most idle
   + 0.35 GB; load times no worse than item 7; `mcp_server_tests`.

   Result (2026-10-25): `Gltf_parse_arguments::mesh_payload = deferred` (the
   editor's async load where workers may build): the parse gives each
   file-backed soup primitive its source and counts; each build job reads
   the payload (`Primitive::load_payload()`: soup + prepared data
   validation), builds, and under `release` releases the soup at once
   (doc/editor/async_asset_loading.md "Mesh build jobs"). The prepared
   topology / BVH views are no longer read at the parse. All payload reads
   go through one `erhe::file::Positional_file_reader` per buffer file,
   shared by the jobs and closed after the last one (`Gltf_source_files`):
   the first version opened the file per read and the jobs took 1.6 s
   (Tracy: 11 worker-s in the soup reads, 21 in `validate()`, almost all
   file opens); the shared positional handle brought the build to 0.8 s.
   The step 1 shared `Gltf_range_reader` (serialized reads) is gone; an
   in-parse load reads through the same handles. Tests: `erhe_gltf_tests`
   18/18 (new: `deferred_mesh_payload_loads_like_the_parse`, including a
   changed file refused), `erhe_file_tests` 6/6 (new: positional reads from
   8 threads), `erhe_primitive_tests` 68/68, `erhe_gltf_gpu_tests` 3/3,
   `ctest -R Mcp_` 92/95 (same 3).

   | | step 3 | step 4 |
   |---|---|---|
   | desktop prepared Bistro `parse_gltf` / build phase | 751 / 358 ms | 211-219 / 785-831 ms |
   | desktop listed / idle | 2.61-2.70 / same | 1.97-2.02 / 2.37-2.44 s (3 runs) |
   | desktop peak working set (`keep`) | 1151-1154 MB | 1134-1149 MB |
   | desktop re-save of the untouched file | byte-identical | byte-identical |
   | foreign Bistro listed / idle | 2.37 / 5.67 s | 2.38 / 5.14 s; 2727 deferred tasks as before |
   | Quest VmHWM (`release`) | 1109-1138 MB | 798-809 MB |
   | Quest listed (= idle) | 6.20-6.42 s | 5.55-5.62 s |
   | Quest parse / build phase | - | 300 ms / 2.18 s, 3852 soups released in the jobs |

   Renders d488fefc... for both files; prepared Bistro 0 deferred tasks.
   Against the pre-item-9 numbers the desktop listed time is 0.9-1.1 s
   lower (step 0 era 3.1-3.5 s) and the Quest load 2 s faster with 60 % less
   peak memory.
5. **Post-save rebind.** Gates: prepared Bistro with `release` saved onto
   its own path, then meshes selected (soups read back, no refusal) and
   saved again (byte-identical); a foreign file saved onto itself pins its
   released soups (count in `get_save_status`); a unit test in
   `erhe_gltf_tests`.
6. **Explicit quantization on `Buffer_mesh`.** Gates: renders identical,
   scene renderer / editor GPU tests, BLAS path unchanged.
7. **Spec, schema and format signature.** Finish
   `ERHE_gpu_vertex_streams.md` + schema;
   `get_vertex_format_signature()`. Gates: `check_doc_links.py`; signature
   unit test.
8. **Save for Quest writes GPU streams.** Gates: the Khronos validator
   reports no new errors (infos only); the file loaded with
   `use_gpu_vertex_streams` off renders like the item 4 Quest file (Bistro
   has no skinning); file size and save time recorded; a test compares the
   stored stream bytes with the loader's pack of the same core, byte for
   byte.
9. **Loader GPU ready path.** Gates: `gpu vertex streams: 3852 stored, 0
   portable` on Quest and desktop; renders identical to the step 8
   portable load; Quest VmHWM, listed / idle and build time recorded
   against step 4; a signature mismatch (quantization off) falls back and
   renders as the portable path.

### Open items

- Codegen: a field marked `removed_in` leaves a `[[deprecated]]` member in
  the generated struct, and clang `-Werror` (the Android build) fails
  wherever the struct's implicit copy is used. Step 2 deleted
  `Load_config::max_decoded_bytes_in_flight` instead of marking it; fix the
  generator (for example a diagnostic push around the generated struct, or
  no member for a removed field) before the next removal.

### Risks

- The fastgltf fork option may touch more of the GLB loader than expected
  (fallback named under "Parse").
- Many small positional reads: per-primitive contiguous regions keep it to
  one read per primitive; foreign files take one read per bufferView range.
- Mesh-first scheduling may delay textures; measured in step 4.
- The export still assembles the whole output in memory (`std::string`
  plus the combined buffer, about twice the file); a streaming writer is
  separate work (D6).
- A source file edited outside erhe between load and save now fails the
  save for images, as it already does for released soups.

### Decisions (user, 2026-10-25)

- D1: GPU vertex streams are written by "Save for Quest" only;
  `save_scene` writes the prepared core as today.
- D2: `load.max_in_flight_bytes` defaults to 128 MiB on desktop and 64 MiB
  under OpenXR.
- D3: "Save for Quest" offers both texture forms as an option: Zstandard
  supercompressed ASTC and raw ASTC levels. Step 9 measures the Quest load
  time of both, and the measurement picks the default.
- D4: "Save for Quest" onto the scene's own source file is refused.
- D5: the lazy base variant on Quest is item 10, after item 9; it is not
  part of item 9's commits.
- D6: a streaming glTF writer and a lazy raytrace stay out of item 9.
- Image bytes are always released after upload; there is no setting.

## Item 10: lazy base variant on Quest (after item 9)

Under OpenXR, do not build or upload a prepared primitive's base variant at
load; build it on first demand (ID rendering, edge lines, solid wireframe,
in-place GPU edits). Expected: about half the mesh upload bytes and an
estimated 470 MB of GPU memory on Bistro, to be measured. Design to be
written when item 9 is done.

