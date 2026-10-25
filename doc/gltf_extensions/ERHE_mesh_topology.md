# ERHE_mesh_topology

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md).

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). Requires [`ERHE_mesh_optimized`](ERHE_mesh_optimized.md)
on the same primitive; without it, or when it is rejected, this extension is
ignored.

## Scope

Mesh **primitive** extension.

## Overview

Stores the colocation of the core vertices into geometric points (geogram
vertices), so the loader builds the primitive's `erhe::geometry::Geometry`
from the core without `GEO::Geom::colocate()`: no kd-tree, no global geogram
lock, and a deterministic point order. Stock viewers ignore it.

## Payload

`vertex_points` gives, for each core vertex `v` (the elements of the
`POSITION` accessor), the point `vertex_points[v]` it belongs to:

- a **bufferView** (not an accessor: glTF allows the UNSIGNED_INT
  componentType only for accessors referenced by `mesh.primitive.indices`),
  without `byteStride` and without `target`;
- tightly packed little-endian uint32 values, one per core vertex, so the
  bufferView's `byteLength` is exactly 4 times the `POSITION` count;
- every value is below `point_count`;
- **point order**: points are numbered in the order of their first
  reference by ascending vertex index. Scanning `v` from 0 upwards, the first
  vertex of each new point gets the next unused point id, so
  `vertex_points[0] == 0` and every value is at most one more than the
  largest value before it. Every point is referenced, so the largest value
  is `point_count - 1`;
- all vertices of one point have bitwise-identical `POSITION` values
  (`ERHE_mesh_optimized` contract item 4), and that value is the point's
  position.

This order is the order the colocate path produces
(`Mesh_from_triangle_soup::make_vertices()` in
`src/erhe/primitive/erhe_primitive/triangle_soup.cpp`): `colocate()` maps each
used vertex to the smallest vertex index of its cluster, the representatives
are then sorted ascending, and point `i` is the `i`-th representative with the
representative's position. Every core vertex is used
(`ERHE_mesh_optimized` contract item 6), so a used slot is a vertex index,
and ascending smallest-member order is first-reference order. When
`colocate()` over the core partitions the vertices into the same clusters as
`vertex_points`, the `Geometry` built from `vertex_points` is bit-exact with
the colocate path's: points, facets, corner attributes, edges and smooth
normals. The writer derives `vertex_points` from the same `Geometry` whose
point positions it writes into `POSITION`.

## JSON layout

```json
{
    "version": 1,
    "point_count": 1234,
    "vertex_points": 17
}
```

- `version`: integer, `1`. A loader rejects any other value.
- `point_count`: number of points (geogram vertices), at least 1 and at most
  the `POSITION` count.
- `vertex_points`: bufferView index of the payload above.

## Validation / fallback

The loader rejects the extension on a primitive when any of these fails:

- `ERHE_mesh_optimized` on the same primitive is present and accepted;
- `version` is 1;
- the bufferView exists, has no `byteStride`, and its `byteLength` is 4
  times the `POSITION` count;
- the first-reference order holds: scanning `v` upwards, each value is
  either an id seen before or exactly the count of distinct ids seen so far,
  and that count ends equal to `point_count` (this also proves every value is
  below `point_count` and every point is referenced);
- every vertex has the bitwise-identical `POSITION` of its point's first
  vertex (with `ERHE_mesh_optimized` contract item 5 this also means no
  triangle references two vertices of one point).

A rejected `ERHE_mesh_topology` leaves `ERHE_mesh_optimized` and
`ERHE_raytrace_bvh` in use; the `Geometry`, when one is built, comes from the
colocate path.

## Schema

[schema/ERHE_mesh_topology.schema.json](schema/ERHE_mesh_topology.schema.json)
