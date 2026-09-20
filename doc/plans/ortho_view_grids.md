# Grid in orthogonal views

Status: proposed

An axis-aligned orthogonal view shows the grid on the axis plane it faces:
Top on XZ, Front on XY, Right on YZ, and likewise Bottom, Back and Left. The
four view (`doc/editor/four_view.md`) gets a grid in all four cells, and so
does any single viewport whose camera is orthogonal and axis-aligned. A
perspective view, and an orthogonal view that is not axis-aligned, show the
grid on the grid's own plane, as today.

## Current state

- F1. The grid is one fullscreen composition pass ("Grid",
  `app_rendering.cpp`) whose shaders (`res/editor/shaders/grid.vert`,
  `grid.frag`) read everything from the camera UBO. `Grid::render()` pushes
  the appearance into that pass's `Grid_parameters` once per rendered view
  (`Tools::render_viewport_tools()` runs before `render_viewport_main()` in
  `Viewport_scene_view`), so pass data set there is current for that view.
- F2. The grid plane never reaches the GPU. `Grid_parameters`
  (`erhe_scene_renderer/camera_buffer.hpp`) carries no transform, and
  `write_camera_entry()` (`camera_buffer.cpp`) writes `world_from_grid` as a
  hard-coded identity. Every grid therefore renders on world XZ, whatever its
  `plane_type`, `center`, `rotation` or host node say.
- F3. `grid.frag` assumes grid space equals world space: `uv = pos.xz`, the
  wrap offset is applied as world `xz`, and the axis labels print world x and
  z.
- F4. Hover and snapping use the real plane: `Grid::intersect_ray()` works in
  `grid_from_world()` space. In a Front or Right view the ray is parallel to
  the XZ plane, so there is no grid hover and nothing can be placed on the
  grid there. `Hover_entry` names the grid (`grid_weak`), and consumers ask
  that grid for `world_from_grid()` / `normal_in_world()`.
- F5. The edge-on XZ plane is why Front and Right show nothing; Top and the
  perspective view work because they see XZ face-on.

## Design

- D1. View frame. `Grid::get_view_frame(const erhe::scene::Camera&)` returns
  the `world_from_grid` to use for one view (a small value class `Grid_frame`
  holding `world_from_grid`, `grid_from_world` and the label signs of D3).
  For a grid whose `plane_type` is XZ, XY or YZ, seen through a camera whose
  `Projection::is_orthogonal()` holds and whose view axis is within
  `1 - 1e-4` (dot product) of a world axis, the frame is built from the
  camera: grid x = the world axis nearest the camera's right vector, grid z =
  the world axis nearest the camera's down vector, grid y (the normal) = the
  world axis pointing at the camera, origin = the grid's `center`. Every other
  case returns the grid's own `world_from_grid()`. Top (from +Y, -Z up) yields
  identity, so that view is unchanged. A `Node` grid always keeps its node's
  frame.
  The frame is derived from the camera at the two use sites (D2 render, D4
  hover); nothing is cached, so nothing can go stale when a camera is turned
  or its projection type is edited.
- D2. GPU path. `Grid_parameters` gains `world_from_grid` (mat4, default
  identity) and `label_sign` (D3). `write_camera_entry()` uses it in place of
  the two identity TODO lines, derives `grid_from_world` by inverse, and
  writes both matrices plus the wrap offset in grid space. The camera struct
  gains `grid_from_world` (mat4), `grid_offset_in_grid` (vec4) and
  `grid_label_sign` (vec4); the struct size stays a multiple of 16 bytes
  (AGENTS.md "Shader Interface Block Layout"). `grid.frag` intersects in
  wrapped world space as now, then takes `uv` and the label coordinates from
  `grid_from_world * pos` (`.xz`), with the grid-space wrap offset for the
  labels. `Grid::render()` passes `get_view_frame(*context.camera)` through a
  new `App_rendering::set_grid_frame()`.
- D3. Labels. The label shader draws glyph up along grid -z and reads left to
  right along grid +x, which D1's camera-derived axes keep upright and
  unmirrored in every axis-aligned view. A grid axis can then map to a
  negative world axis (Right view: grid x = world -Z), so `label_sign.xy`
  (+1 or -1 for grid x and grid z) multiplies the printed value; the labels
  always show world coordinates.
- D4. Hover and snap. `Grid_tool::update_hover()` takes the `Scene_view`'s
  camera and intersects each grid in its view frame
  (`Grid::intersect_ray(frame, origin, direction)`), so the pointer in a
  Front view lands on the XY plane through the grid center. `Hover_entry`
  carries the `Grid_frame` of the hit next to `grid_weak`; consumers that
  need the plane (normal, tangent, snapping, `world_from_grid`) read it from
  the entry. Phase 0 lists those consumers.
- D5. Depth. The grid pass keeps its state (depth test on, depth write off):
  in a side view, content in front of the plane covers the lines, as in Top
  today.

## Phases

Each phase is one commit, built with `scripts\build_ninja_win_vulkan.bat
editor` and verified on `build_vs2026_vulkan_headless` over MCP with
`"vulkan_validation_layers": true` (phases 1 and 2 change a UBO and a shader).

0. Survey. List every reader of `Hover_entry::grid_weak`,
   `Grid::world_from_grid()`, `grid_from_world()`, `normal_in_world()`,
   `tangent_in_world()`, `bitangent_in_world()`, `snap_world_position()` and
   `snap_grid_position()` (starting points: `brushes/reference_frame.hpp`,
   `scene/scene_view.cpp`, `create/`, `brushes/brush_tool.cpp`,
   `transform/`), and record in this document which ones act on a hover hit
   (they move to the entry's frame in phase 3) and which act on the grid
   itself (unchanged).
1. Grid plane reaches the GPU (D2 with the grid's own `world_from_grid()`,
   `label_sign` = +1). Fixes F2 on its own. Verify: set the default grid's
   `plane_type` to "XY-Plane Z+" with `set_item_property`, capture the
   perspective view, see a vertical grid with upright labels; set a non-zero
   `center` and `rotation` and see the lines move; fly the camera past
   x = 100000 and confirm the lines do not jitter (wrap still exact); restore
   XZ and compare against a capture taken before the change.
2. View frame (D1, D3) in `Grid::render()`. Verify: `open_four_view`,
   `capture_screenshot`: lines and labels in all four cells; `create_shape` a
   box at (2, 3, -4) and read it off the labels as x 2 / y 3 in Front,
   z -4 / y 3 in Right, x 2 / z -4 in Top; a single viewport with its camera
   set to "Orthogonal Vertical" (`set_item_property` `projection_type`) and
   rotated to look along -X shows the YZ grid, and tilted 10 degrees shows
   the XZ grid again. Add a gtest for the frame construction if it lands as a
   pure function (six axis views: right-handed, normal towards the camera,
   expected label signs).
3. Hover and snap (D4) plus the consumers from phase 0. Verify: with the
   pointer injected into the Front cell (`debug_imgui_mouse`), the grid hover
   position has z = grid center z and its normal is +Z; `place_brush` through
   the hover path in Front puts the brush on the XY plane, snapped; the same
   in Top is unchanged. Extend the MCP hover query with the grid slot if it
   does not report it.
4. Documents: a new grid document under `doc/editor/` (the standing
   description of F1 and D1-D5), `doc/editor/four_view.md` (grid per cell, drop the future-work
   link), `doc/erhe/scene_renderer.md` (`Grid_parameters` fields),
   `doc/README.md` index; delete this plan. Then the user's interactive pass:
   placing and dragging with the mouse in Front and Right, label legibility
   while zooming.
