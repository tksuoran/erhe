# Node editor: live interaction verification

Status: in progress

Extends `doc/graph_editor.md` "Canvas rendering (native resolution)", which
describes the canvas as built. What is left is verification that needs a
display and a mouse, which the headless MCP loop cannot drive.

Issue: https://github.com/tksuoran/erhe/issues/251

## What is verified headlessly today

Both smoke sweeps (`scripts/geometry_nodes_smoke_test.py`,
`scripts/texture_graph_smoke_test.py`) run green, and the zoom screenshot
matrix at 0.5 / 1 / 2 / 4 shows node frames, widgets and pins scaling
together, crisp text, and node backgrounds and borders rendering. READ the
PNGs when re-running the matrix - a size diff proves nothing about crispness.
Screenshots are taken with the graph window cropped out of the frame: the
default startup scene has a physics-animated viewport that settles to
wall-clock-dependent positions, so a full-frame pixel diff is not an identity
oracle. The node-editor crop is deterministic to 0 px across launches once the
ini layout is fixed, so restore `config/editor/*.{ini,json}` before each
capture and after each run.

## Open: live mouse interaction

With a display and a user at the controls, verify in a graph window:

- Open a combo inside a node and the canvas background "Add node" context menu
  by clicking; both should render and hit-test where they are drawn.
- Drag a node; the node follows the cursor at every zoom level.
- Box-select several nodes.
- Create and delete a link by dragging from a socket; sockets are hittable at
  every zoom level.
- Mouse-wheel zoom-under-cursor keeps the point under the cursor fixed.
- No node-size jitter while zooming through the discrete zoom levels.

Optional extras: open and screenshot the shader graph and rendergraph windows
once each (neither has a sweep, visual sanity only); check font atlas growth
across the zoom levels; drive live zoom in the texture and shader graph
windows, which needs a set-view MCP tool of their own - only the geometry
graph has one (`geometry_graph_set_view`).
