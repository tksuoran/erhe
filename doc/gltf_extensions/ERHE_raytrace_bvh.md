# ERHE_raytrace_bvh

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md).

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). Requires either
[`ERHE_mesh_optimized`](ERHE_mesh_optimized.md) or
[`ERHE_geometry`](ERHE_geometry.md) on the same primitive: both define the
core triangle order as the order erhe's raytrace geometry uses (facet order;
fan triangulation per facet for `ERHE_geometry`). When the required extension
is absent or rejected, this extension is ignored.

## Scope

Mesh **primitive** extension.

## Overview

Stores a prebuilt bounding volume hierarchy over the primitive's core
triangles, so the loader installs the raytrace geometry without building a
BVH or consulting `cache/bvh`. The BVH is built over the core triangles in
index order: triangle `t` is indices `3t`, `3t + 1`, `3t + 2`. Stock viewers
ignore it.

## JSON layout

```json
{
    "version": 1,
    "triangle_hash": "5be0c2d9a1f3e874",
    "triangle_count": 1000,
    "builds": [
        {
            "backend": "bvh_v2",
            "library": "5a9d759c51d9028130b5f25733e2b4b2dc67718b",
            "quality": "high",
            "node_count": 1999,
            "nodes": 21,
            "primitive_ids": 22
        }
    ]
}
```

- `version`: integer, `1`. A loader rejects any other value.
- `triangle_hash`: the hash `Bvh_geometry::commit()`
  (`src/erhe/raytrace/erhe_raytrace/bvh/bvh_geometry.cpp`) computes, which is
  also the `cache/bvh` key: 64-bit FNV-1a (`erhe::hash::hash()`, offset basis
  `0xcbf29ce484222325`, prime `0x100000001b3`, one byte at a time) over, for
  each triangle `t` in order, the positions of its corners in index order
  (`indices[3t]`, `indices[3t + 1]`, `indices[3t + 2]`), each position three
  float32 values x, y, z, 4 bytes little-endian each, the bits as stored in
  `POSITION`. Index values themselves are not hashed: the BVH depends only
  on the triangle positions in order. Encoded as exactly 16 lowercase
  hexadecimal digits, most significant digit first, zero padded.
- `triangle_count`: number of core triangles, the `indices` count divided by
  3.
- `builds`: one entry per stored BVH. Entries with an unknown `backend` are
  skipped. The loader uses the first entry whose `backend` and `library`
  match the running raytrace backend.
  - `backend`: `bvh_v2` for the madmann91/bvh library (`bvh::v2`).
    `tinybvh` is reserved; its payload is not defined yet.
  - `library`: the version of the pinned dependency the build was made and
    is read with: the `GIT_TAG` of the backend's `CPMAddPackage()` pin in
    the top-level `CMakeLists.txt`, verbatim (for `bvh_v2` a 40-digit
    lowercase commit id). This is a build-time constant of erhe, available
    on every platform, unlike `git rev-parse` of the dependency checkout,
    which reports `unknown` where the checkout has no `.git` (Android).
  - `quality`: `low`, `medium` or `high`, the
    `bvh::v2::DefaultBuilder<Node>::Config::quality` the build used; every
    other builder setting is the library default at `library`. The loader
    uses a stored BVH of any quality.
  - `node_count`: number of nodes, at least 1. Node 0 is the root.
  - `nodes`: bufferView index of the node records (below).
  - `primitive_ids`: bufferView index of the primitive id permutation
    (below).

### `bvh_v2` payload

erhe uses `bvh::v2::Bvh<bvh::v2::Node<float, 3>>`. Its index type is
`bvh::v2::Index<32, 4>` (`Node`'s defaults: `IndexBits = sizeof(float) * 8`,
`PrimCountBits = 4`). The bvh::v2 stream serialization is not used, because
it writes `size_t` primitive ids.

**`nodes`**: a bufferView without `byteStride` and without `target`, whose
`byteLength` is exactly `28 * node_count`. Record `n` is the 28 bytes at
offset `28 * n`, all values little-endian, the same layout as
`bvh::v2::Node<float, 3>` in memory:

| Byte offset | Type | Value |
|---|---|---|
| 0 | float32 | `bounds[0]` = min x |
| 4 | float32 | `bounds[1]` = max x |
| 8 | float32 | `bounds[2]` = min y |
| 12 | float32 | `bounds[3]` = max y |
| 16 | float32 | `bounds[4]` = min z |
| 20 | float32 | `bounds[5]` = max z |
| 24 | uint32 | `index.value` |

`index.value` packs `prim_count` in bits 0-3 and `first_id` in bits 4-31
(`value = (first_id << 4) | prim_count`):

- `prim_count == 0`: inner node; its children are nodes `first_id` and
  `first_id + 1`;
- `prim_count` 1 to 15: leaf; it holds primitive slots `first_id` to
  `first_id + prim_count - 1`, which index `primitive_ids`.

`first_id` has 28 bits, so this payload holds at most 2^28 - 1 nodes and
triangles.

**`primitive_ids`**: a bufferView without `byteStride` and without `target`,
whose `byteLength` is exactly `4 * triangle_count`: uint32 little-endian
values, slot `i` holding the core triangle index `prim_ids[i]`. The values are
a permutation of `0 .. triangle_count - 1`.

Both payloads are raw bufferViews rather than accessors: glTF allows the
UNSIGNED_INT componentType only for accessors referenced by
`mesh.primitive.indices`, and an accessor's element is at most a VEC4 or
MAT4, so no valid accessor describes a node record.

## Load semantics

The loader copies the records into `bvh::v2::Node<float, 3>` and the ids
into `Bvh::prim_ids`, then runs the same precompute as after a build
(`PrecomputedTri` per slot `i` from core triangle `prim_ids[i]`). Rays then
hit the same triangles as with a BVH built at load.

## Validation / fallback

The loader rejects the extension on a primitive when any of these fails:

- `ERHE_mesh_optimized` or `ERHE_geometry` on the same primitive is present
  and accepted;
- `version` is 1;
- `triangle_count` equals the `indices` count divided by 3;
- `triangle_hash` parses as 16 lowercase hex digits and equals the hash the
  loader computes over the core;
- a `builds` entry matches the running backend and `library` tag;
- for that entry: both bufferViews exist, have no `byteStride` and have the
  stated `byteLength`; `node_count` is at least 1;
- every inner node has `first_id + 1 < node_count`, every leaf has
  `first_id + prim_count <= triangle_count`;
- every node other than node 0 is a child of exactly one inner node, and
  node 0 is a child of none (a stored cycle could otherwise reach the
  traversal stack);
- `primitive_ids` is a permutation of `0 .. triangle_count - 1`.

A rejected or unmatched `ERHE_raytrace_bvh` falls back to the path without
it: the raytrace geometry is built later, from `cache/bvh` or by a build.
It does not affect `ERHE_mesh_optimized` or `ERHE_mesh_topology`.

## Schema

[schema/ERHE_raytrace_bvh.schema.json](schema/ERHE_raytrace_bvh.schema.json)
