# Mesh modeling operations

Status: proposed

This plan extends `doc/editor/mesh_component_selection.md` (component
selection and its overlay), `doc/editor/transform.md` (the component
transform with its move and extrude modes) and `doc/editor/operations.md`
(`Mesh_operation`, the in-place vertex edits and the Operations window) with
the Blender-style modeling operations the editor does not have: edge loop and
ring selection, loop cut, knife, vertex and edge slide, merge, dissolve,
inset, bevel and the smaller operations around them. Section 4 is the scored
catalog, section 5 the build order.

## 1. Starting point

The pieces the modeling operations build on, each stated where its document
owns it:

- Component selection, picking and overlay:
  `doc/editor/mesh_component_selection.md`. Selection is per (mesh,
  primitive, `Geometry`) with vertex, facet and edge sets; an edge is its
  canonical (min, max) vertex pair. `grow()` / `shrink()`, box and paint
  select (face mode only) and the MCP tools
  (`select_mesh_components`, `get_mesh_component_selection`, ...) exist.
- Component transform with live preview: `doc/editor/transform.md`.
  `Mesh_component_transform` snapshots positions at drag start, patches the
  `Geometry` positions and the GPU buffers while dragging, and commits a
  `Move_mesh_vertices_operation`. Its extrude modes build the new topology
  once, on the first real move
  (`src/editor/transform/mesh_component_extrude.*`), then preview positions
  in place and commit a `Fork_geometry_operation` swap. That two-step shape
  (topology once, positions live, one undo entry) is the template for every
  interactive operation in this plan (D3).
- Discrete geometry operations: `doc/editor/operations.md`. A
  `Mesh_operation` runs an `erhe::geometry::operation::Geometry_operation`
  from a source `Geometry` into a new one on a worker, with weighted
  attribute interpolation (positions, normals, corner texcoords across
  seams, colors, joint data) and `remap_component_selection()` carrying the
  selection to the result. `merge_faces` (dissolve faces), `weld`
  (whole-mesh vertex colocation), `chamfer` (face chamfer by ratio),
  `triangulate`, Catmull-Clark with creases and the Conway operators exist.
- Connectivity: `doc/erhe/geometry.md`. `Geometry::update_connectivity()`
  gives vertex-to-corners and corner-to-facet, `build_edges()` gives the
  edge tables keyed by vertex pair, and Geogram's `facets.connect()` gives
  the facet across each corner's edge (`facets.adjacent()`). Edge indices
  are not stable across `build_edges()`; the vertex pair is the identity.
- Edge attributes: `edge_sharpness` only, keyed by vertex pair
  (`doc/erhe/subdivision_crease_edges.md`).
- Verification: `erhe_geometry_tests` (GoogleTest, cube / grid solids,
  `test_component_selection_remap.cpp` as the template for selection-aware
  operations), the headless editor with `scripts/mcp_call.py`
  (`doc/agents/editor_runs.md`), `scripts/mcp_extrude_normal_test.py` and
  `scripts/geometry_edit_node_order_verify.py` as the templates for scripted
  component edits, and `Mcp_test` injected-input tests for modal tools.

## 2. Gaps

What is missing, as the concrete facts the catalog scores against:

- Edge loops and rings: no walker exists. `build_extra_connectivity()`
  (`src/erhe/geometry/erhe_geometry/geometry.cpp`) orders each vertex's
  corners into a fan, and returns from the whole function at the first
  vertex with fewer than three corners, so on an open mesh every vertex
  after the first boundary vertex keeps its corners unordered. The
  per-corner edge table it builds is a private scratch vector.
- Selection: no select all / none / invert, no select linked, no mode
  conversion (the vertices of a face selection, the faces of an edge
  selection), no loop, ring or boundary select. Box and paint select are
  face-only.
- Topology edits: no delete or dissolve of vertices and edges, no
  selection-aware merge, no loop cut, knife, inset, edge bevel, bridge or
  fill. Every existing topology edit produces a new `Geometry` through
  `Geometry_operation`; nothing edits topology in place, and that stays so
  (D2).
- Sliding: no edge or vertex slide; no screen-space snap to vertices or
  edges (the transform snap is grid-step only).
- Attribute propagation: `Geometry_operation::post_processing()` copies no
  edge attributes, so every operation except Catmull-Clark drops
  `edge_sharpness`.

## 3. Design decisions

- **D1. Topology walkers live in `erhe::geometry`.** A new header
  `erhe_geometry/topology.hpp` provides, over a `Geometry` with
  connectivity and edges built: `next_edge_across_quad(facet, edge)` (the
  opposite edge of a four-corner facet), `walk_edge_loop(edge)`,
  `walk_edge_ring(edge)`, `walk_boundary_loop(edge)` and
  `collect_connected(seed vertices)`, each filling a caller-owned buffer.
  A loop continues through a vertex of valence four along the edge that
  shares no facet with the incoming edge, follows the boundary at a
  boundary vertex, and stops at any other valence or when it returns to
  its start. A ring continues across four-corner facets and stops at any
  other facet. These walkers are the one place the loop rules are coded;
  loop select, loop cut, bridge and slide all call them.
- **D2. Discrete operations produce a new `Geometry`.** Delete, dissolve,
  merge, inset, bevel, subdivide, bridge, fill and split are
  `Geometry_operation` subclasses run through `Mesh_operation`, with
  provenance recorded so `post_processing()` interpolates every attribute
  and `remap_component_selection()` carries the selection over. Undo is the
  primitive swap `Mesh_operation` already performs. Edge attributes join the
  interpolation: `post_processing()` carries `edge_sharpness` to each
  destination edge whose two vertices derive from the two vertices of one
  source edge, so creases survive every operation, not only Catmull-Clark.
- **D3. Interactive operations build topology once, then preview
  positions in place.** Loop cut, knife, slide, inset and bevel are modal
  gestures on the component tool. On confirm of the cut (loop cut, knife)
  or on the first real move (inset, bevel, slide), the tool builds the
  result `Geometry` through the same `Geometry_operation` as D2, swaps it
  in through `swap_mesh_primitives()` and installs the result selection
  with `set_after_operation()`. The drag that follows moves positions in
  place through `Mesh_component_transform` exactly as extrude does. Commit
  queues one `Fork_geometry_operation` (before and after primitives) so the
  whole gesture is one undo entry; cancel restores the before primitive
  without touching the operation stack.
- **D4. Slide is a transform mode.** `Mesh_transform_mode` gains
  `edge_slide` and `vertex_slide`, next to the extrude modes. Each affected
  vertex gets a slide rail (the two neighbouring edges of the loop for edge
  slide, the chosen adjacent edge for vertex slide) computed at `begin()`;
  `apply()` maps the pointer motion to one scalar factor and places every
  vertex on its rail. Loop cut reuses edge slide for its second step.
- **D5. Preselection overlays through the existing hover.** Loop cut and
  knife draw their candidate (the ring to cut, the cut polyline) in
  `tool_render` from the content `Hover_entry` and the walkers, with the
  overlay colors of `Mesh_component_selection_tool`. A knife cut point is
  a snapped vertex, a point on a snapped edge or a point on the hovered
  facet; the snap radius is in pixels, computed with
  `Viewport_scene_view::project_to_viewport`.
- **D6. Every operation has an MCP tool and a numeric path.** Each
  discrete operation is an MCP action with the same arguments the UI has
  (`loop_cut` takes an edge and a cut count and factor, `merge_vertices`
  takes a merge type and distance, ...), so scripts verify it headless;
  each interactive one also has a numeric form (the knife takes a polyline
  of cut points in mesh space) so its geometry is testable without injected
  input. The injected-input tests of `Mcp_test` cover the gesture itself.
- **D7. Polygon policy.** Operations keep n-gons where Blender does
  (dissolve, fill, merge), produce quads where a quad is the natural
  result (loop cut, inset, bevel side faces, bridge) and never introduce a
  facet with fewer than three distinct vertices: a result that would
  collapse a facet deletes it and merges its edges, with the merged edge
  keeping the larger sharpness.
- **D8. Keys follow Blender where the editor's bindings allow it**, and
  every binding goes through the Commands overrides
  (`doc/editor/input_bindings.md`): Alt+click loop select, Ctrl+Alt+click
  ring select, Ctrl+R loop cut, K knife, G G slide, M merge, X delete
  menu, Ctrl+X dissolve, I inset, Ctrl+B bevel, F fill, J connect, A / Alt+A
  select all / none, Ctrl+I invert, L select linked.

## 4. Feature catalog

Impact and effort are 1 (low) to 5 (high); score is `2 * impact - effort`,
so a feature with high impact and low effort sorts first and a large
feature needs to matter a lot to rank. Dependencies name the catalog id.

| Id | Feature | Impact | Effort | Score | Depends on |
|----|---------|-------:|-------:|------:|------------|
| M0 | Connectivity foundation: fix the fan-order early return, expose the corner-to-edge table, add the D1 walkers, carry `edge_sharpness` through `post_processing()` (D2) | 5 | 2 | 8 | - |
| M1 | Selection basics: all, none, invert, linked (L), mode conversion when switching mode, box and paint select in vertex and edge mode | 4 | 1 | 7 | M0 (linked) |
| M2 | Edge loop select, edge ring select, boundary loop select | 5 | 2 | 8 | M0 |
| M3 | Delete vertices / edges / faces; dissolve vertices and edges (dissolve faces exists as Merge Faces) | 5 | 2 | 8 | M0 |
| M4 | Merge vertices: at center, at first, at last, at cursor, collapse (per connected group), by distance on the selection | 4 | 2 | 6 | M3 |
| M5 | Loop cut: hover shows the ring, click cuts (count 1..n, even spacing), drag slides the new loop (D3, D4) | 5 | 3 | 7 | M0, M2, M6 |
| M6 | Edge slide and vertex slide transform modes (D4), with even and clamp options | 4 | 3 | 5 | M0 |
| M7 | Inset faces: region and individual, thickness and depth, live preview | 4 | 2 | 6 | M0 |
| M8 | Subdivide selected edges and faces (cuts count), edge split | 3 | 2 | 4 | M0 |
| M9 | Split (Y), rip (V), separate selection into a new mesh (P) | 3 | 2 | 4 | M3 |
| M10 | Flip selected faces, recalculate normals outside, smooth selected vertices | 2 | 1 | 3 | - |
| M11 | Screen-space snap to vertex, edge and edge midpoint for the knife and the move mode (D5) | 3 | 2 | 4 | - |
| M12 | Knife: free-hand cut polyline over faces with vertex and edge snapping, cut-through option, Enter confirms (D3, D5) | 4 | 5 | 3 | M0, M11 |
| M13 | Bevel edges and vertices: width, segments, profile, clamp overlap; live preview | 5 | 5 | 5 | M0, M2 |
| M14 | Bridge edge loops (two loops, optional cuts and twist) | 3 | 3 | 3 | M2 |
| M15 | Fill (F): n-gon fill of a closed edge loop, edge between two vertices; grid fill as a later step | 3 | 2 | 4 | M2 |
| M16 | Connect vertex path (J): cut faces along the shortest path between selected vertices | 3 | 3 | 3 | M0 |
| M17 | Proportional editing falloff for the component transform | 3 | 2 | 4 | - |
| M18 | Symmetry (mirror) editing across a chosen axis of the component transform | 3 | 3 | 3 | - |
| M19 | Triangles to quads on the selection, selection-aware triangulate | 2 | 2 | 2 | M0 |
| M20 | Shortest-path select (Ctrl+click), select similar, checker deselect | 2 | 2 | 2 | M0 |
| M21 | Extrude individual faces, extrude along a picked axis; spin and screw | 2 | 3 | 1 | - |

Two features on the user's list are covered by this table as follows: edge
loop selection is M2; "slide" is M6 with loop cut's slide step in M5; merge is
M4 (center, collapse, by distance and the other Blender merge types); knife is
M12; loop cut is M5.

## 5. Build order

Each phase ships with its unit tests (`erhe_geometry_tests`), its MCP tools
and its scripted verification (section 6), and updates the documents the
phase extends (section 7) before the next phase starts.

1. **Foundation** - M0, M1, M10. M0 first: the walker unit tests on a
   closed cube, a torus, an open grid and a grid with a triangle fan and a
   pole exercise every valence rule of D1. M1 adds the selection commands and
   extends box and paint select to vertices and edges with the CPU path
   (project every candidate, test the rectangle or disk), which is enough
   for editing-sized meshes; the compute path of
   `doc/plans/mesh_component_selection.md` replaces it later without
   changing the commands.
2. **Loop selection** - M2. Alt+click and Ctrl+Alt+click on the component
   tool; boundary loop when the picked edge is a boundary edge.
3. **Discrete topology edits** - M3, M4, M7, M8, M9. All D2 operations,
   each a `Geometry_operation` with provenance and remap, an Operations
   window button, an MCP action and a test. M3 before M4 because collapse
   merges are edge collapses with degenerate-facet cleanup (D7).
4. **Interactive cuts and slides** - M6, M5, M11, M12. M6 first (it is a
   transform mode with no topology change), then M5 (its cut is M8's edge
   subdivision along a ring, its slide is M6), then M11 and the knife.
5. **Bevel and loop tools** - M13, M14, M15, M16. Bevel is the largest
   single item; its unit tests compare vertex counts and side-face
   planarity on a cube edge, a vertex and a whole cube at several segment
   counts.
6. **Editing conveniences** - M17, M18, M19, M20, M21, in score order, each
   taken up when a user asks for it.

## 6. Verification

- `erhe_geometry_tests`: one file per operation (`test_topology_walkers.cpp`,
  `test_dissolve.cpp`, `test_merge_vertices.cpp`, `test_loop_cut.cpp`, ...)
  asserting vertex, edge and facet counts, positions, the remapped
  selection, corner texcoords across a seam and `edge_sharpness` survival,
  on the solids of section 5 phase 1.
- `scripts/mesh_modeling_verify.py`: headless editor; for every MCP action
  select components on a cube or grid, run the action, check
  `get_mesh_geometry_info` and the selection, undo and check the counts
  return, following `scripts/mcp_extrude_normal_test.py`. It joins the
  `mcp_server_tests` set (`doc/testing.md`).
- `Mcp_test` injected-input tests for the modal tools: loop cut (hover,
  click, drag, release), knife (click, click, Enter) and slide (G G, move,
  click), each asserting the resulting topology and a single undo entry.
- `scripts/geometry_edit_node_order_verify.py` gains one case per new
  primitive swap so the hierarchy order stays put (`doc/editor/operations.md`
  "Primitive swaps keep the node in place").
- Steady-state allocation: the preview paths (walkers during hover, slide
  rails, knife polyline) use persistent scratch buffers on the tool; a
  hover frame allocates nothing (`AGENTS.md` "Run-time Memory Allocation
  Discipline").

## 7. Documents this work updates

- `doc/erhe/geometry.md`: the topology walkers and the corner-to-edge table.
- `doc/editor/mesh_component_selection.md`: the selection commands, loop
  and ring select, vertex and edge box select.
- `doc/editor/transform.md`: the slide modes.
- `doc/editor/operations.md`: each new `Mesh_operation`.
- A new editor document `mesh_modeling.md` under `doc/editor/` for the modal
  tools (loop cut, knife, inset, bevel) once phase 4 lands; until then this
  plan is their description.
- `doc/agents/mcp_api_guidelines.md` and the MCP tool list for the actions
  of D6.

## 8. Reference

Blender's implementations, in a Blender source checkout under
`source/blender/`: `bmesh/operators/bmo_dissolve.cc`,
`bmo_removedoubles.cc` (merge), `bmo_inset.cc`, `bmo_bevel.cc`,
`bmo_bridge.cc`, `bmo_fill_edgeloop.cc`, `bmo_fill_grid.cc`,
`bmo_connect_pair.cc`, `bmo_subdivide_edgering.cc`;
`editors/mesh/editmesh_loopcut.cc`, `editmesh_knife.cc`,
`editmesh_preselect_edgering.cc`, `editmesh_select.cc` (loop and ring
walking in `edbm_select_loop` / `walker`), `editmesh_tools.cc` (merge
types); `editors/transform/transform_mode_edge_slide.cc` and
`transform_mode_vert_slide.cc`.
