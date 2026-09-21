# Four view

Stability: experimental

A four view shows one scene in four linked viewport windows docked as a
2 x 2 grid that shares one cross splitter: three axis-aligned orthogonal views
and the perspective viewport it was opened from.

| cell         | view                                             |
|--------------|--------------------------------------------------|
| top-left     | Top: from +Y looking down, -Z up on screen       |
| top-right    | Front: from +Z looking towards -Z                |
| bottom-left  | Right: from +X looking towards -X                |
| bottom-right | the source viewport, with its camera unchanged   |

## Opening

`Scene_views::open_four_view()` is reached from Window > Open Four View, the
command `Scene_views.open_four_view` and the MCP tool `open_four_view`. The
source viewport is the last hovered viewport, else the only viewport; it has
to show a scene through a camera. That camera is the four view's perspective
camera.

Opening creates three cameras named Top, Front and Right under the scene's
root node. They are orthogonal (`orthogonal_vertical`), flagged `content |
show_in_ui | exclude_from_prefab | session_only` like the default camera
injected on open: they appear in the Hierarchy and are left out of every save.
The focus point lies ahead of the perspective camera at the distance of the
center of the active meshes' world bounds (the focus distance, fixed for the
life of the four view), the view height is 2.2 bounding-sphere radii, and
each orthogonal camera sits 4 radii + 1 from the focus along its axis. A `Viewport_scene_view` and a `Viewport_window` are
created per camera.

## Docking

The windows are docked by a dock operation queued on the desktop
`Window_imgui_host` (`queue_dock_operation`), which runs just before
`ImGui::DockSpace()`, the point of the frame where the dock tree may be
rebuilt. The operation re-queues itself until ImGui knows the three new
windows (each has been submitted once), then calls
`DockBuilderSplitNodeCross` on the source window's dock node and docks the
new windows into the first three cells. The splits carry the source window
and its tab siblings into the bottom-right cell. A floating (undocked) source
window leaves the new windows undocked. The cross splitter is described in
`doc/erhe/imgui.md`.

## Linking

`Four_view` (`src/editor/scene/four_view.hpp`) holds the shared state: focus
point, focus distance, view height (the zoom) and orthogonal camera distance.
The focus is the point `focus distance` ahead of the perspective camera; each
orthogonal camera sits at `focus + axis * distance` looking at the focus.

- Change notification. `Four_view` holds one
  `erhe::scene::Transform_observer_token` per camera (`doc/erhe/scene.md`
  "Transform observers"), whose callback reports every transform write of that
  camera - fly camera, gizmo, Properties, undo - to
  `Four_view::on_camera_moved()`. The tokens are members, so `~Four_view`
  takes the observer off every camera, including the perspective camera (the
  user's own, which outlives the four view).
- Perspective camera moved or turned. The focus becomes the point ahead of
  it and the three orthogonal cameras are placed for the new focus.
- Orthogonal camera moved. The part of its offset that lies in its view
  plane moves the focus; the other two orthogonal cameras are placed for the
  new focus and the perspective camera is translated by the same offset,
  orientation unchanged. Movement along the view axis changes nothing an
  orthogonal view shows and leaves the focus alone.
- Zoom. `Fly_camera_tool::zoom()` scales the size of the view volume for an
  orthogonal camera (`0.9 ^ delta`). For a four view camera it calls
  `Four_view::set_view_height()`, which sets `ortho_height` on all three
  cameras. The `Camera_controls_config::ortho_zoom_mode` setting (Settings,
  Camera Control, "Orthogonal View Zoom") selects what else zoom does:
  `size_only` scales around the view center; `size_and_pan` (the default)
  also translates the zoomed camera by the offset between the pointer ray
  origins before and after the size change, which keeps the point under the
  pointer in place
  (and, in a four view, moves the focus as any in-plane camera move does).
- Rotation. `Fly_camera_tool::is_rotation_locked()` makes turn, tumble and
  the rotation axes no-ops for a four view camera.
- The perspective camera keeps its full navigation (fly, turn, tumble, zoom);
  the link follows the camera the source viewport showed when the four view
  was opened.

## Lifetime

`Scene_views` owns the four views. `Four_view` refers to its scene and
cameras by `weak_ptr` and follows them through tokens it owns, so a four view
keeps nothing of its scene alive and leaves no observer behind.
`Scene_views::unbind_views_from_scene()` (the scene close path) destroys the
four views of the closing scene; their windows remain as empty viewports. A
camera removed from the scene (it stays alive in the undo history) is skipped
by placement until an undo returns it.

## Verification

Headless, over MCP: `open_four_view`, `capture_screenshot` for the layout;
`select_items` a four view camera by name and `transform_selection` it, then
`get_four_views` shows the focus and the other cameras following, and
`undo` returns them - once for an orthogonal camera, once for the perspective
camera; `close_scene` logs a clean `scene-close check`.

Wheel zoom, middle-drag pan and the rotation lock reach the fly camera through
window input events, so they are driven by the input gesture tools
([../agents/mcp_ui_driving.md](../agents/mcp_ui_driving.md)) over the rectangles `get_viewports` reports:

- Wheel zoom. `mouse_wheel` at the center of an orthogonal cell with a
  positive `dy`; `get_four_views` then reports a smaller `view_height` and the
  same value on all three orthogonal cameras.
- Middle-drag pan. `mouse_drag` with `button: "middle"` and
  `modifiers: ["menu"]` (the track command's binding) across an orthogonal
  cell; `get_four_views` then reports a moved `focus` and the other cameras
  placed for it.
- Rotation lock. `mouse_drag` with `button: "right"` across an orthogonal cell
  leaves that camera's `position` unchanged.

## Grid

Each orthogonal cell shows and hovers the grid on the axis plane it faces
(`grid.md`, View frame).
