# glTF-everything scene persistence plan (drop .erhescene)

Goal: a saved erhe scene is a single `.glb` file that round-trips ALL editor
state through glTF 2.1 + extensions + extras. The `.erhescene` directory
bundle (scene.json + data.glb + *.geogram + imgui.ini) is removed.

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

## Capability map

| erhe state | glTF mechanism | status |
|---|---|---|
| node tree, TRS, names | core | exists |
| node Item flags | node extras `erhe_flags` | exists |
| cameras (projection) | core cameras | exists |
| camera exposure, shadow_range | camera extras | NEW |
| lights | KHR_lights_punctual | exists |
| materials + erhe fields | core + material extras | exists |
| textures / images / samplers | core | NEW (exporter currently drops them) |
| animations | core | NEW (exporter currently drops them) |
| skins | core | NEW (exporter currently drops them) |
| meshes (triangle soup) | core | exists |
| meshes (geometry-normative, geogram) | EXT_mesh_polygon + KHR_mesh_primitive_restart + erhe geometry-attributes extras | NEW |
| physics bodies, colliders, triggers, velocities, COM | KHR_physics_rigid_bodies + KHR_implicit_shapes | exists |
| physics materials, collision filters, joint settings (incl. unreferenced library items) | same extension top-level arrays | exists / verify unreferenced |
| node joints | KHR_physics_rigid_bodies joints | exists |
| prefab instances | glTF 2.1 externalAssets | exists |
| brushes + content-library folder tree | asset extras `erhe_brushes` referencing unreferenced meshes | NEW |
| graph textures / graph meshes (node-graph JSON) + bindings | asset extras `erhe_graphs` + node/material extras | NEW |
| layouts / layout items | node extras `erhe_layout` / `erhe_layout_item` | NEW |
| per-scene settings (#239), ambient light (#237), enable_physics | scene extras `erhe_scene` | NEW |
| per-scene imgui layout | DROPPED (decision above) | - |

Editor state intentionally not persisted (unchanged from today): selection,
undo stack, viewport bindings.

ERHE-specific data rides in **extras** (the fastgltf extras read/write
callback plumbing already exists and is used for `erhe_flags` and material
fields). Formalizing them later as `ERHE_*` extensions is possible but not
part of this plan; extras keeps the fork surface small.

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
4. **Camera extras**: exposure + shadow_range via the extras write callback;
   parse side reads them back.
5. Verify unreferenced physics materials / filters / joint settings from the
   content library export into the extension's top-level arrays (they must
   survive save/load like scene.json v3+ does today).

Import side already parses animations / skins / images; add the source-byte
retention and camera extras read.

## Phase 1 - fastgltf fork: polygon extensions

Branch on `tksuoran/fastgltf` (precedent: `khr_physics_rigid_bodies`):

- `KHR_mesh_primitive_restart`: primitive-restart flag on index accessors.
- `EXT_mesh_polygon` per PR 2570: per-primitive extension carrying polygon
  ring indices (LINE_LOOP topology, restart-separated rings, first ring =
  exterior; erhe facets are simple polygons so only exterior rings are
  written, but the reader must accept holes) plus the mandatory TRIANGLES
  render indices in the primitive proper.
- Parse + write both; extensionsUsed/extensionsRequired bookkeeping.
- Record in CMake comments when the fork additions can be dropped (upstream
  fastgltf shipping both extensions).

## Phase 2 - Geometry (geogram) round-trip

New `erhe_gltf` <-> `erhe_geometry` conversion (editor-independent, unit-testable):

- **Export**: for each geometry-normative primitive, write one glTF mesh
  primitive with: POSITION accessor (geogram vertices, no welding/unwelding -
  glTF vertex i == geogram vertex i), TRIANGLES indices from the existing
  triangulation, and EXT_mesh_polygon ring indices from geogram facets.
- **Full attribute dump** via primitive extras `erhe_geometry`: a table of
  {name, element (vertex|facet|corner|edge), type, dimension, accessor}.
  Corner attributes are accessors of length sum(facet sizes) in ring order;
  facet attributes are per-polygon; the edge list itself plus edge attributes
  are dumped so nothing is recomputed lossily. Values that erhe recomputes at
  load anyway (normals, centroids) still round-trip bit-exact because they
  are in the dump; loader applies dump first, then runs process() only for
  genuinely missing data (new-file case).
- **Load**: presence of EXT_mesh_polygon on a primitive marks the mesh
  geometry-normative -> rebuild `erhe::geometry::Geometry` (geogram mesh from
  rings + attribute dump), then the standard Primitive build. Absence ->
  Triangle_soup path exactly as today.
- **Brush geometry** uses the same path (brush meshes are glTF meshes that no
  node references).
- gtest in `src/erhe/geometry/test/` (suite exists): Geometry -> glTF
  primitive -> Geometry, assert bit-exact attribute round-trip (hexfloat
  comparison discipline applies).

## Phase 3 - Editor-state extras

All written by the editor layer (like `build_gltf_physics_data`), carried
through the exporter's extras callback; `erhe::gltf` stays editor-agnostic.
The exporter gains a small hook so the editor can (a) attach extras to
arbitrary exported objects and (b) exclude editor-controlled attachments:

- **Exclusion hook**: graph-mesh-controlled meshes and their Node_physics are
  baked artifacts (rebuilt on load) and must NOT be exported - today's
  save_scene skips them in scene.json but they leak into data.glb (known
  index-shift bug); the hook fixes this class of problem for good.
- `erhe_scene` (scene extras): per-scene settings (#239), ambient_light
  (#237), enable_physics (replace the hard-coded TODO with real tracking).
- `erhe_layout` / `erhe_layout_item` (node extras): all Layout / Layout_item
  fields. Include the prefab-instance skip that today's layout pass misses.
- `erhe_brushes` (asset extras): array of {name, folder_path, mesh index,
  material index, density, normal_style}. Collision shape still rebuilt at
  first instantiation (Brush::late_initialize), as today.
- `erhe_graphs` (asset extras): graph_textures + graph_meshes with their
  node-graph JSON embedded (extras is JSON; no string-in-string escaping
  needed, unlike scene.json today). Material slot bindings move to material
  extras `erhe_texture_source` = {slot, graph_texture_name}; node bindings to
  node extras `erhe_graph_mesh` = {graph_mesh_name}. Graphs load born-dirty
  and re-bake, as today.

Identity: everything binds by glTF index within the same asset (node index,
material index, mesh index) instead of scene.json's parallel numeric id maps -
the whole class of scene.json <-> data.glb index-mismatch bugs disappears.
Names remain only inside graph JSON references (same as today).

## Phase 4 - Save / Open switchover

- **Save**: `save_scene()` becomes a single `export_gltf()` call (binary,
  physics data, prefab external assets, editor extras, exclusion hook) ->
  write one `.glb`. Save dialog filter becomes `.glb`; default location stays
  `res/editor/scenes/`.
- **Open vs Import**: a `.glb`/`.gltf` whose asset carries `erhe_scene`
  extras opens as a full `Scene_root` (new Open-Scene path: nodes, cameras,
  lights, meshes, physics import, brushes, graphs, layouts, settings); any
  other glTF keeps the existing import-as-asset flow (default camera/lights,
  import_root wrapper, undoable compound operation). `scan_gltf` already
  reports enough to distinguish; extend `Gltf_scan` with a
  `has_erhe_scene` flag for the asset browser (`Asset_file_scene` becomes
  "erhe-authored .glb" instead of ".erhescene directory").
- Open-Scene reuses the import machinery (parse_gltf + finalize_imported_meshes
  + physics import) but constructs the Scene_root directly (not undoable),
  mirroring today's load_scene structure.
- MCP `save_scene` / `export_gltf` / `import_gltf` tools: `save_scene` now
  writes `.glb`; `export_gltf` (plain interchange export) remains but is
  nearly the same call minus editor extras - keep both tools, document the
  difference (scene save = full state; export = interchange).
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
  - Asset browser `.erhescene` directory handling.
- Update `src/editor/scene/notes.md`, `src/erhe/gltf/notes.md`, CLAUDE.md
  references, memory-bank.

## Phase 6 - Verification

- Unit: geometry round-trip gtest (phase 2); animation/skin/texture export
  round-trip could live in `mcp_server_tests` or a small erhe_gltf test.
- Headless MCP end-to-end (erhe-headless-verify loop): build a scene with
  shapes (geometry-normative), an imported glTF asset (triangle soup +
  textures), physics bodies + a joint, a brush placement, a graph mesh, a
  graph texture bound to a material slot, layouts, an authored animation ->
  `save_scene` -> `load_scene` -> MCP queries diff (nodes, materials,
  physics items, brushes, animations) + `capture_screenshot` compare.
- Prefab scenes: save/load with an external-asset prefab instance present.

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
   one, reusing the existing fork (physics, 2.1 external assets), extras
   plumbing, and import machinery.
4. **Animation model mismatch**: the animation editor (#243) authors
   glTF-style samplers with cubic tangents; USD is time-sample-first (spline
   curves are a recent addition), so round-trip would need re-encoding.

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
- **fastgltf exporter extras coverage**: extras write callback must fire for
  every object type we need (asset, scene, node, camera, material, mesh
  primitive). Gaps get added in the fork.
- Corner-attribute ring-order mapping is the fiddliest part of phase 2; the
  gtest is written first (bit-exact dump round-trip) to pin it down.
