# glTF: outstanding work

Status: in progress

This plan extends `doc/gltf_scene_roundtrip.md` (the scene persistence design
record), `doc/scene_serialization.md` (the pipeline) and `doc/erhe_gltf.md`
(the library) with the items they do not yet describe as shipping behavior.

## EXT_mesh_polygon ratification watch

Polygon rings live in `ERHE_geometry` until `EXT_mesh_polygon` and its
dependency `KHR_mesh_primitive_restart` (KhronosGroup/glTF#2570 and #2569)
ratify; the draft has already flipped its encoding once. Migration when it
lands is mechanical (counts and indices to rings), using the same
transition-period reader pattern the extras-to-extensions migration used.

## Register the ERHE_ vendor prefix

Register the `ERHE_` prefix in the Khronos glTF registry
(`extensions/Prefixes.md`, a one-line PR) before saved files travel outside
this repository. Until it is registered the names are squattable.

## Offer the generic extension passthrough upstream

The generic vendor-extension read and write callbacks are new fastgltf surface
carried by the `tksuoran/fastgltf` fork. Propose them upstream to shorten the
fork's life: the scope to offer is the object types erhe needs (asset, scene,
node, camera, material, mesh primitive). Two hook points are net-new even
relative to the extras callbacks (the asset root is wired to nothing, and mesh
primitives have no callback category), and the read side re-stringifies
simdjson sub-values, which fastgltf does not otherwise do.

## Text .gltf plus .bin variant

The text export variant writes no buffer URI and therefore cannot be
re-imported. Fixing that is the prerequisite for offering a user-selectable
`.gltf` plus `.bin` save variant beside the single `.glb`.

## Source image byte retention

Retaining the encoded source bytes of every imported textured asset raises
memory per asset. The bytes are kept compressed, so the cost is typically
small relative to the GPU copies, and the re-read-from-source fallback covers
assets imported before the retention existed. Measure the cost on a large
scene, and decide whether the retention should be bounded.

## Persist what a save currently drops

`doc/scene_serialization.md` "What is not persisted" lists the state a save
drops. Three items there are worth closing rather than accepting:

- A content-library material that no mesh references is not exported, because
  glTF materials exist only where meshes reference them, so a graph-texture
  binding on an unused material is dropped at save with a warning.
- A `Brush_placement` attachment is not persisted, so a placed-brush node
  reloads as a plain mesh node with no link back to its source brush. The
  brush library itself round-trips through `ERHE_brushes`.
- A prefab instance parses only the render and physics content of its source,
  so the source's `ERHE_*` payloads (layouts, tags, brushes, node graphs) do
  not transfer into instances. In particular a mesh controlled by a
  `Geometry_graph_mesh` attachment is excluded from the save and is not
  rebuilt in an instance, so graph-baked products are missing from instances
  of such a prefab.
