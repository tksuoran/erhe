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
- **Polygon geometry**: implement `EXT_mesh_polygon` + its dependency
  `KHR_mesh_primitive_restart` exactly as drafted in
  KhronosGroup/glTF#2570 (open PR). Adjust if the spec changes before
  ratification.
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
| node Item flags | `ERHE_node` extension (migrates `erhe_flags` extras) | NEW |
| cameras (projection) | core cameras | exists |
| camera exposure, shadow_range | `ERHE_camera` extension | NEW |
| lights | KHR_lights_punctual | exists |
| materials + erhe fields | core + `ERHE_material` extension (migrates material extras) | NEW (extras exist) |
| textures / images / samplers | core | NEW (exporter currently drops them) |
| animations | core | NEW (exporter currently drops them) |
| skins | core | NEW (exporter currently drops them) |
| meshes (triangle soup) | core | exists |
| meshes (geometry-normative, geogram) | EXT_mesh_polygon + KHR_mesh_primitive_restart + `ERHE_geometry` extension | NEW |
| physics bodies, colliders, triggers, velocities, COM | KHR_physics_rigid_bodies + KHR_implicit_shapes | exists |
| physics materials, collision filters, joint settings (incl. unreferenced library items) | same extension top-level arrays | exists / verify unreferenced |
| node joints | KHR_physics_rigid_bodies joints | exists |
| prefab instances | glTF 2.1 externalAssets | exists |
| brushes + content-library folder tree | `ERHE_brushes` root extension referencing unreferenced meshes | NEW |
| graph textures / graph meshes (node-graph JSON) + bindings | `ERHE_node_graphs` root extension + node/material entries | NEW |
| layouts / layout items | `ERHE_layout` node extension | NEW |
| per-scene settings (#239), ambient light (#237), enable_physics | `ERHE_scene` scene extension | NEW |
| item tags | `ERHE_collections` root extension (USD-inspired, see below) | NEW |
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

## Phase 0 - Exporter completeness (prerequisite; valuable standalone)

`src/erhe/gltf/erhe_gltf/gltf_fastgltf.cpp` `Gltf_exporter`:

1. **Images / textures / samplers export.** Round-trip needs the original
   encoded bytes: retain the compressed source image bytes at import time
   (alongside `erhe::graphics::Texture`, e.g. on the content-library texture
   entry) and write them back verbatim into the GLB buffer. GPU-only textures
   (graph-texture bakes) are never exported as images - they regenerate.
   Wire material texture references (`baseColorTexture` etc., currently
   written empty).
2. **Animation export**: `erhe::scene::Animation` -> samplers + channels
   (node targets by exported node index; T/R/S/weights paths; STEP / LINEAR /
   CUBICSPLINE). This closes the #243 animation-editor data-loss gap.
3. **Skin export**: joints (node indices), inverseBindMatrices accessor,
   skeleton; `Node::skinIndex` on skinned mesh nodes.
4. Verify unreferenced physics materials / filters / joint settings from the
   content library export into the extension's top-level arrays (they must
   survive save/load like scene.json v3+ does today).

Import side already parses animations / skins / images; add the source-byte
retention.

## Phase 1 - fastgltf fork: polygon extensions + generic extension passthrough

Branch on `tksuoran/fastgltf` (precedent: `khr_physics_rigid_bodies`):

- `KHR_mesh_primitive_restart`: primitive-restart flag on index accessors.
- `EXT_mesh_polygon` per PR 2570: per-primitive extension carrying polygon
  ring indices (LINE_LOOP topology, restart-separated rings, first ring =
  exterior; erhe facets are simple polygons so only exterior rings are
  written, but the reader must accept holes) plus the mandatory TRIANGLES
  render indices in the primitive proper.
- **Generic vendor-extension JSON passthrough** (the enabler for `ERHE_*`
  extensions without per-extension fastgltf C++ types): read + write
  callbacks analogous to the existing extras callbacks, invoked per object
  (asset root, scene, node, camera, material, mesh, mesh primitive) with the
  extension name and raw JSON. erhe's editor layer serializes/parses its
  `ERHE_*` payloads through these; fastgltf stays schema-agnostic for them.
  This also lets the existing extras carriers (`erhe_flags`, material
  extras) migrate to `ERHE_node` / `ERHE_material` without new fork work
  per extension.
- extensionsUsed bookkeeping for passthrough extensions.
- Record in CMake comments when the fork additions can be dropped (upstream
  fastgltf shipping polygon extensions; generic passthrough is worth
  offering upstream).

## Phase 2 - Geometry (geogram) round-trip: `ERHE_geometry`

New `erhe_gltf` <-> `erhe_geometry` conversion (editor-independent,
unit-testable). The attribute model is deliberately **shaped after USD
primvar interpolation** (see the USD-inspired section below), which maps
1:1 onto geogram elements:

- **Export**: for each geometry-normative primitive, write one glTF mesh
  primitive with: POSITION accessor (geogram vertices, no welding/unwelding -
  glTF vertex i == geogram vertex i), TRIANGLES indices from the existing
  triangulation, and EXT_mesh_polygon ring indices from geogram facets.
- **`ERHE_geometry` primitive extension**: an array of attribute records
  {name, interpolation, type, dimension, accessor}, where interpolation is
  one of `constant` (whole mesh), `uniform` (per facet), `vertex`
  (per vertex), `faceVarying` (per corner, in ring order), `edge`
  (per edge; the edge index list itself is one more accessor pair) - the
  USD primvar vocabulary plus `edge`, which USD lacks. Full dump: every
  geogram attribute is written, so the round-trip is bit-exact; loader
  applies the dump first and runs process() only for genuinely missing data
  (new-file case).
- **Load**: presence of EXT_mesh_polygon on a primitive marks the mesh
  geometry-normative -> rebuild `erhe::geometry::Geometry` (geogram mesh from
  rings + `ERHE_geometry` attributes), then the standard Primitive build.
  Absence -> Triangle_soup path exactly as today.
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
- `ERHE_node` (node extension): Item flag bits (migrates `erhe_flags`
  extras; legacy extras still parsed during transition).
- `ERHE_camera` (camera extension): exposure, shadow_range.
- `ERHE_material` (material extension): roughness_y, bxdf_model,
  blending_mode, brushed-metal fields (migrates material extras).
- `ERHE_scene` (scene extension): per-scene settings (#239), ambient_light
  (#237), enable_physics (replace the hard-coded TODO with real tracking).
- `ERHE_layout` (node extension): Layout and Layout_item fields (two
  optional sub-objects on one extension). Include the prefab-instance skip
  that today's layout pass misses.
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
  initially used for item tags (today's add_tags/remove_tags state);
  see the USD-inspired section.

Identity: everything binds by glTF index within the same asset (node index,
material index, mesh index) instead of scene.json's parallel numeric id maps -
the whole class of scene.json <-> data.glb index-mismatch bugs disappears.
Names remain only inside graph JSON references (same as today).

## USD-inspired extensions (review, 2026-07-12)

Reviewing USD (see Alternative-considered section) for concepts worth
carrying into glTF. First preference is always an existing **ratified**
glTF extension; only where none exists do we define `ERHE_*`.

### Adopt now (in scope for this plan)

- **Primvar interpolation model** (USD: UsdGeomPrimvar) -> shapes
  `ERHE_geometry` in phase 2. The constant/uniform/vertex/faceVarying
  vocabulary is proven, documented, and understood by DCC developers;
  adopting it verbatim (plus `edge`) makes the extension legible and keeps a
  future USD export path trivial.
- **Collections** (USD: UsdCollectionAPI) -> `ERHE_collections` in phase 3.
  Named sets of node references express item tags today and selection sets /
  render-layer-like groupings later, without inventing per-node tag strings.

### Near-term follow-ups (specified here, implemented after the switchover)

- **Sparse overrides on references** (USD: "overs" / opinion strength) ->
  `ERHE_overrides`: a per-prefab-instance-node list of sparse property
  overrides (node transforms, visibility flags, material rebinds) applied
  on top of the instantiated externalAsset subtree, addressed by
  stable-path-within-asset. This fixes a real current limitation: edits
  inside a prefab instance are silently lost on save (the subtree is
  re-created from the source glTF). Highest-value USD idea for erhe;
  deferred only because it needs a stable intra-asset addressing scheme
  (name paths with disambiguation) designed carefully.
- **Material variants** (USD: variantSets; glTF: **KHR_materials_variants**,
  ratified) -> adopt KHR_materials_variants as-is for per-primitive material
  alternatives (fastgltf already knows this extension). Editor UI for
  authoring/switching variants comes with it.
- **Animate-anything** (USD: time samples on any attribute; glTF:
  **KHR_animation_pointer**, ratified) -> adopt KHR_animation_pointer so
  animations can target lights, materials, camera parameters - a natural
  growth path for the #243 animation editor beyond node TRS.

### Deferred / noted, not planned

- **Node-level variant sets** (USD variants beyond materials) ->
  `ERHE_variants` switching subtree alternatives (LODs, configuration
  options). Powerful but no current editor feature needs it; design only
  when one does.
- **Instancing** (USD: scenegraph/point instancers; glTF:
  **EXT_mesh_gpu_instancing**, ratified) -> relevant if/when erhe adds
  mass placement (scatter) tooling; adopt the EXT then rather than
  inventing anything.
- **Payloads** (USD: deferred loading) -> externalAssets + Prefab_library
  caching already give lazy structure; explicit load/unload policy flags
  can ride `ERHE_scene` later if scenes grow big enough to need it.
- **Sublayering / composition of whole scene files** (USD sublayers) ->
  out of scope; one scene = one asset + external references remains the
  model. Revisit only with a concrete collaborative-workflow requirement.
- **Purpose (render/proxy/guide)** (USD imageable purpose) -> erhe's
  layer/flag system already covers this internally; `ERHE_node` flags carry
  it.

## Phase 4 - Save / Open switchover

- **Save**: `save_scene()` becomes a single `export_gltf()` call (binary,
  physics data, prefab external assets, `ERHE_*` extension payloads,
  exclusion hook) -> write one `.glb`. Save dialog filter becomes `.glb`;
  default location stays `res/editor/scenes/`.
- **Open vs Import**: a `.glb`/`.gltf` whose asset lists `ERHE_scene` in
  extensionsUsed opens as a full `Scene_root` (new Open-Scene path: nodes,
  cameras, lights, meshes, physics import, brushes, graphs, layouts,
  settings); any other glTF keeps the existing import-as-asset flow (default
  camera/lights, import_root wrapper, undoable compound operation).
  `scan_gltf` already reports extensionsUsed - the asset browser
  distinguishes on that (`Asset_file_scene` becomes "erhe-authored .glb"
  instead of ".erhescene directory").
- Open-Scene reuses the import machinery (parse_gltf + finalize_imported_meshes
  + physics import) but constructs the Scene_root directly (not undoable),
  mirroring today's load_scene structure.
- MCP `save_scene` / `export_gltf` / `import_gltf` tools: `save_scene` now
  writes `.glb`; `export_gltf` (plain interchange export) remains but is
  nearly the same call minus editor extensions - keep both tools, document
  the difference (scene save = full state; export = interchange).
- Remove `scene_imgui_ini_path()` and all per-scene imgui.ini save/restore
  call sites (decision: dropped).

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
  showing the render content.

## Alternative considered: OpenUSD (reviewed 2026-07-12, not chosen)

OpenUSD (v26.05, github.com/PixarAnimationStudios/OpenUSD) was evaluated as
the persistence format instead of glTF. Semantically it is the better fit -
almost every mechanism this plan has to invent or track as a draft is
ratified core USD:

- **Polygon meshes are native**: UsdGeomMesh faceVertexCounts /
  faceVertexIndices carries n-gons directly - no EXT_mesh_polygon /
  KHR_mesh_primitive_restart draft-PR tracking (phase 1 disappears).
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
extensions instead (see "USD-inspired extensions" above): primvar
interpolation shapes `ERHE_geometry`, collections shape `ERHE_collections`,
overs shape the planned `ERHE_overrides`, and variants / animate-anything /
instancing arrive via the ratified KHR_materials_variants /
KHR_animation_pointer / EXT_mesh_gpu_instancing.

Revisit trigger: if Android support lands upstream (or write-capable
tinyusdz matures) AND erhe starts needing composition features glTF cannot
express (variants, non-destructive layering, collaborative workflows), USD
export could first be added as an *interchange target* without changing the
persistence format.

## Risks / open items

- **PR 2570 is a moving target**: encoding may change before ratification;
  we track the draft (decision above). Contained in the fastgltf fork.
- **Source image byte retention** increases memory per imported textured
  asset (kept compressed, so typically small relative to GPU copies).
- **Generic extension passthrough in the fork** is new fastgltf surface
  (read + write callbacks per object type); scope it to the object types the
  plan needs (asset, scene, node, camera, material, mesh primitive) and
  propose it upstream to shorten the fork's life.
- **ERHE prefix registration** (Khronos glTF registry Prefixes.md PR) should
  land before files escape into the wild; until then the names are squattable.
- Corner-attribute ring-order mapping is the fiddliest part of phase 2; the
  gtest is written first (bit-exact dump round-trip) to pin it down.
- `ERHE_overrides` intra-asset addressing (stable node paths inside a
  referenced glTF) is the hardest USD-inspired design; kept out of the
  switchover critical path deliberately.
