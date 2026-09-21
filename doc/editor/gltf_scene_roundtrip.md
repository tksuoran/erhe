# glTF scene persistence: design record

Stability: mostly stable

A saved erhe scene is a single `.glb` file that round-trips ALL editor state
through glTF 2.1 plus extensions. This document is the design record behind
that: the decisions the format rests on, and the numbered phases whose labels
source comments cite (`doc/editor/gltf_scene_roundtrip.md phase 3`). The pipeline as
it runs, and the list of what is and is not persisted, is
`doc/editor/scene_serialization.md`; the wire format of each extension is
`doc/gltf_extensions/`.

## Decisions

- **File shape**: a single `.glb`. Prefab instances stay glTF 2.1
  `externalAssets` file references rather than being flattened. A
  user-selectable `.gltf` plus `.bin` text variant waits on the exporter
  writing a buffer URI.
- **Per-scene window layout**: not persisted. Only the global editor layout
  is.
- **Polygon geometry**: the core primitive is TRIANGLES; polygon rings ride
  inside the `ERHE_geometry` extension as a `facet_vertex_counts` /
  `facet_vertex_indices` accessor pair. `EXT_mesh_polygon` and its dependency
  `KHR_mesh_primitive_restart` (KhronosGroup/glTF#2570 and #2569) are drafts
  and are not adopted: their LINE_LOOP-primary encoding would make stock
  viewers render shape-authored meshes as ring outlines, and the Khronos
  validator rejects the restart sentinels until ratification. Adopt
  `EXT_mesh_polygon` when it ratifies; the mapping from the counts / indices
  encoding is mechanical, with the same transition-period reader pattern the
  extras-to-extensions migration used.
- **Geometry fidelity**: a full geogram attribute dump. Every vertex, facet,
  corner and edge attribute is serialized bit-exact, not only the authored
  ones.
- **Extensions, not extras**: erhe-specific data is carried in formal `ERHE_*`
  vendor extensions with documented JSON schemas. Extras remain only for
  truly ad-hoc annotations; the parser still reads the legacy `erhe_flags`
  node extras and material extras fields for the transition period.

## Capability map

| erhe state | glTF mechanism |
|---|---|
| node tree, TRS, names | core |
| node Item flags plus mesh-attachment Item flags | `ERHE_node` extension |
| cameras (interchange approximation) | core cameras, lossy by design |
| camera full projection (all 9 `Projection::Type`s, asymmetric fov / ortho / frustum fields, z_near / z_far), exposure, shadow_range, Item flags | `ERHE_camera` extension; core cameras carry only yfov / aspect plus xmag / ymag and cannot express erhe's `Projection` |
| lights (type, color, intensity, range, spot angles) | KHR_lights_punctual |
| light cast_shadow, Item flags | `ERHE_light` extension; KHR_lights_punctual has no shadow flag |
| materials plus erhe fields | core plus `ERHE_material` extension |
| textures / images / samplers | core |
| animations | core |
| skins | core |
| meshes (triangle soup) | core |
| meshes (geometry-normative, geogram) | core TRIANGLES plus `ERHE_geometry` extension (polygon rings plus attribute dump) |
| physics bodies, colliders, triggers, velocities, COM | KHR_physics_rigid_bodies plus KHR_implicit_shapes |
| physics motion_mode detail (both kinematic modes), per-body friction / restitution, linear / angular damping | `ERHE_physics` node extension; KHR_physics_rigid_bodies has a single isKinematic bool and no per-body friction, restitution or damping |
| physics materials, collision filters, joint settings, including unreferenced library items | the same extension's top-level arrays |
| node joints | KHR_physics_rigid_bodies joints |
| prefab instances | glTF 2.1 externalAssets |
| brushes plus content-library folder tree | `ERHE_brushes` root extension referencing unreferenced meshes |
| graph textures / graph meshes (node-graph JSON) plus bindings | `ERHE_node_graphs` root extension plus node and material entries |
| layout nodes and per-child layout hints | the `Layout.*` values of the nodes, in `ERHE_node` `properties` |
| per-scene settings, ambient light, enable_physics | `ERHE_scene` scene extension |
| item tags | `ERHE_collections` root extension |

Selection, the undo stack and viewport bindings are intentionally not
persisted.

### ERHE extension conventions

- Names use the `ERHE_` vendor prefix.
- Each extension has a JSON schema and a short spec page under
  `doc/gltf_extensions/ERHE_<name>.md`, following the KHR extension template:
  scope, dependencies, JSON layout, example. The spec is the contract that
  makes the files self-describing for other tools, which is the main reason to
  prefer extensions over extras.
- An extension that writes accessors (`ERHE_geometry`) references standard
  bufferViews and accessors, so a generic viewer can at least validate them.
- Every `ERHE_*` extension is optional: a file lists them in `extensionsUsed`
  only, never `extensionsRequired`, so any glTF loader can open a saved scene
  and see the plain render content.
- Item flags serialize as name lists, never as raw bit values: `Item_flags`
  bit positions are not stable across erhe versions. Unknown names are ignored
  on load, so the sets can grow.
- Attachment Item flags ride the extension that describes the attachment:
  `ERHE_camera` and `ERHE_light` each carry an optional `flags` list. Mesh-attachment flags ride `ERHE_node`, because core meshes have no
  erhe payload of their own and the erhe `Mesh` attachment is per-node while
  glTF meshes are shareable.

## Phase 0 - Exporter completeness

`Gltf_exporter` (`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp`) writes every
kind of content a round-trip needs.

1. **Images, textures and samplers.** The original encoded bytes are retained
   at import time on the content-library texture entry (the editor layer, next
   to the `Gltf_source_reference`), not on `erhe::graphics::Texture`, which is
   deliberately GPU-only. They are written back verbatim into the GLB buffer.
   Export falls back to re-reading the bytes from
   `Gltf_source_reference::gltf_path` plus `item_index` when no retained bytes
   exist, and warns loudly when the source file is gone. A GPU-only texture (a
   graph-texture bake) is never exported as an image; it regenerates. Sampler
   state has its own retention, because `erhe::graphics::Sampler` exposes no
   wrap or filter getters: the glTF sampler state is consumed into
   `Sampler_create_info` at import and is otherwise unqueryable.
2. **Animations.** `erhe::scene::Animation` writes as samplers plus channels,
   with node targets by exported node index (STEP, LINEAR, CUBICSPLINE). This
   needs no extra retention: `Animation_sampler` keeps `timestamps` and `data`
   permanently in glTF-native layout (quaternions in glTF [x,y,z,w] order,
   CUBICSPLINE interleaved [in_tangent, value, out_tangent]) and the animation
   editor mutates that same storage, so export is a near-passthrough. The
   `weights` path carries nothing, because erhe has no morph-target support
   end to end.
3. **Skins.** Joints (node indices) plus an inverseBindMatrices accessor, with
   `Node::skinIndex` on skinned mesh nodes (erhe stores the skin on
   `erhe::scene::Mesh::skin`). The optional glTF `skeleton` field is omitted,
   because `Skin_data::skeleton` is never populated by the importer.
4. **Camera correctness.** `Projection::Type::other` and `generic_frustum`
   export as a best-effort core approximation rather than aborting a save, and
   the importer keeps `z_near` on perspective cameras. Under this design the
   core camera is only the interchange approximation - full fidelity is
   `ERHE_camera`, phase 3 - but both paths are correct for foreign files.
5. Unreferenced physics materials, filters and joint settings from the content
   library export into the extension's top-level arrays, so they survive a
   save and load.

## Phase 1 - fastgltf fork: generic extension passthrough

The fork `tksuoran/fastgltf` carries generic vendor-extension JSON
passthrough, which is what lets erhe attach `ERHE_*` extensions without a
fastgltf C++ type per extension: read and write callbacks analogous to the
existing extras callbacks (`Parser::setExtrasParseCallback` /
`Exporter::setExtrasWriteCallback`, which erhe already drives), invoked per
object (asset root, scene, node, camera, material, mesh, mesh primitive) with
the extension name and raw JSON. The editor layer serializes and parses its
`ERHE_*` payloads through these; fastgltf stays schema-agnostic for them.

Facts that shaped the fork:

- Stock fastgltf drops unknown extension keys at each per-object extension
  parse site and preserves no raw JSON, so the read side re-stringifies
  simdjson sub-values.
- There is no generic per-object `"extensions"` emitter: each writer emits
  known extensions from hardcoded code and the block is often conditional, so
  name-keyed injection with correct comma handling is the fiddliest part.
- `Category::Asset` exists in the enum but is wired to nothing (even the
  extras callbacks never fire for the asset root), and mesh primitives have no
  callback category at all, so those two hook points are net-new on both
  sides.
- The read callback fires before the object is appended, so the index it
  reports is the future index; erhe accumulates and replays extras the same
  way (`Parse_extras_context`) and the passthrough inherits that pattern.

`extensionsUsed` bookkeeping stays caller-side: the editor layer adds each
`ERHE_*` name it attaches, as it already does for `KHR_lights_punctual`.
Generic passthrough is worth offering upstream; the root `CMakeLists.txt`
comment records when the fork additions can be dropped.

## Phase 2 - Geometry round-trip: `ERHE_geometry`

An editor-independent, unit-testable conversion between `erhe_gltf` and
`erhe_geometry`. The attribute model uses erhe and geogram's own element
vocabulary rather than USD primvar terms, which would be confusing in a
Khronos context; the USD naming mapping is in `doc/erhe/usd_compatibility.md`.

- **Export**: each geometry-normative primitive writes one glTF mesh
  primitive with a POSITION accessor (geogram vertices, with no welding or
  unwelding, so glTF vertex i equals geogram vertex i) and TRIANGLES indices.
  The indices are generated by fan-triangulating each facet directly over
  geogram vertex ids: the render mesh's index buffer cannot be reused, because
  it indexes the corner-expanded vertex stream `build_buffer_mesh` produces.
  The facet triangulation scheme is the one the primitive builder uses, so the
  render output matches.
- **Polygon rings** live in `ERHE_geometry`: a `facet_vertex_counts` accessor
  (one uint per facet) and a `facet_vertex_indices` accessor (flat
  corner-to-vertex ids in facet-corner order). erhe facets are simple
  polygons, so no hole encoding is needed and there are no restart sentinels,
  which keeps a saved file validator-clean. This is deliberately the same
  shape as `UsdGeomMesh` faceVertexCounts / faceVertexIndices, and maps
  mechanically to `EXT_mesh_polygon` rings when that ratifies.
- **The `ERHE_geometry` primitive extension** carries those rings plus an
  array of attribute records {name, element, type, dimension, accessor}, where
  element is one of `mesh` (whole mesh), `facet` (per polygon), `vertex` (per
  vertex), `corner` (per polygon-corner, in the flat `facet_vertex_indices`
  order) or `edge` (per edge; the edge index list itself is one more accessor
  pair). The dump is full - every geogram attribute is written - so the
  round-trip is bit-exact; the loader applies the dump first and runs
  `process()` only for genuinely missing data.
- **Viewer friendliness comes for free**: a vertex-element attribute that maps
  to a standard glTF semantic (smooth vertex normals to NORMAL, a per-vertex
  UV set to TEXCOORD_0) is ALSO referenced from the core attributes map. The
  same accessor listed twice costs no extra bytes and gives stock viewers
  smooth shading. A corner-element attribute cannot be dual-listed, since it
  is not per-vertex, and a missing NORMAL falls back to the spec-mandated flat
  shading. Dual-listing requires a FULLY PRESENT vertex attribute: a
  dual-listed NORMAL written from an allocated-but-unset geogram attribute is
  all-zero bytes with the `present_*` mask false, which fails the validator's
  `ACCESSOR_VECTOR3_NON_UNIT` on every geometry-normative mesh.
- **Load**: the presence of `ERHE_geometry` on a primitive marks that
  PRIMITIVE geometry-normative, so `erhe::geometry::Geometry` is rebuilt (a
  geogram mesh from the rings and attributes) and then the standard primitive
  build runs. Its absence takes the `Triangle_soup` path. Detection is
  per-primitive, not per-mesh, so a mesh holding both `Geometry` and soup-only
  primitives keeps all of them.
- **When a triangle soup is present it is the primitive's source of truth**
  and exports as-is, with its geometry re-derived on load; `ERHE_geometry`
  attaches only to authored geometry. Exporting the derived, welded geometry
  instead drops `TEXCOORD_0`, `JOINTS_0` and `WEIGHTS_0` from the render
  payload, which fails `MESH_PRIMITIVE_TOO_FEW_TEXCOORDS` for a textured
  import and `NODE_SKIN_WITH_NON_SKINNED_MESH` for a skinned mesh. An exported
  glTF mesh name comes from the erhe mesh name.
- **Brush geometry** uses the same path: brush meshes are glTF meshes that no
  node references.
- The round-trip is pinned by a gtest in `src/erhe/geometry/test/`: Geometry to
  glTF primitive to Geometry, asserting a bit-exact attribute round-trip with
  the hexfloat comparison discipline.

## Phase 3 - `ERHE_*` extensions for editor state

All payloads are built by the editor layer (as `build_gltf_physics_data` is)
and carried through the phase-1 generic extension passthrough, so `erhe::gltf`
stays editor-agnostic. The exporter offers a hook so the editor can attach
extension JSON to an arbitrary exported object and can exclude
editor-controlled attachments.

- **Exclusion hook**: graph-mesh-controlled meshes and their `Node_physics`
  are baked artifacts, rebuilt on load, and are not exported.
- `ERHE_node` (node extension): node Item flags as a name list, plus the
  node's mesh-attachment Item flags.
- `ERHE_camera` (camera extension): the FULL `erhe::scene::Projection`
  (projection_type, all 9 values; fov_x / fov_y / fov_left / right / up / down;
  ortho_left / width / bottom / height; frustum_left / right / bottom / top;
  z_near; z_far), exposure, shadow_range and Item flags. The core camera
  object is only the interchange approximation: without this extension
  asymmetric frusta, XR projections and offset orthos cannot round-trip.
- `ERHE_light` (node extension on the light-carrying node, not inside the
  KHR_lights_punctual light entry, since erhe lights are 1:1 with their node):
  cast_shadow, Item flags, and an explicit `infinite_range` marker that
  resolves the asymmetry between an export that omits range 0 and an import
  that defaults a missing range to 1000.
- `ERHE_physics` (node extension, alongside KHR_physics_rigid_bodies):
  motion_mode (which distinguishes `e_kinematic_non_physical` from
  `e_kinematic_physical`, where KHR has a single isKinematic bool), per-body
  friction and restitution (KHR carries them on physics materials only, while
  erhe rigid bodies carry them with no material assigned), linear_damping and
  angular_damping (which have no KHR carrier at all).
- `ERHE_material` (material extension): roughness_y, bxdf_model,
  blending_mode and the brushed-metal fields.
- `ERHE_scene` (scene extension): per-scene settings, ambient_light and
  enable_physics.
- `ERHE_brushes` (asset-root extension): an array of {name, folder_path, mesh
  index, material index, density, normal_style}. The collision shape is still
  rebuilt at first instantiation (`Brush::late_initialize`).
- `ERHE_node_graphs` (asset-root extension): graph_textures and graph_meshes
  with their node-graph JSON embedded as native JSON, so there is no
  string-in-string escaping; material slot bindings as {material index, slot,
  graph_texture name}; node bindings as {node index, graph_mesh name}. Graphs
  load born-dirty and re-bake.
- `ERHE_collections` (asset-root extension): named node collections, used for
  item tags (the `add_tags` / `remove_tags` state). The initial scope is tags
  on scene nodes; tags on content-library items extend the same mechanism.

Identity: everything binds by glTF index within the same asset (node index,
material index, mesh index), which removes the whole class of parallel-numeric-
id mismatch bugs. Names remain only inside graph JSON references.

### USD-inspired features

The first preference is always an existing RATIFIED glTF extension; only where
none exists does erhe define an `ERHE_*` one. The erhe-to-USD naming and
concept mapping lives in `doc/erhe/usd_compatibility.md`, and the steps toward USD
interchange and composition in `doc/erhe/usd_compatibility_design.md`. erhe
extensions use erhe and glTF-context naming, never USD vocabulary.

- **Collections** (USD: `UsdCollectionAPI`) shape `ERHE_collections`. Named
  sets of node references express item tags today, and selection sets or
  render-layer-like groupings later, without inventing per-node tag strings.

Per-instance prefab overrides (USD: sparse "overs" on a reference) are carried
by the property system's local value layer plus item paths,
`doc/erhe/usd_compatibility_design.md` step X2, rather than by an `ERHE_overrides`
extension; material and node variants are that document's step X4.

## Phase 4 - Save and Open

- **Save**: `save_scene()` is a single `export_gltf()` call (binary, physics
  data, prefab external assets, `ERHE_*` extension payloads, exclusion hook)
  writing one `.glb`. There is no save file dialog: `Operations::save_scene`
  derives `<scene name>.glb` from the scene name with an Overwrite / Cancel
  modal, defaulting to `res/editor/scenes/`. File > Load Scene is a file
  picker filtered to `.glb` and `.gltf`.
- **Open versus import**: a `.glb` or `.gltf` whose asset lists `ERHE_scene`
  in `extensionsUsed` opens as a full `Scene_root` (nodes, cameras, lights,
  meshes, physics import, brushes, graphs, layouts, settings); any other glTF
  takes the import-as-asset flow (default camera and lights, `import_root`
  wrapper, undoable compound operation). `erhe::gltf::scan_gltf` reports
  `extensionsUsed` and the editor wrapper (`editor::scan_gltf` ->
  `Gltf_scan_summary`, `Asset_file_gltf`) carries the structured list, so the
  asset browser branches on `ERHE_scene` rather than on the file extension
  alone.
- **Two open paths exist and mean different things.** The ERHE_scene
  Open-Scene path reuses the import machinery (`parse_gltf` plus
  `finalize_imported_meshes` plus physics import) but constructs the
  `Scene_root` directly, is not undoable, and repurposes an empty viewport.
  `Scene_open_operation` is the undoable Operation that always opens a NEW
  viewport; it is the explicit "open a foreign glTF as a new scene" action,
  used by the asset browser and the MCP `open_scene` tool.
- MCP tools: `save_scene` writes `.glb` (full editor state), `load_scene`
  opens an erhe-authored `.glb`, and `export_gltf` is the plain interchange
  export - the same call minus the editor extensions. Scene save is full
  state; export is interchange.

## Phase 5 - Removal of the legacy bundle

The `.erhescene` directory bundle (scene.json plus data.glb plus `*.geogram`
plus imgui.ini) is gone, and with it:

- `scene_serialization.{hpp,cpp}` in both directions,
- the `scene/definitions/*.py` codegen schemas that existed only for
  scene.json (`scene_file.py`, `node_data.py`, the physics, layout, brush and
  graph serial types) and their generated code; `gltf_source_reference.py`
  stays, because the content library uses it,
- geogram `mesh_save` / `mesh_load` in the save path,
- asset browser `.erhescene` directory handling,
- `scene_imgui_ini_path()` and every per-scene imgui.ini save and restore call
  site.

## Phase 6 - Verification

`scripts/scene_roundtrip_verify.py` is the standing harness; its run book is
`doc/editor/scene_serialization.md` "Verifying round-trips". Its sections are:

- Schema validation: each `ERHE_*` payload validates against its JSON schema
  (the schemas under `doc/gltf_extensions/` double as test fixtures).
- Headless MCP end to end (the `erhe-headless-verify` loop): build a scene
  with shapes (geometry-normative), an imported glTF asset (triangle soup plus
  textures), physics bodies plus a joint, a brush placement, a graph mesh, a
  graph texture bound to a material slot, layouts, tags and an authored
  animation; `save_scene`; `load_scene`; diff the MCP queries (nodes,
  materials, physics items, brushes, animations, tags) and compare
  `capture_screenshot`.
- Prefab scenes: save and load with an external-asset prefab instance present.
- Foreign-tool smoke check: a saved scene passes the Khronos glTF validator
  (ERHE extensions in `extensionsUsed` only) and opens in a stock viewer
  showing the render content. This holds cleanly under the TRIANGLES-primary
  polygon decision: no draft extensions, no restart sentinels.
  Geometry-normative meshes render filled, flat-shaded wherever no per-vertex
  NORMAL could be dual-listed, since corner normals stay
  `ERHE_geometry`-only.

Animation, skin and texture round-trip is asserted at the harness level
(channel-exact animation diff, skin and embedded-image checks against the
saved GLB, the Khronos validator, a Blender import and render) rather than by
an `erhe_gltf` unit test, because `parse_gltf` requires a live
`erhe::graphics::Device` plus executor plus `Image_transfer`, so an import only
runs inside an editor session. `get_scene_nodes` reports `parent_id` and
`import_root` so the harness can apply the exporter's import_root transparency
to a pre-save snapshot.

A settings-less (free six-dof) physics joint exports as a joint description
with no limits or drives (`gltf_physics_export.cpp`); a reload materializes a
settings item for it.

Known non-round-tripping state is listed in `doc/editor/scene_serialization.md`
"What is not persisted".

## Why glTF and not OpenUSD for persistence

OpenUSD is semantically the better fit for almost every mechanism this design
has to define: `UsdGeomMesh` faceVertexCounts / faceVertexIndices carries
n-gons natively; constant / uniform / vertex / faceVarying primvars map 1:1
onto the phase 2 attribute dump (only edge attributes need a custom encoding
either way); `UsdPhysics` is a ratified schema where KHR_physics_rigid_bodies
rides a fastgltf fork; composition arcs are native prefabs, strictly stronger
than the glTF 2.1 externalAssets proposal; `.usda` is diffable text, `.usdc`
binary and `.usdz` a single-file package; and editor state goes into codeless
schemas rather than extras conventions.

It is nonetheless not the persistence format, on operational grounds:

1. **Quest and Android.** OpenUSD supports Linux, macOS, Windows, iOS,
   visionOS and WASM, but not Android, and the erhe editor runs on Quest and
   must load scenes there.
2. **Dependency weight clashes with erhe's CPM model.** OpenUSD is a very
   large source build (TBB required, long configure and build even with
   imaging and Python disabled) fetched from source at configure time like
   every erhe dependency; fastgltf is tiny and already integrated.
3. **It would add a stack rather than replace one.** glTF import and export
   stays regardless, for interchange and because prefab sources are glTF. USD
   for persistence means maintaining two serialization stacks, where this
   design collapses to one and reuses the existing fork, extension plumbing
   and import machinery.
4. **Animation model mismatch.** The animation editor authors glTF-style
   samplers with cubic tangents; USD is time-sample-first, so a round-trip
   would need re-encoding.

USD's transferable ideas are harvested into glTF extensions instead (see
"USD-inspired features" and `doc/erhe/usd_compatibility.md`): collections shape
`ERHE_collections`, and overs, variants and animate-anything are steps of
`doc/erhe/usd_compatibility_design.md`. `ERHE_geometry` keeps erhe and geogram
element naming; the primvar mapping is documented, not adopted.

USD is nonetheless a second, independent scene format, through LightUSD, which
does build for Android: `doc/erhe/usd_compatibility_design.md` describes it. A USD
scene is loaded, edited and saved on its own, glTF scenes are untouched, and
nothing is converted between the two.

## Future work

- [plans/gltf.md](../plans/gltf.md) - the open items of this design, including
  the `EXT_mesh_polygon` ratification watch and the `ERHE_` prefix
  registration.
