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
- Helper functions in `imgui_helpers.hpp`: `make_button()`, `make_combo()`, `make_scalar_button()`, etc.

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
- The `windows/` subdirectory has reusable utility windows (performance, log, pipeline inspector, graph plotter, framebuffer viewer).

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

## Future work

- [plans/mcp_ui_driving.md](../plans/mcp_ui_driving.md)
