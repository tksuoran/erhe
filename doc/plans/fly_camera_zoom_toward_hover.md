# Fly camera: mouse-wheel motion towards the hovered point

Status: proposed

Extends `doc/editor/tools.md` (`Fly_camera_tool`) and `doc/editor/four_view.md`
(which owns the orthogonal-camera zoom behavior and the `ortho_zoom_mode`
setting this plan mirrors).

## Requirements

- R1. Settings, Camera Control, gains "Perspective View Zoom" with two values:
  "Along view axis" and "Towards hovered point". The default is
  "Towards hovered point". The setting is a `Camera_controls_config` field, so
  it has the per-scene override every camera control has.
- R2. "Along view axis": a wheel step moves the camera along its view axis
  (the behavior of `Fly_camera_tool::zoom()` today).
- R3. "Towards hovered point": a wheel step moves the camera along the axis
  from the camera position towards the point under the pointer. The point
  under the pointer stays under the pointer for the whole glide the step
  starts.
- R4. The W / S keys and the controller translate-z axis move the camera
  along the view axis under both values of the setting.
- R5. Key motion (R4) and wheel motion (R3) run simultaneously, each along its
  own axis; the camera velocity is their sum.
- R6. The setting applies to perspective cameras. An orthogonal camera zooms
  as `doc/editor/four_view.md` states, under both values.

## Design

- D1. Setting. New codegen enum `Perspective_zoom_mode`
  (`src/editor/config/definitions/perspective_zoom_mode.py`: `view_axis` = 0,
  `hover_point` = 1, `underlying_type=UInt`), listed in
  `src/editor/CMakeLists.txt` next to `ortho_zoom_mode.py`. New field
  `perspective_zoom_mode` in `camera_controls_config.py`, `added_in=1`,
  default `Perspective_zoom_mode::hover_point`, placed after
  `ortho_zoom_mode`. The Settings row and the per-scene override come from
  reflection, as they do for `ortho_zoom_mode`. `zoom()` reads the value at
  the wheel event through `get_writable_camera_controls()` (the wheel event is
  the change site; nothing is cached).
- D2. The axis of R3 is the pointer ray. For a perspective camera the ray
  from the camera position through the pointer IS the axis towards the
  hovered point, whatever the hover hit distance is, and it exists when the
  pointer is over empty space. `zoom()` takes it from the hover scene view:
  `update_hover(true)` on the `Viewport_scene_view`, then
  `Scene_view::get_control_ray_direction_in_world()`. When the hover scene
  view or the ray is absent (headset view, pointer outside a viewport),
  `zoom()` uses the view axis.
- D3. A separate wheel channel in `Frame_controller`: a new
  `erhe::math::Input_axis zoom` and a `glm::vec3 m_zoom_direction_in_world`
  (unit vector), with `set_zoom_direction(glm::vec3)`. `zoom` gets the same
  damp / max delta as `translate_z` (constructor, and both
  `set_damp_and_max_delta` sites in `fly_camera_tool.cpp`), and is reset
  wherever `translate_z` is reset (`Frame_controller::reset()`, the three
  reset sites in `fly_camera_tool.cpp`). `translate_z` keeps serving keys and
  the controller axis only - this is what gives R4 and R5 by construction:
  `update_fixed_step()` adds
  `m_zoom_direction_in_world * zoom.current_value() * speed` after the
  `translate_z` term, so the two contributions sum.
- D4. `zoom()` for a perspective camera, both modes, writes the wheel channel:
  it sets the direction (`-get_axis_z()` for `view_axis`, the D2 ray direction
  for `hover_point`) and calls `zoom.adjust(k)` with `k` positive for wheel
  forward. The direction is a WORLD vector captured at the wheel event and
  held for the glide: the camera translates without rotating along a fixed
  world line through the hovered point, so the point keeps its screen
  position (R3). A new wheel step replaces the direction; the residual
  velocity of the previous glide continues along the new direction (steps
  arrive faster than the pointer moves, so the two directions are near equal).
  Turning the camera during a glide leaves the direction unchanged.
- D5. Step size. `k` keeps today's form, `(1/32) * l * delta`, with `l` the
  distance that makes a step proportional to what is being approached: in
  `hover_point` mode the distance from the camera to the hover hit
  (`get_nearest_hover(Hover_entry::content_bit | Hover_entry::grid_bit)`
  position, with the selection-mode mask adjustment other callers use); when
  there is no hit, and always in `view_axis` mode, today's
  `glm::length(position)`. A lower bound on `l` (camera near clip distance)
  keeps the wheel responsive right at a surface.
- D6. Diagnostics. The Fly Camera window shows the new axis with
  `show_input_axis_ui("Zoom", ...)` next to "Tz".
- D7. MCP. A `debug_camera_zoom` tool (`viewport` pointer position via the
  existing `debug_imgui_mouse`, then `delta`) calls
  `Fly_camera_tool::zoom()` so the headless build can exercise the wheel path;
  window wheel events are not reachable by `debug_imgui_mouse` today
  (`doc/editor/four_view.md` trap).

## Phases

1. D1: enum, field, CMake entry. Build `editor` twice (codegen definition
   change). Verify the Settings row appears and
   `config/editor/editor_settings.json` round-trips the value.
2. D3: `Frame_controller` wheel channel, resets, damp wiring, D6 row.
   `zoom()` writes the channel with the view axis (R2 in both modes at this
   point). Verify W/S and wheel still move the camera as before.
3. D2 + D4 + D5: `hover_point` branch in `zoom()`.
4. D7, then the headless acceptance check below; then hand to the user for
   the interactive check.
5. Docs: `doc/editor/tools.md` `Fly_camera_tool` entry states the two wheel
   modes and the separate wheel channel; this plan is deleted and its link
   removed from the "Future work" section of
   `doc/editor/tools.md` and from `doc/README.md`; `py -3 scripts/check_doc_links.py`.

## Verification

- Headless (`build_vs2026_vulkan_headless`, `ERHE_AI_DRIVER=1`, MCP port read
  from the log): place the pointer off-center over a mesh, read the hover
  position `P` and its viewport position, send wheel steps, wait for the glide
  to end. Acceptance, worst over the run: `P` re-projects within 1 pixel of
  the pointer; the camera position moved along `normalize(P - start)` with a
  perpendicular residue under 1e-4 of the distance moved; camera orientation
  unchanged. Repeat with `view_axis`: displacement is along the view axis.
- R5: hold the forward key command active (`set_active_control_value` path
  through MCP) while sending wheel steps; the displacement equals the sum of
  the two single-input displacements within the fixed-step quantization.
- Orthogonal camera: `Ortho_zoom_mode` behavior unchanged under both values.
- User, windowed: wheel towards an off-center object with and without W held;
  four view perspective cell; per-scene override of the new setting.
