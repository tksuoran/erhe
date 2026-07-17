# erhe glTF vendor extensions (`ERHE_*`)

erhe persists full editor scenes as single glTF files (process and parts:
[`../scene_serialization.md`](../scene_serialization.md); design history:
[`../gltf-scene-roundtrip-plan.md`](../gltf-scene-roundtrip-plan.md)).
Editor state that core glTF 2.x cannot express is carried in the vendor
extensions specified here. Conventions shared by all of them:

- Every `ERHE_*` extension is **optional**: files list them in
  `extensionsUsed` only, never `extensionsRequired`, so any glTF loader can
  open a saved scene and see the plain render content.
- Item flags serialize as **name lists** (never raw bit values - erhe flag
  bit positions are not stable across versions). Unknown names are ignored
  on load. The persistent flag name set is defined in
  [`flags.md`](flags.md).
- Floats are written in shortest-round-trip decimal form.
- Cross-references use **glTF indices within the same asset** (node index,
  material index, mesh index); names appear only inside node-graph JSON.
- The `ERHE_` prefix is registered (to be registered) in the Khronos glTF
  registry `extensions/Prefixes.md`.

Presence of `ERHE_scene` in `extensionsUsed` marks a file as an
erhe-authored scene (the editor opens it as a full scene instead of
importing it as an asset).

| Extension | Attaches to | Carries |
|---|---|---|
| [`ERHE_geometry`](ERHE_geometry.md) | mesh primitive | polygon rings + full geogram attribute dump (bit-exact geometry) |
| [`ERHE_node`](ERHE_node.md) | node | node Item flags, mesh-attachment Item flags |
| [`ERHE_camera`](ERHE_camera.md) | camera | full erhe projection, exposure, shadow range, Item flags |
| [`ERHE_light`](ERHE_light.md) | node (light-carrying) | cast_shadow, infinite_range, Item flags |
| [`ERHE_material`](ERHE_material.md) | material | roughness_y, bxdf_model, blending_mode, brushed-metal fields |
| [`ERHE_physics`](ERHE_physics.md) | node (rigid-body-carrying) | motion_mode, per-body friction / restitution, damping |
| [`ERHE_scene`](ERHE_scene.md) | scene | per-scene settings, ambient light, enable_physics |
| [`ERHE_layout`](ERHE_layout.md) | node | Layout / Layout_item attachment fields |
| [`ERHE_brushes`](ERHE_brushes.md) | asset root | brush library (geometry via unreferenced meshes) |
| [`ERHE_node_graphs`](ERHE_node_graphs.md) | asset root | procedural texture / mesh node graphs + bindings |
| [`ERHE_collections`](ERHE_collections.md) | asset root | named node collections (item tags) |
| [`ERHE_asset_reference`](ERHE_asset_reference.md) | top-level object (materials first) | DRAFT: proxy for a definition in another glTF file ({file, uid}) |

JSON schemas live in [`schema/`](schema/); each `*.schema.json` validates
the extension's JSON value (the object stored under the extension name in
`"extensions"`).

Writer / reader: `src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` (library-domain
extensions: geometry, node, camera, light, material) and
`src/editor/parsers/gltf_extensions_export.cpp` /
`gltf_extensions_import.cpp` (editor-domain extensions: physics, scene,
layout, brushes, node_graphs, collections). Legacy carriers (`erhe_flags`
node extras, material extras) remain parsed for one transition period but
are no longer written.
