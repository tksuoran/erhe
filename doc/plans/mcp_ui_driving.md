# Driving the editor user interface over MCP

Status: in progress

Extends [erhe/imgui.md](../erhe/imgui.md), [erhe/window.md](../erhe/window.md)
and the editor MCP server (`src/editor/mcp/`, usage in `mcp_server_usage.md`
at the repository root).

Goal: an AI agent drives the editor the way a user does - it reads which ImGui
windows and items exist and where they are, and it sends pointer, keyboard and
text input that every consumer of `Context_window::get_input_events()`
receives. The two parts serve different input consumers and are both needed:
part A gives insight into ImGui content, part B reaches the consumers that are
not ImGui (viewport tools through `erhe::commands`, `Fly_camera_tool`, window
events).

## 1. Requirements

- R1. A query reports, for one ImGui context, every window (name, rectangle,
  visible, docked, focused) and every item submitted in a frame (id, debug
  label, window, rectangle, status flags: checked, openable, opened, disabled,
  hovered, active).
- R2. An item is addressed by window name plus label path, or by ImGui id; the
  addressing resolves to a rectangle in window pixel coordinates, the same
  coordinate space R3 events use.
- R3. An MCP tool injects `erhe::window::Input_event`s into the editor's
  `Context_window`. The events travel the ordinary path:
  `get_input_events()` -> `Editor::dispatch_input_event` -> `Imgui_windows`
  hosts and `erhe::commands`. Supported types: key, text, char, mouse move,
  mouse button, mouse wheel, cursor enter, window focus, controller axis,
  controller button.
- R4. A gesture (press, a number of moves, release; a key chord; a typed
  string) is one tool call that spans the frames it needs and returns when the
  last event has been dispatched and one further frame has rendered, so a
  following `capture_screenshot` shows the result.
- R5. Works in the headless build (`null_window`) and in the windowed SDL
  build. In the windowed build injected events interleave with the user's real
  events; the tool description says so.
- R6. Steady-state frames perform no allocation for either part: item
  recording runs only in the frame a query asks for, into persistent scratch;
  the gesture queue is persistent scratch.
- R7. The UI-driving tools are for exercising the interactive entry points
  (menus, drags, docking, viewport gestures). Scene scripting keeps using the
  explicit-argument tools; `doc/agents/mcp_api_guidelines.md` gains a section
  stating this split.

## 2. Findings that shape the design

- F1. `SaloQT/imgui-mcp` (MIT) is a standalone UI design tool: a Python stdio
  MCP server that spawns its own SDL2 + OpenGL3 renderer process
  (`src/main.cpp` owns the window, the ImGui context and the loop) and renders
  a widget tree that the MCP client declares as JSON. It records rectangles
  only for widgets it declared itself (`render_widget()` stores
  `rect_min`/`rect_max`) and has no embedding API and no hook into a host
  application's ImGui calls. It cannot observe the erhe editor. What carries
  over is its tool vocabulary (`imgui_get_state`, `imgui_get_layout`,
  `imgui_get_widget_rect`, `imgui_click_widget`, `imgui_hover_widget`,
  `imgui_type_text`, `imgui_press_key`, `imgui_scroll_window`,
  `imgui_focus_widget`, `imgui_screenshot_annotated`) and its input technique
  (queue, then `io.AddMousePosEvent` / `AddMouseButtonEvent` /
  `AddInputCharactersUTF8` on the main loop).
- F2. Dear ImGui core (the in-tree copy `src/imgui/imgui`, 1.93.0 WIP, MIT)
  already reports every item to four extern functions when
  `IMGUI_ENABLE_TEST_ENGINE` is defined and `g.TestEngineHookItems` is true:
  `ImGuiTestEngineHook_ItemAdd(ctx, id, bb, item_data)`,
  `ImGuiTestEngineHook_ItemInfo(ctx, id, label, flags)`,
  `ImGuiTestEngineHook_Log`, `ImGuiTestEngine_FindItemDebugLabel`
  (`imgui_internal.h`, "Test Engine specific hooks"). The application supplies
  the four functions. The `imgui_test_engine` library is one implementation of
  them; it carries its own non-MIT license and erhe does not use it.
- F3. `Context_window::inject_input_event(const Input_event&)` and
  `set_input_event_synthesizer_callback()` exist in all three window backends
  (`sdl_window`, `null_window`, `glfw_window`). `inject_input_event` appends
  to the write buffer; the next `poll_events()` swaps it to the read side. An
  MCP handler runs inside `Editor::tick` (after that frame's dispatch), so an
  event it injects is dispatched on the next frame in both backends.
- F4. The synthesizer callback is a single slot and `Fly_camera_tool` takes it
  for its own scripted input test (`fly_camera_tool.cpp`, "Synthesizing").
- F5. The MCP server has a one-frame request deferral
  (`m_defer_current_request`, `m_deferred_requests`) that `drag_selection` and
  `physics_drag` already use to step a gesture one frame per pass.
- F6. `Imgui_windows` forwards every dispatched event to every `Imgui_host`,
  so an injected window event reaches ImGui too. `debug_imgui_mouse` writes to
  the desktop host's `ImGuiIO` directly and is the subset of part B that only
  ImGui sees.
- F7. `null_window` answers `get_cursor_position()` with (0, 0) and keeps no
  modifier state; events carry their own `modifier_mask`.

## 3. Design

### Part A: ImGui introspection (the imgui-mcp integration)

- A1. Integration form. erhe implements the F2 hook functions itself and
  exposes imgui-mcp's inspection vocabulary as tools of the editor's own MCP
  server. imgui-mcp is not added as a dependency (F1); its README is credited
  in `doc/erhe/imgui.md` as the source of the tool vocabulary.
- A2. `erhe::imgui::Imgui_item_recorder` (new, `erhe_imgui/imgui_item_recorder.{hpp,cpp}`)
  owns the four hook functions and one record buffer per `ImGuiContext`:
  `Item_record{ImGuiID id; ImGuiID window_id; ImRect rect; ImGuiItemStatusFlags status; ImGuiItemFlags item_flags; uint32_t label_offset;}`
  in a `std::vector` plus a `std::vector<char>` label arena, both cleared with
  capacity kept (R6). `ItemAdd` appends a record; `ItemInfo` finds the record
  of the same id from the back and fills label and status.
- A3. Recording is on request. `Imgui_host::request_item_recording()` sets
  `TestEngineHookItems` on that host's context for the next `NewFrame` ..
  `Render` and clears it after; the recorder keeps the finished frame's
  records until the next request. `IMGUI_ENABLE_TEST_ENGINE` is defined for
  the `imgui` target in `src/imgui/CMakeLists.txt` (public definition, every
  configuration); with `TestEngineHookItems` false the cost is one branch per
  item.
- A4. Windows come from `ImGuiContext::Windows` at query time (name, `Pos`,
  `Size`, `Active`, `Hidden`, `DockIsActive`, `DockId`, `NavWindow` match);
  no hook is needed for them.
- A5. Label path. A record's path is `<window name>/<label>`; when two records
  of one window share a label the query reports them with an `index` in
  submission order and R2 addressing takes that index. Items without an
  `ItemInfo` label (plain `ItemAdd`) are reported by id and rectangle only.
- A6. Hosts. Tools take `host` (default: the desktop `Window_imgui_host`);
  `get_imgui_hosts` lists host names including `Rendertarget_imgui_host`s.
  Pointer tools of part A target the desktop host only; a rendertarget host is
  inspected but driven through part B at the viewport pixel that shows it.
- A7. MCP tools (file `src/editor/mcp/mcp_server_ui.cpp`):
  - `get_imgui_hosts` -> names, sizes.
  - `get_imgui_windows {host}` -> A4 list.
  - `get_imgui_items {host, window?, label_contains?, visible_only=true}` ->
    records of one frame. Deferring request: pass 1 calls
    `request_item_recording()` and defers, pass 2 reads the recorder.
  - `get_imgui_item_rect {host, window, label | id, index?}` -> rectangle and
    center.
  - `capture_screenshot` gains `annotate_imgui_items` (bool, default false):
    draws id-numbered rectangles over the captured pixels on the CPU and
    returns the number -> label table (imgui-mcp's `screenshot_annotated`).
  - Convenience actions resolve an item (same deferral), then run a part B
    gesture at its center: `imgui_click {.., button=0, double=false}`,
    `imgui_hover`, `imgui_scroll {window, dx, dy}`. They return the resolved
    rectangle.

### Part B: input event injection

- B1. `erhe::window::Input_event` construction stays in the MCP layer;
  `Context_window::inject_input_event` is the only window API used and the
  three backends keep it as is (F3). The synthesizer callback slot stays with
  `Fly_camera_tool` (F4).
- B2. `Mcp_server::Input_gesture_steps` (same shape as `Selection_drag_steps`,
  F5): a persistent `std::vector<Input_event>` plus a per-event frame index.
  Each pass of the deferred request injects the events of the current frame
  index with `timestamp_ns` = now, advances, and defers; after the last frame
  it defers once more (R4) and returns the count of injected events. One
  gesture runs at a time; a second gesture call while one is stepping is
  refused with `isError`.
- B3. Pointer state. The MCP server keeps the last injected pointer position
  and held buttons/modifiers, reported by `get_input_state`. Every injected
  button and wheel event is preceded in the same frame by a move event to its
  position, so no consumer depends on `get_cursor_position()` (F7). The first
  pointer gesture of a session first injects `cursor_enter{entered=1}` and
  `window_focus{focused=true}`.
- B4. MCP tools:
  - `inject_input_events {events:[{type, frame?, ...fields}]}` - the raw form;
    `frame` is the offset from the first pass (default: previous event's
    frame). Field names equal the `*_event` member names; `keycode` accepts
    the `erhe::window::Keycode` name as text; `modifiers` is a list of
    `ctrl|shift|super|menu`.
  - `mouse_click {x, y, button=left, modifiers=[], double=false}`.
  - `mouse_drag {from:[x,y], to:[x,y], button=left, modifiers=[], frames=10, hold=false}`
    - press at `from`, `frames` interpolated moves one per frame (dx/dy
    filled), release at `to` unless `hold`; `mouse_release` ends a held drag.
  - `mouse_wheel {x, y, dx=0, dy}`.
  - `key_press {key, modifiers=[], hold_frames=1}` and
    `type_text {text}` (one `text_event` per UTF-8 chunk that fits
    `Text_event::utf8_text`).
  - `get_input_state` (B3).
  - `get_viewport_rects` is the existing `get_viewports` query; its rectangles
    are already window pixels, the coordinate space of `x`/`y` above.
- B5. `debug_imgui_mouse` is removed once `mouse_click` / `mouse_drag` pass
  its use (cross splitter drag in `doc/erhe/imgui.md`); that document's
  verification recipe is rewritten to the new tools in the same commit.
- B6. Relative-hold consumers (`set_cursor_relative_hold`, fly camera mouse
  look) read `dx`/`dy` of move events, which B4 fills; `null_window` keeps
  ignoring the hold request itself.

## 4. Phases

Each phase is one commit through `doc/agents/orchestration_harness.md`, built
and verified on `build_vs2026_vulkan_headless` with the agent's own MCP port.

1. Part B core: `Input_gesture_steps`, `inject_input_events`,
   `get_input_state`, B3 rules. Verify: a `Mcp_test` case injects a key event
   bound to a command (undo after a `create_shape`) and checks the undo stack;
   a second case injects press/move/release over a viewport and checks
   `get_selection` picks the mesh under the pointer.
2. Part B gestures: `mouse_click`, `mouse_drag`, `mouse_release`,
   `mouse_wheel`, `key_press`, `type_text`. Verify: wheel over a viewport
   changes the camera distance (`get_scene_cameras`); `mouse_drag` on a
   Transform tool handle moves the selection (compare with `drag_selection`);
   the four-view interactive list in `doc/editor/four_view.md` (wheel zoom,
   middle-drag pan) becomes scriptable - add it to that document's
   verification section.
3. Part A recorder: CMake definition, `Imgui_item_recorder`, host request API,
   `get_imgui_hosts` / `get_imgui_windows` / `get_imgui_items` /
   `get_imgui_item_rect`. Verify: every build configuration AGENTS.md lists
   still links (the four extern functions live in `erhe_imgui`, which every
   ImGui user links; `hextiles` and `example` included); `get_imgui_items` on
   the Hierarchy window lists the default scene's node labels; a Tracy or
   timer check shows no recorder work in a frame without a request.
4. Part A actions and annotation: `imgui_click`, `imgui_hover`,
   `imgui_scroll`, `capture_screenshot.annotate_imgui_items`. Verify: open
   Window menu -> "Open Four View" by label only and confirm with
   `get_four_views`; remove `debug_imgui_mouse` (B5).
5. Documents: a new `mcp_ui_driving` run-book under `doc/agents/` (agent run-book: inspect ->
   resolve -> act -> screenshot loop, coordinate space, windowed-build
   caveat R5), R7 section in `doc/agents/mcp_api_guidelines.md`, new sections
   in `doc/erhe/imgui.md` (recorder) and `doc/erhe/window.md` (injection
   ordering F3), tool table in `mcp_server_usage.md`, AGENTS.md "In-editor MCP
   server" tool-group list and the "routinely cannot exercise the menu- and
   mouse-driven entry points" statement; this plan is deleted.

## 5. Decisions

- imgui-mcp itself is not run or registered; A1 is the whole integration.
- Tool naming: the part A actions carry the `imgui_` prefix, part B tools are
  unprefixed because they are not ImGui-specific.
- Quest / OpenXR input (`Xr_*_event`) injection is future work after this
  plan; R3 lists the supported types.
