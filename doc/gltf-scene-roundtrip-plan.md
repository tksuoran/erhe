# glTF-everything scene persistence plan (drop .erhescene)

Goal: a saved erhe scene is a single `.glb` file that round-trips ALL editor
state through glTF 2.1 + extensions. The `.erhescene` directory bundle
(scene.json + data.glb + *.geogram + imgui.ini) is removed.

Decisions taken (user-confirmed 2026-07-12):

- **File shape**: single `.glb`. Prefab instances stay glTF 2.1
  `externalAssets` file references (not flattened). A user-selectable
  `.gltf` + `.bin` text variant is DEFERRED (requires fixing the exporter's
  missing buffer URI first).
- **Per-scene window layout**: dropped entirely. No imgui.ini persistence per
  scene; only the global editor layout remains.
- **Polygon geometry** (revised 2026-07-12 design review, user-confirmed):
  the core primitive stays TRIANGLES; polygon rings are carried inside the
  `ERHE_geometry` extension as a facet_vertex_counts / facet_vertex_indices
  accessor pair. `EXT_mesh_polygon` + its dependency
  `KHR_mesh_primitive_restart` (KhronosGroup/glTF#2570 / #2569, open PRs)
  are NOT adopted while they are drafts: the 2026-05-20 draft revision
  flipped the encoding to LINE_LOOP-primary (TRIANGLES render indices moved
  into the extension as `triangleIndices`, optionality still under
  discussion), which would make stock viewers render shape-authored meshes
  as ring outlines and make the Khronos validator reject the restart
  sentinels until ratification. Adopt EXT_mesh_polygon when it ratifies
  (mechanical mapping from the counts/indices encoding; same
  transition-period reader pattern as extras -> extensions).
- **Geometry fidelity**: full geogram attribute dump - every vertex / facet /
  corner / edge attribute is serialized (bit-exact round-trip), not just
  authored attributes.
- **Extensions, not extras** (2026-07-12 revision): erhe-specific data is
  carried in formal `ERHE_*` vendor extensions with documented JSON schemas,
  not in extras blobs. Existing erhe extras carriers (`erhe_flags` node
  extras, material extras fields) migrate into extensions too; the parser
  keeps reading the legacy extras for one transition period. Extras remain
  only for truly ad-hoc annotations.

## Capability map

| erhe state | glTF mechanism | status |
|---|---|---|
| node tree, TRS, names | core | exists |
| node Item flags + mesh-attachment Item flags | `ERHE_node` extension (migrates `erhe_flags` extras) | NEW |
| cameras (interchange approximation) | core cameras | exists (lossy by design; exporter currently ERHE_FATALs on `other` / `generic_frustum` and importer drops z_near - phase 0 fixes) |
| camera full projection (all 9 `Projection::Type`s, asymmetric fov/ortho/frustum fields, z_near/z_far), exposure, shadow_range, Item flags | `ERHE_camera` extension | NEW (core cameras carry only yfov/aspect + xmag/ymag - they CANNOT express erhe's Projection) |
| lights (type, color, intensity, range, spot angles) | KHR_lights_punctual | exists (fix range round-trip asymmetry: export omits range 0 = infinite, import defaults missing range to 1000) |
| light cast_shadow, Item flags | `ERHE_light` extension | NEW (KHR_lights_punctual has no shadow flag; cast_shadow is silently lost today) |
| materials + erhe fields | core + `ERHE_material` extension (migrates material extras) | NEW (extras exist) |
| textures / images / samplers | core | NEW (exporter currently drops them; sampler wrap/filter state is not even queryable - see phase 0) |
| animations | core | NEW (exporter currently drops them) |
| skins | core | NEW (exporter currently drops them) |
| meshes (triangle soup) | core | exists |
| meshes (geometry-normative, geogram) | core TRIANGLES + `ERHE_geometry` extension (polygon rings + attribute dump; see decision above) | NEW |
| physics bodies, colliders, triggers, velocities, COM | KHR_physics_rigid_bodies + KHR_implicit_shapes | exists |
| physics motion_mode detail (both kinematic modes), per-body friction / restitution, linear / angular damping | `ERHE_physics` node extension | NEW (KHR_physics_rigid_bodies has a single isKinematic bool and no per-body friction / restitution / damping - scene.json carries all of these today) |
| physics materials, collision filters, joint settings (incl. unreferenced library items) | same extension top-level arrays | exists / verify unreferenced |
| node joints | KHR_physics_rigid_bodies joints | exists |
| prefab instances | glTF 2.1 externalAssets | exists |
| brushes + content-library folder tree | `ERHE_brushes` root extension referencing unreferenced meshes | NEW |
| graph textures / graph meshes (node-graph JSON) + bindings | `ERHE_node_graphs` root extension + node/material entries | NEW |
| layouts / layout items | `ERHE_layout` node extension | NEW |
| per-scene settings (#239), ambient light (#237), enable_physics | `ERHE_scene` scene extension | NEW |
| item tags | `ERHE_collections` root extension (USD-inspired, see below) | NEW (net-new persistence: tags live only in `Item_base::m_tags` at runtime and are never saved in .erhescene - there is nothing to migrate) |
| per-scene imgui layout | DROPPED (decision above) | - |

Editor state intentionally not persisted (unchanged from today): selection,
undo stack, viewport bindings.

### ERHE extension conventions

- Names use the `ERHE_` vendor prefix; register the prefix in the Khronos
  glTF registry (`extensions/Prefixes.md`, a one-line PR) before shipping.
- Each extension gets a JSON schema + a short spec page under
  `doc/gltf_extensions/ERHE_<name>.md` (following the KHR extension template:
  scope, dependencies, JSON layout, example). The spec is the contract that
  makes the files self-describing for other tools - the main reason to
  prefer extensions over extras.
- Extensions writing accessors (`ERHE_geometry`) reference standard
  bufferViews/accessors so generic viewers can at least validate them.
- Every `ERHE_*` extension is optional: files list them in `extensionsUsed`
  only, never `extensionsRequired`, so any glTF loader can open a saved
  scene and see the plain render content.
- Item flags serialize as name lists (like today's `erhe_flags` extras,
  which carry only `exclude_from_prefab`), never as raw bit values -
  `Item_flags` bit positions are not stable across erhe versions. Unknown
  names are ignored on load so the sets can grow. This corrects
  scene.json's fragile raw `flag_bits` uint64.
- Attachment Item flags (defect: today load_scene assigns fixed flag sets
  to cameras / lights / meshes / layouts) ride the extension that describes
  the attachment: `ERHE_camera`, `ERHE_light`, `ERHE_layout` each carry an
  optional `flags` list. Mesh-attachment flags ride `ERHE_node` (core
  meshes have no erhe payload of their own, and the erhe Mesh attachment
  is per-node while glTF meshes are shareable).

## Phase 0 - Exporter completeness (prerequisite; valuable standalone)

`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` `Gltf_exporter`:

1. **Images / textures / samplers export.** Round-trip needs the original
   encoded bytes: retain the compressed source image bytes at import time
   and write them back verbatim into the GLB buffer. Retention home
   (design reviewed 2026-07-12): the **content-library texture entry**
   (editor layer, next to the `Gltf_source_reference` that
   `Content_library_node` already carries) - NOT `erhe::graphics::Texture`,
   which is deliberately GPU-only. Export falls back to re-reading the
   bytes from `Gltf_source_reference::gltf_path` + `item_index` when no
   retained bytes exist (assets imported before this lands), with a loud
   warning when the source file is gone. GPU-only textures (graph-texture
   bakes) are never exported as images - they regenerate. Wire material
   texture references (`baseColorTexture` etc., currently written empty).
   **Samplers need their own retention**: `erhe::graphics::Sampler` exposes
   no wrap / filter getters - the glTF sampler state is consumed into
   `Sampler_create_info` at import and becomes unqueryable. Retain the
   create-info (or add accessors) so samplers can be written back.
2. **Animation export**: `erhe::scene::Animation` -> samplers + channels
   (node targets by exported node index; STEP / LINEAR / CUBICSPLINE).
   Verified feasible with no new retention: `Animation_sampler` stores
   `timestamps` + `data` permanently in glTF-native layout (quaternions in
   glTF [x,y,z,w] order, CUBICSPLINE interleaved [in_tangent, value,
   out_tangent]), and the #243 animation editor mutates that same storage
   directly - export is a near-passthrough. The `weights` path is
   DEFERRED: erhe has no morph-target support end-to-end
   (`erhe::scene::Mesh` has no weights, `Animation_sampler::evaluate` has
   no WEIGHTS case, the importer drops scalar sampler outputs), so there
   is no data to export. This closes the #243 animation-editor data-loss
   gap for node TRS animation.
3. **Skin export**: joints (node indices), inverseBindMatrices accessor;
   `Node::skinIndex` on skinned mesh nodes (erhe stores the skin on
   `erhe::scene::Mesh::skin`). `Skin_data::skeleton` is never populated by
   the importer, so the optional glTF `skeleton` field is omitted.
4. **Camera correctness**: the exporter `ERHE_FATAL`s on
   `Projection::Type::other` / `generic_frustum` - replace with a
   best-effort core approximation (never abort a save); the importer drops
   `z_near` on perspective cameras - fix. Under this plan the core camera
   is only the interchange approximation (full fidelity is `ERHE_camera`,
   phase 3), but both paths must be correct for foreign files.
5. Verify unreferenced physics materials / filters / joint settings from the
   content library export into the extension's top-level arrays (they must
   survive save/load like scene.json v3+ does today).

Import side already parses animations / skins / images; add the source-byte
and sampler-state retention.

## Phase 1 - fastgltf fork: generic extension passthrough

Branch on `tksuoran/fastgltf` (precedent: `khr_physics_rigid_bodies`).
Polygon extensions dropped from this phase - see the polygon-geometry
decision above (rings live in `ERHE_geometry`, no draft-PR tracking).

- **Generic vendor-extension JSON passthrough** (the enabler for `ERHE_*`
  extensions without per-extension fastgltf C++ types): read + write
  callbacks analogous to the existing extras callbacks
  (`Parser::setExtrasParseCallback` / `Exporter::setExtrasWriteCallback`,
  which erhe already drives for `erhe_flags` and material extras), invoked
  per object (asset root, scene, node, camera, material, mesh, mesh
  primitive) with the extension name and raw JSON. erhe's editor layer
  serializes/parses its `ERHE_*` payloads through these; fastgltf stays
  schema-agnostic for them. This also lets the existing extras carriers
  (`erhe_flags`, material extras) migrate to `ERHE_node` / `ERHE_material`
  without new fork work per extension.
  Scope facts (verified against the fork source, fastgltf 0.9.0 base):
  - Today unknown extension keys are silently dropped at each per-object
    extension parse site; nothing preserves raw JSON. The read side must
    re-stringify simdjson sub-values - new surface for fastgltf.
  - The write side has no generic per-object `"extensions"` emitter; each
    writer emits known extensions from hardcoded code and the block is
    often conditional - name-keyed injection with correct comma handling
    is the fiddliest part.
  - `Category::Asset` exists in the enum but is wired to nothing (even the
    extras callbacks never fire for the asset root), and mesh primitives
    have no callback category at all - these two hook points are net-new
    on both the read and write sides.
  - The read callback fires before the object is appended (index =
    future index); erhe already accumulates+replays extras this way
    (`Parse_extras_context`) and the passthrough inherits the same
    pattern.
- extensionsUsed bookkeeping stays caller-side (erhe already pushes
  `KHR_lights_punctual` etc. manually); the editor layer adds each
  `ERHE_*` name it attaches.
- Record in CMake comments when the fork additions can be dropped
  (generic passthrough is worth offering upstream).

## Phase 2 - Geometry (geogram) round-trip: `ERHE_geometry`

New `erhe_gltf` <-> `erhe_geometry` conversion (editor-independent,
unit-testable). The attribute model uses erhe/geogram's own element
vocabulary - USD primvar terms would be confusing in a Khronos/glTF
context; the USD naming mapping is documented in `doc/usd_compatibility.md`:

- **Export**: for each geometry-normative primitive, write one glTF mesh
  primitive with: POSITION accessor (geogram vertices, no welding/unwelding -
  glTF vertex i == geogram vertex i) and TRIANGLES indices. The indices are
  generated by fan-triangulating each facet directly over geogram vertex
  ids - the existing render mesh's index buffer CANNOT be reused (it
  indexes the corner-expanded vertex stream `build_buffer_mesh` produces,
  not geogram vertices). Use the same facet triangulation scheme the
  Primitive builder uses so render output matches.
- **Polygon rings** (in `ERHE_geometry`, per the decision above): a
  `facet_vertex_counts` accessor (one uint per facet) + a
  `facet_vertex_indices` accessor (flat corner->vertex ids in facet-corner
  order). erhe facets are simple polygons, so no hole encoding is needed;
  no restart sentinels, validator-clean. (Deliberately the same shape as
  UsdGeomMesh faceVertexCounts/faceVertexIndices and mechanically mappable
  to EXT_mesh_polygon rings when that ratifies.)
- **`ERHE_geometry` primitive extension**: the rings above plus an array of
  attribute records {name, element, type, dimension, accessor}, where
  element is one of `mesh` (whole mesh), `facet` (per polygon), `vertex`
  (per vertex), `corner` (per polygon-corner, in the flat
  facet_vertex_indices order), `edge` (per edge; the edge index list itself
  is one more accessor pair). Full dump: every geogram attribute is
  written, so the round-trip is bit-exact; loader applies the dump first
  and runs process() only for genuinely missing data (new-file case).
- **Viewer friendliness for free**: vertex-element attributes that map to
  standard glTF semantics (e.g. smooth vertex normals -> NORMAL, a
  per-vertex UV set -> TEXCOORD_0) are ALSO referenced from the core
  attributes map - the same accessor listed twice costs no extra bytes and
  gives stock viewers smooth shading. Corner-element attributes cannot be
  (they are not per-vertex); missing NORMAL falls back to spec-mandated
  flat shading, which is fine for polygonal shapes.
- **Load**: presence of `ERHE_geometry` on a primitive marks that
  PRIMITIVE geometry-normative -> rebuild `erhe::geometry::Geometry`
  (geogram mesh from rings + attributes), then the standard Primitive
  build. Absence -> Triangle_soup path exactly as today. Detection is
  per-primitive, not per-mesh - this also fixes today's mixed-mesh loss
  (`is_geometry_normative` is per-mesh, so a mesh with both Geometry and
  soup-only primitives silently drops the soup primitives on load).
- **Brush geometry** uses the same path (brush meshes are glTF meshes that no
  node references).
- gtest in `src/erhe/geometry/test/` (suite exists): Geometry -> glTF
  primitive -> Geometry, assert bit-exact attribute round-trip (hexfloat
  comparison discipline applies).

## Phase 3 - `ERHE_*` extensions for editor state

All payloads are built by the editor layer (like `build_gltf_physics_data`)
and carried through the phase-1 generic extension passthrough; `erhe::gltf`
stays editor-agnostic. The exporter gains a small hook so the editor can
(a) attach extension JSON to arbitrary exported objects and (b) exclude
editor-controlled attachments:

- **Exclusion hook**: graph-mesh-controlled meshes and their Node_physics are
  baked artifacts (rebuilt on load) and must NOT be exported - today's
  save_scene skips them in scene.json but they leak into data.glb (known
  index-shift bug); the hook fixes this class of problem for good.
- `ERHE_node` (node extension): node Item flags as a name list (migrates
  `erhe_flags` extras, which today carry only `exclude_from_prefab`;
  scene.json carries the full raw flag_bits - the extension serializes the
  full serializable set by name; legacy extras still parsed during
  transition), plus the node's mesh-attachment Item flags (see extension
  conventions).
- `ERHE_camera` (camera extension): the FULL `erhe::scene::Projection`
  (projection_type - all 9 values - fov_x/fov_y/fov_left/right/up/down,
  ortho_left/width/bottom/height, frustum_left/right/bottom/top, z_near,
  z_far), exposure, shadow_range, Item flags. The core camera object is
  only the interchange approximation; without this extension asymmetric
  frusta, XR projections and offset orthos cannot round-trip (scene.json
  serializes all 17 projection fields today).
- `ERHE_light` (node extension on the light-carrying node - NOT inside the
  KHR_lights_punctual light entry, which would need an extra passthrough
  hook point in the fork; erhe lights are 1:1 with their node anyway):
  cast_shadow (silently lost today), Item flags, and an explicit
  `infinite_range` marker resolving the export-omits-0 / import-defaults-
  1000 asymmetry.
- `ERHE_physics` (node extension, alongside KHR_physics_rigid_bodies):
  motion_mode (distinguishes e_kinematic_non_physical from
  e_kinematic_physical - KHR has a single isKinematic bool), per-body
  friction and restitution (KHR only has them on physics materials; erhe
  rigid bodies carry them even with no material assigned), linear_damping,
  angular_damping (no KHR carrier at all). All of these round-trip through
  scene.json today and would silently regress without this extension.
- `ERHE_material` (material extension): roughness_y, bxdf_model,
  blending_mode, brushed-metal fields (migrates material extras).
- `ERHE_scene` (scene extension): per-scene settings (#239), ambient_light
  (#237), enable_physics (replace the hard-coded TODO with real tracking).
- `ERHE_layout` (node extension): Layout and Layout_item fields (two
  optional sub-objects on one extension), each with its Item flags.
  Include the prefab-instance skip that today's layout pass misses
  (confirmed: scene_serialization.cpp's layout pass is the only save pass
  without the `is_inside_prefab_instance` guard, producing dangling
  node_id references).
- `ERHE_brushes` (asset-root extension): array of {name, folder_path, mesh
  index, material index, density, normal_style}. Collision shape still
  rebuilt at first instantiation (Brush::late_initialize), as today.
- `ERHE_node_graphs` (asset-root extension): graph_textures + graph_meshes
  with their node-graph JSON embedded (native JSON; no string-in-string
  escaping, unlike scene.json today); material slot bindings as
  {material index, slot, graph_texture name}; node bindings as
  {node index, graph_mesh name}. Graphs load born-dirty and re-bake, as
  today.
- `ERHE_collections` (asset-root extension): named node collections -
  initially used for item tags (the add_tags/remove_tags MCP state, which
  is runtime-only today - `.erhescene` never persisted tags, so this is
  net-new persistence, not a migration); see the USD-inspired section.
  Initial scope: tags on scene nodes; tags on content-library items are a
  later extension of the same mechanism.

Identity: everything binds by glTF index within the same asset (node index,
material index, mesh index) instead of scene.json's parallel numeric id maps -
the whole class of scene.json <-> data.glb index-mismatch bugs disappears.
Names remain only inside graph JSON references (same as today).

## USD-inspired features (review, 2026-07-12)

USD was reviewed (see Alternative-considered section) for concepts worth
carrying into glTF. First preference is always an existing **ratified**
glTF extension; only where none exists do we define `ERHE_*`. Full detail -
the erhe <-> USD naming/concept mapping and the specifications of the
planned future features - lives in **`doc/usd_compatibility.md`**; erhe
extensions use erhe/glTF-context naming, never USD vocabulary.

In scope for this plan:

- **Collections** (USD: UsdCollectionAPI) -> `ERHE_collections` in phase 3.
  Named sets of node references express item tags today and selection sets /
  render-layer-like groupings later, without inventing per-node tag strings.

Documented in `doc/usd_compatibility.md`, implemented after the switchover:

- **`ERHE_overrides`** (USD: sparse "overs" on a reference) - persist edits
  made inside prefab instances, which are silently lost on save today.
  Highest-value USD idea; off the critical path because its intra-asset
  addressing scheme needs careful design.
- **KHR_materials_variants** and **KHR_animation_pointer** adoption (both
  ratified) - USD's variants and animate-anything, respectively.
- Deferred: `ERHE_variants` (node-level variant sets),
  EXT_mesh_gpu_instancing, payload-style load policies. Non-goal:
  sublayering.

## Phase 4 - Save / Open switchover

- **Save**: `save_scene()` becomes a single `export_gltf()` call (binary,
  physics data, prefab external assets, `ERHE_*` extension payloads,
  exclusion hook) -> write one `.glb`. There is NO save file dialog today:
  `Operations::save_scene` derives `<scene name>.erhescene` from the scene
  name with an Overwrite/Cancel modal - keep that scheme, the derived name
  becomes `<scene name>.glb`; default location stays `res/editor/scenes/`.
  File > Load Scene currently uses a FOLDER dialog
  (`SDL_ShowOpenFolderDialog`, because the bundle is a directory) - it
  becomes a file picker filtered to `.glb`/`.gltf`.
- **Open vs Import**: a `.glb`/`.gltf` whose asset lists `ERHE_scene` in
  extensionsUsed opens as a full `Scene_root` (new Open-Scene path: nodes,
  cameras, lights, meshes, physics import, brushes, graphs, layouts,
  settings); any other glTF keeps the existing import-as-asset flow (default
  camera/lights, import_root wrapper, undoable compound operation).
  The library-layer `erhe::gltf::scan_gltf` already reports extensionsUsed,
  but the editor wrapper (`editor::scan_gltf` -> `Gltf_scan_summary`)
  flattens it into tooltip text and discards the structured list - extend
  `Gltf_scan_summary` / `Asset_file_gltf` to carry `extensions_used` so the
  asset browser (which classifies purely by file extension in `make_node`
  today) can branch on `ERHE_scene` (`Asset_file_scene` becomes
  "erhe-authored .glb" instead of ".erhescene directory").
- **Two open paths exist today and must be reconciled**: the `.erhescene`
  `load_scene` path (not undoable, repurposes an empty viewport window,
  #265) and `Scene_open_operation` (undoable Operation, always opens a NEW
  viewport, used by the asset browser's manual "open as scene" and MCP
  `open_scene`). The ERHE_scene Open-Scene path follows the `load_scene`
  shape: reuse the import machinery (parse_gltf + finalize_imported_meshes
  + physics import) but construct the Scene_root directly (not undoable)
  and repurpose an empty viewport. `Scene_open_operation` remains as the
  explicit "open a foreign glTF as a new scene" action.
- MCP `save_scene` / `load_scene` / `open_scene` / `export_gltf` /
  `import_gltf` tools: `save_scene` now writes `.glb` (and stops saving the
  per-scene imgui.ini); `load_scene` opens erhe-authored `.glb`;
  `export_gltf` (plain interchange export) remains but is nearly the same
  call minus editor extensions - keep the tools, update their descriptions
  in `mcp_server_tool_list.cpp`, and document the difference (scene save =
  full state; export = interchange).
- Remove `scene_imgui_ini_path()` and all per-scene imgui.ini save/restore
  call sites (decision: dropped; call sites: operations_window.cpp save +
  restore, mcp_server_file_io.cpp save).

## Phase 5 - Migration and removal

- One-shot migration path: keep the legacy bundle loader compiled for one
  transition period, exposed only as "Open legacy .erhescene" (or a
  `scripts/`-driven batch: load bundle -> save .glb for everything under
  `res/editor/scenes/`). After migration of local scenes, delete:
  - `scene_serialization.{hpp,cpp}` (both directions),
  - the `scene/definitions/*.py` codegen schemas that exist only for
    scene.json (`scene_file.py`, `node_data.py`, physics/layout/brush/graph
    serial types, ...) and their generated code; `gltf_source_reference.py`
    stays (used by content library),
  - geogram mesh_save/mesh_load usage in the save path,
  - Asset browser `.erhescene` directory handling,
  - legacy extras writers (`erhe_flags`, material extras) once the
    `ERHE_node` / `ERHE_material` transition period ends.
- Update `src/editor/scene/notes.md`, `src/erhe/gltf/notes.md`, CLAUDE.md
  references, memory-bank.

## Phase 6 - Verification

- Unit: geometry round-trip gtest (phase 2); animation/skin/texture export
  round-trip could live in `mcp_server_tests` or a small erhe_gltf test.
- Schema validation: each `ERHE_*` payload validates against its JSON schema
  in tests (the schemas from doc/gltf_extensions/ double as test fixtures).
- Headless MCP end-to-end (erhe-headless-verify loop): build a scene with
  shapes (geometry-normative), an imported glTF asset (triangle soup +
  textures), physics bodies + a joint, a brush placement, a graph mesh, a
  graph texture bound to a material slot, layouts, tags, an authored
  animation -> `save_scene` -> `load_scene` -> MCP queries diff (nodes,
  materials, physics items, brushes, animations, tags) +
  `capture_screenshot` compare.
- Prefab scenes: save/load with an external-asset prefab instance present.
- Foreign-tool smoke check: a saved scene passes the Khronos glTF validator
  (ERHE extensions in extensionsUsed only) and opens in a stock viewer
  showing the render content. This holds cleanly with the TRIANGLES-primary
  polygon decision (no draft extensions, no restart sentinels);
  geometry-normative meshes render filled - flat-shaded where no
  per-vertex NORMAL could be dual-listed (corner normals stay
  ERHE_geometry-only).

### Phase 6 as built (2026-07-12)

All five bullets landed in one standing harness,
**`scripts/scene_roundtrip_verify.py`** (run book:
`doc/scene_serialization.md` "Verifying round-trips"). The schema
validation, end-to-end diff, prefab round-trip and foreign-tool checks are
sections of that script; a dedicated erhe_gltf unit test was rejected
because `parse_gltf` requires a live `erhe::graphics::Device` + executor +
`Image_transfer` (import only runs inside an editor session), so
animation / skin / texture round-trip is asserted at the harness level
(channel-exact animation diff, skin + embedded-image checks against the
saved GLB, Khronos validator, Blender import+render).

Verification surfaced and fixed three export defects plus one MCP gap:

1. Dual-listed `NORMAL` accessors were written from allocated-but-unset
   geogram attributes (all-zero bytes, `present_*` mask false) - every
   geometry-normative mesh failed `ACCESSOR_VECTOR3_NON_UNIT`. Fixed:
   dual-listing requires a fully-present vertex attribute
   (`gltf_fastgltf.cpp` `process_geometry`).
2. Imported triangle-soup primitives exported geometry-normative (the
   derived, welded geometry won over the soup), dropping `TEXCOORD_0` /
   `JOINTS_0` / `WEIGHTS_0` from the render payload - textured imports
   failed `MESH_PRIMITIVE_TOO_FEW_TEXCOORDS`, skinned meshes failed
   `NODE_SKIN_WITH_NON_SKINNED_MESH`. Fixed: the soup, when present, is
   the primitive's source of truth and exports as-is (its geometry is
   re-derived on load); `ERHE_geometry` attaches only to authored
   geometry. Exported glTF mesh names now come from the erhe mesh name.
3. Settings-less (free six-dof) physics joints were skipped on export.
   Fixed: they export as a joint description with no limits/drives
   (`gltf_physics_export.cpp`); reload materializes a settings item.
4. MCP `get_scene_nodes` now reports `parent_id` and `import_root` so the
   harness can apply the exporter's import_root transparency to pre-save
   snapshots.

Known non-round-tripping state is documented in
`doc/scene_serialization.md` "What is not persisted" (`Brush_placement`
attachments, static-body mass).

## Alternative considered: OpenUSD (reviewed 2026-07-12, not chosen)

OpenUSD (v26.05, github.com/PixarAnimationStudios/OpenUSD) was evaluated as
the persistence format instead of glTF. Semantically it is the better fit -
almost every mechanism this plan has to invent or track as a draft is
ratified core USD:

- **Polygon meshes are native**: UsdGeomMesh faceVertexCounts /
  faceVertexIndices carries n-gons directly - no draft-PR tracking.
  (The 2026-07-12 review neutralized this advantage on the glTF side too:
  `ERHE_geometry` now carries rings as the same counts/indices shape and
  the draft polygon extensions are not adopted until ratified.)
- **Primvar interpolation matches geogram elements**: constant / uniform
  (per-facet) / vertex / faceVarying (per-corner) primvars map 1:1 onto the
  attribute dump of phase 2. Only edge attributes need a custom encoding
  (same as under glTF).
- **UsdPhysics is a ratified schema** (rigid bodies, colliders, joints,
  materials, collision groups, mass/COM/velocities) vs KHR_physics_rigid_bodies
  carried in our fastgltf fork.
- **Composition arcs (references/payloads) are native prefabs** - strictly
  stronger than the glTF 2.1 externalAssets proposal (also fork-carried),
  and layering/variants would naturally express per-scene overrides (#239)
  and future non-destructive workflows.
- **.usda is diffable text**, .usdc binary, .usdz single-file package -
  covers the deferred ".gltf + .bin" wish natively.
- **Editor state** goes into custom (codeless) schemas or namespaced custom
  attributes - first-class, instead of extras conventions.

Rejected on operational grounds:

1. **Quest/Android is unsupported.** OpenUSD officially supports
   Linux/macOS/Windows/iOS/visionOS/WASM but not Android; the erhe editor
   runs on Quest and must load scenes there. A port is nontrivial ongoing
   maintenance; tinyusdz (third-party MIT reader) has incomplete/experimental
   write support. This alone is close to a hard blocker.
2. **Dependency weight clashes with erhe's CPM model.** OpenUSD is a very
   large source build (TBB required, long configure+build even with imaging
   and Python disabled) fetched-from-source at configure time like all erhe
   deps; fastgltf is tiny and already integrated.
3. **It adds a stack instead of replacing one.** glTF import/export stays
   regardless (interchange, prefab sources are glTF). USD-for-persistence
   means maintaining two serialization stacks; the glTF plan collapses to
   one, reusing the existing fork (physics, 2.1 external assets), extension
   plumbing, and import machinery.
4. **Animation model mismatch**: the animation editor (#243) authors
   glTF-style samplers with cubic tangents; USD is time-sample-first (spline
   curves are a recent addition), so round-trip would need re-encoding.

The 2026-07-12 revision harvests USD's transferable ideas into glTF
extensions instead (see "USD-inspired features" above and
`doc/usd_compatibility.md`): collections shape `ERHE_collections`, overs
shape the planned `ERHE_overrides`, and variants / animate-anything /
instancing arrive via the ratified KHR_materials_variants /
KHR_animation_pointer / EXT_mesh_gpu_instancing. `ERHE_geometry` keeps
erhe/geogram element naming; the primvar mapping is documented, not adopted.

Revisit trigger: if Android support lands upstream (or write-capable
tinyusdz matures) AND erhe starts needing composition features glTF cannot
express (variants, non-destructive layering, collaborative workflows), USD
export could first be added as an *interchange target* without changing the
persistence format.

## Risks / open items

- **EXT_mesh_polygon ratification watch**: rings live in `ERHE_geometry`
  until the draft ratifies (decision above; the draft already flipped its
  encoding once, 2026-05-20). Migration when it lands is mechanical
  (counts/indices -> rings) using the extras->extensions transition-period
  reader pattern.
- **Source image byte retention** increases memory per imported textured
  asset (kept compressed, so typically small relative to GPU copies). The
  re-read-from-source fallback covers pre-existing imports but silently
  depends on the source file still being present - warn loudly.
- **Generic extension passthrough in the fork** is new fastgltf surface
  (read + write callbacks per object type); scope it to the object types the
  plan needs (asset, scene, node, camera, material, mesh primitive) and
  propose it upstream to shorten the fork's life. Two hook points are
  net-new even relative to the extras callbacks (asset root is wired to
  nothing today; mesh primitives have no callback category), and the read
  side must re-stringify simdjson sub-values (fastgltf never round-trips
  raw JSON today).
- **ERHE prefix registration** (Khronos glTF registry Prefixes.md PR) should
  land before files escape into the wild; until then the names are squattable.
- Corner-attribute order mapping is the fiddliest part of phase 2 (corner
  records follow the flat facet_vertex_indices order, which must equal
  geogram's facet-corner iteration order); the gtest is written first
  (bit-exact dump round-trip) to pin it down.
- `ERHE_overrides` intra-asset addressing (stable node paths inside a
  referenced glTF) is the hardest USD-inspired design; kept out of the
  switchover critical path deliberately (design notes in
  `doc/usd_compatibility.md`).
