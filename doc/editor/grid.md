# Grid

Stability: mostly stable

The editor grid (`src/editor/grid/`) is an infinite reference plane with four
levels of lines and axis coordinate labels, used for display, hover and
snapping. `Grid_tool` owns the grids; `Grid` is a `Node_attachment` whose
registered properties (plane, offset, rotation, cell sizes, colors, labels,
snap, Behind Content) are edited in the Grid window and persist through
`Grid_config` in `editor_settings.json` (per-scene overrides apply to the
appearance).

## Rendering

The grid is one fullscreen composition pass ("Grid", `app_rendering.cpp`)
whose shaders (`res/editor/shaders/grid.vert`, `grid.frag`) read everything
from the camera UBO. `Grid::render()` runs once per rendered view, before
that view's composition passes, and pushes the appearance and the view's
`Grid_frame` into the pass's `erhe::scene_renderer::Grid_parameters`
(`world_from_grid`, `grid_flags`). `write_camera_entry()` derives
`grid_from_world` and the precision wrap offset (grid space, snapped to the
level 0 cell) from it. `grid.frag` intersects the view ray with the plane in
wrapped world space and takes line and label coordinates from
`grid_from_world`.

## View frame

`Grid::get_view_frame(camera)` returns the plane one view shows and hovers
(`Grid_frame`: `world_from_grid`, `grid_from_world`, `label_sign`). For a grid
with an XZ, XY or YZ plane seen through an orthogonal camera whose right, up
and back vectors each run along a world axis (dot product above `1 - 1e-4`),
the frame is the axis plane facing the camera through the grid origin: grid x
= camera right, grid z = camera down, grid y (normal) = towards the camera.
Top views get XZ, Front and Back XY, Right and Left YZ; the four view
(`four_view.md`) shows a grid in every cell. Every other case - perspective
camera, tilted orthogonal camera, `Node` plane - uses the grid's own plane.
The frame is derived from the camera at each use, never cached.

The label shader draws glyph up along grid -z and reads along grid +x, so the
camera-derived axes keep labels upright and unmirrored. `label_sign` (+1 / -1
per labelled grid axis) multiplies the printed value, so labels show world
coordinates when a grid axis runs along a negative world axis.

## Hover and snap

`Grid_tool::update_hover(camera, ray)` intersects each grid in its view frame
for the hovering `Scene_view`'s camera. The grid `Hover_entry` carries that
`Grid_frame` (`grid_frame`) next to `grid_weak`; consumers acting on a grid
hover (`Brush_tool`, `Reference_frame`, `Hover_tool`) take the plane, its
normal / tangent / bitangent and `Grid::snap_world_position(frame, ...)` from
the entry, so placement lands on the plane the view shows.

## Depth

The grid pipeline tests depth and never writes it. `Grid_depth_mode` selects
where the grid sits: `depth_tested` (default) at its plane, so content in
front of the plane covers the lines; `behind_content` (the Behind Content
property, `Grid_config::behind_content`) at far depth, where the
`less_or_equal` test passes only on pixels no content has drawn.
