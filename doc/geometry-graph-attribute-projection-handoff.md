# Implementation handoff: project_attribute geometry-graph node

Fresh-context handoff, written 2026-08-10. The DESIGN is final and lives in
`doc/geometry-graph-attribute-projection.md` - read it first, in full. This
file adds only what a fresh session needs to implement it: repo state, file
map, phase plan, and the session-learned gotchas that are not in the design.

## Goal (one paragraph)

A geometry-graph node `project_attribute`: inputs target mesh + source mesh
+ attribute choice; output = target with that ONE channel replaced by values
projected from the source surface. Requirements locked during review:
texture coordinates are TILED (values > 1 transfer verbatim, no fract
anywhere; a seam is a value DISAGREEMENT across an edge, never tiling
continuation), and seams transfer EXACTLY via seam imprinting (cut target
facets along the chart-distance bisector contour, per design doc). Prime
client: creation 18's fish body (box -> lattice -> subdivide graph), whose
inherited per-quad UVs are unusable - transfer clean UVs from a proxy
branch instead.

## Repo state as of this handoff

- Branch `main`. Relevant commits already landed:
  - `f64584ba` creation_18_fish.py (the end-to-end client + skill reference
    `.agents/skills/erhe-creations/references/geometry_graph_sculpt.md`).
  - `47958da7` + `131b5d86` graphics fixes that make the editor RUN on this
    AMD iGPU machine (SPIR-V relaxed-extended-instruction fallback;
    vulkan.disable_ray_tracing). Do not revert.
  - Design doc commits (`doc/geometry-graph-attribute-projection.md`).
- `config/editor/erhe_graphics.json` is locally modified (INTENTIONAL,
  uncommitted): `"vulkan": {"_version": 2, "disable_ray_tracing": true}` -
  required for the editor to start on this machine; the `_version: 2` is
  mandatory (versionless JSON parses as v1 and drops the field).
- The windowed editor build is `build_vs2026_vulkan` (VS generator):
  `cmake --build build_vs2026_vulkan --config Release --target editor
  --parallel`. OpenGL check build: same with `build_vs2026_opengl`.

## File map

Core operation (new):
- `src/erhe/geometry/erhe_geometry/operation/project_attribute.{hpp,cpp}`
- Register in `src/erhe/geometry/CMakeLists.txt` next to lattice_deform.

Templates to copy from (read before writing):
- `operation/geometry_operation.{hpp,cpp}` - two-source ctor
  (lhs=target, rhs=source; CSG uses it), `Source_table`,
  `make_edge_midpoints` (uniform-t edge splits with provenance; you need a
  per-edge-t variant), `interpolate_mesh_attributes()`,
  `copy_mesh_attributes()`, batch element creation notes (Geogram
  create_sub_elements is quadratic when elements are created one at a
  time - create in bulk, see the "No-create variants" comment block).
- `operation/lattice_deform.cpp` - a recent, clean operation of similar
  size; `operation/make_atlas.cpp` - attribute bind/unbind discipline
  around Geogram calls (attributes must be UNBOUND before Geogram
  mutates/copies meshes; it rebinds after).
- `erhe_geometry/geometry.hpp` - `Mesh_attributes` typed accessors
  (`corner_texcoord(i)` etc.), `Attribute_present<T>` (value + present
  flag), `Attribute_descriptor::Interpolation_mode`.

Graph node (new):
- `src/editor/geometry_graph/nodes/project_attribute_node.{cpp,hpp}`
- Register: `geometry_graph_node_factory.cpp` (type name
  "project_attribute") + the palette (grep how "boolean" registers in
  both; `boolean_node.{cpp,hpp}` is the exact 2-geometry-input template,
  `subdivide_node` the parameter/serialization template).
- Node UI: attribute combo, method combo, max_distance drag, cut_seams
  checkbox, projected/missed counts text.

Tests (new):
- `src/erhe/geometry/test/test_project_attribute.cpp`, registered in
  `src/erhe/geometry/test/CMakeLists.txt`. Test list is in the design
  doc's "Verification plan" - implement all four, the tiled-seam one is
  the point of the feature.
- Test build dir: `build_tests` (plus `configure_tests.bat` exists; check
  how test_lattice_deform.cpp builds/runs before inventing anything).

## Phase plan (one commit per phase; build + test before each commit)

1. **Core op, no cutting**: chart decomposition (value-space continuity),
   per-chart triangulated copies + `GEO::MeshFacetsAABB`, per-target-facet
   chart anchoring, corner sampling with facet-local barycentrics,
   Interpolation_mode-aware blend, Attribute_present-aware miss fallback,
   backface rejection. cut_seams=false behavior complete. gtests: cube
   projection + miss fallback + normal rejection.
2. **Seam imprinting**: vertex labeling, bisector zero crossings (per-edge,
   shared, bisection-refined, epsilon-snapped), facet splits (chord;
   CDT only if a junction case actually needs it), per-edge-t edge-split
   provenance variant, then re-sample sub-facets. gtest: tiled-UV seam
   imprint (exact 4.0|0.0 per side, no cuts on tiling continuation, no
   T-junctions).
3. **Graph node**: pins/params/serialization/factory/palette/UI, evaluate()
   calling the op; missing source -> passthrough + warning.
4. **End-to-end on the fish**: extend `scripts/creations/creation_18_fish.py`
   (or a scratch probe first - see workflow below) with a proxy branch:
   e.g. cylinder-ish mesh with clean cylindrical UVs run through the SAME
   lattice node, then `project_attribute(texcoord_0)` before `output`.
   Verify with the texcoord debug view (below), then bind the scales
   texture graphs (recipe in the skill's geometry_graph_sculpt.md).

## Session-learned gotchas (cost real time - trust these)

- **MeshFacetsAABB triangulates its input mesh, even via the const
  overload** (const_cast inside Geogram) - always build it on a
  triangulated COPY carrying an `orig_facet` facet attribute. Geogram fan
  triangulation preserves vertex ids -> hit triangle maps to (original
  facet, 3 original vertices) -> vertex-match to that facet's corners.
- Geometry-graph node params arrive via `read_parameters(json)` (the MCP
  `geometry_graph_set_parameter` path) - keep every param in
  write/read_parameters or MCP scripting cannot drive it.
- `get_geometry_graph` is the MCP evaluation completion barrier; graph
  meshes evaluate async on shadow clones (never touch live scene state
  from evaluate()).
- Attribute-channel history gotchas from #244: `build_edges` wipes
  edge-domain values; `transform_mesh` has a hardcoded channel list.
  Not directly on this path, but check both if a channel goes missing.
- Editor verification workflow: drive the running editor's MCP server
  (`py -3 scripts/mcp_call.py <tool> '<json>'`, port 8080). The fish
  script self-launches the editor: `py -3
  scripts/creations/creation_18_fish.py --pause 0 --no-save` (add
  `--reuse` to attach to a running editor instead of relaunching).
- **Texcoord debug view**: `res/shaders/standard.frag` has
  `ERHE_SHADER_DEBUG == 7` -> `fract(v_texcoord_0)` visualization (user-
  added); the active graphics preset in `config/editor/editor_settings.json`
  carries `"shader_debug": 7`. Success on the fish = continuous cylindrical
  gradient; failure = per-quad moire (screenshots
  `logs/creations/fish_texcoord_*.png` show the current broken state).
  Note: tail fin / sweep fins / eyes render BLACK there - those meshes
  have no texcoords at all (CSG output and sweep don't emit them);
  out of scope unless asked.
- Screenshot iteration: `capture_screenshot` works windowed; the user's
  `config/editor/default_viewport_config.json` may have `edge_lines: true`
  (dense meshes read black) - back up, set false, restore; it is read at
  every viewport construction, no restart needed. `presentation()` in
  common.py HIDES the user's windows - re-show Scene Hierarchy etc. if
  the user is watching (or avoid screenshot helpers in a live session).
- Follow repo conventions from memory: always class not struct, explicit
  types not auto, plenty of parentheses; split commits; test before
  commit; stage explicit paths and review `git diff --cached`; commit
  messages via `git commit -F <file>` (PowerShell 5.1 mangles quotes -
  use the Bash tool for git).

## Definition of done

- All 4 gtest groups green; vulkan editor + opengl editor targets build.
- Fish body shows a continuous texcoord gradient under ERHE_SHADER_DEBUG 7
  with the proxy-projection graph, seams cut exactly (inspect the seam
  line under the belly), scales albedo+normal graphs bind and render.
- Skill reference geometry_graph_sculpt.md updated with the working
  recipe; this handoff doc deleted (fold anything still relevant into the
  design doc or the skill), design doc's "Open questions" resolved.
