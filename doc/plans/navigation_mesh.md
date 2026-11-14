# Navigation mesh: outstanding work

Status: in progress

This plan extends [../editor/navigation.md](../editor/navigation.md), which
describes the landed subsystem: the navigation mesh of a scene built with
Recast Navigation, the seed nodes, the reachable area, the manual build job,
the Export stage, the item in the editor asset and the visualization. What
remains is the consumer (the triangle classification, a separate plan still
to be written), the unverified paths and the follow-ups below.

## Decisions that govern the remaining work

The user's rulings (2026-11-10):

- U1: a seed is a node whose position marks a place the viewer can stand;
  the reachable area is the polygons reachable from the seeds. The input
  geometry is every static mesh, with no per-node selection.
- U2: the settings are the `navigation` struct of the scene's export
  settings.
- U3: a scene without seeds gets no navigation mesh, and so no triangle
  processing.
- U4: the navigation mesh and the triangle processing both run before the
  bake to world space, on the source meshes.
- U5: the editor calls Recast and Detour directly (`ERHE_USE_RECAST`,
  `ERHE_USE_DETOUR`; desktop builds have both, Android builds neither).
- U6: Detour on Android / Quest, and with it the navigation mesh in the
  prepared asset, is future work.
- U7: the triangle processing keeps both sets of meshes, the ones before
  and the ones after it, as one more dimension beside the LOD levels: the
  processed set is stored in the editor asset, the user chooses in the UI
  which set is shown, and neither an Export nor the manual pass changes the
  scene's source meshes.

## Triangle classification from walkable sample points

A plan to write (it needs triangle visibility marking, which this
branch does not have). The user's
outline: sample points where the camera can be, from the reachable area and
outside every mesh; per sample point a cubemap of keep marks through the id
render; an optional step that also keeps the unmarked neighbours of kept
triangles; an optional step that removes the unmarked triangles; each step
also runnable by hand; in an Export it runs after the navigation stage and
before the bake to world space (U4).

What it reads from the landed subsystem:

- `Navigation_mesh`: the reachable polygons' detail triangles with their
  areas, for area-weighted sample points, in scene root space. Without a
  current navigation mesh the pass does not run (U3).
- `agent_height` and `agent_radius` of the navigation settings: the bounds
  of the eye positions above a walkable point and its distance from walls.
- The sample point free test of the PVS bake (`Pvs_baker`,
  `doc/editor/renderers.md`), for "not inside any mesh".
- The six-face `Scene_id_capture` loop of the PVS `sampled` method.
- U7 is its to design: the processed set as stored items of the editor
  asset, the choice of the shown set (the viewport toolbar combo of
  `doc/editor/world_space_bake.md` "Two forms" is the precedent), which set
  the world-space bake, the PVS bake and the Scene item read, and how the
  set follows edits of the source meshes.

## Open

- The build record of an installed navigation mesh holds the render shapes
  it read, so a shape an edit replaced stays in memory until the next build
  or Clear. A content hash per shape (which the saved item already uses)
  can replace the held shapes.
- The `navigation_seed` flag is written into the prepared Scene item, so
  marking a seed changes the Scene key. Leave the flag out of the Scene
  item, or keep it for U6.
- The hand-computed reachable area of `scripts/navigation_verify.py` counts
  the floor's outer border as a Recast ledge (agent radius plus one cell);
  confirm that reading against Recast's ledge filter.
- Recast sees surfaces only: a closed hollow mesh taller than the agent
  height gets walkable polygons inside it. They are unreachable from
  outside; the sample points of the triangle classification must stay on
  reachable polygons.
- A second Bistro build of the same input measured 0.39 s and 1.20 s;
  find what varies.

## Not verified

- The Clang and MSVC builds, the Android build (both options forced off
  there), macOS.
- Interactive use: the Debug Visualization "Navigation Mesh" combo, the
  scene Properties "Navigation" rows and its four seed buttons, undo of
  "Add Seed At Camera", the headset view.
- The geometry-normative (derived triangles) input path in a script.
- `get_navigation_status` `pending` during an Export's navigation stage.

## Later

- A per-node "not walkable" mark and Recast area types (stairs, water).
- Off-mesh connections (ladders, jumps) so the flood crosses them.
- Detour on Android / Quest (`ERHE_USE_DETOUR` on there) and the navigation
  mesh item named by the Scene item and copied into the prepared asset, for
  run-time queries (U6).
- Rebuilding only the tiles a geometry change touches.
