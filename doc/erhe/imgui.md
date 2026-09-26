# erhe_imgui

Stability: stable

## Purpose
Custom ImGui backend and window management layer for erhe. Provides GPU-accelerated
ImGui rendering via erhe::graphics, a host abstraction that supports multiple
independent ImGui contexts (one per viewport or render target), and a window
management system that tracks, registers, and dispatches input events to ImGui windows.

## Key Types
- `Imgui_renderer` -- GPU rendering backend: manages shaders, buffers, font atlas, texture samplers; renders ImDrawData
- `Imgui_host` -- abstract base (also a `Rendergraph_node` + `Input_event_handler`): hosts an ImGui context; subclassed by `Window_imgui_host` and editor's `Rendertarget_imgui_host`
- `Window_imgui_host` -- concrete host that renders ImGui into the main OS window via swapchain
- `Imgui_window` -- base class for individual ImGui windows; override `imgui()` to draw content
- `Imgui_windows` -- registry/manager: registers windows, dispatches input events, persists visibility state, provides menu entries
- `Imgui_settings` -- font paths, sizes, scale factor configuration
- `Scoped_imgui_context` -- RAII guard for switching the active ImGui context
- `File_dialog_window` -- simple file browser dialog window
- Helper functions in `imgui_helpers.hpp`: `make_button()`, `make_combo()`, `make_scalar_button()`, `draw_spinner()`, etc.

## Public API
- Create an `Imgui_renderer`, then create `Imgui_host` subclasses (each gets its own ImGuiContext).
- Derive from `Imgui_window` and register with `Imgui_windows` to add UI panels.
- Per frame: `begin_frame()` -> draw windows -> `render_draw_data()` -> `next_frame()`.
- `image()` / `image_button()` for rendering GPU textures in ImGui.

## Dependencies
- `erhe::graphics` -- shaders, buffers, textures, render pipeline, ring buffers
- `erhe::rendergraph` -- `Rendergraph_node` base class for hosts
- `erhe::window` -- `Input_event_handler`, `Context_window`
- `erhe::math` -- `Viewport`
- `erhe::dataformat` -- vertex format for ImGui geometry
- External: Dear ImGui, glm

## Notes
- Each `Imgui_host` has its own `ImGuiContext`, enabling multiple independent ImGui viewports (e.g., main window + VR render targets).
- The renderer uses indirect draw calls with a ring buffer strategy for vertex/index/draw-parameter data.
- Font atlas is shared across all hosts.
- A host with a layout ini path (`get_imgui_ini_path()`, `set_imgui_ini_path()`) persists its window positions, sizes and docking there. Under `erhe::codegen::Config_persistence::read_only` the ini is read before the host's first frame and never written (`io.IniFilename` stays null; the host loads it itself right before its first `ImGui::NewFrame()`).
- The `windows/` subdirectory has reusable utility windows (performance, log, pipeline inspector, graph plotter, framebuffer viewer).
- `draw_spinner(center, radius, thickness, color)` is the indeterminate
  progress spinner: an arc added to the current window's draw list whose start
  angle is a function of `ImGui::GetTime()`. It holds no state and emits no
  ImGui item, so it animates from the draws that are already happening and the
  caller keeps ownership of the layout - a row that spins instead of showing
  its content keeps the height it would have had. It works in every host,
  including the editor's `Rendertarget_imgui_host` (the hotbar in the 3D
  viewport), because each host sets `io.DeltaTime` and renders every frame, so
  nothing has to force a redraw for the animation.

## Cross splitter (Dear ImGui fork feature)

erhe's Dear ImGui (the in-tree copy `src/imgui/imgui/`, file-identical to the
`erhe` branch of the `tksuoran/imgui` fork, where ImGui changes are made
first) adds a cross-bar splitter for four windows docked as a 2 x 2 grid. It
is generic docking code, independent of erhe.

- The grid is one dock split node whose two children are both split on the
  other axis. `ImGuiDockNodeFlags_CrossSplit` on that node turns its splitter
  (the outer bar) and its children's splitters (the two inner segments) into
  one cross: child 1's split follows child 0's in `DockNodeTreeUpdatePosSize`,
  so the inner segments stay collinear through host resizes and with a
  central node in any cell.
- `DockNodeTreeUpdateCrossSplitter` drives the cross. Dragging an inner
  segment moves both inner segments within the intersection of their limits;
  dragging the outer bar moves it alone; the crossing (grown by
  `WindowsBorderHoverPadding`) is a handle that moves both axes, with the
  ResizeAll cursor. The inner segments highlight together, the handle
  highlights all three bars. `NoResize` / `NoResizeX` / `NoResizeY` disable
  the matching axis.
- The cross is engaged only while all four cells are visible
  (`ImGuiDockNode::IsCrossSplitEngaged()`); otherwise the nodes are plain dock
  nodes and the flag stays set, so reopening a closed window re-engages it.
  The flag follows the child nodes through tree merges
  (`DockNodeMoveChildNodes`).
- The flag is saved in the docking ini as ` Cross=1`.
- API (`imgui_internal.h`): `DockBuilderSplitNodeCross(node_id, outer_axis,
  ratio_outer, ratio_inner, out_ids[4])` builds the grid (leaf ids in
  row-major order) and `DockBuilderSetNodeCrossSplit(node_id, enabled)` sets
  the flag on an existing split node.
- User control: the dock node window menu of the four cells offers "Cross
  splitter"; Metrics > Docking shows a `CrossSplit` checkbox per node.
- Editor default layout: a `Dock_placement` entry (`"_version": 2`) with
  `cross_windows` (four titles: top-left, top-right, bottom-left,
  bottom-right) splits the target's dock node into the grid; see
  `src/editor/editor_default_layout.cpp`.
- Headless verification: the MCP tool `mouse_drag` injects the pointer gesture
  into the editor's window, so the cross is driven the way a user drives it.
  Open a 2 x 2 grid (the editor's four view is one: Window > Open Four View),
  read the four cells' rectangles with `get_imgui_windows`, hide any floating
  window covering the bars (`set_window_visibility`), then
  `mouse_drag {from: [crossing], to: [elsewhere], frames: 12}` and read the
  rectangles again: both columns and both rows change together, the cells of
  one column keep one width and the cells of one row keep one height, which is
  the inner segments staying collinear. `capture_screenshot` shows the same
  thing as pixels.

## Input routing

Every input event goes to the `Imgui_host` first, then to the application
(`erhe::commands` in the editor) unless the host marks it handled. The host
marks a mouse event handled when `io.WantCaptureMouse` holds, and a key, text
or char event when `io.WantCaptureKeyboard` holds - except while one of its
windows requests that input: a window whose `want_mouse_events()` /
`want_keyboard_events()` returns true makes the host pass the events through
to the application too (`Imgui_host::want_capture_mouse()` /
`want_capture_keyboard()`). The editor's viewport windows request input this
way, so that the 3D view gets the pointer and the keys while the pointer is
over it.

- **An interaction keeps the input until it ends.**
  `is_input_owned_elsewhere(window)` (`imgui_helpers.hpp`) is true while Dear
  ImGui is in the middle of an interaction that `window` does not own: an
  active item submitted outside `window` and its child windows (a drag field
  being dragged, a text field being edited - it stays active after its click
  until Enter, Escape or a click elsewhere - a window being moved), or a mouse
  button held since a press outside `window`'s rectangle. Popup and dock
  hierarchies are not crossed.
  `Imgui_windows::draw_imgui_windows()` honors a window's
  `want_mouse_events()` / `want_keyboard_events()` only while
  `Imgui_window::is_input_owned_elsewhere()` (the predicate as of the window's
  last `end()`) is false. So typing into a text field of one window with the
  pointer over a viewport edits the text and does not reach the viewport's
  key bindings; a drag that started in another window and moves over a
  viewport stays that window's drag, its release included.
- **The request lags one frame.** Requests are collected while the windows are
  drawn and apply to the events the next frame delivers. The event that ends
  an interaction (the button release, Enter) is therefore still routed by the
  request made while it was in progress, and stays with ImGui; the window gets
  the input from the following frame on.
- A window with finer-grained needs calls `is_input_owned_elsewhere()` itself
  with the window that holds its interactive area. The editor's
  `Viewport_window` does so for its child window, and lets an ImGui drag and
  drop over the viewport drive its hover (for the drop preview) without
  requesting input (`doc/editor/windows.md`).

## Item recorder

`Imgui_item_recorder` (`erhe_imgui/imgui_item_recorder.{hpp,cpp}`) records
what Dear ImGui submitted in one frame, which is what the editor's
`get_imgui_*` MCP tools read
([../agents/mcp_ui_driving.md](../agents/mcp_ui_driving.md)).

- **Hooks.** Dear ImGui reports every item to four extern functions -
  `ImGuiTestEngineHook_ItemAdd`, `ImGuiTestEngineHook_ItemInfo`,
  `ImGuiTestEngineHook_Log` and `ImGuiTestEngine_FindItemDebugLabel` - when
  `IMGUI_ENABLE_TEST_ENGINE` is defined and `ImGuiContext::TestEngineHookItems`
  is true. `imgui_item_recorder.cpp` supplies the four; the `imgui_test_engine`
  library, one other implementation of them, carries its own non-MIT license
  and erhe does not use it.
- **The definition is PUBLIC on the `imgui` target**
  (`src/imgui/CMakeLists.txt`). It decides whether `imgui_internal.h` declares
  the hook functions and the `ImGuiItemStatusFlags_Openable` / `Opened` /
  `Checkable` / `Checked` / `Inputable` enumerators, so every translation unit
  that includes the headers must agree on it.
- **Recording is on request.** `Imgui_host::request_item_recording()` sets
  `TestEngineHookItems` for exactly the next `NewFrame` .. `Render` and clears
  it after, and the recorder keeps that frame's records until the next
  request. With the flag false Dear ImGui calls no hook at all, which
  `get_imgui_hosts`'s `hook_calls_total` shows. The record vector and the
  label arena are cleared with their capacity kept, so a recorded frame
  reaches a high-water mark and stops allocating.
- **Visibility is recorded at ItemAdd.** Dear ImGui calls the ItemAdd hook
  before it sets `ImGuiItemStatusFlags_Visible`, and some widgets (a combo
  box) never report ItemInfo, so the ItemAdd hook applies ItemAdd's own
  clipping test (the item rectangle against the window clip rectangle) and
  records the result; an ItemInfo that follows replaces the status flags.
- **One recorder per `ImGuiContext`**, owned by the `Imgui_host` that owns
  the context; the hooks find it through a registry keyed by context.
- **The first non-empty label wins.** A widget built out of another one
  reports twice - a menu is a `Selectable("")` reporting an empty label and
  `BeginMenu()` then reports the menu's name for the same id - so a recorded
  name is not overwritten and an empty one gives way.
- **Naming an item erhe draws itself.** `set_item_debug_label()` names the
  item submitted last; `set_recorded_item_labels(first_index, label)` names a
  run of items bracketed with `get_recorded_item_count()`, giving a single
  item the label and several the `.x` / `.y` / `.z` / `.w` / `.<position>`
  component suffixes. An item given a role with `set_item_debug_role()` is
  named `<label>.<role>` instead and takes no position (the reference field
  names its picker arrow `pick`, its select and clear buttons `select` and
  `clear`, so the value button alone takes the row's name). It names only the items of the window current when it
  is called (the row's window), so a popup the widget opened - a combo's
  list - keeps its entries' own labels. `is_item_recording()` guards building the text. Item
  tree rows and property rows use these.
- **Threading.** Every ImGui context erhe creates is driven from the main
  (tick) thread - `Imgui_windows::begin_frame` / `draw_imgui_windows` /
  `end_frame` for the desktop host and for every `Rendertarget_imgui_host`
  alike - so the registry and the records need no synchronization.

The tool vocabulary the MCP server exposes over this
(`get_imgui_items`, `get_imgui_item_rect`, `imgui_click`, `imgui_hover`,
`imgui_scroll`, annotated screenshots) comes from
[SaloQT/imgui-mcp](https://github.com/SaloQT/imgui-mcp) (MIT). That project is
a standalone UI design tool: it spawns its own SDL2 + OpenGL renderer process
and records rectangles only for the widgets it declared itself, with no
embedding API and no hook into a host application's ImGui calls, which is why
erhe implements the hooks itself instead of depending on it.
