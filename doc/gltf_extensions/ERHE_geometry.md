# ERHE_geometry

## Contributors / status

erhe project. Draft vendor extension.

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only).

## Scope

Mesh **primitive** extension.

## Overview

Marks a primitive as *geometry-normative*: the authoring-level polygon mesh
(a geogram mesh: vertices, polygonal facets, corners, edges, and every
attribute on them) is carried alongside the core TRIANGLES payload, so the
editor can rebuild its `erhe::geometry::Geometry` bit-exact on load. Stock
viewers render the core primitive (POSITION + fan-triangulated indices) and
never need this extension.

Core-payload contract when this extension is present:

- glTF vertex `i` == geogram vertex `i` (no welding / corner expansion).
- The core indices fan-triangulate each facet in facet order.
- Vertex-element attributes that map to standard glTF semantics may be
  dual-listed as core attributes over the same bytes (e.g. smooth vertex
  normals as `NORMAL`); corner-element attributes cannot be (they are not
  per-vertex), so shading falls back to spec-mandated flat shading in
  viewers that only read core data.

## JSON layout

```json
{
    "vertex_count": 8,
    "facet_count": 6,
    "corner_count": 24,
    "edge_count": 12,
    "facet_vertex_counts": 10,
    "facet_vertex_indices": 11,
    "edge_vertices": 12,
    "attributes": [
        {
            "name": "normal_smooth",
            "element": "vertex",
            "element_type": "vec3f",
            "element_size": 4,
            "dimension": 3,
            "item_count": 8,
            "buffer_view": 7
        }
    ]
}
```

- `vertex_count` / `facet_count` / `corner_count` / `edge_count`:
  geogram element counts.
- `facet_vertex_counts`: accessor index (unsigned int scalar, one per
  facet) - vertices per polygon.
- `facet_vertex_indices`: accessor index (unsigned int scalar) - flat
  corner-to-vertex ids in facet-corner order (same shape as USD
  faceVertexCounts / faceVertexIndices; facets are simple polygons, no
  hole encoding, no restart sentinels).
- `edge_vertices` (optional): accessor index (unsigned int scalar,
  2 * edge_count) - vertex id pairs per edge.
- `attributes`: full geogram attribute-store dump. Each record references
  a raw **bufferView** (not an accessor - the payload is an opaque,
  bit-exact byte image of the store):
  - `name`: geogram attribute name (e.g. `normal`, `normal_smooth`,
    `corner_texcoord_0`).
  - `element`: `mesh` | `facet` | `vertex` | `corner` | `edge`. Corner
    records follow the flat `facet_vertex_indices` order.
  - `element_type`: store element type name (e.g. `float`, `vec3f`,
    `uint32`).
  - `element_size`: bytes per scalar element.
  - `dimension`: scalars per item.
  - `item_count`: number of items (must match the element's count).
  - `buffer_view`: bufferView index holding the raw bytes.

## Load semantics

Presence on a primitive rebuilds `erhe::geometry::Geometry` from the rings
and attribute dump; the dump is applied verbatim (bit-exact) and
`Geometry::process()` runs only for genuinely missing derived data.
Absence keeps the plain triangle-soup import path. Detection is
per-primitive: one glTF mesh may mix geometry-normative and soup-only
primitives.

Brush geometry (`ERHE_brushes`) uses this same extension on meshes that no
node references.

## Schema

[schema/ERHE_geometry.schema.json](schema/ERHE_geometry.schema.json)
