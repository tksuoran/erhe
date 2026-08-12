# Fix plan: SDF Mesh graph product is not pickable (missing raytrace)

RESOLVED 2026-08-12, commit 3b6dd9af. Root cause was none of H1-H4 and not
SDF-specific: Mesh::update_rt_primitives() rebuilt Raytrace_primitive
instances at the identity transform and uncommitted; only a node MOVE
(handle_node_transform_update) pushed the world transform in. Every
primitive swap on an already-placed node (geometry graph re-bakes,
initial bind of a placed node) therefore left raytrace hits at the
origin - the mesh rendered but picking missed it until the node moved.
Fix: update_rt_primitives() now calls handle_node_transform_update()
after rebuilding. The investigation below is kept for the record; the
raycast + closest_point + probe-at-origin technique isolated it
(render surface correct at world position, raytrace hit present but at
origin-local coordinates).

Reported 2026-08-12: a mesh generated through the SDF node chain
(sdf_sphere -> ... -> sdf_mesh -> output) renders in the editor but hover /
click picking misses it, i.e. it behaves as if it has no raytrace shape.

What reading the code says should happen (so the bug is not obvious):

- Geometry_output_node bake (geometry_output_node.cpp:186-198) wraps the
  final geometry in a Primitive, builds the renderable mesh AND calls
  primitive->make_raytrace(); a failure logs
  "failed to build raytrace shape".
- Graph_mesh::apply products path (geometry_graph_mesh.cpp:224-233) swaps
  primitives inside a begin/end_mesh_rt_update bracket precisely so the new
  raytrace instances attach to the scene's raytrace world.
- The ghost/template companion mesh deliberately has NO raytrace and no id
  flag (not pickable by design).

## Step 0 -- reproduce headlessly and isolate the variable

Editor from build_vs2026_vulkan_openvdb (Release), driven over MCP
(127.0.0.1:8080). Two graphs in one scene, both brought into the scene the
same way the user does it (discover the exact path first: reference the
Graph_mesh asset into the scene / instantiate from the content library -- in
the Phase 3 verification session has_bake was true but no scene node
existed, so the scene-instantiation step is part of the repro):

- A: box -> output           (non-SDF control)
- B: sdf_sphere -> sdf_mesh -> output

Probe both with the `raycast` MCP tool (closest raytrace-world hit) and
`pick_at` (full hover pipeline, call twice for the async id merge).

Discriminating outcome:
- B misses but A hits  -> SDF-geometry-specific raytrace failure (H1).
- Both miss            -> graph-mesh bake/apply or scene-instantiation
                          path never attaches raytrace instances (H2/H4).
- Both hit             -> user-workflow-specific (ghost vs display mesh,
                          stale build, flags) (H3); reproduce closer to the
                          user's exact steps before proceeding.

## Step 1 -- instrument (verification-driven; strip before commit)

- grep the editor log for "failed to build raytrace shape" /
  "failed to build renderable mesh" first -- H1 may confirm from the
  existing warn alone.
- If needed, add temporary trace: after bake log
  primitive->get_shape()->has_raytrace_triangles() / has_real_raytrace();
  in the rt-update bracket log how many raytrace instances get attached.

## Hypotheses, ranked

- H1: Primitive_raytrace build fails or builds empty for volumeToMesh
  output (quad-dominant, possibly degenerate fan triangles or an
  element-mappings count mismatch) -- the warn branch fires or the BVH is
  empty. Fix candidates: sanitize / triangulate the render geometry before
  make_raytrace, or fix the quad path in Primitive_raytrace.
- H2: the scene-instantiation path for Graph_mesh products (reference
  asset into scene / adopt node) attaches the mesh without the rt-update
  bracket, so instances never join the raytrace world -- would affect ALL
  graph mesh products, SDF is just where it was noticed. Fix: bracket +
  Mesh::update_rt_primitives on that path.
- H3: the observed mesh is not the display product (ghost companion has no
  raytrace by design; node previews have none either), or the mesh lacks
  the id flag / raytrace mask bit on that path.
- H4: deferred-raytrace variant: a proxy or pending real raytrace is never
  committed (prepare_real_raytrace without commit_real_raytrace) on the
  graph-mesh path.

## Step 2 -- fix at the root

Apply the fix matching the confirmed hypothesis (candidates above). Keep it
on the erhe_voxel / geometry_graph side if H1 (do not weaken
Primitive_raytrace generally); keep it on the graph-mesh apply path if H2/H4
(one bracket, no per-node changes).

## Step 3 -- verify before/after via MCP

- raycast + pick_at hit both A and B products, with correct node ids and
  plausible hit normals/distances.
- Regression: an ordinary scene mesh (brush-placed cube) still picks; the
  ghost companion still does NOT pick (its no-raytrace behavior is by
  design).
- Remove all temporary instrumentation, rebuild, rerun the probes.

## Step 4 -- close out

- Commit(s) per split convention (fix separate from any drive-by cleanup),
  update doc/openvdb-integration-plan.md Phase 3 notes + memory with the
  root cause, and note the lesson if it generalizes (e.g. "volumeToMesh
  output needs X before raytrace").
