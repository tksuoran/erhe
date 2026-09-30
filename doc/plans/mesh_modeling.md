# Mesh modeling operations

Status: proposed

This plan extends `doc/editor/mesh_component_selection.md` (component
selection and its overlay), `doc/editor/transform.md` (the component
transform with its move and extrude modes) and `doc/editor/operations.md`
(`Mesh_operation`, the in-place vertex edits and the Operations window) with
the Blender-style modeling operations the editor does not have: edge loop and
ring selection, loop cut, knife, vertex and edge slide, merge, dissolve,
inset, bevel and the smaller operations around them. Section 3 holds the
design decisions, section 4 the behaviour of each operation as Blender
defines it, translated to erhe's mesh representation, section 5 the scored
catalog and section 6 the build order.

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
  (whole-mesh vertex colocation through Geogram), `chamfer` (face chamfer
  by ratio), `triangulate`, Catmull-Clark with creases and the Conway
  operators exist.
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
  fill. Every existing topology edit builds its destination `GEO::Mesh`
  directly from the source in one pass; there is no mutable intermediate
  on which a sequence of local edits (split this edge, join these two
  facets, collapse this vertex) can be composed, which is the form every
  Blender operator in section 4 takes.
- Sliding: no edge or vertex slide; no screen-space snap to vertices or
  edges (the transform snap is grid-step only).
- Attribute propagation: `Geometry_operation::post_processing()` copies no
  edge attributes, so every operation except Catmull-Clark drops
  `edge_sharpness`.

## 3. Design decisions

- **D1. Blender is the behaviour reference, never the source.** erhe is
  MIT-licensed and Blender is GPL. Section 4 states each operation's rules
  and options as Blender's editor exposes them, so a Blender user finds the
  same behaviour; every line of erhe code is written against erhe's own
  structures (D2), and no Blender source is transcribed. Section 9 lists
  the Blender files each rule was read from, for re-checking a rule.
- **D2. An `Edit_mesh` scratch carries composed edits.**
  `erhe_geometry/edit_mesh.hpp` is a mutable polygon mesh built from a
  `Geometry`: vertices (position, provenance as weighted source vertices),
  facets (vertex list, provenance source facet, per-corner provenance as
  weighted source corners), an edge map keyed by vertex pair with per-edge
  sharpness and facet list, and per-vertex facet lists. It offers the core
  primitives every section 4 operation is composed of, each keeping the
  adjacency maps current:
  `split_edge(edge, t)`, `split_facet(facet, corner_a, corner_b)`,
  `split_facet_edgenet(facet, edges)`, `join_facets(region)`,
  `join_facet_pair(edge)`, `collapse_vertex(v)` (two-valent),
  `weld_vertices(map)`, `separate_vertex(v, edges)` (rip),
  `delete(vertices | edges | facets, context)` and
  `create_facet(vertices)`. A `Geometry_operation` subclass loads the
  scratch from its source, runs the primitives, and emits the scratch into
  the destination through the existing provenance API
  (`make_new_dst_vertex_from_src_vertex`, `add_corner_source`, ...), so
  `post_processing()` interpolates every attribute and
  `remap_component_selection()` carries the selection over unchanged.
  `post_processing()` additionally carries `edge_sharpness` to each
  destination edge whose two vertices derive from the two vertices of one
  source edge, so creases survive every operation. Undo stays the primitive
  swap `Mesh_operation` already performs. The interactive operations (D3)
  use the same scratch and emission for their one topology step.
- **D3. Interactive operations build topology once, then preview
  positions in place.** Loop cut, knife, slide, inset and bevel are modal
  gestures on the component tool. On confirm of the cut (loop cut, knife)
  or on the first real move (inset, bevel), the tool builds the result
  `Geometry` through the D2 operation, swaps it in through
  `swap_mesh_primitives()` and installs the result selection with
  `set_after_operation()`. The drag that follows moves positions in place
  through `Mesh_component_transform` exactly as extrude does. Commit queues
  one `Fork_geometry_operation` (before and after primitives) so the whole
  gesture is one undo entry; cancel restores the before primitive without
  touching the operation stack.
- **D4. Slide is a transform mode.** `Mesh_transform_mode` gains
  `edge_slide` and `vertex_slide`, next to the extrude modes, with the rail
  rules of section 4.6. Loop cut chains into edge slide for its second step.
- **D5. Preselection overlays and snapping through the existing hover.**
  Loop cut and knife draw their candidate (the ring to cut, the cut
  polyline) in `tool_render` from the content `Hover_entry` and the D1
  walkers, with the overlay colors of `Mesh_component_selection_tool`. A
  new `Screen_snap` helper on the tool projects the hovered facet's
  vertices and edges with `Viewport_scene_view::project_to_viewport` and
  reports the nearest within a pixel radius; the radius follows Blender's
  rule of shrinking with the number of candidate vertices near the cursor
  (section 4.7). The move transform mode gains the same vertex and edge
  snap as an option.
- **D6. Every operation has an MCP tool and a numeric path.** Each
  discrete operation is an MCP action with the same arguments the UI has,
  so scripts verify it headless; each interactive one also has a numeric
  form (the knife takes a polyline of cut points in mesh space, loop cut
  takes an edge, a count and a factor) so its geometry is testable without
  injected input. The injected-input tests of `Mcp_test` cover the gesture.
- **D7. Keys follow Blender**, and every binding goes through the Commands
  overrides (`doc/editor/input_bindings.md`): Alt+click loop select,
  Ctrl+Alt+click ring select, Ctrl+R loop cut, K knife, G G slide (vertex
  slide in vertex mode, edge slide otherwise), M merge menu, X delete menu,
  Ctrl+X dissolve, I inset, Ctrl+B bevel, F fill, J connect, A select all,
  Alt+A select none, Ctrl+I invert, L select linked under the cursor,
  Ctrl+L select linked from the selection. Modal keys per tool are in
  section 4.

## 4. Operation behaviour

Each subsection states the rules the erhe implementation follows. "Valence"
is the number of non-wire edges at a vertex; "manifold edge" has exactly
two facets, "boundary edge" one. erhe meshes have no wire edges, so the wire
cases of Blender's walkers are absent here.

### 4.1 Walkers (M0)

`erhe_geometry/topology.hpp` provides these walks over a `Geometry` with
connectivity, edges and facet adjacency built, each filling a caller-owned
buffer in end-to-end order:

- **Edge loop** from a seed edge. Walk from one end vertex, then restart
  from the far end in the other direction so an open loop is ordered end
  to end and a closed loop ends where it started.
  - Interior seed (the seed edge is manifold): at the far vertex `v` with
    valence `n`, continue only when `n` is 4 or 2. Rotate around `v`
    through `n / 2` facets: in the current facet take the other edge at
    `v`, cross it to the adjacent facet; every edge crossed is manifold,
    otherwise stop. Facet size plays no part, so triangles and n-gons at a
    valence-4 vertex still yield the opposite edge. Stop at a vertex of any
    other valence, at an already visited edge, and at a crease delimit: a
    crease (`edge_sharpness` above 0) edge continues only onto crease
    edges, and a plain edge stops when any edge at `v` is a crease. So on
    a cube, whose vertices all have valence 3, the loop is the seed edge
    alone.
  - Hub seed: an interior seed with an end vertex of valence 3 and exactly
    three facets takes the largest facet of the seed edge as its hub when
    that facet has more than four corners. With a hub, every step continues
    only at a vertex of valence 3, onto the edge from it to its other
    neighbour in the hub facet, and that edge is not a boundary edge; the
    hub is fixed by the seed for both directions. A pentagon cap on a prism
    is a hub, so the loop from a cap edge is the cap's five edges.
  - Boundary seed: follow boundary edges. Stop at a convex corner (valence
    2, "outer corners" delimit, the default) and at a non-manifold edge;
    a vertex of valence 3 or more continues onto the next boundary edge
    reached by rotating through manifold edges.
  - Non-manifold seed (three or more facets): continue onto the
    non-manifold edge with the same facet count reached by rotating around
    `v` through manifold edges; stop at a junction (two candidates).
- **Edge ring** from a seed edge, quads only: the next edge is the
  opposite edge (`local corner + 2`) of the facet across the current
  edge; when that facet is not a quad, the opposite edge of the current
  facet. A step is valid only onto an edge with one or two facets, in a
  quad, not yet visited. Both directions, as for the loop. A ring stops at
  triangles, n-gons, boundaries and non-manifold edges; a closed ring ends
  on the visited set.
- **Face loop** from a seed edge, quads only: the next facet is the facet
  across the current edge; when that is rejected, cross the opposite edge
  of the current facet instead. A facet may be visited once per crossing
  direction, so self-crossing face loops work.
- **Boundary loop**: flood over boundary edges through their vertices.
- **Connected region** (select linked): flood from seed vertices across
  edges, with an optional delimit that refuses to cross a crease edge, an
  edge between facets of different materials, or an edge whose two facets
  disagree in winding.

### 4.2 Selection commands (M1, M2)

- **Select all / none / invert / linked.** Invert acts on the elements of
  the current mode, then flushes (below). Select linked floods with the
  connected-region walk from every selected element (Ctrl+L) or from the
  element under the cursor (L); face mode uses a facet flood across
  non-delimit edges.
- **Flush and mode switch.** Selecting an edge selects its two vertices;
  selecting a facet selects its edges and vertices. Switching to vertex
  mode keeps the vertices and derives edges (both vertices selected) and
  facets (all vertices selected). Switching to edge mode keeps the edges
  and derives vertices and facets from them. Switching to face mode keeps
  the facets and derives the rest. Ctrl+click on a mode button expands
  instead: any element touching the selection is selected going up (vertex
  to edge to face), and only elements completely surrounded by the
  selection survive going down.
- **Loop and ring select.** The picked edge is always the nearest edge to
  the cursor, in every mode. Face mode runs the face loop walk for both
  Alt+click and Ctrl+Alt+click. Edge and vertex mode run the edge loop
  (Alt) or edge ring (Ctrl+Alt) walk and select the yielded edges, which
  selects their vertices. A plain click replaces the selection, Shift
  extends, Shift on an already selected loop deselects it. Clicking a
  boundary edge whose loop is already fully selected switches to the whole
  boundary loop; clicking again returns to the loop.
- **Box and paint select in vertex and edge mode** project every vertex or
  edge of the hovered meshes to the viewport and test the rectangle or
  disk on the CPU, with the same modifiers as face mode. The compute path
  of `doc/plans/mesh_component_selection.md` replaces the projection later
  without changing the commands.

### 4.3 Delete and dissolve (M3)

- **Delete** (X menu): vertices (their edges and facets go with them),
  edges (their facets go, endpoints that become loose go), faces (facets,
  then edges and vertices no surviving facet uses), only edges and faces,
  only faces.
- **Dissolve faces**: `merge_faces` as it is; with "dissolve vertices"
  on, vertices that became two-valent are collapsed afterwards. A region
  whose join fails (non-manifold, invalid boundary) is kept unchanged.
- **Dissolve edges** (options: dissolve vertices on, preserve quads on,
  angle threshold 180 degrees, face split off): for each selected manifold
  edge join its facet pair. Then, with dissolve vertices on, collapse each
  end vertex that became two-valent, skipping a vertex that touches an
  unselected facet while having more than one selected edge, and skipping
  the corners of a triangle pair when preserve quads is on (dissolving the
  edge between two triangles yields a quad). The angle test between the
  two remaining edges is measured for every candidate before any collapse.
- **Dissolve vertices** (options: face split off, boundary tear off): join
  every facet pair around each selected vertex of valence three or more,
  collapse the remaining two-valent selected vertices, delete the ones left
  loose. Face split first separates the vertex's corners along the two
  neighbouring corners so the surrounding facets stay planar; boundary tear
  separates a boundary vertex into one vertex per facet.
- **Limited dissolve** (angle limit 5 degrees, delimit by winding flip;
  crease and material delimits as options): a heap of manifold edges keyed
  by dihedral angle, joining pairs below the limit and re-scoring the
  joined facet's edges; then a second heap collapsing two-valent vertices
  whose edge angle is below the limit and whose collapse keeps every
  adjacent corner convex.

### 4.4 Merge (M4)

- **Types**: at center (default), at cursor, collapse (per connected
  island of selected edges: every vertex moves to the island mean and maps
  to one survivor), at first, at last (the survivor is the first or last
  vertex of the selection history, position kept). At center and cursor,
  the first selected vertex survives and moves to the target; vertex
  attributes average over the cluster. Corner attributes: the "UVs" option
  off leaves each kept corner's attributes as they were; on, center and
  cursor write the midpoint of the per-component extent of the selected
  vertices' corners to every corner, and collapse snaps each UV island to
  its extent midpoint.
- **By distance** (threshold 1e-4, centroid on, include unselected off):
  cluster the selected vertices with a KD-tree (Geogram's, as `weld` does);
  a pair keeps the lower index, a cluster of three or more keeps the vertex
  nearest its centroid; centroid on moves the survivor to the cluster
  mean; include unselected lets an unselected vertex be the survivor of a
  selected one.
- **Weld core** (`Edit_mesh::weld_vertices`, shared by every merge type,
  the two-valent collapse and bridge's merge): first, in every facet
  containing two vertices that map to the same survivor and are not
  adjacent in that facet, split the facet between them and recurse, so
  every facet only holds adjacent merge pairs. Then map every edge: an edge
  whose endpoints map to one vertex disappears, an edge that maps onto an
  existing edge is replaced by it. Then rebuild every facet touching a
  merged vertex from its mapped corners, dropping consecutive duplicates;
  a facet left with fewer than three vertices, or one where a vertex
  repeats non-consecutively, is deleted; a facet identical to an existing
  facet is dropped in favour of the existing one; each kept corner keeps
  the attributes of the original corner at its position.

### 4.5 Loop cut (M5) and subdivide (M8)

- **Hover**: the nearest edge under the cursor within the pick distance.
  The preview walks the edge ring (4.1) from it and, for each consecutive
  pair of ring edges, pairs their endpoints through the facet that contains
  both so the preview lines do not cross, then draws `cuts` segments at
  `i / (cuts + 1)` between the paired edges; when the last and first ring
  edge share a quad, one more set of segments closes the ring. A seed edge
  with no quad facet previews only its own cut points.
- **Modal keys**: wheel, PageUp / PageDown or numpad plus / minus change
  the cut count (minimum 1, maximum 500); Alt with the same keys changes
  smoothness by 0.05; typed numbers set the count; left click or Enter
  confirms, right click or Escape cancels.
- **Cut**: select the ring's edges and run subdivide (below) with the cut
  count, grid fill on and only the new inner edges selected afterwards. A
  seed with no quad facet subdivides that one edge and selects nothing.
  Then chain into edge slide (4.6) with the new loops as the selection;
  releasing the button in the tool form confirms the slide at once.
- **Subdivide edges** (M8; cuts 1, smoothness 0): split every selected
  edge `cuts` times, the k-th split at `(k + 1) / (cuts + 1)` of the
  original edge. Fill each facet by the count of its split edges: a quad
  with two opposite split edges connects cut `j` on one to cut `cuts - 1 -
  j` on the other; a quad with two adjacent split edges connects mirrored
  cuts plus one corner-to-corner edge; three split edges connect the two
  opposite ones with the middle switched; four split edges build a
  `(cuts + 2)` square grid (grid fill); a triangle with one split edge fans
  the cuts to the opposite vertex, with three a triangular lattice; an
  n-gon with two split edges connects the two runs of new vertices
  pairwise, skipping a pair that already shares a facet; any other count
  leaves the new vertices on the facet boundary. Smoothness places each new
  vertex on the arc built from the two endpoint normals, blended with the
  straight position by the smoothness, strongest at the edge middle.

### 4.6 Edge slide and vertex slide (M6)

- **Edge slide** builds one entry per selected vertex. Every selected
  vertex has one or two selected edges and every selected edge is manifold
  or boundary; otherwise the mode refuses to start. Vertices chain into
  loops (open loops walk from an end, closed loops from anywhere). For each
  loop edge and each of its facets, a vertex's rail on that side is the
  other edge of the facet at that vertex; the two sides stay consistent
  along the loop (same facet or same rail target as the previous edge,
  else the fan of facets reaching a side already assigned, else the empty
  side). An interior vertex of valence 2 has no rail and slides across the
  facet to the opposite corner (quad) or to the far side of the facet along
  the facet's in-plane direction (n-gon). Where the loop turns inside one
  facet, the destination is the intersection of the two rails' lines when
  it lies inside the cone the rails span, else their midpoint. A boundary
  side has a zero rail.
  - Input: the slide direction on screen is the projected rail pair of the
    vertex nearest the cursor; loops whose projected direction opposes it
    swap sides so parallel loops move together. The factor is the signed
    projection of the drag onto that direction, negative toward the second
    side, clamped to [-1, 1] unless clamp is off (C or Alt), in which case
    the last clamped side is kept and extrapolated. Even (E) moves every
    vertex by the same distance along its rail pair; Flipped (F) measures
    that distance from the other end. Numeric input replaces the factor.
- **Vertex slide**: each selected vertex slides toward one of its
  neighbours. The neighbour is the one whose direction has the largest dot
  product with the drag direction, re-picked on every mouse move while
  clamped and frozen when clamp is off; the factor is 0 at the drag start
  and 1 when the cursor reaches the projected neighbour of the active
  (nearest to cursor) vertex, clamped to [0, 1]. Even and Flipped as above,
  measured against the active vertex's edge length.
- Corner attributes of the facets around a slid vertex are re-interpolated
  from the facet's pre-slide corners at the vertex's new position
  (Blender's "correct UVs", on by default), which is what keeps texture
  coordinates from stretching under a slide.

### 4.7 Knife (M12)

- **Modal keys**: left click adds a cut point (drag adds points
  continuously when cut-through is off), double click closes the cut,
  right click ends the current polyline, Enter or Space confirms, Escape
  cancels, Ctrl+Z removes the last cut, Shift held snaps to edge midpoints,
  Ctrl held ignores snapping, C toggles cut through, A cycles angle
  constraint (screen, relative to an edge, off), X / Y / Z lock an axis.
- **Point under the cursor**: ray-cast the hovered facet; within it, the
  nearest edge to the cursor ray if its screen distance is within the snap
  radius; then that edge's endpoint if within the (slightly smaller) vertex
  radius; otherwise the point on the facet. The base radius is 10 pixels
  scaled by the UI scale, divided by half the number of cut vertices of the
  facet within twice that radius of the cursor, so dense regions snap
  tighter. With nothing hit the point lies on the view plane through the
  previous point.
- **Segment to cuts**: the cut plane contains the two points and the eye
  (perspective) or the view direction (orthographic). Facets crossing the
  plane are found through the raytrace BVH. Hits are: the two end points;
  vertices whose projection lies on the screen segment within 0.5 px;
  edges whose projection crosses the screen segment (not within tolerance
  of an endpoint), the 3D point being the edge's intersection with the cut
  plane; facet interiors only at the segment ends when the end is not
  snapped. Without cut-through, a hit is kept only when a ray from it to
  the eye reaches it unoccluded (facets containing the hit element do not
  occlude). Hits sort along the segment and duplicates merge. Per facet,
  consecutive hits become cut edges of that facet unless both are the same
  element, both are original vertices already joined by an edge, either
  lies outside the facet, or the midpoint of two vertex hits lies outside
  the facet (concave facets). A hit on an edge splits the edge (scratch
  only); a hit inside a facet creates a floating vertex.
- **Apply on confirm**: split each original edge at its cut vertices in
  distance order, then split each facet along its edge network
  (`split_facet_edgenet`): a network with a floating island (a closed cut
  inside a facet) is joined to the boundary with two non-crossing
  connecting edges first, so no ring facet repeats a vertex; a dangling
  cut edge no facet uses afterwards is removed. Nothing touches
  the mesh before confirm; undo during the gesture only edits the scratch.
- **Knife project** (later): the boundary edges of another selected mesh,
  projected to the view, replayed as cuts with snapping off; the facets
  inside the projected polygons are selected.

### 4.8 Inset (M7)

Options: boundary on, even offset on, relative offset off, edge rail off,
thickness, depth, outset off, individual off, interpolate on.

- **Region**: the selected region is detached along its boundary edges and
  moved inward; the rim facets fill between the old boundary edges, which
  stay in place, and the moved copies. A boundary edge lies between a
  selected and an unselected facet, or is a mesh boundary edge of a
  selected facet when boundary is on. Each boundary edge stores the in-face
  tangent of its selected facet. A region vertex on two boundary edges
  moves along the normalized sum of the two tangents, scaled by
  `1 / cos(half angle)` with even offset, by the mean edge length with
  relative offset, or toward the shared neighbour vertex's original
  position with edge rail; a region vertex on one boundary edge (the
  region touches the mesh boundary) slides along its unsplit neighbouring
  edge so the inset stays flush. Rim facets are quads (a triangle where an
  endpoint was not split); their inner corners copy the region corners,
  their outer corners copy the pre-move corners of the region facet.
  Interpolate re-samples the moved region facets' corner attributes from
  their pre-move shape. Outset inverts the region. Depth moves the region
  along the averaged vertex normals, scaled by the shell factor.
- **Individual**: each selected facet gets its own vertices, moved along
  the normalized sum of the two adjacent edge tangents with the same even
  and relative scaling, a rim quad per edge and depth along the facet
  normal.

### 4.9 Bevel (M13)

Options: offset type (offset, width, depth, percent, absolute), amount,
segments 1, profile 0.5, affect edges or vertices, clamp overlap off, loop
slide on, material index, miter outer (sharp, patch, arc) and inner (sharp,
arc), spread, harden normals off.

- **First version (M13a)**: edges only, one segment, offset type
  `offset` and `width` (`width / (2 sin(half dihedral))`), loop slide. Per
  affected vertex: sort its edges counter-clockwise, for each beveled edge
  offset its line into both adjacent facets, intersect consecutive offset
  lines within their shared facet (or slide along an unbeveled middle edge
  with loop slide, or offset in the facet plane for a single beveled
  edge); the boundary vertices so found form one polygon replacing the
  vertex (two of them: no polygon), one quad per beveled edge between the
  matching boundary vertices at its ends, and every original facet touching
  a beveled vertex is rebuilt with its corner replaced by the chain of
  boundary vertices. Corner attributes come from the adjacent original
  facets.
- **Second version (M13b)**: segments and profile (a superellipse whose
  exponent derives from the profile value; 0.5 is circular), the vertex
  patch filled by the adjacent-pattern grid subdivision for three or more
  beveled edges, offset adjustment so widths match at both ends of each
  edge, clamp overlap, vertex bevel, miters, seam and crease propagation.
  Blender's implementation of the full feature is about 8,500 lines; M13a
  is the roughly one-eighth of it that the single-segment edge case needs.

### 4.10 Bridge, fill, connect (M14, M15, M16)

- **Bridge edge loops** (type single / closed / pairs, merge off, merge
  factor 0.5, twist offset 0, cuts 0): selected facets are deleted first
  and their region boundaries become the loops. Two open loops: flip the
  second when its end-to-end direction opposes the first's; flip when the
  loop normals oppose along the vector between loop centres; winding is
  voted from the facets adjacent to the loops. Unequal counts: the shorter
  loop repeats entries (doubling until past half the target, then spread
  evenly), consecutive repeats yield triangles, and the triangulated strip
  is beautified by rotating only edges between the two loops. Closed
  loops: the rotation of the second loop minimizing the sum of paired
  distances, plus the twist offset. One quad per pair, corner attributes
  copied from the adjacent facets' corners on the same loop edge. Merge
  welds each pair at the factor (4.4 weld core). Cuts subdivide the bridge
  as an edge ring (4.5).
- **Fill (F)**, in order, stopping at the first case that creates
  anything: exactly two vertices make an edge; one free vertex plus a chain
  connects the free vertex to both chain ends; selected edges forming
  closed cycles each become an n-gon (one or two open chains are closed
  first, choosing the more planar closure to avoid a bow-tie), otherwise
  an edge net is filled by growing a breadth-first front from both ends of
  each boundary edge until they meet in a cycle not already a facet;
  selected facets dissolve; three or more vertices alone are sorted
  radially in their fitted plane and become one n-gon. New facet
  attributes come from a neighbouring facet.
- **Grid fill** (span, offset, simple interpolation off): a single closed
  loop of even length splits at its sharpest corner (rotated by offset)
  and the next sharpest, or at quarter points when the corners are alike,
  into two rails and two arcs; unequal arcs are expanded by splitting
  edges that are collapsed again afterwards; interior points come from
  each boundary row mapped through the triangle frame of its side and
  blended by row fraction (simple: a four-side weighted blend); winding
  votes from the boundary facets.
- **Connect vertex path (J)**: two selected vertices sharing a facet, or
  more vertices, split each facet between consecutive selected corners
  that are not adjacent (vertices inside a contiguous selected run are
  skipped), dropping splits that leave the facet or cross each other. Two
  vertices not sharing a facet: a cutting plane through both containing
  their averaged normals; a best-first search keyed by accumulated path
  length crosses facets at the edges the plane intersects, each visited at
  most once, until the target is reached; the crossed edges are split at
  the plane and the resulting vertices connected as above.

## 5. Feature catalog

Impact and effort are 1 (low) to 5 (high); score is `2 * impact - effort`,
so a feature with high impact and low effort sorts first and a large
feature needs to matter a lot to rank. Dependencies name the catalog id;
"4.n" names the section stating the behaviour.

| Id | Feature | Impact | Effort | Score | Depends on |
|----|---------|-------:|-------:|------:|------------|
| M0 | Foundation: fix the fan-order early return, `topology.hpp` walkers (4.1), `Edit_mesh` with its primitives and emission, `edge_sharpness` through `post_processing()` (D2) | 5 | 3 | 7 | - |
| M1 | Selection basics: all, none, invert, linked, flush and mode conversion, box and paint select in vertex and edge mode (4.2) | 4 | 1 | 7 | M0 (linked) |
| M2 | Edge loop, edge ring, face loop and boundary loop select (4.2) | 5 | 2 | 8 | M0 |
| M3 | Delete contexts; dissolve edges, vertices, faces-with-vertices; limited dissolve (4.3) | 5 | 2 | 8 | M0 |
| M4 | Merge: center, cursor, collapse, first, last, by distance, with the weld core (4.4) | 4 | 2 | 6 | M0 |
| M8 | Subdivide selected edges and faces with patterns and smoothness (4.5) | 3 | 2 | 4 | M0 |
| M6 | Edge slide and vertex slide transform modes (4.6) | 4 | 3 | 5 | M0 |
| M5 | Loop cut with ring preview, cut count, chained edge slide (4.5) | 5 | 2 | 8 | M2, M6, M8 |
| M7 | Inset region and individual with every option (4.8) | 4 | 2 | 6 | M0 |
| M9 | Split (Y), rip (V, `separate_vertex`), separate selection into a new mesh (P) | 3 | 2 | 4 | M0 |
| M10 | Flip selected facets, recalculate normals outside, smooth selected vertices | 2 | 1 | 3 | - |
| M11 | `Screen_snap`: vertex, edge and midpoint snap for the knife and the move mode (D5) | 3 | 2 | 4 | - |
| M12 | Knife (4.7), without knife project | 4 | 5 | 3 | M0, M11 |
| M13a | Bevel first version: edges, one segment, offset and width (4.9) | 5 | 4 | 6 | M0, M2 |
| M13b | Bevel second version: segments, profile, vertex bevel, clamp, miters (4.9) | 3 | 5 | 1 | M13a |
| M14 | Bridge edge loops (4.10) | 3 | 3 | 3 | M2, M4, M8 |
| M15 | Fill (F) with the contextual order, grid fill as a later step (4.10) | 3 | 2 | 4 | M0 |
| M16 | Connect vertex path (4.10) | 3 | 3 | 3 | M0 |
| M17 | Proportional editing falloff for the component transform | 3 | 2 | 4 | - |
| M18 | Symmetry (mirror) editing across a chosen axis of the component transform | 3 | 3 | 3 | - |
| M19 | Triangles to quads on the selection, selection-aware triangulate | 2 | 2 | 2 | M0 |
| M20 | Shortest-path select (Ctrl+click), select similar, checker deselect | 2 | 2 | 2 | M0 |
| M21 | Extrude individual faces, extrude along a picked axis; spin and screw | 2 | 3 | 1 | - |
| M22 | Knife project (4.7) and bisect (plane cut with fill, clear inner / outer) | 2 | 3 | 1 | M12 |

The user's list maps as follows: edge loop selection is M2; loop cut is M5;
knife is M12; slide is M6; merge is M4.

## 6. Build order

Each phase ships with its unit tests (`erhe_geometry_tests`), its MCP tools
and its scripted verification (section 7), and updates the documents the
phase extends (section 8) before the next phase starts.

1. **Foundation** - M0, M1, M10. M0 first: the walker unit tests on a
   closed cube, a torus, an open grid, a grid with a triangle fan and a
   pole, and a mesh with a crease line exercise every rule of 4.1; the
   `Edit_mesh` primitive tests cover each primitive on a quad grid with the
   provenance checked through emission.
2. **Loop selection** - M2.
3. **Discrete topology edits** - M3, M4, M8, M7, M9. All D2 operations,
   each an `Edit_mesh`-based `Geometry_operation`, an Operations window
   button, an MCP action and a test.
4. **Slides and loop cut** - M6, then M5 (its cut is M8 on the M2 ring,
   its slide is M6).
5. **Knife** - M11, then M12.
6. **Bevel and loop tools** - M13a, M15, M14, M16, then M13b.
7. **Editing conveniences** - M17, M18, M19, M20, M21, M22, in score
   order, each taken up when a user asks for it.

## 7. Verification

- `erhe_geometry_tests`: one file per operation (`test_topology_walkers.cpp`,
  `test_edit_mesh.cpp`, `test_dissolve.cpp`, `test_merge_vertices.cpp`,
  `test_loop_cut.cpp`, ...) asserting vertex, edge and facet counts,
  positions, the remapped selection, corner texcoords across a seam and
  `edge_sharpness` survival, on the solids of section 6 phase 1. Each
  section 4 rule that names a degenerate case (a non-adjacent merge pair
  in one facet, a concave facet under the knife, a ring ending at a
  triangle, a slide loop turning inside one facet) has a test of that case.
- `scripts/mesh_modeling_verify.py`: headless editor; for every MCP action
  select components on a cube or grid, run the action, check
  `get_mesh_geometry_info` and the selection, undo and check the counts
  return, following `scripts/mcp_extrude_normal_test.py`. It joins the
  `mcp_server_tests` set (`doc/testing.md`).
- `Mcp_test` injected-input tests for the modal tools: loop cut (hover,
  wheel, click, drag, release), knife (click, click, Enter), edge slide
  (G G, move, click) and vertex slide, each asserting the resulting
  topology and a single undo entry.
- `scripts/geometry_edit_node_order_verify.py` gains one case per new
  primitive swap so the hierarchy order stays put (`doc/editor/operations.md`
  "Primitive swaps keep the node in place").
- Steady-state allocation: the preview paths (walkers during hover, slide
  rails, knife polyline and hits) use persistent scratch buffers on the
  tool; a hover frame allocates nothing (`AGENTS.md` "Run-time Memory
  Allocation Discipline"). `Edit_mesh` itself runs on the operation's
  worker and may allocate.

## 8. Documents this work updates

- `doc/erhe/geometry.md`: the topology walkers, `Edit_mesh` and its
  primitives, the corner-to-edge table.
- `doc/editor/mesh_component_selection.md`: the selection commands, flush
  rules, loop and ring select, vertex and edge box select.
- `doc/editor/transform.md`: the slide modes and the snap option.
- `doc/editor/operations.md`: each new `Mesh_operation`.
- A new editor document `mesh_modeling.md` under `doc/editor/` for the modal
  tools (loop cut, knife, inset, bevel) once phase 4 lands; until then this
  plan is their description.
- `doc/agents/mcp_api_guidelines.md` and the MCP tool list for the actions
  of D6.

## 9. Reference

The rules of section 4 were read from these files of a Blender source
checkout, under `source/blender/` (behaviour reference only, D1):

| Section | Files |
|---------|-------|
| 4.1 walkers | `bmesh/intern/bmesh_walkers_impl.cc` (edge loop, edge ring, face loop, boundary, non-manifold loop, shell walkers) |
| 4.2 selection | `editors/mesh/editmesh_select.cc` (loop and ring pick, select linked delimits, select all, mode set and convert) |
| 4.3 dissolve | `bmesh/operators/bmo_dissolve.cc`, `bmesh/tools/bmesh_decimate_dissolve.cc`, `bmesh/intern/bmesh_delete.cc`, `bmesh/intern/bmesh_mods.cc` (join pair, collapse, dissolve, edge split, face split), `bmesh/intern/bmesh_core.cc` (faces join, vertex separate) |
| 4.4 merge | `editors/mesh/editmesh_tools.cc` (merge types), `bmesh/operators/bmo_removedoubles.cc` (weld, find doubles, point merge, collapse) |
| 4.5 loop cut | `editors/mesh/editmesh_loopcut.cc`, `editors/mesh/editmesh_preselect_edgering.cc`, `bmesh/operators/bmo_subdivide.cc` |
| 4.6 slide | `editors/transform/transform_convert_mesh.cc` (slide data), `transform_mode_edge_slide.cc`, `transform_mode_vert_slide.cc` |
| 4.7 knife | `editors/mesh/editmesh_knife.cc`, `editmesh_knife_project.cc`, `editmesh_bisect.cc`, `bmesh/tools/bmesh_bisect_plane.cc` |
| 4.8 inset | `bmesh/operators/bmo_inset.cc`, `editors/mesh/editmesh_inset.cc` |
| 4.9 bevel | `bmesh/tools/bmesh_bevel.cc`, `editors/mesh/editmesh_bevel.cc` |
| 4.10 bridge, fill, connect | `bmesh/operators/bmo_bridge.cc`, `bmesh/intern/bmesh_edgeloop.cc`, `bmo_create.cc`, `bmo_edgenet.cc`, `bmesh/tools/bmesh_edgenet.cc`, `bmo_fill_edgeloop.cc`, `bmo_fill_grid.cc`, `bmo_connect.cc`, `bmo_connect_pair.cc` |
| keys | `scripts/presets/keyconfig/keymap_data/blender_default.py` |
