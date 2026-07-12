# ERHE_node_graphs

## Scope

**Asset-root** extension (the top-level `extensions` object). Optional
(`extensionsUsed` only).

## Overview

Persists the editor's procedural node-graph assets and their scene
bindings:

- `graph_textures`: Graph Texture assets (procedural textures) with their
  node graph embedded as native JSON (nodes with factory type +
  parameters, links by node index + pin slot - the geometry/texture-graph
  v1 format; no string-in-string escaping).
- `graph_meshes`: Graph Mesh assets (procedural geometry), same shape.
- `material_bindings`: material texture slots sourced from a Graph
  Texture: `{material (glTF material index), slot, graph_texture (name)}`.
  Slot is one of `base_color`, `metallic_roughness`, `normal`,
  `occlusion`, `emissive`.
- `node_bindings`: scene nodes whose mesh is controlled by a Graph Mesh:
  `{node (glTF node index), graph_mesh (name)}`.

Baked products are NOT persisted: a bound node's controlled mesh and
rigid body are excluded from the export (they would duplicate on every
save/load cycle); graphs load born-dirty and the first evaluation re-bakes
and pushes the products to the re-attached bindings.

## JSON layout

```json
{
    "graph_textures": [
        {"name": "Noise", "graph": {"format": "erhe_texture_graph", "nodes": ["..."]}}
    ],
    "graph_meshes": [
        {"name": "Terrain", "graph": {"format": "erhe_geometry_graph", "nodes": ["..."]}}
    ],
    "material_bindings": [
        {"material": 2, "slot": "base_color", "graph_texture": "Noise"}
    ],
    "node_bindings": [
        {"node": 5, "graph_mesh": "Terrain"}
    ]
}
```

(The `graph` object's exact shape is owned by the editor's graph
serialization - `src/editor/geometry_graph/graph_mesh_serialization.cpp`
and `src/editor/texture_graph/graph_texture_serialization.cpp`.)

## Schema

[schema/ERHE_node_graphs.schema.json](schema/ERHE_node_graphs.schema.json)
