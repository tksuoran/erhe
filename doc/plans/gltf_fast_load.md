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
| KTX2 Basis transcode | parse worker | 0.9 s wall, 21 worker-s | step 8 (texture decision) |
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
7. **Lazy `Geometry` and on-demand full build.** Gates:
   - no `Geometry` alive after a Bistro load (`get_memory_usage`; RSS down
     by about 1.9 GB);
   - turning edge lines on builds them and renders like the foreign load at
     idle;
   - `mcp_server_tests` mesh-component and lightmap cases pass;
   - an export of an untouched prepared scene is byte-identical to its
     source.
8. **Textures**, depending on decision 3: a transcode cache, or embedded
   per-target images.
9. **Quest.** Push the erhe-saved Bistro to the headset and time the load
   against the foreign file, with logcat phase lines and memory, following
   `doc/agents/quest.md`. Gate: 0 deferred tasks, plus load time and peak
   memory recorded here.

## Decisions (2026-10-25, user: "proceed" on the recommendations)

1. The first save rewrites the imported vertex data into the prepared core
   (reorder, colocation snap within 1e-5, degenerate triangles dropped,
   tangents baked, weights normalized). No lossless remap form.
2. `Geometry` and the full buffer mesh are lazy (step 7).
3. Textures: a device-local transcode cache under `cache/` (step 8); no
   per-target images in the file. Revisit after the Quest measurement.
4. `save_scene` always writes the extensions; `export_gltf` only when its
   arguments ask for it.
5. The BVH is stored in the file; `cache/bvh` keeps serving foreign files.
