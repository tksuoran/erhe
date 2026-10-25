# ERHE_mesh_optimized

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md).

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). No other extension is required.
[`ERHE_mesh_topology`](ERHE_mesh_topology.md) and
[`ERHE_raytrace_bvh`](ERHE_raytrace_bvh.md) depend on this one.

## Scope

Mesh **primitive** extension.

## Overview

Marks a triangle-soup primitive (one without
[`ERHE_geometry`](ERHE_geometry.md)) whose core accessors are already in the
form erhe's mesh optimizer produces, so the loader packs them into the
device's vertex formats without running meshoptimizer. The primitive remains
a plain TRIANGLES primitive that stock viewers render unchanged.

## Core-primitive contract

A writer attaches the extension only when every statement below holds for the
core primitive. Vertex, index and triangle below refer to the core
accessors.

1. `mode` is TRIANGLES (4) and `indices` is present. The primitive has no
   morph `targets`.
2. `POSITION` is VEC3 FLOAT. `NORMAL` (VEC3 FLOAT) and `TANGENT` (VEC4 FLOAT)
   are present. Every other attribute uses a componentType glTF allows for
   its semantic; erhe writes FLOAT wherever glTF allows it (`JOINTS_n` stay
   unsigned integers).
3. **Bitwise weld.** No two vertices have identical bytes across all core
   attributes.
4. **Colocated positions.** Vertices that belong to one geometric point
   (geogram colocation with tolerance 1e-5) carry bitwise-identical
   `POSITION` values: the position of the point's representative.
5. **No degenerate triangles.** No triangle has two corners whose `POSITION`
   values are bitwise identical. Therefore core triangle `t` is facet `t` of
   the `erhe::geometry::Geometry` built from the primitive, and every element
   mapping (triangle to facet, corner to vertex) is the identity.
6. **Every vertex is referenced** by at least one triangle.
7. **Order.** The indices are in meshoptimizer vertex-cache order, then
   overdraw order (with `overdraw_threshold`), and the vertices are in
   vertex-fetch order, applied in the order `passes` lists them. The order is
   a performance property only: the loader never checks it.
8. **Joint weights** (`WEIGHTS_0`) are normalized the way
   `erhe::primitive::normalize_joint_weights()` normalizes them.

## JSON layout

```json
{
    "version": 1,
    "passes": ["weld_bitwise", "vertex_cache", "overdraw", "vertex_fetch"],
    "overdraw_threshold": 1.05,
    "content_hash": "8c3f04a1d2e97b60",
    "bounding_sphere": {"center": [0.0, 1.25, 0.0], "radius": 2.5}
}
```

- `version`: integer, `1`. A loader rejects any other value.
- `passes`: the optimizer passes the core went through, in application
  order. Names: `weld_bitwise`, `vertex_cache`, `overdraw`, `vertex_fetch`.
  `weld_bitwise` is always present (contract item 3).
- `overdraw_threshold`: the threshold given to `meshopt_optimizeOverdraw()`.
  Present exactly when `passes` contains `overdraw`.
- `content_hash`: 64-bit FNV-1a (`erhe::hash::hash()` in
  `src/erhe/hash/erhe_hash/hash.hpp`: offset basis `0xcbf29ce484222325`,
  prime `0x100000001b3`, one byte at a time) over this byte sequence:
  1. every index value of the `indices` accessor, in accessor order, each
     widened to uint32 and written as 4 bytes little-endian (regardless of
     the accessor's componentType);
  2. then every `POSITION` element, in accessor order, as three float32
     values x, y, z, 4 bytes little-endian each, the bits as stored.

  Encoded as exactly 16 lowercase hexadecimal digits, most significant digit
  first, zero padded.
- `bounding_sphere`: a sphere in the primitive's local space (the space of
  `POSITION`) that encloses every vertex position; `center` is three numbers,
  `radius` a non-negative number. erhe writes the sphere
  `erhe::math::calculate_bounding_volume()` computes over the `POSITION`
  values, so the loader uses it in place of computing it. The axis-aligned
  box comes from the `POSITION` accessor's required `min` / `max`.

Joint bounds for skinned primitives (`Buffer_mesh::joint_bounding_boxes`)
are not stored: they are one linear pass over `POSITION`, `JOINTS_0` and
`WEIGHTS_0` of the core.

## Load semantics

The optimized render variant is one linear pack of the core into the
device's optimized vertex format. A device whose vertex formats quantize more
coarsely than float only misses weld merges that would have been possible;
the packed bytes are still exact.

## Validation / fallback

The loader rejects the extension on a primitive, and loads the primitive
through the path for primitives without it, when any of these fails:

- `version` is 1 and every `passes` entry is a known name;
- contract items 1, 2 and 6: mode, indices, attribute presence and types,
  no `targets`, every vertex referenced;
- the `indices` count is a multiple of 3 and every index is below the
  `POSITION` count;
- contract item 5: no triangle has two corners with bitwise-identical
  `POSITION`;
- `content_hash` parses as 16 lowercase hex digits and equals the hash the
  loader computes;
- `bounding_sphere` has a finite center and a finite, non-negative radius.

Contract items 3, 4, 7 and 8 are not checked: violating them costs
performance or weld merges, never correctness of the loaded primitive.

A rejected `ERHE_mesh_optimized` also rejects `ERHE_mesh_topology` and
`ERHE_raytrace_bvh` on the same primitive, because both depend on core
triangle `t` being facet `t`.

## Schema

[schema/ERHE_mesh_optimized.schema.json](schema/ERHE_mesh_optimized.schema.json)
